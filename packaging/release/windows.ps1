<#
.SYNOPSIS
  Portable Windows build: nebula.exe plus the Qt it needs in one folder, zipped.

.DESCRIPTION
  Needs Visual Studio's C++ tools on PATH (run from a "x64 Native Tools" prompt, or after ilammy/msvc-dev-cmd in CI),
  CMake, Ninja, and Qt 6.8+ for MSVC 2022 with the WebEngine, WebChannel and Positioning modules (windeployqt on PATH).

  packaging/release/windows.ps1 -Version 0.2.3 -Out dist     ->  dist/nebula-windows-x86_64.zip
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$Out = "dist",
  [string]$Build = "build-win"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root

# runs a program and shows its output; native programs that print warnings on stderr must not abort the script
function Run {
  Write-Host "> $args"
  $before = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    & $args[0] $args[1..($args.Count - 1)] 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $before
  }
  if ($code -ne 0) { throw "failed ($code): $args" }
}

New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$stage = Join-Path $Out "stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$app = Join-Path $stage "nebula"

Run cmake -S . -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release
Run cmake --build $Build
Run cmake --install $Build --prefix $app

# Qt: libraries, plugins, QML modules, the web engine process and its resources
Run windeployqt --release --qmldir qml (Join-Path $app "nebula.exe")

# Qt's headless platform: not needed to use nebula, but it lets the package be tested in CI and over SSH
$qtBin = Split-Path (Get-Command windeployqt).Source
$offscreen = Join-Path $qtBin "..\plugins\platforms\qoffscreen.dll"
if (Test-Path $offscreen) { Copy-Item $offscreen (Join-Path $app "platforms") }

Set-Content -Path (Join-Path $app "VERSION") -Value $Version -Encoding ascii
$zip = Join-Path $Out "nebula-windows-x86_64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $app -DestinationPath $zip -CompressionLevel Optimal
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "bundle: $zip ($size MB)"
