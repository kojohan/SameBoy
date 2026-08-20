[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$RomPath,

    [ValidateRange(5, 120)]
    [int]$TimeoutSeconds = 20,

    [ValidateRange(1024, 65534)]
    [int]$BasePort = 45970,

    [ValidateRange(0, 4294967294)]
    [uint32]$SessionId = 0,

    [switch]$SkipBuild,

    [switch]$ShowWindows
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-Log {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ""
    }
    $stream = [System.IO.File]::Open($Path,
                                     [System.IO.FileMode]::Open,
                                     [System.IO.FileAccess]::Read,
                                     [System.IO.FileShare]::ReadWrite)
    $reader = [System.IO.StreamReader]::new($stream)
    try {
        return $reader.ReadToEnd()
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Wait-LogPattern {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Pattern,
        [Parameter(Mandatory)][string]$Description,
        [Parameter(Mandatory)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory)][int]$Timeout
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($Timeout)
    do {
        if ((Read-Log -Path $Path) -match $Pattern) {
            return
        }
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "$Description failed: process $($Process.Id) exited with code $($Process.ExitCode). See $Path"
        }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "$Description timed out after $Timeout seconds. See $Path"
}

function Wait-LogPatternCount {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Pattern,
        [Parameter(Mandatory)][int]$Count,
        [Parameter(Mandatory)][string]$Description,
        [Parameter(Mandatory)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory)][int]$Timeout
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($Timeout)
    do {
        if ([regex]::Matches((Read-Log -Path $Path), $Pattern).Count -ge $Count) {
            return
        }
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "$Description failed: process $($Process.Id) exited with code $($Process.ExitCode). See $Path"
        }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "$Description timed out after $Timeout seconds. See $Path"
}

function Assert-LogPattern {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Pattern,
        [Parameter(Mandatory)][string]$Description
    )

    if ((Read-Log -Path $Path) -notmatch $Pattern) {
        throw "$Description was not found in $Path"
    }
}

function Assert-NoFatalLog {
    param([Parameter(Mandatory)][string[]]$Paths)

    $fatalPattern = "(?im)(Could not |GLSL (Shader|Program) Error|Assertion failed|AddressSanitizer|fatal error)"
    foreach ($path in $Paths) {
        $match = [regex]::Match((Read-Log -Path $path), $fatalPattern)
        if ($match.Success) {
            throw "Fatal log entry '$($match.Value.Trim())' found in $path"
        }
    }
}

function Stop-TestProcess {
    param([System.Diagnostics.Process]$Process)

    if (-not $Process) {
        return
    }
    $Process.Refresh()
    if ($Process.HasExited) {
        return
    }

    if ($Process.MainWindowHandle -ne 0) {
        $null = $Process.CloseMainWindow()
        if ($Process.WaitForExit(3000)) {
            return
        }
    }

    Stop-Process -Id $Process.Id -ErrorAction SilentlyContinue
    $null = $Process.WaitForExit(3000)
}

function Start-SameBoyTestProcess {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string[]]$Arguments
    )

    $stdoutPath = Join-Path $script:LogDirectory "$Name.stdout.log"
    $stderrPath = Join-Path $script:LogDirectory "$Name.stderr.log"
    $windowStyle = if ($ShowWindows) { "Normal" } else { "Minimized" }
    $process = Start-Process -FilePath $script:Executable `
                             -ArgumentList $Arguments `
                             -WorkingDirectory $script:RepositoryRoot `
                             -RedirectStandardOutput $stdoutPath `
                             -RedirectStandardError $stderrPath `
                             -WindowStyle $windowStyle `
                             -PassThru
    return [pscustomobject]@{
        Name = $Name
        Process = $process
        StdoutPath = $stdoutPath
        StderrPath = $stderrPath
    }
}

function Assert-PortAvailable {
    param([Parameter(Mandatory)][int]$Port)

    $probe = $null
    try {
        $probe = [System.Net.Sockets.UdpClient]::new($Port)
    }
    catch {
        throw "UDP port $Port is unavailable. Select another port with -BasePort."
    }
    finally {
        if ($probe) {
            $probe.Dispose()
        }
    }
}

