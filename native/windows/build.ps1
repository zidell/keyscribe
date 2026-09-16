param([switch]$DebugBuild)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$manifest = Join-Path $PSScriptRoot 'Cargo.toml'
$cargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
if (-not (Test-Path -LiteralPath $cargo)) {
    $cargo = (Get-Command cargo -ErrorAction Stop).Source
}

$profile = if ($DebugBuild) { 'debug' } else { 'release' }
$linker = Get-Command link.exe -ErrorAction SilentlyContinue
if ($linker) {
    if ($DebugBuild) {
        & $cargo +stable build --manifest-path $manifest
    } else {
        & $cargo +stable build --release --manifest-path $manifest
    }
} else {
    $clang = Get-Command x86_64-w64-mingw32-clang.exe -ErrorAction SilentlyContinue
    if ($clang) {
        $bin = Split-Path $clang.Source
    } else {
        $packages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
        $package = Get-ChildItem -LiteralPath $packages -Directory -Filter 'MartinStorsjo.LLVM-MinGW.UCRT_*' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if (-not $package) { throw 'Install Visual Studio C++ Build Tools or LLVM-MinGW UCRT.' }
        $installation = Get-ChildItem -LiteralPath $package.FullName -Directory -Filter 'llvm-mingw-*-ucrt-x86_64' |
            Select-Object -First 1
        if (-not $installation) { throw 'LLVM-MinGW installation was not found.' }
        $bin = Join-Path $installation.FullName 'bin'
    }
    $installationRoot = Split-Path $bin
    $staticUnwind = Join-Path $installationRoot 'x86_64-w64-mingw32\lib\libunwind.a'
    if (-not (Test-Path -LiteralPath $staticUnwind)) { throw 'Static libunwind.a was not found.' }
    $staticDirectory = Join-Path $env:TEMP 'keyscribe-static-unwind'
    New-Item -ItemType Directory -Path $staticDirectory -Force | Out-Null
    Copy-Item -LiteralPath $staticUnwind -Destination (Join-Path $staticDirectory 'libunwind.a') -Force
    $env:PATH = "$bin;$env:PATH"
    if ($DebugBuild) {
        & $cargo +stable-x86_64-pc-windows-gnullvm rustc --manifest-path $manifest -- -L "native=$staticDirectory"
    } else {
        & $cargo +stable-x86_64-pc-windows-gnullvm rustc --release --manifest-path $manifest -- -L "native=$staticDirectory"
    }
}
if ($LASTEXITCODE -ne 0) { throw 'Rust build failed.' }

$built = Join-Path $PSScriptRoot "target\$profile\KeyScribe.exe"
if (-not (Test-Path -LiteralPath $built)) { throw "Build output missing: $built" }
$outputDirectory = Join-Path $root 'dist-native'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$output = Join-Path $outputDirectory 'KeyScribe.exe'
Copy-Item -LiteralPath $built -Destination $output -Force
Write-Output $output
