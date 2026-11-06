# Guts & Bolts Home Server Remote Agent
# Windows PowerShell 5.1+; run as a normal (non-elevated) Windows user.
# The agent makes an outbound TLS WebSocket connection. It opens no listening ports.

param(
  [string]$ServerExe = ''
)

$ErrorActionPreference = 'Stop'
$AgentVersion = '1.0.0'
$BaseUri = [Uri]'wss://gutsandbolts.net/ws/remote'
$Root = Join-Path $env:LOCALAPPDATA 'GutsAndBolts\RemoteAgent'
$ConfigPath = Join-Path $Root 'agent.json'
$TokenPath = Join-Path $Root 'device-token.dpapi'
$PidPath = Join-Path $Root 'managed-server.pid'
$script:Config = $null
$script:LastError = ''
$script:CrashTimes = New-Object 'System.Collections.Generic.List[datetime]'
$script:LastProcessId = 0
$script:PairingCode = $null
$script:SeenCommands = New-Object 'System.Collections.Generic.HashSet[string]'

function Ensure-Root {
  if (-not (Test-Path -LiteralPath $Root)) { New-Item -ItemType Directory -Path $Root -Force | Out-Null }
  $acl = Get-Acl -LiteralPath $Root
  $acl.SetAccessRuleProtection($true, $false)
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
  $rule = [Security.AccessControl.FileSystemAccessRule]::new($identity, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
  $acl.SetAccessRule($rule)
  Set-Acl -LiteralPath $Root -AclObject $acl
}

function Save-Config {
  $json = $script:Config | ConvertTo-Json -Depth 4
  [IO.File]::WriteAllText($ConfigPath, $json, ([System.Text.UTF8Encoding]::new($false)))
}

function Resolve-ServerExecutable([string]$Path) {
  if (-not $Path) { throw 'A server executable path is required.' }
  $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
  if ([IO.Path]::GetFileName($resolved) -ne 'GutsAndBoltsGameServer.exe') {
    throw 'Select the official GutsAndBoltsGameServer.exe file.'
  }
  $file = Get-Item -LiteralPath $resolved -ErrorAction Stop
  if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'The server executable cannot be a symbolic link or reparse point.' }
  if ([IO.Path]::GetPathRoot($resolved) -notmatch '^[A-Za-z]:\\$') {
    throw 'The server executable must be on a local Windows drive, not a network share.'
  }
  return $resolved
}

function Load-Config {
  if (Test-Path -LiteralPath $ConfigPath) {
    $cfg = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
    if ($ServerExe) {
      $cfg.ServerExe = Resolve-ServerExecutable $ServerExe
    } else {
      if (-not $cfg.ServerExe -or -not (Test-Path -LiteralPath $cfg.ServerExe -PathType Leaf)) {
        throw 'The configured server executable is missing. Re-run the agent with -ServerExe "C:\path\GutsAndBoltsGameServer.exe".'
      }
      $cfg.ServerExe = Resolve-ServerExecutable ([string]$cfg.ServerExe)
    }
    if ([int]$cfg.Slots -lt 1 -or [int]$cfg.Slots -gt 50) { throw 'Saved slot count is invalid.' }
    if (@('Normal','AboveNormal','High') -notcontains [string]$cfg.Priority) { throw 'Saved process priority is invalid.' }
    return $cfg
  }
  if (-not $ServerExe) { $ServerExe = Read-Host 'Full path to GutsAndBoltsGameServer.exe' }
  $resolved = Resolve-ServerExecutable $ServerExe
  return [pscustomobject]@{
    ServerExe = $resolved
    Slots = 4
    Priority = 'Normal'
    AutoRestart = $false
  }
}

function Save-DeviceToken([string]$Token) {
  if ($Token -notmatch '^[a-f0-9]{64}$') { throw 'The server returned an invalid device credential.' }
  $secure = ConvertTo-SecureString $Token -AsPlainText -Force
  $protected = ConvertFrom-SecureString $secure
  [IO.File]::WriteAllText($TokenPath, $protected, ([System.Text.UTF8Encoding]::new($false)))
  $secure.Dispose()
}

