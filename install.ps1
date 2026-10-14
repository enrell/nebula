<#
.SYNOPSIS
  nebula installer for Windows 10 1809 / Windows 11 (x64): installs the portable build from the latest GitHub release
  into %LOCALAPPDATA%\Programs\nebula, puts it on your PATH and adds a Start Menu entry. No administrator needed.

.EXAMPLE
  irm https://raw.githubusercontent.com/enrell/nebula/main/install.ps1 | iex

.EXAMPLE
  & ([scriptblock]::Create((irm https://raw.githubusercontent.com/enrell/nebula/main/install.ps1))) -Uninstall

.PARAMETER Version
  A release to install, such as 0.3.0 (default: the latest).
.PARAMETER Dir
  Where to install (default: %LOCALAPPDATA%\Programs\nebula).
.PARAMETER Zip
  Install from a zip file you already have instead of downloading one.
.PARAMETER Uninstall
  Remove nebula (your settings in %APPDATA%\nebula stay).
#>
param(
  [string]$Version = $(if ($env:NEBULA_VERSION) { $env:NEBULA_VERSION } else { "latest" }),
  [string]$Dir = (Join-Path $env:LOCALAPPDATA "Programs\nebula"),
  [string]$Zip = "",
  [switch]$Uninstall
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # Invoke-WebRequest is far slower with its progress bar
$repo = "enrell/nebula"
$startMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\nebula.lnk"

function Say($m) { Write-Host "nebula: $m" -ForegroundColor Cyan }

function Get-UserPath { [Environment]::GetEnvironmentVariable("Path", "User") }
function Set-UserPath($value) { [Environment]::SetEnvironmentVariable("Path", $value, "User") }
function Remove-FromPath($dir) {
  $parts = (Get-UserPath) -split ";" | Where-Object { $_ -and ($_.TrimEnd("\") -ne $dir.TrimEnd("\")) }
  Set-UserPath ($parts -join ";")
}

# the pty host (nebula.exe --host) and the window are the same program: they must not be running while files change
function Stop-Nebula {
  Get-Process nebula -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($Dir, [StringComparison]::OrdinalIgnoreCase) } | ForEach-Object {
    Say "stopping running nebula ($($_.Id))"
    $_ | Stop-Process -Force
  }
  Start-Sleep -Milliseconds 500
}

if ($Uninstall) {
  Stop-Nebula
  if (Test-Path $Dir) { Remove-Item -Recurse -Force $Dir }
  if (Test-Path $startMenu) { Remove-Item -Force $startMenu }
  Remove-FromPath $Dir
  Say "removed (your settings in $env:APPDATA\nebula are untouched)"
  return
}

if (-not [Environment]::Is64BitOperatingSystem) { throw "nebula needs 64-bit Windows" }
if ([Environment]::OSVersion.Version.Build -lt 17763) { throw "nebula needs Windows 10 version 1809 or newer (it uses the pseudo console)" }

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("nebula-install-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $tmp | Out-Null
try {
  if ($Zip) {
    $file = (Resolve-Path $Zip).Path
  } else {
    $base = if ($Version -eq "latest") { "https://github.com/$repo/releases/latest/download" } else { "https://github.com/$repo/releases/download/v$($Version.TrimStart('v'))" }
    $name = "nebula-windows-x86_64.zip"
    $file = Join-Path $tmp $name
    Say "downloading nebula ($Version)..."
    Invoke-WebRequest -UseBasicParsing -Uri "$base/$name" -OutFile $file
    Invoke-WebRequest -UseBasicParsing -Uri "$base/SHA256SUMS" -OutFile (Join-Path $tmp "SHA256SUMS")
    $want = (Get-Content (Join-Path $tmp "SHA256SUMS") | Where-Object { $_ -match [regex]::Escape($name) } | Select-Object -First 1) -split "\s+" | Select-Object -First 1
    $have = (Get-FileHash $file -Algorithm SHA256).Hash
    if (-not $want -or $want.ToLower() -ne $have.ToLower()) { throw "checksum mismatch for $name" }
  }

  Stop-Nebula
  Expand-Archive -Path $file -DestinationPath $tmp -Force
  $src = Join-Path $tmp "nebula"
  if (-not (Test-Path (Join-Path $src "nebula.exe"))) { throw "the archive has no nebula\nebula.exe" }
  if (Test-Path $Dir) { Remove-Item -Recurse -Force $Dir }
  New-Item -ItemType Directory -Force (Split-Path $Dir) | Out-Null
  Move-Item $src $Dir

  # on the user's PATH, so `nebula`, `nebula ctl ...` and the MCP server command work from any terminal
  $path = Get-UserPath
  if (-not (($path -split ";") | Where-Object { $_.TrimEnd("\") -eq $Dir.TrimEnd("\") })) {
    Set-UserPath ((@($path, $Dir) | Where-Object { $_ }) -join ";")
  }

  New-Item -ItemType Directory -Force (Split-Path $startMenu) | Out-Null
  $shell = New-Object -ComObject WScript.Shell
  $link = $shell.CreateShortcut($startMenu)
  $link.TargetPath = Join-Path $Dir "nebula.exe"
  $link.WorkingDirectory = $env:USERPROFILE
  $link.Description = "Workspace for terminal AI agents"
  $link.Save()

  $ver = if (Test-Path (Join-Path $Dir "VERSION")) { (Get-Content (Join-Path $Dir "VERSION")).Trim() } else { $Version }
  Say "installed nebula $ver in $Dir"
  Say 'start it from the Start menu, or run "nebula" in a new terminal'
} finally {
  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
