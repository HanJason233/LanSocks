param(
  [Parameter(Mandatory = $true)]
  [string]$SocksAddress,

  [Parameter(Mandatory = $true)]
  [ValidateRange(1, 65535)]
  [int]$SocksPort,

  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug',

  [string]$SocksUsername,
  [string]$SocksPassword
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bundleDirectory = Join-Path $projectRoot "build\windows\x64\runner\$Configuration"
$runnerPath = Join-Path $bundleDirectory 'lansocks.exe'
$helperPath = Join-Path $bundleDirectory 'data\lansocks-helper.exe'
$localAppData = [Environment]::GetFolderPath('LocalApplicationData')
$runtimeDirectory = Join-Path $localAppData 'LanSocks\runtime'
$recoveryDirectory = Join-Path $localAppData 'LanSocks\recovery'
$configPath = Join-Path $runtimeDirectory 'hev.yml'
$statePath = Join-Path $recoveryDirectory 'session.bin'
$dnsSnapshotPath = Join-Path $recoveryDirectory 'dns.bin'
$mapDnsAddress = '198.18.0.2'
$mappedNetwork = '100.64.0.0/10'
$tunnelName = 'LanSocks'
$etlPath = Join-Path $env:TEMP ("lansocks-dns-{0}.etl" -f [guid]::NewGuid())
$pcapPath = [IO.Path]::ChangeExtension($etlPath, '.pcapng')
$appProcess = $null
$tunnelProcess = $null
$pktmonStarted = $false

function Assert-Test($condition, [string]$message) {
  if (-not $condition) { throw $message }
  Write-Host "PASS: $message"
}

function Get-DnsSnapshot {
  $activeIndexes = @(Get-NetAdapter -IncludeHidden |
    Where-Object { $_.Status -eq 'Up' -and $_.InterfaceDescription -notmatch 'Loopback' } |
    Select-Object -ExpandProperty ifIndex | Sort-Object -Unique)
  @(
    Get-DnsClientServerAddress |
      Where-Object { $activeIndexes -contains $_.InterfaceIndex } |
      ForEach-Object {
        [pscustomobject]@{
          InterfaceIndex = $_.InterfaceIndex
          AddressFamily = [int]$_.AddressFamily
          Servers = (@($_.ServerAddresses) -join ',')
        }
      } |
      Sort-Object InterfaceIndex, AddressFamily
  )
}

function Get-RouteSnapshot {
  $prefixes = @(
    "$SocksAddress/32",
    "$mapDnsAddress/32",
    $mappedNetwork,
    '0.0.0.0/1',
    '128.0.0.0/1'
  )
  @(
    Get-NetRoute -AddressFamily IPv4 -PolicyStore ActiveStore |
      Where-Object { $prefixes -contains $_.DestinationPrefix } |
      ForEach-Object {
        [pscustomobject]@{
          Prefix = $_.DestinationPrefix
          InterfaceIndex = $_.InterfaceIndex
          NextHop = $_.NextHop
          Metric = $_.RouteMetric
        }
      } |
      Sort-Object Prefix, InterfaceIndex, NextHop, Metric
  )
}

function Compare-Snapshots($before, $after, [string]$name) {
  $left = ConvertTo-Json -InputObject @($before) -Compress -Depth 5
  $right = ConvertTo-Json -InputObject @($after) -Compress -Depth 5
  Assert-Test ($left -ceq $right) "$name restored to its pre-test state"
}

function Quote-Argument([string]$value) {
  '"' + $value.Replace('"', '\"') + '"'
}

function Invoke-HelperCommand([string]$command) {
  $process = Start-Process -FilePath $helperPath -ArgumentList $command `
    -WindowStyle Hidden -PassThru -Wait
  if ($process.ExitCode -ne 0) {
    $lastError = Join-Path $recoveryDirectory 'last-error.txt'
    $detail = if (Test-Path -LiteralPath $lastError) {
      Get-Content -LiteralPath $lastError -Raw -Encoding utf8
    } else { "helper exit $($process.ExitCode)" }
    throw "Helper command failed: $detail"
  }
}

function Find-BestRoute([string]$address) {
  Find-NetRoute -RemoteIPAddress $address |
    Where-Object { $_.PSObject.Properties['DestinationPrefix'] } |
    Select-Object -First 1
}

function Assert-MappedAddress($answer, [string]$description) {
  $addresses = @($answer | Where-Object { $_.Type -eq 'A' } |
    ForEach-Object { [Net.IPAddress]::Parse($_.IPAddress) })
  $isMapped = $false
  foreach ($address in $addresses) {
    $bytes = $address.GetAddressBytes()
    if ($address.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork -and
        $bytes[0] -eq 100 -and $bytes[1] -ge 64 -and $bytes[1] -le 127) {
      $isMapped = $true
      break
    }
  }
  Assert-Test $isMapped "$description returned an HEV synthetic IPv4 address"
}

function Read-U16([byte[]]$bytes, [int]$offset, [bool]$littleEndian) {
  if ($littleEndian) { return [BitConverter]::ToUInt16($bytes, $offset) }
  [byte[]]$ordered = @($bytes[$offset + 1], $bytes[$offset])
  [BitConverter]::ToUInt16($ordered, 0)
}

function Read-U32([byte[]]$bytes, [int]$offset, [bool]$littleEndian) {
  if ($littleEndian) { return [BitConverter]::ToUInt32($bytes, $offset) }
  [byte[]]$ordered = @($bytes[$offset + 3], $bytes[$offset + 2], $bytes[$offset + 1], $bytes[$offset])
  [BitConverter]::ToUInt32($ordered, 0)
}

function Test-DnsPort([byte[]]$packet, [int]$linkType) {
  if ($packet.Length -lt 20) { return $false }
  $offset = 0
  $etherType = 0
  if ($linkType -eq 1) {
    if ($packet.Length -lt 14) { return $false }
    $etherType = ([int]$packet[12] -shl 8) -bor [int]$packet[13]
    $offset = 14
    while ($etherType -in @(0x8100, 0x88a8, 0x9100)) {
      if ($packet.Length -lt ($offset + 4)) { return $false }
      $etherType = ([int]$packet[$offset + 2] -shl 8) -bor [int]$packet[$offset + 3]
      $offset += 4
    }
  } elseif ($linkType -eq 101) {
    $version = $packet[0] -shr 4
    $etherType = if ($version -eq 4) { 0x0800 } elseif ($version -eq 6) { 0x86dd } else { 0 }
  } else {
    return $false
  }

  $protocol = 0
  if ($etherType -eq 0x0800) {
    if ($packet.Length -lt ($offset + 20)) { return $false }
    $headerLength = ($packet[$offset] -band 0x0f) * 4
    if ($headerLength -lt 20) { return $false }
    $protocol = $packet[$offset + 9]
    $offset += $headerLength
  } elseif ($etherType -eq 0x86dd) {
    if ($packet.Length -lt ($offset + 40)) { return $false }
    $protocol = $packet[$offset + 6]
    $offset += 40
    for ($hop = 0; $hop -lt 8 -and $protocol -in @(0, 43, 44, 51, 60); $hop++) {
      if ($packet.Length -lt ($offset + 2)) { return $false }
      $next = $packet[$offset]
      $extensionLength = if ($protocol -eq 44) { 8 } elseif ($protocol -eq 51) {
        ($packet[$offset + 1] + 2) * 4
      } else { ($packet[$offset + 1] + 1) * 8 }
      $protocol = $next
      $offset += $extensionLength
    }
  } else {
    return $false
  }
  if ($protocol -notin @(6, 17) -or $packet.Length -lt ($offset + 4)) { return $false }
  $sourcePort = ([int]$packet[$offset] -shl 8) -bor [int]$packet[$offset + 1]
  $destPort = ([int]$packet[$offset + 2] -shl 8) -bor [int]$packet[$offset + 3]
  return $sourcePort -eq 53 -or $destPort -eq 53
}

function Assert-NoPhysicalDnsPackets([string[]]$physicalNames) {
  $bytes = [IO.File]::ReadAllBytes($pcapPath)
  $littleEndian = $true
  $interfaceNames = [Collections.Generic.List[string]]::new()
  $linkTypes = [Collections.Generic.List[int]]::new()
  $leaked = $false
  $capturedDnsPackets = 0
  $offset = 0
  while ($offset + 12 -le $bytes.Length) {
    $blockType = [BitConverter]::ToUInt32($bytes, $offset)
    if ($blockType -eq 0x0a0d0d0a) {
      $byteOrder = [BitConverter]::ToUInt32($bytes, $offset + 8)
      $littleEndian = $byteOrder -eq 0x1a2b3c4d
    }
    $blockLength = Read-U32 $bytes ($offset + 4) $littleEndian
    if ($blockLength -lt 12 -or $offset + $blockLength -gt $bytes.Length) { break }
    if ($blockType -eq 1) {
      $linkType = Read-U16 $bytes ($offset + 8) $littleEndian
      $linkTypes.Add([int]$linkType)
      $name = ''
      $optionOffset = $offset + 16
      $blockEnd = $offset + $blockLength - 4
      while ($optionOffset + 4 -le $blockEnd) {
        $code = Read-U16 $bytes $optionOffset $littleEndian
        $length = Read-U16 $bytes ($optionOffset + 2) $littleEndian
        $optionOffset += 4
        if ($code -eq 0) { break }
        if ($optionOffset + $length -gt $blockEnd) { break }
        if ($code -in @(2, 3)) {
          $name += ' ' + [Text.Encoding]::UTF8.GetString($bytes, $optionOffset, $length)
        }
        $optionOffset += (($length + 3) -band (-bnot 3))
      }
      $interfaceNames.Add($name)
    } elseif ($blockType -eq 6 -and $blockLength -ge 32) {
      $interfaceId = [int](Read-U32 $bytes ($offset + 8) $littleEndian)
      $capturedLength = [int](Read-U32 $bytes ($offset + 20) $littleEndian)
      $packetOffset = $offset + 28
      if ($interfaceId -lt $interfaceNames.Count -and
          $capturedLength -gt 0 -and $packetOffset + $capturedLength -le $bytes.Length) {
        $interfaceName = $interfaceNames[$interfaceId]
        $isPhysical = $false
        foreach ($physicalName in $physicalNames) {
          if ($interfaceName -match [regex]::Escape($physicalName)) { $isPhysical = $true }
        }
        if ($isPhysical) {
          [byte[]]$packet = $bytes[$packetOffset..($packetOffset + $capturedLength - 1)]
          if (Test-DnsPort $packet $linkTypes[$interfaceId]) {
            $leaked = $true
            $capturedDnsPackets++
          }
        } elseif (Test-DnsPort ([byte[]]$bytes[$packetOffset..($packetOffset + $capturedLength - 1)]) $linkTypes[$interfaceId]) {
          $capturedDnsPackets++
        }
      }
    }
    $offset += $blockLength
  }

  $matchedPhysical = @($interfaceNames | Where-Object {
    $candidate = $_
    @($physicalNames | Where-Object { $candidate -match [regex]::Escape($_) }).Count -gt 0
  })
  Assert-Test ($matchedPhysical.Count -gt 0) 'Pktmon capture identifies the active physical NIC'
  Assert-Test ($capturedDnsPackets -gt 0) 'Pktmon captured DNS traffic for the system lookup'
  Assert-Test (-not $leaked) 'no UDP/TCP DNS packets were captured on the physical NIC'
}

function Write-HevConfig([string]$destination, [bool]$invalidMapDns = $false) {
  $escapedUsername = if ($SocksUsername) { $SocksUsername.Replace("'", "''") } else { '' }
  $escapedPassword = if ($SocksPassword) { $SocksPassword.Replace("'", "''") } else { '' }
  $lines = @(
    'tunnel:',
    '  name: LanSocks',
    '  guid: 6f06c4a2-6f95-4f25-98f0-cf4608d50b41',
    '  mtu: 1500',
    '  multi-queue: false',
    '  ipv4: 198.18.0.1',
    "  icmp: 'off'",
    'socks5:',
    "  address: $SocksAddress",
    "  port: $SocksPort",
    "  udp: 'udp'"
  )
  if ($SocksUsername -or $SocksPassword) {
    if (-not $SocksUsername -or -not $SocksPassword) { throw 'Both SOCKS5 credentials must be supplied.' }
    $lines += "  username: '$escapedUsername'"
    $lines += "  password: '$escapedPassword'"
  }
  $configuredMapDns = if ($invalidMapDns) { '198.18.0.3' } else { $mapDnsAddress }
  $lines += @(
    'mapdns:',
    "  address: $configuredMapDns",
    '  port: 53',
    '  network: 100.64.0.0',
    '  netmask: 255.192.0.0',
    '  cache-size: 10000',
    'misc:',
    '  log-file: stdout',
    '  log-level: info'
  )
  [IO.File]::WriteAllText($destination, ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
}

function Start-TestTunnel([string]$parentPid) {
  $arguments = '--start {0} {1} {2} {3}' -f `
    (Quote-Argument $configPath), (Quote-Argument $SocksAddress), $SocksPort, $parentPid
  $script:tunnelProcess = Start-Process -FilePath $helperPath -ArgumentList $arguments `
    -WindowStyle Hidden -PassThru
  $deadline = (Get-Date).AddSeconds(30)
  do {
    if (Test-Path -LiteralPath $statePath) {
      $stateBytes = [IO.File]::ReadAllBytes($statePath)
      $adapter = Get-NetAdapter -Name $tunnelName -ErrorAction SilentlyContinue
      if ($stateBytes.Length -ge 12 -and [BitConverter]::ToUInt32($stateBytes, 8) -eq 2 -and
          $adapter -and $adapter.Status -eq 'Up') { return }
    }
    if ($script:tunnelProcess.HasExited) {
      $lastError = Join-Path $recoveryDirectory 'last-error.txt'
      $detail = if (Test-Path -LiteralPath $lastError) {
        Get-Content -LiteralPath $lastError -Raw -Encoding utf8
      } else { "helper exit $($script:tunnelProcess.ExitCode)" }
      throw "Tunnel startup failed: $detail"
    }
    Start-Sleep -Milliseconds 250
  } while ((Get-Date) -lt $deadline)
  throw 'Timed out waiting for the LanSocks TUN adapter.'
}

function Test-ActiveTunnel([string[]]$physicalNames) {
  $tunnelAdapter = Get-NetAdapter -Name $tunnelName -ErrorAction Stop
  Assert-Test ($tunnelAdapter.Status -eq 'Up') 'TUN adapter is up'
  $activeAdapters = @(Get-NetAdapter -IncludeHidden |
    Where-Object { $_.Status -eq 'Up' -and $_.InterfaceDescription -notmatch 'Loopback' })
  $activeIndexes = @($activeAdapters | Select-Object -ExpandProperty ifIndex)
  foreach ($entry in (Get-DnsClientServerAddress -AddressFamily IPv4 |
      Where-Object { $activeIndexes -contains $_.InterfaceIndex })) {
    $servers = @($entry.ServerAddresses | Sort-Object -Unique)
    Assert-Test (($servers.Count -eq 1) -and ($servers[0] -eq $mapDnsAddress)) `
      "IPv4 DNS on $($entry.InterfaceAlias) points only to HEV MapDNS"
  }
  foreach ($entry in (Get-DnsClientServerAddress -AddressFamily IPv6 |
      Where-Object { $activeIndexes -contains $_.InterfaceIndex })) {
    $servers = @($entry.ServerAddresses | Sort-Object -Unique)
    Assert-Test (($servers.Count -eq 1) -and ($servers[0] -eq '::1')) `
      "IPv6 DNS on $($entry.InterfaceAlias) is blocked locally"
  }

  $mapRoute = Find-BestRoute $mapDnsAddress
  Assert-Test ($mapRoute.InterfaceIndex -eq $tunnelAdapter.ifIndex) '198.18.0.2/32 routes through the TUN'
  $mappedRoute = Find-BestRoute '100.64.0.1'
  Assert-Test ($mappedRoute.InterfaceIndex -eq $tunnelAdapter.ifIndex) '100.64.0.0/10 routes through the TUN'
  $socksRoute = Find-BestRoute $SocksAddress
  Assert-Test ($socksRoute.InterfaceIndex -ne $tunnelAdapter.ifIndex) 'SOCKS5 endpoint bypasses the TUN'

  Clear-DnsClientCache
  $explicit = Resolve-DnsName 'example.com.' -Type A -Server $mapDnsAddress -DnsOnly -NoHostsFile
  Assert-MappedAddress $explicit 'explicit HEV MapDNS query'
  Clear-DnsClientCache
  $system = Resolve-DnsName 'example.com.' -Type A -DnsOnly -NoHostsFile
  Assert-MappedAddress $system 'Windows system DNS query'

  $socksAdapter = Get-NetAdapter -InterfaceIndex $socksRoute.InterfaceIndex -ErrorAction Stop
  Assert-Test ($socksAdapter.HardwareInterface -eq $true) 'SOCKS route uses a physical network adapter'
  $physicalNames = @(Get-NetAdapter -IncludeHidden |
    Where-Object { $_.Status -eq 'Up' -and $_.HardwareInterface -eq $true } |
    Select-Object -ExpandProperty Name)
  if ($physicalNames -notcontains $socksAdapter.Name) { $physicalNames += $socksAdapter.Name }
  $script:physicalNames = $physicalNames
}

function Run-StartStopTest([string]$parentPid) {
  $dnsBefore = Get-DnsSnapshot
  $routesBefore = Get-RouteSnapshot
  Start-TestTunnel $parentPid
  Test-ActiveTunnel $script:physicalNames

  $pktmon = Get-Command pktmon.exe -ErrorAction Stop
  $status = (& $pktmon.Source status 2>&1 | Out-String)
  if ($status -match '(?i)\b(running|capturing|started)\b' -and
      $status -notmatch '(?i)\bnot\s+(running|capturing|started)\b') {
    throw 'Pktmon is already active; stop the existing capture before running this integration test.'
  }
  & $pktmon.Source start --capture --comp nics --pkt-size 0 --file-name $etlPath | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'Pktmon could not start the physical-NIC DNS leak capture.' }
  $script:pktmonStarted = $true
  Clear-DnsClientCache
  Resolve-DnsName 'example.com.' -Type A -DnsOnly -NoHostsFile | Out-Null
  & $pktmon.Source stop | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'Pktmon could not stop its capture.' }
  $script:pktmonStarted = $false
  & $pktmon.Source etl2pcap $etlPath --out $pcapPath | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'Pktmon could not convert the capture to pcapng.' }
  Assert-NoPhysicalDnsPackets $script:physicalNames

  Invoke-HelperCommand '--stop'
  if ($script:tunnelProcess -and -not $script:tunnelProcess.HasExited) {
    $script:tunnelProcess.WaitForExit(30000) | Out-Null
  }
  Compare-Snapshots $dnsBefore (Get-DnsSnapshot) 'DNS settings'
  Compare-Snapshots $routesBefore (Get-RouteSnapshot) 'routes'
  Assert-Test (-not (Test-Path -LiteralPath $dnsSnapshotPath)) 'DNS recovery snapshot removed after disconnect'
}

function Run-CrashRecoveryTest([string]$parentPid) {
  $dnsBefore = Get-DnsSnapshot
  $routesBefore = Get-RouteSnapshot
  Start-TestTunnel $parentPid
  Test-ActiveTunnel $script:physicalNames
  Stop-Process -Id $script:tunnelProcess.Id -Force
  Start-Sleep -Milliseconds 500
  Invoke-HelperCommand '--recover'
  Compare-Snapshots $dnsBefore (Get-DnsSnapshot) 'DNS settings after helper crash recovery'
  Compare-Snapshots $routesBefore (Get-RouteSnapshot) 'routes after helper crash recovery'
  Assert-Test (-not (Test-Path -LiteralPath $dnsSnapshotPath)) 'crash recovery snapshot removed'
  $script:tunnelProcess = $null
}

function Run-StartupFailureTest([string]$parentPid) {
  $dnsBefore = Get-DnsSnapshot
  $routesBefore = Get-RouteSnapshot
  Write-HevConfig $configPath $true
  $arguments = '--start {0} {1} {2} {3}' -f `
    (Quote-Argument $configPath), (Quote-Argument $SocksAddress), $SocksPort, $parentPid
  $failed = Start-Process -FilePath $helperPath -ArgumentList $arguments `
    -WindowStyle Hidden -PassThru -Wait
  Assert-Test ($failed.ExitCode -ne 0) 'invalid MapDNS startup is rejected'
  Compare-Snapshots $dnsBefore (Get-DnsSnapshot) 'DNS settings after startup failure'
  Compare-Snapshots $routesBefore (Get-RouteSnapshot) 'routes after startup failure'
  Assert-Test (-not (Test-Path -LiteralPath $dnsSnapshotPath)) 'failed startup left no DNS recovery snapshot'
}

try {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  Assert-Test ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) `
    'running with administrator rights'
  $parsedAddress = [Net.IPAddress]::Parse($SocksAddress)
  Assert-Test ($parsedAddress.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork) `
    'SOCKS5 endpoint is IPv4'
  if (-not (Test-Path -LiteralPath $runnerPath) -or -not (Test-Path -LiteralPath $helperPath)) {
    throw "Build the $Configuration Windows bundle before running this integration test."
  }
  if ((Test-Path -LiteralPath $statePath) -or (Test-Path -LiteralPath $dnsSnapshotPath) -or
      (Get-NetAdapter -Name $tunnelName -ErrorAction SilentlyContinue)) {
    throw 'An existing LanSocks session or recovery record was found; recover it before testing.'
  }

  New-Item -ItemType Directory -Force -Path $runtimeDirectory, $recoveryDirectory | Out-Null
  Write-HevConfig $configPath
  $appProcess = Start-Process -FilePath $runnerPath -WindowStyle Hidden -PassThru
  Start-Sleep -Milliseconds 750
  if ($appProcess.HasExited) { throw 'Could not keep the LanSocks parent process alive for the helper.' }

  Run-StartupFailureTest ([string]$appProcess.Id)
  Write-HevConfig $configPath
  Run-StartStopTest ([string]$appProcess.Id)
  Write-HevConfig $configPath
  Run-CrashRecoveryTest ([string]$appProcess.Id)
  Write-Host 'All Windows TUN DNS integration scenarios passed.'
} finally {
  if ($pktmonStarted) {
    pktmon stop | Out-Null
  }
  if ((Test-Path -LiteralPath $statePath) -or (Test-Path -LiteralPath $dnsSnapshotPath)) {
    try { Invoke-HelperCommand '--recover' } catch { Write-Error $_ }
  }
  if ($appProcess -and -not $appProcess.HasExited) {
    Stop-Process -Id $appProcess.Id -Force
  }
  foreach ($path in @($etlPath, $pcapPath)) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
  }
}