function Load-DeviceToken {
  if (-not (Test-Path -LiteralPath $TokenPath)) { return '' }
  $protected = Get-Content -LiteralPath $TokenPath -Raw
  $secure = ConvertTo-SecureString $protected
  $ptr = [IntPtr]::Zero
  try {
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
  } finally {
    if ($ptr -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
    $secure.Dispose()
  }
}

function New-PairingCode {
  $bytes = New-Object byte[] 32
  $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
  try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
  return ([BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
}

function Send-Json($Socket, $Value) {
  $text = ConvertTo-Json -InputObject $Value -Depth 8 -Compress
  $bytes = [Text.Encoding]::UTF8.GetBytes($text)
  if ($bytes.Length -gt 8192) { throw 'Outgoing agent message exceeded its size limit.' }
  $segment = [ArraySegment[byte]]::new($bytes)
  $Socket.SendAsync($segment, [Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
}

function Get-ManagedProcess {
  if (-not (Test-Path -LiteralPath $PidPath)) { return $null }
  $pidValue = 0
  if (-not [int]::TryParse((Get-Content -LiteralPath $PidPath -Raw).Trim(), [ref]$pidValue)) {
    Remove-Item -LiteralPath $PidPath -Force -ErrorAction SilentlyContinue
    return $null
  }
  try {
    $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $pidValue" -ErrorAction Stop
    if (-not $proc -or -not $proc.ExecutablePath) { return $null }
    if ([string]::Compare([IO.Path]::GetFullPath($proc.ExecutablePath), [IO.Path]::GetFullPath($script:Config.ServerExe), $true) -ne 0) {
      Remove-Item -LiteralPath $PidPath -Force -ErrorAction SilentlyContinue
      return $null
    }
    return Get-Process -Id $pidValue -ErrorAction Stop
  } catch {
    return $null
  }
}

function Start-ManagedServer {
  $current = Get-ManagedProcess
  if ($current) { return 'Server is already running (PID ' + $current.Id + ').' }
  if (-not (Test-Path -LiteralPath $script:Config.ServerExe -PathType Leaf)) { throw 'The configured game server executable is missing.' }
  try { $existingServers = @(Get-CimInstance Win32_Process -Filter "Name = 'GutsAndBoltsGameServer.exe'" -ErrorAction Stop) }
  catch { throw 'Could not safely inspect running game-server processes. Refusing to risk starting a duplicate.' }
  foreach ($existing in $existingServers) {
    if (-not $existing.ExecutablePath) { throw 'An existing game-server process could not be identified safely. Refusing to start another.' }
    if ([string]::Compare([IO.Path]::GetFullPath($existing.ExecutablePath), [IO.Path]::GetFullPath($script:Config.ServerExe), $true) -eq 0) {
      throw 'The configured game server is already running outside this agent. It will not start a duplicate or take control of that process.'
    }
    throw 'Another Guts & Bolts game-server process is already running from a different location. Stop it before starting this one.'
  }
  $work = Split-Path -Parent $script:Config.ServerExe
  $serverArgs = @('--slots', [string]$script:Config.Slots)
  $proc = Start-Process -FilePath $script:Config.ServerExe -ArgumentList $serverArgs -WorkingDirectory $work -PassThru -WindowStyle Hidden
  Set-Content -LiteralPath $PidPath -Value $proc.Id -NoNewline
  Start-Sleep -Milliseconds 800
  $proc.Refresh()
  if ($proc.HasExited) {
    Remove-Item -LiteralPath $PidPath -Force -ErrorAction SilentlyContinue
    throw 'The game server exited immediately after launch. Check its local logs.'
  }
  try { $proc.PriorityClass = [Enum]::Parse([Diagnostics.ProcessPriorityClass], [string]$script:Config.Priority) } catch { }
  $script:LastProcessId = $proc.Id
  return 'Server started (PID ' + $proc.Id + ').'
}

function Stop-ManagedServer {
  $proc = Get-ManagedProcess
  if (-not $proc) {
    Remove-Item -LiteralPath $PidPath -Force -ErrorAction SilentlyContinue
    return 'Server is already stopped.'
  }
  Stop-Process -Id $proc.Id -Force -ErrorAction Stop
  try { $proc.WaitForExit(10000) | Out-Null } catch { }
  Remove-Item -LiteralPath $PidPath -Force -ErrorAction SilentlyContinue
  return 'Server stopped.'
}

function Set-ManagedPriority {
  $proc = Get-ManagedProcess
  if ($proc) { $proc.PriorityClass = [Enum]::Parse([Diagnostics.ProcessPriorityClass], [string]$script:Config.Priority) }
  return 'Priority set to ' + $script:Config.Priority + '.'
}

function Invoke-AllowedCommand($Message) {
  if (-not $Message.id -or [string]$Message.id -notmatch '^[a-f0-9-]{16,64}$') { throw 'Invalid command ID.' }
  if (-not ($Message.expires -is [long] -or $Message.expires -is [int] -or $Message.expires -is [double])) { throw 'Command has no expiry.' }
  if ([long]$Message.expires -lt [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()) { throw 'Command expired and was ignored.' }
  if (-not $script:SeenCommands.Add([string]$Message.id)) { throw 'Duplicate command ID; replay was rejected.' }
  if ($script:SeenCommands.Count -gt 1000) { $script:SeenCommands.Clear(); [void]$script:SeenCommands.Add([string]$Message.id) }
  $commandArgs = $Message.args
  if ($null -eq $commandArgs) { $commandArgs = [pscustomobject]@{} }
  $argNames = @($commandArgs.PSObject.Properties.Name)
  switch ([string]$Message.command) {
    'start' {
      if ($argNames.Count -ne 0) { throw 'Unexpected start arguments.' }
      return Start-ManagedServer
    }
    'stop' {
      if ($argNames.Count -ne 0) { throw 'Unexpected stop arguments.' }
      return Stop-ManagedServer
    }
    'restart' {
      if ($argNames.Count -ne 0) { throw 'Unexpected restart arguments.' }
      Stop-ManagedServer | Out-Null
      Start-Sleep -Seconds 2
      return Start-ManagedServer
    }
    'set-slots' {
      if ($argNames.Count -ne 1 -or $argNames[0] -ne 'slots' -or (($commandArgs.slots -isnot [int]) -and ($commandArgs.slots -isnot [long])) -or [int]$commandArgs.slots -lt 1 -or [int]$commandArgs.slots -gt 50) { throw 'Slots must be an integer from 1 to 50.' }
      $script:Config.Slots = [int]$commandArgs.slots
      Save-Config
      if (Get-ManagedProcess) {
        Stop-ManagedServer | Out-Null
        Start-Sleep -Seconds 2
        return Start-ManagedServer
      }
      return 'Saved server slots: ' + $script:Config.Slots + '.'
    }
    'set-priority' {
      if ($argNames.Count -ne 1 -or $argNames[0] -ne 'priority' -or @('Normal','AboveNormal','High') -notcontains [string]$commandArgs.priority) { throw 'Priority is not allowed.' }
      $script:Config.Priority = [string]$commandArgs.priority
      Save-Config
      return Set-ManagedPriority
    }
    'set-auto-restart' {
      if ($argNames.Count -ne 1 -or $argNames[0] -ne 'enabled' -or $commandArgs.enabled -isnot [bool]) { throw 'Recovery setting must be true or false.' }
      $script:Config.AutoRestart = [bool]$commandArgs.enabled
      Save-Config
      return 'Automatic crash recovery ' + ($(if ($script:Config.AutoRestart) { 'enabled' } else { 'disabled' })) + '.'
    }
    default { throw 'Command is not in the agent allowlist.' }
  }
}
function Get-GpuMetrics {
  $path = Join-Path $env:ProgramFiles 'NVIDIA Corporation\NVSMI\nvidia-smi.exe'
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return @{} }
  $sig = Get-AuthenticodeSignature -FilePath $path
  if ($sig.Status -ne 'Valid') { return @{} }
  try {
    $line = & $path --query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total --format=csv,noheader,nounits 2>$null | Select-Object -First 1
    $parts = @($line -split ',\s*')
    if ($parts.Count -ne 4) { return @{} }
    return @{
      gpuPercent = [double]$parts[0]
      gpuTemperatureC = [double]$parts[1]
      gpuMemoryUsedMB = [double]$parts[2]
      gpuMemoryTotalMB = [double]$parts[3]
    }
  } catch { return @{} }
}

function Get-AgentStatus {
  $os = Get-CimInstance Win32_OperatingSystem
  $cpu = Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average
  $totalMb = [math]::Round(([double]$os.TotalVisibleMemorySize / 1024), 1)
  $freeMb = [math]::Round(([double]$os.FreePhysicalMemory / 1024), 1)
  $usedMb = [math]::Max(0, $totalMb - $freeMb)
  $proc = Get-ManagedProcess
  $uptime = 0
  $pidValue = $null
  if ($proc) {
    $pidValue = $proc.Id
    try { $uptime = [math]::Max(0, [math]::Floor(([DateTime]::UtcNow - $proc.StartTime.ToUniversalTime()).TotalSeconds)) } catch { }
  }
  $status = @{
    cpuPercent = [math]::Round([double]$cpu.Average, 1)
    memoryPercent = if ($totalMb -gt 0) { [math]::Round(($usedMb / $totalMb) * 100, 1) } else { 0 }
    memoryUsedMB = $usedMb
    memoryTotalMB = $totalMb
    serverRunning = [bool]$proc
    serverPid = $pidValue
    serverUptimeSeconds = $uptime
    slots = [int]$script:Config.Slots
    priority = [string]$script:Config.Priority
    autoRestart = [bool]$script:Config.AutoRestart
    agentVersion = $AgentVersion
    lastError = [string]$script:LastError
  }
  $gpu = Get-GpuMetrics
  foreach ($key in $gpu.Keys) { $status[$key] = $gpu[$key] }
  return $status
}

function Check-CrashRecovery($Socket) {
  if (-not (Test-Path -LiteralPath $PidPath)) { return }
  $pidValue = 0
  if (-not [int]::TryParse((Get-Content -LiteralPath $PidPath -Raw).Trim(), [ref]$pidValue)) { return }
  $stillThere = Get-CimInstance Win32_Process -Filter "ProcessId = $pidValue" -ErrorAction SilentlyContinue
  if ($stillThere) { return }
  Remove-Item -LiteralPath $PidPath -Force -ErrorAction SilentlyContinue
  $script:CrashTimes.Add([DateTime]::UtcNow)
  while ($script:CrashTimes.Count -gt 0 -and ([DateTime]::UtcNow - $script:CrashTimes[0]).TotalMinutes -gt 10) { $script:CrashTimes.RemoveAt(0) }
  Send-Json $Socket @{ t = 'agent.error'; message = 'The managed game server exited unexpectedly.' }
  if (-not $script:Config.AutoRestart) {
    $script:LastError = 'The managed game server exited unexpectedly. Automatic recovery is disabled.'
    return
  }
  if ($script:CrashTimes.Count -ge 3) {
    $script:Config.AutoRestart = $false
    Save-Config
    $script:LastError = 'Automatic restart disabled after three crashes within ten minutes.'
    Send-Json $Socket @{ t = 'agent.error'; message = $script:LastError }
    return
  }
  Start-Sleep -Seconds 5
  try { Start-ManagedServer | Out-Null; $script:LastError = '' }
  catch { $script:LastError = $_.Exception.Message }
}

function Handle-AgentMessage($Socket, $Message) {
  switch ([string]$Message.t) {
    'pair.complete' {
      Save-DeviceToken ([string]$Message.token)
      $script:PairingCode = $null
      Write-Host 'Device paired successfully. The credential is encrypted for this Windows user.' -ForegroundColor Green
      Send-Json $Socket @{ t = 'status'; status = (Get-AgentStatus) }
    }
    'device.ready' {
      Write-Host 'Connected to the Guts & Bolts remote relay.' -ForegroundColor Green
      Send-Json $Socket @{ t = 'status'; status = (Get-AgentStatus) }
    }
    'device.rejected' {
      Remove-Item -LiteralPath $TokenPath -Force -ErrorAction SilentlyContinue
      $script:PairingCode = $null
      throw 'The stored device credential was revoked or rejected. A new local pairing code will be generated.'
    }
    'pair.expired' {
      $script:PairingCode = $null
      throw 'The one-time pairing code expired. A new code will be displayed.'
    }
    'pair.rate-limited' { Write-Warning 'Pairing attempts are temporarily rate-limited. Wait one hour before restarting the agent.'; exit 3 }
    'device.busy' { throw 'Another authenticated agent is active. Waiting for its connection to expire before retrying.' }
    'device.replaced' { Write-Warning 'This agent was replaced by a newer authenticated connection. Stopping this copy.'; exit 0 }
    'command' {
      $ok = $false
      $result = ''
      try { $result = Invoke-AllowedCommand $Message; $ok = $true; $script:LastError = '' }
      catch { $result = $_.Exception.Message; $script:LastError = $result }
      Send-Json $Socket @{ t = 'ack'; id = [string]$Message.id; ok = $ok; message = $result }
    }
    default { }
  }
}

function Connect-Agent {
  $socket = [Net.WebSockets.ClientWebSocket]::new()
  $socket.Options.KeepAliveInterval = [TimeSpan]::FromSeconds(20)
  $cancel = [Threading.CancellationToken]::None
  try {
    $socket.ConnectAsync($BaseUri, $cancel).GetAwaiter().GetResult()
    $token = Load-DeviceToken
    if ($token) {
      Send-Json $socket @{ t = 'device.auth'; token = $token }
    } else {
      $newCode = -not $script:PairingCode
      if ($newCode) { $script:PairingCode = New-PairingCode }
      Send-Json $socket @{ t = 'pair.begin'; code = $script:PairingCode; device = $env:COMPUTERNAME }
      if ($newCode) {
        Write-Host ''
        Write-Host 'PAIR THIS PC FROM YOUR PHONE' -ForegroundColor Yellow
        Write-Host 'Open https://gutsandbolts.net/server/ while signed in to the official Guts & Bolts account.' -ForegroundColor Yellow
        Write-Host 'Paste this one-time code into Device pairing (expires in five minutes):' -ForegroundColor Yellow
        Write-Host $script:PairingCode -ForegroundColor Cyan
        Write-Host 'Do not share this code with anyone. Press Ctrl+C to stop the agent.' -ForegroundColor Yellow
      }
    }
    $buffer = New-Object byte[] 8192
    $segment = [ArraySegment[byte]]::new($buffer)
    $receive = $socket.ReceiveAsync($segment, $cancel)
    while ($socket.State -eq [Net.WebSockets.WebSocketState]::Open) {
      $completed = [Threading.Tasks.Task]::WhenAny($receive, [Threading.Tasks.Task]::Delay(10000)).GetAwaiter().GetResult()
      if ([object]::ReferenceEquals($completed, $receive)) {
        $frame = $receive.GetAwaiter().GetResult()
        if ($frame.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { break }
        if (-not $frame.EndOfMessage) { throw 'Unexpected fragmented relay message.' }
        $text = [Text.Encoding]::UTF8.GetString($buffer, 0, $frame.Count)
        $message = $text | ConvertFrom-Json
        Handle-AgentMessage $socket $message
        $receive = $socket.ReceiveAsync($segment, $cancel)
      } else {
        Check-CrashRecovery $socket
        $status = Get-AgentStatus
        Send-Json $socket @{ t = 'status'; status = $status }
      }
    }
  } finally {
    try { $socket.Abort() } catch { }
    $socket.Dispose()
  }
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'This agent only runs on Windows.' }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'For security, do not run the remote agent as Administrator. Start it in a normal, non-elevated PowerShell window.'
}
Ensure-Root
$mutex = [Threading.Mutex]::new($false, 'Local\GutsBoltsRemoteAgent')
if (-not $mutex.WaitOne(0)) { throw 'The Guts & Bolts remote agent is already running for this Windows user.' }
$script:Config = Load-Config
Save-Config
Write-Host 'Guts & Bolts Home Server Remote Agent ' $AgentVersion
Write-Host 'Server executable: ' $script:Config.ServerExe
Write-Host 'The agent uses outbound TLS only. It does not open a port or run a remote shell.'
while ($true) {
  try { Connect-Agent }
  catch { $script:LastError = $_.Exception.Message; Write-Warning $script:LastError }
  Start-Sleep -Seconds 5
}