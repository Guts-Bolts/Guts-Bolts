# Guts & Bolts Dedicated Server USB Bootstrapper
# Windows PowerShell 5.1+. Copy this single file to a USB drive and run it
# deliberately on the server as the normal Windows account that will host it.
# Windows intentionally does not auto-run scripts from USB devices.

[CmdletBinding()]
param(
  [ValidateRange(1,50)]
  [int]$Slots = 8,
  [string]$PackagePath,
  [string]$ChecksumPath
)

$ErrorActionPreference = 'Stop'
$Repo = 'Guts-Bolts/Guts-Bolts'
$AssetName = 'GutsAndBolts-DedicatedServer-Windows.zip'
$ChecksumName = "$AssetName.sha256"
$InstallRoot = Join-Path $env:LOCALAPPDATA 'GutsAndBolts\DedicatedServer'
$DownloadRoot = Join-Path $env:TEMP ('GutsAndBolts-Setup-' + [guid]::NewGuid().ToString('N'))
$TaskName = 'GutsAndBoltsDedicatedServer'
$AgentName = 'Remote Server Agent.ps1'

function Assert-NotElevated {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this installer as a normal, non-elevated Windows user. The server agent intentionally refuses Administrator sessions.'
  }
}

function Get-ReleaseAssets {
  $headers = @{ 'User-Agent' = 'GutsAndBolts-Server-Bootstrapper'; 'Accept' = 'application/vnd.github+json' }
  $releases = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases?per_page=20" -Headers $headers -TimeoutSec 30
  foreach ($release in $releases) {
    $archive = @($release.assets | Where-Object { $_.name -eq $AssetName }) | Select-Object -First 1
    $checksum = @($release.assets | Where-Object { $_.name -eq $ChecksumName }) | Select-Object -First 1
    if ($archive -and $checksum) {
      return [pscustomobject]@{ Tag = $release.tag_name; Archive = $archive.browser_download_url; Checksum = $checksum.browser_download_url }
    }
  }
  throw 'No published Windows dedicated-server package was found. The latest Guts & Bolts release may not include the server kit yet.'
}

