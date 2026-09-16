param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$CertificatePath,
    [Parameter(Mandatory = $true)][string]$CertificatePassword,
    [Parameter(Mandatory = $true)][string]$ExecutablePath
)

$ErrorActionPreference = 'Stop'
$parts = $Version.Split('.')
if ($parts.Count -ne 3 -or ($parts | Where-Object { $_ -notmatch '^\d+$' }).Count -ne 0) {
    throw 'Version must have three numeric parts, for example 1.2.3.'
}
$msixVersion = "$Version.0"
$certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
    (Resolve-Path $CertificatePath).Path, $CertificatePassword
)
$publisher = [System.Security.SecurityElement]::Escape($certificate.Subject)
$root = (Resolve-Path '.').Path
$stage = Join-Path $root 'build\msix'
$appDirectory = Join-Path $stage 'App\KeyScribe'
$assetsDirectory = Join-Path $stage 'Assets'
$output = Join-Path $root "dist\KeyScribe-windows-x64-$Version.msix"

if (-not (Test-Path $ExecutablePath)) {
    throw "Portable executable not found: $ExecutablePath"
}
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item $appDirectory -ItemType Directory -Force | Out-Null
New-Item $assetsDirectory -ItemType Directory -Force | Out-Null
Copy-Item -LiteralPath $ExecutablePath -Destination (Join-Path $appDirectory 'KeyScribe.exe')
$sourceAssets = Join-Path $root 'packaging\msix-assets'
foreach ($name in @('StoreLogo.png', 'Logo44.png', 'Logo150.png', 'Logo310.png', 'LogoWide.png')) {
    $source = Join-Path $sourceAssets $name
    if (-not (Test-Path $source)) { throw "MSIX asset not found: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $assetsDirectory $name)
}

$manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
         xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
         IgnorableNamespaces="uap rescap">
  <Identity Name="KeyScribe" Publisher="$publisher" Version="$msixVersion" ProcessorArchitecture="x64" />
  <Properties>
    <DisplayName>KeyScribe</DisplayName>
    <PublisherDisplayName>KeyScribe</PublisherDisplayName>
    <Logo>Assets\StoreLogo.png</Logo>
  </Properties>
  <Resources><Resource Language="en-us" /></Resources>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.17763.0" MaxVersionTested="10.0.26100.0" />
  </Dependencies>
  <Applications>
    <Application Id="KeyScribe" Executable="App\KeyScribe\KeyScribe.exe" EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="KeyScribe" Description="Voice dictation"
        BackgroundColor="transparent" Square150x150Logo="Assets\Logo150.png"
        Square44x44Logo="Assets\Logo44.png">
        <uap:DefaultTile Wide310x150Logo="Assets\LogoWide.png" Square310x310Logo="Assets\Logo310.png" />
      </uap:VisualElements>
    </Application>
  </Applications>
  <Capabilities><rescap:Capability Name="runFullTrust" /></Capabilities>
</Package>
"@
[System.IO.File]::WriteAllText((Join-Path $stage 'AppxManifest.xml'), $manifest,
    [System.Text.UTF8Encoding]::new($false))

$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
$tools = Get-ChildItem $sdkRoot -Recurse -Filter 'makeappx.exe' |
    Where-Object { $_.Directory.Name -eq 'x64' } |
    Sort-Object FullName -Descending
$makeappx = $tools | Select-Object -First 1
if (-not $makeappx) { throw 'Windows SDK MakeAppx.exe was not found.' }
$signtool = Join-Path $makeappx.Directory.FullName 'signtool.exe'
if (-not (Test-Path $signtool)) { throw 'Windows SDK SignTool.exe was not found.' }

& $makeappx.FullName pack /d $stage /p $output /o
if ($LASTEXITCODE -ne 0) { throw 'MakeAppx failed.' }
& $signtool sign /fd SHA256 /f $CertificatePath /p $CertificatePassword `
    /tr 'http://timestamp.digicert.com' /td SHA256 $output
if ($LASTEXITCODE -ne 0) { throw 'MSIX signing failed.' }
& $signtool verify /pa /v $output
if ($LASTEXITCODE -ne 0) { throw 'MSIX signature verification failed.' }
& $signtool sign /fd SHA256 /f $CertificatePath /p $CertificatePassword `
    /tr 'http://timestamp.digicert.com' /td SHA256 $ExecutablePath
if ($LASTEXITCODE -ne 0) { throw 'EXE signing failed.' }
& $signtool verify /pa /v $ExecutablePath
if ($LASTEXITCODE -ne 0) { throw 'EXE signature verification failed.' }
Write-Output $output
