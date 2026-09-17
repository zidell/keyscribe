param([switch]$Once)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $PSScriptRoot 'build.ps1'
$built = Join-Path $PSScriptRoot 'target\release\KeyScribe.exe'
$output = Join-Path $root 'dist-native\KeyScribe.exe'
$app = $null

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class KeyScribeDevStop {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr OpenEvent(uint access, bool inheritHandle, string name);
    [DllImport("kernel32.dll")]
    public static extern bool SetEvent(IntPtr handle);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
'@

function Get-SourceSnapshot {
    $files = @(
        Get-Item -LiteralPath (Join-Path $root 'config.toml.example')
        Get-Item -LiteralPath (Join-Path $PSScriptRoot 'Cargo.toml')
        Get-Item -LiteralPath (Join-Path $PSScriptRoot 'Cargo.lock')
        Get-Item -LiteralPath (Join-Path $PSScriptRoot 'build.rs')
        Get-Item -LiteralPath $build
        Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -Recurse -File
        Get-ChildItem -LiteralPath (Join-Path $root 'assets') -Recurse -File
    )
    return (($files | Sort-Object FullName | ForEach-Object {
        '{0}|{1}|{2}' -f $_.FullName, $_.LastWriteTimeUtc.Ticks, $_.Length
    }) -join "`n")
}

function Follow-RestartedApp {
    if ($null -eq $script:app -or -not $script:app.HasExited) { return }
    $child = Get-CimInstance Win32_Process -Filter "ParentProcessId = $($script:app.Id)" |
        Where-Object { $_.ExecutablePath -eq $output } |
        Sort-Object CreationDate -Descending |
        Select-Object -First 1
    if ($null -eq $child) { return }
    $replacement = Get-Process -Id $child.ProcessId -ErrorAction SilentlyContinue
    if ($null -eq $replacement) { return }
    $script:app.Dispose()
    $script:app = $replacement
    Write-Output "Following restarted KeyScribe: PID $($script:app.Id)"
}

function Stop-OwnedApp {
    Follow-RestartedApp
    if ($null -ne $script:app) {
        if (-not $script:app.HasExited) {
            $eventName = 'Local\KeyScribeDevStop-{0}' -f $script:app.Id
            $eventHandle = [KeyScribeDevStop]::OpenEvent(0x0002, $false, $eventName)
            if ($eventHandle -ne [IntPtr]::Zero) {
                [void][KeyScribeDevStop]::SetEvent($eventHandle)
                [void][KeyScribeDevStop]::CloseHandle($eventHandle)
                [void]$script:app.WaitForExit(5000)
            }
            if (-not $script:app.HasExited) {
                Write-Warning 'KeyScribe did not close gracefully; forcing shutdown.'
                $script:app.Kill()
                [void]$script:app.WaitForExit(5000)
            }
        }
        $script:app.Dispose()
        $script:app = $null
    }
}

try {
    while ($true) {
        $before = Get-SourceSnapshot
        Write-Output 'Building KeyScribe...'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $build -NoCopy
        $succeeded = $LASTEXITCODE -eq 0
        $after = Get-SourceSnapshot
        if ($after -ne $before) {
            Write-Output 'Source changed during build; rebuilding.'
            continue
        }
        if ($succeeded -and (Test-Path -LiteralPath $built)) {
            Stop-OwnedApp
            New-Item -ItemType Directory -Path (Split-Path $output) -Force | Out-Null
            Copy-Item -LiteralPath $built -Destination $output -Force
            $previousLog = [Environment]::GetEnvironmentVariable('KEYSCRIBE_DEBUG_LOG', 'Process')
            try {
                [Environment]::SetEnvironmentVariable('KEYSCRIBE_DEBUG_LOG', (Join-Path $root 'dist-native\windows-debug.log'), 'Process')
                $app = Start-Process -FilePath $output -WorkingDirectory $root -WindowStyle Hidden -PassThru
            } finally {
                [Environment]::SetEnvironmentVariable('KEYSCRIBE_DEBUG_LOG', $previousLog, 'Process')
            }
            Write-Output "KeyScribe running: PID $($app.Id)"
        } else {
            Write-Output 'Build failed; the last working app remains running.'
        }
        if ($Once) { break }

        $observed = $after
        do {
            Start-Sleep -Milliseconds 500
            Follow-RestartedApp
            $current = Get-SourceSnapshot
        } while ($current -eq $observed)
        Start-Sleep -Milliseconds 800
    }
} finally {
    if (-not $Once) { Stop-OwnedApp }
}