function Assert-SafeArchive([string]$Path) {
  Add-Type -AssemblyName System.IO.Compression
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip = [IO.Compression.ZipFile]::OpenRead($Path)
  try {
    foreach ($entry in $zip.Entries) {
      $name = $entry.FullName.Replace('\','/')
      if ($name.StartsWith('/') -or $name -match '(^|/)\.\.(/|$)' -or $name -match '^[A-Za-z]:') {
        throw 'The downloaded server archive contains an unsafe path. Installation was stopped.'
      }
    }
  } finally { $zip.Dispose() }
}

try {
  Assert-NotElevated
  if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'This bootstrapper supports Windows only.' }
  if (-not $env:LOCALAPPDATA) { throw 'The current Windows user profile is unavailable.' }

  if ([bool]$PackagePath -ne [bool]$ChecksumPath) {
    throw 'Supply both -PackagePath and -ChecksumPath, or omit both to use the official GitHub release feed.'
  }

  Write-Host 'Guts & Bolts Dedicated Server Setup'
  New-Item -ItemType Directory -Path $DownloadRoot -Force | Out-Null
  $archivePath = Join-Path $DownloadRoot $AssetName
  $checksumPath = Join-Path $DownloadRoot $ChecksumName

  if ($PackagePath) {
    $resolvedPackage = (Resolve-Path -LiteralPath $PackagePath -ErrorAction Stop).Path
    $resolvedChecksum = (Resolve-Path -LiteralPath $ChecksumPath -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath $resolvedPackage -PathType Leaf) -or
        -not (Test-Path -LiteralPath $resolvedChecksum -PathType Leaf)) {
      throw 'The local package and checksum must both be existing files.'
    }
    Copy-Item -LiteralPath $resolvedPackage -Destination $archivePath
    Copy-Item -LiteralPath $resolvedChecksum -Destination $checksumPath
    $release = [pscustomobject]@{ Tag = 'local validation package' }
    Write-Host 'Using the locally supplied package and checksum.'
  } else {
    Write-Host 'Checking the official release feed...'
    $release = Get-ReleaseAssets
    Write-Host "Downloading server package $($release.Tag)..."
    Invoke-WebRequest -Uri $release.Archive -OutFile $archivePath -UseBasicParsing -TimeoutSec 300
    Invoke-WebRequest -Uri $release.Checksum -OutFile $checksumPath -UseBasicParsing -TimeoutSec 30
  }

  $checksumText = Get-Content -LiteralPath $checksumPath -Raw
  if ($checksumText -notmatch '(?i)\b([a-f0-9]{64})\b') { throw 'The release checksum file is malformed.' }
  $expectedHash = $Matches[1].ToLowerInvariant()
  $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actualHash -ne $expectedHash) { throw 'The downloaded server package failed SHA-256 verification. Nothing was installed.' }
  Assert-SafeArchive $archivePath

  $stage = Join-Path $DownloadRoot 'package'
  Expand-Archive -LiteralPath $archivePath -DestinationPath $stage -Force
  $server = Get-ChildItem -LiteralPath $stage -Filter 'GutsAndBoltsGameServer.exe' -File -Recurse | Select-Object -First 1
  if (-not $server) { throw 'The verified package does not contain GutsAndBoltsGameServer.exe.' }
  $packageRoot = $server.Directory.FullName
  if (-not (Test-Path (Join-Path $packageRoot 'games') -PathType Container) -or
      -not (Test-Path (Join-Path $packageRoot 'catalog') -PathType Container)) {
    throw 'The server package is missing its games or catalog data. Installation was stopped.'
  }

  $parent = Split-Path -Parent $InstallRoot
  New-Item -ItemType Directory -Path $parent -Force | Out-Null
  $stagingInstall = "$InstallRoot.new"
  if (Test-Path $stagingInstall) { Remove-Item -LiteralPath $stagingInstall -Recurse -Force }
  New-Item -ItemType Directory -Path $stagingInstall -Force | Out-Null
  Copy-Item -Path (Join-Path $packageRoot '*') -Destination $stagingInstall -Recurse -Force

  $agent = Join-Path $stagingInstall $AgentName
  if (-not (Test-Path -LiteralPath $agent -PathType Leaf)) { throw 'The package does not include the remote management agent.' }
  $installedServer = Join-Path $stagingInstall 'GutsAndBoltsGameServer.exe'
  if (-not (Test-Path -LiteralPath $installedServer -PathType Leaf)) { throw 'The server executable was not staged correctly.' }

  if (Test-Path $InstallRoot) {
    $backup = "$InstallRoot.previous"
    if (Test-Path $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
    Move-Item -LiteralPath $InstallRoot -Destination $backup
    try { Move-Item -LiteralPath $stagingInstall -Destination $InstallRoot }
    catch { Move-Item -LiteralPath $backup -Destination $InstallRoot; throw }
    Remove-Item -LiteralPath $backup -Recurse -Force
  } else {
    Move-Item -LiteralPath $stagingInstall -Destination $InstallRoot
  }

  $agent = Join-Path $InstallRoot $AgentName
  $installedServer = Join-Path $InstallRoot 'GutsAndBoltsGameServer.exe'
  $powershell = Join-Path $PSHOME 'powershell.exe'
  if (-not (Test-Path $powershell)) { $powershell = 'powershell.exe' }
  $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $agent + '" -ServerExe "' + $installedServer + '" -StartServerOnLaunch'
  $action = New-ScheduledTaskAction -Execute $powershell -Argument $arguments -WorkingDirectory $InstallRoot
  $trigger = New-ScheduledTaskTrigger -AtLogOn -User ([Security.Principal.WindowsIdentity]::GetCurrent().Name)
  $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
  $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit ([TimeSpan]::Zero)
  Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'Starts the Guts & Bolts dedicated server and secure outbound remote-management agent at user logon.' -Force | Out-Null

  Write-Host ''
  Write-Host 'Installation completed.' -ForegroundColor Green
  Write-Host "Version:       $($release.Tag)"
  Write-Host "Install path:  $InstallRoot"
  Write-Host "Startup task:  $TaskName (runs when this Windows user signs in)"
  Write-Host 'The server and remote agent are starting. The agent will display a one-time pairing code.'
  Write-Host 'Open https://gutsandbolts.net/server/ while signed into your official account and enter that code.'
  Write-Host 'Keep this Windows account signed in. This first version uses a per-user logon task, not a Windows service.'
  Write-Host 'Starting the agent in this window so you can see and enter its one-time pairing code.'
  & $powershell -NoProfile -ExecutionPolicy Bypass -File $agent -ServerExe $installedServer -StartServerOnLaunch -InitialSlots $Slots
}
catch {
  Write-Error $_
  exit 1
}
finally {
  if (Test-Path $DownloadRoot) { Remove-Item -LiteralPath $DownloadRoot -Recurse -Force -ErrorAction SilentlyContinue }
}
