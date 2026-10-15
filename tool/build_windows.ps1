param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug',
  [switch]$UseSourceHev
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$hevDirectory = Join-Path $projectRoot 'native\hev-socks5-tunnel'
$vendorDirectory = Join-Path $projectRoot 'build\vendor'
$releaseZip = Join-Path $vendorDirectory 'hev-socks5-tunnel-win64.zip'
$releaseDirectory = Join-Path $vendorDirectory 'hev-release\hev-socks5-tunnel'
$expectedHevHash = '3affbcdd00d699430768bcfa8b79b854a951e30bbb11b011ebb3f7f01aa09928'
$hevReleaseUrl = 'https://github.com/heiher/hev-socks5-tunnel/releases/download/2.19.0/hev-socks5-tunnel-win64.zip'
$wintunZip = Join-Path $vendorDirectory 'wintun-0.14.1.zip'
$wintunArchive = Join-Path $vendorDirectory 'wintun-0.14.1'
$expectedWintunHash = '07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51'
$wintunUrl = 'https://www.wintun.net/builds/wintun-0.14.1.zip'
$msysSourceArchive = Join-Path $vendorDirectory 'msys2-runtime-3.6.9-1.src.tar.zst'
$expectedMsysSourceHash = 'bff64ab3d9386b0c29754951e7c121f85c30c846258e45198437940b00621ac8'
$msysSourceUrl = 'https://repo.msys2.org/msys/sources/msys2-runtime-3.6.9-1.src.tar.zst'

$tag = git -C $hevDirectory describe --exact-match --tags HEAD 2>$null
if ($LASTEXITCODE -ne 0 -or $tag.Trim() -ne '2.19.0') {
  throw 'The HEV submodule must be checked out at the official 2.19.0 tag.'
}

New-Item -ItemType Directory -Force -Path $vendorDirectory | Out-Null
if (-not (Test-Path -LiteralPath $releaseZip)) {
  Invoke-WebRequest -Uri $hevReleaseUrl -OutFile $releaseZip -UseBasicParsing
}
$actualHevHash = (Get-FileHash -LiteralPath $releaseZip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHevHash -ne $expectedHevHash) {
  throw 'The HEV release archive failed SHA-256 verification.'
}

if (-not (Test-Path -LiteralPath $wintunZip)) {
  Invoke-WebRequest -Uri $wintunUrl -OutFile $wintunZip -UseBasicParsing
}
$actualWintunHash = (Get-FileHash -LiteralPath $wintunZip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualWintunHash -ne $expectedWintunHash) {
  throw 'The Wintun 0.14.1 archive failed SHA-256 verification.'
}
if (-not (Test-Path -LiteralPath (Join-Path $wintunArchive 'wintun\bin\amd64\wintun.dll'))) {
  Expand-Archive -LiteralPath $wintunZip -DestinationPath $wintunArchive -Force
}
$wintunRuntime = Join-Path $wintunArchive 'wintun\bin\amd64\wintun.dll'
$wintunLicense = Join-Path $wintunArchive 'wintun\LICENSE.txt'

if (-not (Test-Path -LiteralPath $msysSourceArchive)) {
  Invoke-WebRequest -Uri $msysSourceUrl -OutFile $msysSourceArchive -UseBasicParsing
}
$actualMsysSourceHash = (Get-FileHash -LiteralPath $msysSourceArchive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualMsysSourceHash -ne $expectedMsysSourceHash) {
  throw 'The MSYS2 runtime source archive failed SHA-256 verification.'
}

$hevSource = Join-Path $releaseDirectory 'hev-socks5-tunnel.exe'
$msysRuntime = Join-Path $releaseDirectory 'msys-2.0.dll'
if (-not (Test-Path -LiteralPath $hevSource)) {
  $extractRoot = Join-Path $vendorDirectory 'hev-release'
  Expand-Archive -LiteralPath $releaseZip -DestinationPath $extractRoot -Force
}
foreach ($required in @($hevSource, $msysRuntime, $wintunRuntime, $wintunLicense)) {
  if (-not (Test-Path -LiteralPath $required)) {
    throw "The official HEV or Wintun package is missing a required file: $required"
  }
}

