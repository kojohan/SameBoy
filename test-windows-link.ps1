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
        [switch]$DisableClientGl
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
        Stop-TestProcess -Process $clientTest.Process
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
                          -DisableClientGl

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