function Invoke-StartupSmoke {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$ReadyPattern
    )

    Write-Host "[RUN ] $Name"
    $testProcess = $null
    try {
        $testProcess = Start-SameBoyTestProcess -Name $Name -Arguments $Arguments
        Wait-LogPattern -Path $testProcess.StderrPath `
                        -Pattern $ReadyPattern `
                        -Description $Name `
                        -Process $testProcess.Process `
                        -Timeout $TimeoutSeconds
        Assert-NoFatalLog -Paths @($testProcess.StderrPath)
        Write-Host "[PASS] $Name"
    }
    finally {
        if ($testProcess) {
            Stop-TestProcess -Process $testProcess.Process
        }
    }
}

function Invoke-RemoteLoopback {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][int]$Port,
        [Parameter(Mandatory)][uint32]$TestSessionId,
        [Parameter(Mandatory)][string]$ExpectedPresentation,
        [switch]$DisableClientGl,
        [switch]$StopHostFirst
    )

    Write-Host "[RUN ] $Name"
    Assert-PortAvailable -Port $Port
    $hostTest = $null
    $clientTest = $null
    try {
        $hostArguments = @(
            "--remote-input-host", $Port.ToString(),
            "--remote-session", $TestSessionId.ToString(),
            "--remote-host-view", "p1",
            $script:QuotedRomPath
        )
        $hostTest = Start-SameBoyTestProcess -Name "$Name-host" -Arguments $hostArguments
        Wait-LogPattern -Path $hostTest.StderrPath `
                        -Pattern "remote_input_host listening_udp_port=$Port" `
                        -Description "$Name host startup" `
                        -Process $hostTest.Process `
                        -Timeout $TimeoutSeconds

        $clientArguments = @()
        if ($DisableClientGl) {
            $clientArguments += "--nogl"
        }
        $clientArguments += @(
            "--remote-input-client", "127.0.0.1:$Port",
            "--remote-session", $TestSessionId.ToString()
        )
        $clientTest = Start-SameBoyTestProcess -Name "$Name-client" -Arguments $clientArguments

        Wait-LogPattern -Path $clientTest.StderrPath `
                        -Pattern "remote_video_client frames=1[2-9][0-9].*dropped=0 rejected=0" `
                        -Description "$Name video stream" `
                        -Process $clientTest.Process `
                        -Timeout $TimeoutSeconds
        Wait-LogPattern -Path $clientTest.StderrPath `
                        -Pattern "remote_audio_client packets=([6-9][0-9]{2}|[0-9]{4,}).*dropped=0 stale=0.*arrival_samples=[1-9][0-9]*.*arrival_p50_ms=.*arrival_p95_ms=.*arrival_p99_ms=.*decode_errors=0" `
                        -Description "$Name audio stream" `
                        -Process $clientTest.Process `
                        -Timeout $TimeoutSeconds
        Assert-LogPattern -Path $clientTest.StderrPath `
                          -Pattern "remote_clock_sync_client samples=[1-9][0-9]* t_ms=[0-9]+ rtt_ms=.*jitter_ms=" `
                          -Description "$Name timestamped clock telemetry"

        Assert-LogPattern -Path $clientTest.StderrPath `
                          -Pattern "remote_client_presentation=$ExpectedPresentation" `
                          -Description "$Name presentation mode"
        if ($ExpectedPresentation -eq "OpenGL") {
            Assert-LogPattern -Path $clientTest.StderrPath `
                              -Pattern "remote_client_presentation=OpenGL.*vsync=(adaptive|off-fallback)" `
                              -Description "$Name adaptive VSync selection"
        }
        Assert-LogPattern -Path $hostTest.StderrPath `
                          -Pattern "remote_input_host client_active first_sequence=1" `
                          -Description "$Name host input reception"
        Assert-LogPattern -Path $hostTest.StderrPath `
                          -Pattern "remote_host_notice text=player_2_connected duration_frames=120" `
                          -Description "$Name Player 2 host notice"
        Assert-LogPattern -Path $clientTest.StderrPath `
                          -Pattern "remote_client_notice text=player_1_connected duration_ms=2000" `
                          -Description "$Name Player 1 client notice"
        Assert-LogPattern -Path $hostTest.StderrPath `
                          -Pattern "remote_video_host first_stream_frame=1" `
                          -Description "$Name host video transmission"
        Assert-LogPattern -Path $hostTest.StderrPath `
                          -Pattern "remote_video_host first_stream_frame=1.*burst_us=" `
                          -Description "$Name host video burst telemetry"
        Assert-LogPattern -Path $clientTest.StderrPath `
                          -Pattern "remote_video_client frames=1[2-9][0-9].*receive_span_average_ms=" `
                          -Description "$Name client receive-span telemetry"
        Assert-LogPattern -Path $hostTest.StderrPath `
                          -Pattern "remote_audio_host packets=([6-9][0-9]{2}|[0-9]{4,}) dropped=0" `
                          -Description "$Name host audio transmission"
        if ($StopHostFirst) {
            Stop-TestProcess -Process $hostTest.Process
            Wait-LogPattern -Path $clientTest.StderrPath `
                            -Pattern "remote_client_notice text=player_1_disconnected duration_ms=2000" `
                            -Description "$Name Player 1 disconnect notice" `
                            -Process $clientTest.Process `
                            -Timeout $TimeoutSeconds
            Stop-TestProcess -Process $clientTest.Process
        }
        else {
            Stop-TestProcess -Process $clientTest.Process
            Wait-LogPattern -Path $hostTest.StderrPath `
                            -Pattern "remote_host_notice text=player_2_disconnected duration_frames=120" `
                            -Description "$Name Player 2 disconnect notice" `
                            -Process $hostTest.Process `
                            -Timeout $TimeoutSeconds
        }
        Assert-LogPattern -Path $clientTest.StderrPath `
                          -Pattern "remote_audio_arrival_window .*samples=[1-9][0-9]*.*p50_ms=.*p95_ms=.*p99_ms=.*maximum_ms=" `
                          -Description "$Name client audio arrival window telemetry"
        Assert-LogPattern -Path $clientTest.StderrPath `
                          -Pattern "remote_input_client stopped .*audio_arrival_samples=[1-9][0-9]*.*audio_arrival_average_ms=.*audio_arrival_p50_ms=.*audio_arrival_p95_ms=.*audio_arrival_p99_ms=" `
                          -Description "$Name client audio arrival session telemetry"
        Assert-NoFatalLog -Paths @($hostTest.StderrPath, $clientTest.StderrPath)
        Write-Host "[PASS] $Name"
    }
    finally {
        if ($clientTest) {
            Stop-TestProcess -Process $clientTest.Process
        }
        if ($hostTest) {
            Stop-TestProcess -Process $hostTest.Process
        }
    }
}

