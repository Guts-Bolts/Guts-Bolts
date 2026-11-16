# G&B Nexus Infrastructure Agent
# Windows PowerShell 5.1+. Telemetry and scoped local Ollama inference; no shell or arbitrary command execution.
# Run as a normal, non-elevated Windows user.

param(
  [string]$RelayUri = 'wss://gutsandbolts.net/ws/infrastructure',
  [string]$DashboardUri = 'https://gutsandbolts.net/server/'
)

$ErrorActionPreference = 'Stop'
$AgentVersion = '0.2.0'
try { $BaseUri = [Uri]$RelayUri } catch { throw 'RelayUri must be a valid secure WebSocket URL.' }
if (-not $BaseUri.IsAbsoluteUri -or $BaseUri.Scheme -ne 'wss' -or $BaseUri.UserInfo -or $BaseUri.Query -or $BaseUri.Fragment -or $BaseUri.AbsolutePath.TrimEnd('/') -ne '/ws/infrastructure') {
  throw 'RelayUri must use wss:// and end with /ws/infrastructure.'
}
try { $Dashboard = [Uri]$DashboardUri } catch { throw 'DashboardUri must be a valid HTTPS URL.' }
if (-not $Dashboard.IsAbsoluteUri -or $Dashboard.Scheme -ne 'https' -or $Dashboard.UserInfo -or $Dashboard.Query -or $Dashboard.Fragment) {
  throw 'DashboardUri must use https:// without credentials, query parameters or a fragment.'
}
$Root = Join-Path $env:LOCALAPPDATA 'GutsAndBolts\NexusAgent'
$TokenPath = Join-Path $Root 'device-token.dpapi'
$DevicePath = Join-Path $Root 'device-id'
$script:PairingCode = $null
$script:PairingCreatedAt = $null
$script:LastNetworkSample = $null
$script:OllamaInventory = $null
$script:OllamaInventoryAt = [DateTime]::MinValue

