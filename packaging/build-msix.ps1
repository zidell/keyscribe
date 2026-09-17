[CmdletBinding(DefaultParameterSetName = 'Signed')]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$ExecutablePath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Signed')][string]$CertificatePath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Signed')][string]$CertificatePassword,
    [Parameter(Mandatory = $true, ParameterSetName = 'Store')][string]$StorePackageName,
    [Parameter(Mandatory = $true, ParameterSetName = 'Store')][string]$StorePublisher,
    [Parameter(Mandatory = $true, ParameterSetName = 'Store')][string]$StorePublisherDisplayName
)

$ErrorActionPreference = 'Stop'
$parts = $Version.Split('.')
if ($parts.Count -ne 3 -or ($parts | Where-Object { $_ -notmatch '^\d+$' }).Count -ne 0) {
    throw 'Version must have three numeric parts, for example 1.2.3.'
}
$msixVersion = "$Version.0"
$storeBuild = $PSCmdlet.ParameterSetName -eq 'Store'
if ($storeBuild) {
    if ([int]$parts[0] -eq 0 -or @($parts | Where-Object { [int]$_ -gt 65535 }).Count -ne 0) {
        throw 'Store package version components must be 0..65535 and the major version must be nonzero.'
    }
    $packageName = $StorePackageName
    $publisherName = $StorePublisher
    $publisherDisplayName = $StorePublisherDisplayName
} else {
    $certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
        (Resolve-Path $CertificatePath).Path, $CertificatePassword
    )
    $packageName = 'KeyScribe'
    $publisherName = $certificate.Subject
    $publisherDisplayName = 'KeyScribe'
}
$packageName = [System.Security.SecurityElement]::Escape($packageName)
$publisher = [System.Security.SecurityElement]::Escape($publisherName)
$publisherDisplayName = [System.Security.SecurityElement]::Escape($publisherDisplayName)
$root = (Resolve-Path '.').Path
$stageName = if ($storeBuild) { 'build\msix-store' } else { 'build\msix' }
$stage = Join-Path $root $stageName
$appDirectory = Join-Path $stage 'App\KeyScribe'
$assetsDirectory = Join-Path $stage 'Assets'
$suffix = if ($storeBuild) { '-store' } else { '' }
$output = Join-Path $root "dist\KeyScribe-windows-x64-$Version$suffix.msix"
New-Item (Join-Path $root 'dist') -ItemType Directory -Force | Out-Null

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
  <Identity Name="$packageName" Publisher="$publisher" Version="$msixVersion" ProcessorArchitecture="x64" />
  <Properties>
    <DisplayName>KeyScribe</DisplayName>
    <PublisherDisplayName>$publisherDisplayName</PublisherDisplayName>
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

& $makeappx.FullName pack /d $stage /p $output /o
if ($LASTEXITCODE -ne 0) { throw 'MakeAppx failed.' }
if (-not $storeBuild) {
    $signtool = Join-Path $makeappx.Directory.FullName 'signtool.exe'
    if (-not (Test-Path $signtool)) { throw 'Windows SDK SignTool.exe was not found.' }
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
}
Write-Output $output