function Set-UdpU16BigEndian {
    param([byte[]]$Buffer, [int]$Offset, [uint16]$Value)

    $Buffer[$Offset] = [byte](($Value -shr 8) -band 0xff)
    $Buffer[$Offset + 1] = [byte]($Value -band 0xff)
}

function Set-UdpU32BigEndian {
    param([byte[]]$Buffer, [int]$Offset, [uint32]$Value)

    for ($index = 0; $index -lt 4; $index++) {
        $shift = (3 - $index) * 8
        $Buffer[$Offset + $index] = [byte](($Value -shr $shift) -band 0xff)
    }
}

function Set-UdpU64BigEndian {
    param([byte[]]$Buffer, [int]$Offset, [uint64]$Value)

    for ($index = 0; $index -lt 8; $index++) {
        $shift = (7 - $index) * 8
        $Buffer[$Offset + $index] = [byte](($Value -shr $shift) -band 0xff)
    }
}

function Get-UdpU16BigEndian {
    param([byte[]]$Buffer, [int]$Offset)

    return [uint16](([uint16]$Buffer[$Offset] -shl 8) -bor $Buffer[$Offset + 1])
}

function Invoke-RemoteHandshakeDiagnostics {
    param(
        [Parameter(Mandatory)][int]$Port,
        [Parameter(Mandatory)][uint32]$TestSessionId
    )

    Write-Host "[RUN ] remote-handshake-errors"
    Assert-PortAvailable -Port $Port
    $hostTest = $null
    $clientTest = $null
    $probe = $null
    try {
        $waitingArguments = @(
            "--nogl",
            "--remote-input-client", "127.0.0.1:$Port",
            "--remote-session", $TestSessionId.ToString()
        )
        $clientTest = Start-SameBoyTestProcess -Name "remote-handshake-waiting-client" `
                                               -Arguments $waitingArguments
        Wait-LogPattern -Path $clientTest.StderrPath `
                        -Pattern "remote_handshake_client status=waiting client_protocol=6 host_protocol=0" `
                        -Description "client waiting-for-host status" `
                        -Process $clientTest.Process `
                        -Timeout $TimeoutSeconds
        Stop-TestProcess -Process $clientTest.Process
        $clientTest = $null

        $probe = [System.Net.Sockets.UdpClient]::new($Port)
        $probe.Client.ReceiveTimeout = $TimeoutSeconds * 1000
        $clientTest = Start-SameBoyTestProcess -Name "remote-handshake-protocol-mismatch-client" `
                                               -Arguments $waitingArguments
        $clientSender = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
        [byte[]]$clientHello = $probe.Receive([ref]$clientSender)
        if ($clientHello.Length -ne 24 -or $clientHello[6] -ne 6) {
            throw "Client sent an invalid handshake hello."
        }
        [byte[]]$mismatchResponse = New-Object byte[] 32
        [System.Text.Encoding]::ASCII.GetBytes("SBLK").CopyTo($mismatchResponse, 0)
        Set-UdpU16BigEndian -Buffer $mismatchResponse -Offset 4 -Value 5
        $mismatchResponse[6] = 7
        $mismatchResponse[7] = 3
        [Array]::Copy($clientHello, 8, $mismatchResponse, 8, 12)
        Set-UdpU64BigEndian -Buffer $mismatchResponse -Offset 20 -Value 1
        $null = $probe.Send($mismatchResponse,
                            $mismatchResponse.Length,
                            $clientSender)
        Wait-LogPattern -Path $clientTest.StderrPath `
                        -Pattern "remote_handshake_client status=protocol_mismatch client_protocol=6 host_protocol=5" `
                        -Description "client protocol mismatch status" `
                        -Process $clientTest.Process `
                        -Timeout $TimeoutSeconds
        Stop-TestProcess -Process $clientTest.Process
        $clientTest = $null
        $probe.Dispose()
        $probe = $null

        $hostArguments = @(
            "--remote-input-host", $Port.ToString(),
            "--remote-session", $TestSessionId.ToString(),
            "--remote-host-view", "p1",
            $script:QuotedRomPath
        )
        $hostTest = Start-SameBoyTestProcess -Name "remote-handshake-errors-host" `
                                             -Arguments $hostArguments
        Wait-LogPattern -Path $hostTest.StderrPath `
                        -Pattern "remote_input_host listening_udp_port=$Port" `
                        -Description "handshake diagnostic host startup" `
                        -Process $hostTest.Process `
                        -Timeout $TimeoutSeconds

        $wrongSessionId = [uint32]($TestSessionId + 1)
        $clientArguments = @(
            "--nogl",
            "--remote-input-client", "127.0.0.1:$Port",
            "--remote-session", $wrongSessionId.ToString()
        )
        $clientTest = Start-SameBoyTestProcess -Name "remote-handshake-session-mismatch-client" `
                                               -Arguments $clientArguments
        Wait-LogPattern -Path $clientTest.StderrPath `
                        -Pattern "remote_handshake_client status=session_mismatch client_protocol=6 host_protocol=6" `
                        -Description "client session mismatch status" `
                        -Process $clientTest.Process `
                        -Timeout $TimeoutSeconds
        Assert-LogPattern -Path $hostTest.StderrPath `
                          -Pattern "remote_handshake_host status=session_mismatch client_protocol=6 host_protocol=6" `
                          -Description "host session mismatch response"
        if ((Read-Log -Path $hostTest.StderrPath) -match "remote_input_host client_active") {
            throw "Session-mismatched client was incorrectly activated by the host."
        }
        Stop-TestProcess -Process $clientTest.Process
        $clientTest = $null

        $probe = [System.Net.Sockets.UdpClient]::new()
        $probe.Client.ReceiveTimeout = $TimeoutSeconds * 1000
        $probe.Connect("127.0.0.1", $Port)
        [byte[]]$hello = New-Object byte[] 24
        [System.Text.Encoding]::ASCII.GetBytes("SBLK").CopyTo($hello, 0)
        Set-UdpU16BigEndian -Buffer $hello -Offset 4 -Value 65535
        $hello[6] = 6
        Set-UdpU32BigEndian -Buffer $hello -Offset 8 -Value $TestSessionId
        [uint64]$requestId = 0x0102030405060708
        Set-UdpU64BigEndian -Buffer $hello -Offset 12 -Value $requestId
        $null = $probe.Send($hello, $hello.Length)
        $sender = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
        [byte[]]$response = $probe.Receive([ref]$sender)
        if ($response.Length -ne 32 -or
            [System.Text.Encoding]::ASCII.GetString($response, 0, 4) -ne "SBLK" -or
            (Get-UdpU16BigEndian -Buffer $response -Offset 4) -ne 6 -or
            $response[6] -ne 7 -or
            $response[7] -ne 3) {
            throw "Host returned an invalid protocol-mismatch handshake response."
        }
        Assert-LogPattern -Path $hostTest.StderrPath `
                          -Pattern "remote_handshake_host status=protocol_mismatch client_protocol=65535 host_protocol=6" `
                          -Description "host protocol mismatch response"
        Assert-NoFatalLog -Paths @($hostTest.StderrPath)
        Write-Host "[PASS] remote-handshake-errors"
    }
    finally {
        if ($probe) {
            $probe.Dispose()
        }
        if ($clientTest) {
            Stop-TestProcess -Process $clientTest.Process
        }
        if ($hostTest) {
            Stop-TestProcess -Process $hostTest.Process
        }
    }
}