function Ensure-Root {
  if (-not (Test-Path -LiteralPath $Root)) { New-Item -ItemType Directory -Path $Root -Force | Out-Null }
  $item = Get-Item -LiteralPath $Root -Force
  if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'The agent data directory cannot be a symbolic link or reparse point.' }
}
function Save-Token([string]$Token) {
  if ($Token -notmatch '^[a-f0-9]{64}$') { throw 'The server returned an invalid device credential.' }
  $secure = ConvertTo-SecureString $Token -AsPlainText -Force
  try { [IO.File]::WriteAllText($TokenPath, (ConvertFrom-SecureString $secure), ([Text.UTF8Encoding]::new($false))) }
  finally { $secure.Dispose() }
}
function Load-Token {
  if (-not (Test-Path -LiteralPath $TokenPath)) { return '' }
  $secure = ConvertTo-SecureString (Get-Content -LiteralPath $TokenPath -Raw)
  $ptr = [IntPtr]::Zero
  try { $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure); return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) }
  finally { if ($ptr -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }; $secure.Dispose() }
}
function Save-DeviceId([string]$Id) {
  if ($Id -notmatch '^[a-f0-9]{32}$') { throw 'The server returned an invalid device ID.' }
  [IO.File]::WriteAllText($DevicePath, $Id, ([Text.UTF8Encoding]::new($false)))
}
function Send-Json($Socket, $Value) {
  $bytes = [Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $Value -Depth 24 -Compress))
  if ($bytes.Length -gt 400 * 1024) { throw 'Agent message exceeded its size limit.' }
  $Socket.SendAsync([ArraySegment[byte]]::new($bytes), [Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
}
function New-PairingCode {
  $bytes = New-Object byte[] 32
  $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
  try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
  return ([BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
}
function Get-GpuMetrics {
  $candidates = @((Join-Path $env:ProgramFiles 'NVIDIA Corporation\NVSMI\nvidia-smi.exe'), (Join-Path $env:WINDIR 'System32\nvidia-smi.exe'))
  try { $candidates += @(where.exe nvidia-smi.exe 2>$null) } catch {}
  $path = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -Unique -First 1
  if (-not $path) { return @{} }
  try {
    if ((Get-AuthenticodeSignature -FilePath $path).Status -ne 'Valid') { return @{} }
    $line = & $path --query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total --format=csv,noheader,nounits 2>$null | Select-Object -First 1
    $p = @($line -split ',\s*')
    if ($p.Count -ne 4) { return @{} }
    return @{ gpuPercent = [double]$p[0]; gpuTemperatureC = [double]$p[1]; gpuMemoryUsedMB = [double]$p[2]; gpuMemoryTotalMB = [double]$p[3] }
  } catch { return @{} }
}
function Get-OllamaInventory {
  if ($script:OllamaInventory -and ([DateTime]::UtcNow - $script:OllamaInventoryAt).TotalSeconds -lt 120) { return $script:OllamaInventory }
  $models = @(); $toolModels = @()
  try {
    $result = Invoke-RestMethod -Uri 'http://127.0.0.1:11434/api/tags' -Method Get -TimeoutSec 3
    $models = @($result.models | ForEach-Object { [string]$_.name } | Where-Object { $_ -match '^[a-zA-Z0-9][a-zA-Z0-9._:/-]{0,127}$' } | Select-Object -Unique -First 8)
    foreach ($name in $models) {
      try {
        $detail = Invoke-RestMethod -Uri 'http://127.0.0.1:11434/api/show' -Method Post -ContentType 'application/json' `
          -Body (ConvertTo-Json -InputObject @{ model = $name } -Compress) -TimeoutSec 2
        $caps = @($detail.capabilities | ForEach-Object { [string]$_ })
        if ($caps -contains 'completion' -and $caps -contains 'tools') { $toolModels += $name }
      } catch { }
    }
  } catch { }
  $script:OllamaInventory = @{ models = $models; toolModels = $toolModels }
  $script:OllamaInventoryAt = [DateTime]::UtcNow
  return $script:OllamaInventory
}
function Invoke-OllamaInference($Socket, $Message) {
  try {
    $model = [string]$Message.model
    if ($model -notmatch '^[a-zA-Z0-9][a-zA-Z0-9._:/-]{0,127}$') { throw 'Invalid model name.' }
    $body = @{ model = $model; stream = $false; messages = @($Message.messages); options = @{ num_predict = 2048 } }
    if ($Message.tools) { $body.tools = @($Message.tools) }
    $response = Invoke-RestMethod -Uri 'http://127.0.0.1:11434/api/chat' -Method Post -ContentType 'application/json' `
      -Body (ConvertTo-Json -InputObject $body -Depth 24 -Compress) -TimeoutSec 85
    if (-not $response.message -or $response.message.role -ne 'assistant') { throw 'Ollama returned an invalid assistant message.' }
    Send-Json $Socket @{ t = 'node.infer.result'; requestId = [string]$Message.requestId; message = $response.message }
  } catch {
    $detail = [string]$_.Exception.Message
    Send-Json $Socket @{ t = 'node.infer.error'; requestId = [string]$Message.requestId; error = $detail.Substring(0, [math]::Min(220, $detail.Length)) }
  }
}
function Get-NodeStatus {
  $os = Get-CimInstance Win32_OperatingSystem
  $cpu = Get-CimInstance Win32_Processor
  $load = $cpu | Measure-Object -Property LoadPercentage -Average
  $total = [math]::Round(([double]$os.TotalVisibleMemorySize / 1024), 1)
  $used = [math]::Max(0, $total - [double]$os.FreePhysicalMemory / 1024)
  $disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID = 'C:'" | Select-Object -First 1
  $caps = @('telemetry')
  $ollama = Get-OllamaInventory
  $gpu = Get-GpuMetrics
  if ($gpu.Count -gt 0) { $caps += 'gpu' }
  if (Get-Process -Name 'ollama','vllm' -ErrorAction SilentlyContinue | Select-Object -First 1) { $caps += 'ai-inference' }
  if (Get-Process -Name 'GutsAndBoltsGameServer' -ErrorAction SilentlyContinue | Select-Object -First 1) { $caps += 'game-hosting' }
  $result = @{
    hostname = $env:COMPUTERNAME
    platform = 'Windows'
    osVersion = [string]$os.Caption + ' ' + [string]$os.Version
    cpuPercent = [math]::Round([double]$load.Average, 1)
    cpuCores = [int](($cpu | Measure-Object -Property NumberOfCores -Sum).Sum)
    memoryPercent = if ($total -gt 0) { [math]::Round(($used / $total) * 100, 1) } else { 0 }
    memoryUsedMB = $used
    memoryTotalMB = $total
    diskUsedGB = if ($disk) { [math]::Round(([double]$disk.Size - [double]$disk.FreeSpace) / 1GB, 1) } else { $null }
    diskTotalGB = if ($disk) { [math]::Round([double]$disk.Size / 1GB, 1) } else { $null }
    agentVersion = $AgentVersion
    capabilities = $caps
    models = @($ollama.models)
    toolModels = @($ollama.toolModels)
    services = @()
  }
  foreach ($key in $gpu.Keys) { $result[$key] = $gpu[$key] }
  return $result
}
function Handle-NodeMessage($Socket, $Message) {
  switch ([string]$Message.t) {
    'pair.complete' {
      Save-DeviceId ([string]$Message.id)
      Save-Token ([string]$Message.token)
      $script:PairingCode = $null
      $script:PairingCreatedAt = $null
      Write-Host 'Machine paired with G&B Nexus. Credential protected using Windows DPAPI.' -ForegroundColor Green
      Send-Json $Socket @{ t = 'node.status'; status = (Get-NodeStatus) }
    }
    'node.ready' {
      Write-Host 'Connected to the G&B Nexus infrastructure relay.' -ForegroundColor Green
      Send-Json $Socket @{ t = 'node.status'; status = (Get-NodeStatus) }
    }
    'node.infer' { Invoke-OllamaInference $Socket $Message }
    'device.rejected' {
      Remove-Item -LiteralPath $TokenPath,$DevicePath -Force -ErrorAction SilentlyContinue
      throw 'This machine credential was revoked or rejected. Reconnect to generate a new pairing code.'
    }
    'pair.rate-limited' { throw 'Pairing attempts are rate-limited. Wait before reconnecting.' }
    default { }
  }
}
function Connect-Node {
  $socket = [Net.WebSockets.ClientWebSocket]::new()
  $socket.Options.KeepAliveInterval = [TimeSpan]::FromSeconds(20)
  try {
    $socket.ConnectAsync($BaseUri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    $token = Load-Token
    if ($token -and (Test-Path -LiteralPath $DevicePath)) {
      $id = (Get-Content -LiteralPath $DevicePath -Raw).Trim()
      if ($id -notmatch '^[a-f0-9]{32}$') { throw 'Saved device ID is invalid. Remove the local agent credentials and pair again.' }
      Send-Json $socket @{ t = 'node.auth'; id = $id; token = $token }
    } else {
      $newCode = -not $script:PairingCode
      if ($newCode) { $script:PairingCode = New-PairingCode; $script:PairingCreatedAt = [DateTime]::UtcNow }
      Send-Json $socket @{ t = 'node.pair.begin'; code = $script:PairingCode; name = $env:COMPUTERNAME; platform = 'Windows' }
      if ($newCode) {
        Write-Host ''
        Write-Host 'G&B NEXUS MACHINE PAIRING' -ForegroundColor Yellow
        Write-Host ('Open ' + $Dashboard.AbsoluteUri + ' while signed into the owner account.') -ForegroundColor Yellow
        Write-Host 'Paste this one-time code into Machine fleet (expires in five minutes):' -ForegroundColor Yellow
        Write-Host $script:PairingCode -ForegroundColor Cyan
        Write-Host 'Keep this code private. Press Ctrl+C to stop the agent.' -ForegroundColor Yellow
      }
    }
    $buffer = New-Object byte[] 16384
    $segment = [ArraySegment[byte]]::new($buffer)
    $receive = $socket.ReceiveAsync($segment, [Threading.CancellationToken]::None)
    while ($socket.State -eq [Net.WebSockets.WebSocketState]::Open) {
      $completed = [Threading.Tasks.Task]::WhenAny($receive, [Threading.Tasks.Task]::Delay(10000)).GetAwaiter().GetResult()
      if ([object]::ReferenceEquals($completed, $receive)) {
        $frame = $receive.GetAwaiter().GetResult()
        if ($frame.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { break }
        $stream = [IO.MemoryStream]::new()
        try {
          do {
            $stream.Write($buffer, 0, $frame.Count)
            if ($stream.Length -gt 400 * 1024) { throw 'Relay message exceeded its size limit.' }
            if (-not $frame.EndOfMessage) { $frame = $socket.ReceiveAsync($segment, [Threading.CancellationToken]::None).GetAwaiter().GetResult() }
          } while (-not $frame.EndOfMessage)
          $message = [Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
        } finally { $stream.Dispose() }
        Handle-NodeMessage $socket $message
        $receive = $socket.ReceiveAsync($segment, [Threading.CancellationToken]::None)
      } else {
        if (-not $token -and $script:PairingCreatedAt -and ([DateTime]::UtcNow - $script:PairingCreatedAt).TotalMinutes -ge 5) {
          $script:PairingCode = $null; $script:PairingCreatedAt = $null
          throw 'Pairing code expired. Reconnecting to generate a new code.'
        }
        Send-Json $socket @{ t = 'node.status'; status = (Get-NodeStatus) }
      }
    }
  } finally {
    try { $socket.Abort() } catch {}
    $socket.Dispose()
  }
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'This agent only runs on Windows.' }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Run this agent as a normal, non-elevated Windows user.'
}
Ensure-Root
Write-Host 'G&B Nexus Infrastructure Agent ' $AgentVersion
Write-Host 'Telemetry and narrowly scoped local Ollama inference relay. No shell or arbitrary command execution.'
while ($true) {
  try { Connect-Node }
  catch { Write-Warning $_.Exception.Message }
  Start-Sleep -Seconds 5
}