if ($UseSourceHev) {
  $sourceDirectory = Join-Path $vendorDirectory 'hev-source'
  $sourceExecutable = Join-Path $hevDirectory 'bin\hev-socks5-tunnel.exe'
  if (-not (Test-Path -LiteralPath $sourceExecutable)) {
    $sourceExecutable = Join-Path $hevDirectory 'bin\hev-socks5-tunnel'
  }
  $sourceMsysRuntime = Join-Path $sourceDirectory 'msys-2.0.dll'
  if (-not (Test-Path -LiteralPath $sourceExecutable) -or
      -not (Test-Path -LiteralPath $sourceMsysRuntime)) {
    throw 'Build HEV from the MSYS2 shell with tool/build_hev.sh first.'
  }
  $hevSource = $sourceExecutable
  $msysRuntime = $sourceMsysRuntime
}

$runtimeVersion = (Get-Item -LiteralPath $msysRuntime).VersionInfo.FileVersion
if ($runtimeVersion -notmatch '^3\.6\.9-') {
  throw "Expected the pinned MSYS2 runtime 3.6.9, found: $runtimeVersion"
}

$flutter = Get-Command flutter.bat -ErrorAction SilentlyContinue
if ($flutter) {
  $flutterCommand = $flutter.Source
} else {
  $localFlutter = Join-Path $projectRoot '.tool\flutter\bin\flutter.bat'
  if (-not (Test-Path -LiteralPath $localFlutter)) {
    throw 'Flutter 3.47.7 was not found. Install it or add flutter.bat to PATH.'
  }
  $flutterCommand = $localFlutter
}

Push-Location $projectRoot
try {
  & $flutterCommand pub get
  if ($LASTEXITCODE -ne 0) { throw 'flutter pub get failed.' }
  & $flutterCommand analyze
  if ($LASTEXITCODE -ne 0) { throw 'flutter analyze failed.' }
  & $flutterCommand test
  if ($LASTEXITCODE -ne 0) { throw 'flutter test failed.' }

  $buildFlag = if ($Configuration -eq 'Release') { '--release' } else { '--debug' }
  & $flutterCommand build windows $buildFlag
  if ($LASTEXITCODE -ne 0) { throw 'The Flutter Windows build failed.' }

  $bundleDirectory = Join-Path $projectRoot "build\windows\x64\runner\$Configuration"
  $dataDirectory = Join-Path $bundleDirectory 'data'
  if (-not (Test-Path -LiteralPath $dataDirectory)) {
    throw "The Flutter output data directory does not exist: $dataDirectory"
  }
  Copy-Item -LiteralPath $hevSource -Destination (Join-Path $dataDirectory 'hev-socks5-tunnel.exe') -Force
  Copy-Item -LiteralPath $msysRuntime -Destination (Join-Path $dataDirectory 'msys-2.0.dll') -Force
  Copy-Item -LiteralPath $wintunRuntime -Destination (Join-Path $dataDirectory 'wintun.dll') -Force
  Copy-Item -LiteralPath $wintunLicense -Destination (Join-Path $dataDirectory 'LICENSE-Wintun.txt') -Force
  Copy-Item -LiteralPath (Join-Path $hevDirectory 'LICENSE') -Destination (Join-Path $dataDirectory 'LICENSE-HEV.txt') -Force
  $thirdPartyDirectory = Join-Path $bundleDirectory 'third_party_sources'
  New-Item -ItemType Directory -Force -Path $thirdPartyDirectory | Out-Null
  Copy-Item -LiteralPath $msysSourceArchive -Destination $thirdPartyDirectory -Force
  Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $thirdPartyDirectory 'THIRD_PARTY_NOTICES.md') -Force
} finally {
  Pop-Location
}

Write-Host "Build completed: build\windows\x64\runner\$Configuration\lansocks.exe"