function Invoke-RemoteReconnect {
    param(
        [Parameter(Mandatory)][int]$Port,
        [Parameter(Mandatory)][uint32]$TestSessionId
    )

    Write-Host "[RUN ] remote-reconnect"
    Assert-PortAvailable -Port $Port
    $hostTest = $null
    $firstClientTest = $null
    $secondClientTest = $null
    try {
        $hostArguments = @(
            "--remote-input-host", $Port.ToString(),
            "--remote-session", $TestSessionId.ToString(),
            "--remote-host-view", "p1",
            $script:QuotedRomPath
        )
        $hostTest = Start-SameBoyTestProcess -Name "remote-reconnect-host" `
                                             -Arguments $hostArguments
        Wait-LogPattern -Path $hostTest.StderrPath `
                        -Pattern "remote_input_host listening_udp_port=$Port" `
                        -Description "reconnect host startup" `
                        -Process $hostTest.Process `
                        -Timeout $TimeoutSeconds

        $clientArguments = @(
            "--nogl",
            "--remote-input-client", "127.0.0.1:$Port",
            "--remote-session", $TestSessionId.ToString()
        )
        $firstClientTest = Start-SameBoyTestProcess -Name "remote-reconnect-first-client" `
                                                    -Arguments $clientArguments
        Wait-LogPattern -Path $firstClientTest.StderrPath `
                        -Pattern "remote_video_client first_frame=" `
                        -Description "first client video" `
                        -Process $firstClientTest.Process `
                        -Timeout $TimeoutSeconds
        Stop-TestProcess -Process $firstClientTest.Process
        Wait-LogPattern -Path $hostTest.StderrPath `
                        -Pattern "remote_host_notice text=player_2_disconnected" `
                        -Description "host disconnect before rejoin" `
                        -Process $hostTest.Process `
                        -Timeout $TimeoutSeconds

        $secondClientTest = Start-SameBoyTestProcess -Name "remote-reconnect-second-client" `
                                                     -Arguments $clientArguments
        Wait-LogPattern -Path $secondClientTest.StderrPath `
                        -Pattern "remote_video_client first_frame=" `
                        -Description "rejoined client video" `
                        -Process $secondClientTest.Process `
                        -Timeout $TimeoutSeconds
        Wait-LogPatternCount -Path $hostTest.StderrPath `
                             -Pattern "remote_input_host client_active first_sequence=1" `
                             -Count 2 `
                             -Description "host second client activation from sequence 1" `
                             -Process $hostTest.Process `
                             -Timeout $TimeoutSeconds
        Assert-LogPattern -Path $secondClientTest.StderrPath `
                          -Pattern "remote_handshake_client status=connected client_protocol=6 host_protocol=6" `
                          -Description "rejoined client handshake"
        Assert-NoFatalLog -Paths @($hostTest.StderrPath,
                                   $firstClientTest.StderrPath,
                                   $secondClientTest.StderrPath)
        Write-Host "[PASS] remote-reconnect"
    }
    finally {
        if ($secondClientTest) {
            Stop-TestProcess -Process $secondClientTest.Process
        }
        if ($firstClientTest) {
            Stop-TestProcess -Process $firstClientTest.Process
        }
        if ($hostTest) {
            Stop-TestProcess -Process $hostTest.Process
        }
    }
}

function Invoke-RemoteHostReconnect {
    param(
        [Parameter(Mandatory)][int]$Port,
        [Parameter(Mandatory)][uint32]$TestSessionId
    )

    Write-Host "[RUN ] remote-host-reconnect"
    Assert-PortAvailable -Port $Port
    $firstHostTest = $null
    $secondHostTest = $null
    $clientTest = $null
    try {
        $hostArguments = @(
            "--remote-input-host", $Port.ToString(),
            "--remote-session", $TestSessionId.ToString(),
            "--remote-host-view", "p1",
            $script:QuotedRomPath
        )
        $firstHostTest = Start-SameBoyTestProcess -Name "remote-host-reconnect-first-host" `
                                                    -Arguments $hostArguments
        Wait-LogPattern -Path $firstHostTest.StderrPath `
                        -Pattern "remote_input_host listening_udp_port=$Port" `
                        -Description "first reconnect host startup" `
                        -Process $firstHostTest.Process `
                        -Timeout $TimeoutSeconds

        $clientArguments = @(
            "--nogl",
            "--remote-input-client", "127.0.0.1:$Port",
            "--remote-session", $TestSessionId.ToString()
        )
        $clientTest = Start-SameBoyTestProcess -Name "remote-host-reconnect-client" `
                                               -Arguments $clientArguments
        Wait-LogPattern -Path $clientTest.StderrPath `
                        -Pattern "remote_video_client host_generation_first_frame=.*generation=1" `
                        -Description "first host video" `
                        -Process $clientTest.Process `
                        -Timeout $TimeoutSeconds

        Stop-TestProcess -Process $firstHostTest.Process
        Wait-LogPattern -Path $clientTest.StderrPath `
                        -Pattern "remote_client_notice text=player_1_disconnected" `
                        -Description "client waiting after first host stops" `
                        -Process $clientTest.Process `
                        -Timeout $TimeoutSeconds
        Assert-PortAvailable -Port $Port

        $secondHostTest = Start-SameBoyTestProcess -Name "remote-host-reconnect-second-host" `
                                                   -Arguments $hostArguments
        Wait-LogPattern -Path $secondHostTest.StderrPath `
                        -Pattern "remote_input_host listening_udp_port=$Port" `
                        -Description "second reconnect host startup" `
                        -Process $secondHostTest.Process `
                        -Timeout $TimeoutSeconds
        Wait-LogPattern -Path $clientTest.StderrPath `
                        -Pattern "remote_handshake_client host_generation_changed generation=2 initial=no" `
                        -Description "client second host generation" `
                        -Process $clientTest.Process `
                        -Timeout $TimeoutSeconds
        Wait-LogPattern -Path $clientTest.StderrPath `
                        -Pattern "remote_video_client host_generation_first_frame=.*generation=2" `
                        -Description "second host video" `
                        -Process $clientTest.Process `
                        -Timeout $TimeoutSeconds
        Assert-LogPattern -Path $secondHostTest.StderrPath `
                          -Pattern "remote_input_host client_active first_sequence=[1-9][0-9]*" `
                          -Description "second host input reception"
        Assert-NoFatalLog -Paths @($firstHostTest.StderrPath,
                                   $secondHostTest.StderrPath,
                                   $clientTest.StderrPath)
        Write-Host "[PASS] remote-host-reconnect"
    }
    finally {
        if ($clientTest) {
            Stop-TestProcess -Process $clientTest.Process
        }
        if ($secondHostTest) {
            Stop-TestProcess -Process $secondHostTest.Process
        }
        if ($firstHostTest) {
            Stop-TestProcess -Process $firstHostTest.Process
        }
    }
}

$script:RepositoryRoot = $PSScriptRoot
$resolvedRom = Resolve-Path -LiteralPath $RomPath -ErrorAction Stop
$script:Executable = Join-Path $script:RepositoryRoot "build\bin\SDL\sameboy.exe"
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$script:LogDirectory = Join-Path $script:RepositoryRoot "build\regression\automated-$timestamp-$PID"
if ($SessionId -eq 0) {
    $SessionId = [uint32](Get-Random -Minimum 1 -Maximum ([int]::MaxValue - 1))
}

New-Item -ItemType Directory -Path $script:LogDirectory -Force | Out-Null
$romExtension = [System.IO.Path]::GetExtension($resolvedRom.Path)
if ([string]::IsNullOrWhiteSpace($romExtension)) {
    $romExtension = ".gb"
}
$testRomPath = Join-Path $script:LogDirectory "smoke-rom$romExtension"
Copy-Item -LiteralPath $resolvedRom.Path -Destination $testRomPath
$script:QuotedRomPath = '"' + $testRomPath.Replace('"', '\"') + '"'

try {
    if (-not $SkipBuild) {
        Write-Host "[RUN ] Windows debug build"
        & (Join-Path $script:RepositoryRoot "build-windows.ps1")
        Write-Host "[PASS] Windows debug build"
    }
    elseif (-not (Test-Path -LiteralPath $script:Executable -PathType Leaf)) {
        throw "SameBoy executable not found at $script:Executable. Run without -SkipBuild first."
    }

    Invoke-StartupSmoke -Name "single-player" `
                        -Arguments @($script:QuotedRomPath) `
                        -ReadyPattern "measured_frame_rate=.*normal_frames=120"
    Invoke-StartupSmoke -Name "local-link" `
                        -Arguments @("--local-link", $script:QuotedRomPath) `
                        -ReadyPattern "local_link synchronized_frames=120"

    Invoke-RemoteLoopback -Name "remote-opengl" `
                          -Port $BasePort `
                          -TestSessionId $SessionId `
                          -ExpectedPresentation "OpenGL"
    Invoke-RemoteLoopback -Name "remote-sdl" `
                          -Port ($BasePort + 1) `
                          -TestSessionId ($SessionId + 1) `
                          -ExpectedPresentation "SDL" `
                          -DisableClientGl `
                          -StopHostFirst
    Invoke-RemoteHandshakeDiagnostics -Port ($BasePort + 2) `
                                      -TestSessionId ($SessionId + 2)
    Invoke-RemoteReconnect -Port ($BasePort + 3) `
                           -TestSessionId ($SessionId + 3)
    Invoke-RemoteHostReconnect -Port ($BasePort + 4) `
                               -TestSessionId ($SessionId + 4)

    Write-Host ""
    Write-Host "All SameBoy Link Windows smoke tests passed."
    Write-Host "Logs: $script:LogDirectory"
}
catch {
    [Console]::Error.WriteLine("[FAIL] $($_.Exception.Message)")
    Write-Host "Logs: $script:LogDirectory"
    exit 1
}
finally {
    if (Test-Path -LiteralPath $testRomPath -PathType Leaf) {
        Remove-Item -LiteralPath $testRomPath -Force
    }
}
