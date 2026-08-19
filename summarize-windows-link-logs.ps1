[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [Alias("HostLogPath")]
    [string]$HostPath,

    [Parameter(Mandatory)]
    [Alias("ClientLogPath")]
    [string]$ClientPath,

    [string]$TestLabel,

    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$nominalFrameRate = 59.727501
$pcmPacketDurationSeconds = 0.005
$invariantCulture = [System.Globalization.CultureInfo]::InvariantCulture

function Read-SharedText {
    param([Parameter(Mandatory)][string]$Path)

    $stream = [System.IO.File]::Open($Path,
                                     [System.IO.FileMode]::Open,
                                     [System.IO.FileAccess]::Read,
                                     [System.IO.FileShare]::ReadWrite)
    $reader = [System.IO.StreamReader]::new($stream, $true)
    try {
        return $reader.ReadToEnd()
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Resolve-LogInput {
    param([Parameter(Mandatory)][string]$Path)

    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    $item = Get-Item -LiteralPath $resolved.Path
    if ($item.PSIsContainer) {
        $logPath = Join-Path $item.FullName "sameboy-session.log"
        $directory = $item.FullName
    }
    else {
        $logPath = $item.FullName
        $directory = Split-Path -Parent $item.FullName
    }
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        throw "Combined SameBoy log not found: $logPath"
    }

    $infoPath = Join-Path $directory "session-info.txt"
    return [pscustomobject]@{
        Directory = $directory
        LogPath = $logPath
        InfoPath = if (Test-Path -LiteralPath $infoPath -PathType Leaf) { $infoPath } else { $null }
    }
}

function Convert-LogScalar {
    param([Parameter(Mandatory)][string]$Value)

    [long]$integer = 0
    if ([long]::TryParse($Value,
                         [System.Globalization.NumberStyles]::Integer,
                         $invariantCulture,
                         [ref]$integer)) {
        return $integer
    }
    [double]$number = 0
    if ([double]::TryParse($Value,
                           [System.Globalization.NumberStyles]::Float,
                           $invariantCulture,
                           [ref]$number)) {
        return $number
    }
    return $Value
}

function Convert-KeyValueText {
    param([Parameter(Mandatory)][string]$Text)

    $values = [ordered]@{}
    foreach ($match in [regex]::Matches($Text,
                                        '(?<key>[A-Za-z][A-Za-z0-9_]*)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] =
            Convert-LogScalar -Value $match.Groups['value'].Value
    }
    return [pscustomobject]$values
}

function Get-LastRecord {
    param(
        [Parameter(Mandatory)][string]$Content,
        [Parameter(Mandatory)][string]$Marker,
        [switch]$Required
    )

    $pattern = '(?m)^.*' + [regex]::Escape($Marker) + '[^\r\n]*\r?$'
    $matches = [regex]::Matches($Content, $pattern)
    if ($matches.Count -eq 0) {
        if ($Required) {
            throw "Required log record not found: $Marker"
        }
        return $null
    }
    return Convert-KeyValueText -Text $matches[$matches.Count - 1].Value
}

function Get-RecordValue {
    param(
        [AllowNull()][object]$Record,
        [Parameter(Mandatory)][string]$Name,
        [object]$Default = 0
    )

    if ($null -eq $Record) {
        return $Default
    }
    $property = $Record.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $Default
    }
    return $property.Value
}

function Get-Metadata {
    param(
        [Parameter(Mandatory)][string]$LogContent,
        [AllowNull()][string]$InfoPath
    )

    $content = if ($InfoPath) { Read-SharedText -Path $InfoPath } else { $LogContent }
    $metadata = [ordered]@{}
    $knownKeys = @(
        "Launch ID",
        "Started UTC",
        "Ended UTC",
        "Duration seconds",
        "Parent exit code",
        "Remote client PID",
        "Exit code",
        "Computer",
        "Windows user",
        "Requested role",
        "Role",
        "Test label",
        "Network interface",
        "Network description",
        "Network medium",
        "Link speed",
        "Local IPv4",
        "Default gateway",
        "Log storage during run",
        "Shared log root",
        "Local staging directory",
        "Shared log directory",
        "Log directory",
        "Executable",
        "Executable SHA256",
        "Source commit",
        "Dirty source",
        "Arguments"
    )
    foreach ($line in $content -split "`r?`n") {
        foreach ($key in $knownKeys) {
            if ($line.StartsWith("${key}: ", [StringComparison]::Ordinal)) {
                $name = ($key -replace '[^A-Za-z0-9]+', ' ').Trim().Split(' ') |
                    ForEach-Object -Begin { $first = $true } -Process {
                        if ($first) {
                            $first = $false
                            $_.Substring(0, 1).ToLowerInvariant() + $_.Substring(1)
                        }
                        else {
                            $_.Substring(0, 1).ToUpperInvariant() + $_.Substring(1)
                        }
                    }
                $metadata[($name -join '')] = Convert-LogScalar -Value $line.Substring($key.Length + 2)
                break
            }
        }
    }
    return [pscustomobject]$metadata
}

function Get-Percentile {
    param(
        [Parameter(Mandatory)][double[]]$SortedValues,
        [Parameter(Mandatory)][ValidateRange(0, 1)][double]$Probability
    )

    if ($SortedValues.Count -eq 0) {
        return $null
    }
    if ($SortedValues.Count -eq 1) {
        return $SortedValues[0]
    }
    $position = ($SortedValues.Count - 1) * $Probability
    $lower = [int][Math]::Floor($position)
    $upper = [int][Math]::Ceiling($position)
    if ($lower -eq $upper) {
        return $SortedValues[$lower]
    }
    $weight = $position - $lower
    return $SortedValues[$lower] * (1 - $weight) + $SortedValues[$upper] * $weight
}

function Get-Statistics {
    param([AllowEmptyCollection()][double[]]$Values)

    if ($null -eq $Values -or $Values.Count -eq 0) {
        return [pscustomobject][ordered]@{
            samples = 0
            average = $null
            p50 = $null
            p95 = $null
            p99 = $null
            maximum = $null
        }
    }
    [double[]]$sorted = @($Values | Sort-Object)
    return [pscustomobject][ordered]@{
        samples = $sorted.Count
        average = [Math]::Round(($sorted | Measure-Object -Average).Average, 3)
        p50 = [Math]::Round((Get-Percentile -SortedValues $sorted -Probability 0.50), 3)
        p95 = [Math]::Round((Get-Percentile -SortedValues $sorted -Probability 0.95), 3)
        p99 = [Math]::Round((Get-Percentile -SortedValues $sorted -Probability 0.99), 3)
        maximum = [Math]::Round($sorted[$sorted.Count - 1], 3)
    }
}

function Get-RecordSeries {
    param(
        [Parameter(Mandatory)][string]$Content,
        [Parameter(Mandatory)][string]$Marker
    )

    $records = @()
    $pattern = '(?m)^.*' + [regex]::Escape($Marker) + '[^\r\n]*\r?$'
    foreach ($match in [regex]::Matches($Content, $pattern)) {
        $records += Convert-KeyValueText -Text $match.Value
    }
    return @($records)
}

function Get-FieldStatistics {
    param(
        [Parameter(Mandatory)][AllowNull()][AllowEmptyCollection()][object[]]$Records,
        [Parameter(Mandatory)][string]$Field
    )

    [double[]]$values = @(
        foreach ($record in $Records) {
            $property = $record.PSObject.Properties[$Field]
            if ($null -ne $property -and $property.Value -is [ValueType]) {
                [double]$property.Value
            }
        }
    )
    return Get-Statistics -Values $values
}

function Get-TimedEventWindows {
    param(
        [Parameter(Mandatory)][string]$Content,
        [int]$WindowSeconds = 10
    )

    $events = @{}
    foreach ($line in $Content -split "`r?`n") {
        if ($line -notmatch '^\[SameBoy Link\]\[[^]]+\] (?<event>remote_[A-Za-z0-9_]+ (?:underflow|sequence_gap|incomplete_frame_drop|target_changed)) .*\bt_ms=(?<time>[0-9]+)') {
            continue
        }
        $timeMs = [long]$Matches['time']
        $windowStart = [long]([Math]::Floor($timeMs / ($WindowSeconds * 1000.0)) * $WindowSeconds)
        $eventName = $Matches['event']
        $key = "${windowStart}|${eventName}"
        if (-not $events.ContainsKey($key)) {
            $events[$key] = 0
        }
        $events[$key]++
    }

    $windows = @()
    foreach ($key in $events.Keys) {
        $parts = $key.Split('|', 2)
        $start = [long]$parts[0]
        $windows += [pscustomobject][ordered]@{
            startSeconds = $start
            endSeconds = $start + $WindowSeconds
            event = $parts[1]
            count = $events[$key]
        }
    }
    return @($windows | Sort-Object startSeconds, event)
}

function Format-Number {
    param(
        [AllowNull()][object]$Value,
        [int]$Digits = 3
    )

    if ($null -eq $Value) {
        return "n/a"
    }
    return ([double]$Value).ToString("F$Digits", $invariantCulture)
}

$hostInput = Resolve-LogInput -Path $HostPath
$clientInput = Resolve-LogInput -Path $ClientPath
$hostContent = Read-SharedText -Path $hostInput.LogPath
$clientContent = Read-SharedText -Path $clientInput.LogPath
$hostMetadata = Get-Metadata -LogContent $hostContent -InfoPath $hostInput.InfoPath
$clientMetadata = Get-Metadata -LogContent $clientContent -InfoPath $clientInput.InfoPath

$connection = Get-LastRecord -Content $hostContent -Marker "remote_input_host listening_udp_port=" -Required
$hostInputFinal = Get-LastRecord -Content $hostContent -Marker "remote_input_host stopped" -Required
$hostVideoFinal = Get-LastRecord -Content $hostContent -Marker "remote_video_host stopped" -Required
$hostAudioFinal = Get-LastRecord -Content $hostContent -Marker "remote_audio_host stopped" -Required
$hostClockFinal = Get-LastRecord -Content $hostContent -Marker "remote_clock_sync_host stopped" -Required
$clientFinal = Get-LastRecord -Content $clientContent -Marker "remote_input_client stopped" -Required
$clientActive = Get-LastRecord -Content $hostContent -Marker "remote_input_host client_active"
$clientPresentation = Get-LastRecord -Content $clientContent -Marker "remote_client_presentation="

$hostVideoFrames = [double](Get-RecordValue -Record $hostVideoFinal -Name "frames_sent")
$hostAudioPackets = [double](Get-RecordValue -Record $hostAudioFinal -Name "packets_sent")
$clientVideoFrames = [double](Get-RecordValue -Record $clientFinal -Name "video_frames")
$clientAudioPackets = [double](Get-RecordValue -Record $clientFinal -Name "audio_packets")
$hostVideoSeconds = if ($hostVideoFrames) { $hostVideoFrames / $nominalFrameRate } else { 0 }
$hostAudioSeconds = $hostAudioPackets * $pcmPacketDurationSeconds
$clientVideoSeconds = if ($clientVideoFrames) { $clientVideoFrames / $nominalFrameRate } else { 0 }
$clientAudioSeconds = $clientAudioPackets * $pcmPacketDurationSeconds
$clientActiveSeconds = if ($clientVideoSeconds -and $clientAudioSeconds) {
    ($clientVideoSeconds + $clientAudioSeconds) / 2
}
else {
    [Math]::Max($clientVideoSeconds, $clientAudioSeconds)
}

$videoDropped = [double](Get-RecordValue -Record $clientFinal -Name "video_dropped")
$videoSuperseded = [double](Get-RecordValue -Record $clientFinal -Name "video_superseded")
$videoPresentedValue = Get-RecordValue -Record $clientFinal -Name "video_presented" -Default $null
$videoPresentedMeasured = $null -ne $videoPresentedValue
$videoPresented = if ($videoPresentedMeasured) {
    [double]$videoPresentedValue
}
else {
    [Math]::Max(0, $clientVideoFrames - $videoSuperseded)
}
$audioDropped = [double](Get-RecordValue -Record $clientFinal -Name "audio_dropped")
$audioUnderflows = [double](Get-RecordValue -Record $clientFinal -Name "audio_underflows")
$audioFramesTrimmed = [double](Get-RecordValue -Record $clientFinal -Name "audio_frames_trimmed")
$clockPings = [double](Get-RecordValue -Record $hostClockFinal -Name "pings")
$clockPongs = [double](Get-RecordValue -Record $hostClockFinal -Name "pongs")
$encodedVideoBytes = [double](Get-RecordValue -Record $hostVideoFinal -Name "encoded_bytes")
$hostVideoChunks = [double](Get-RecordValue -Record $hostVideoFinal -Name "chunks_sent")
$videoMbps = if ($hostVideoSeconds) { $encodedVideoBytes * 8 / $hostVideoSeconds / 1000000 } else { 0 }
$videoChunksPerFrame = if ($hostVideoFrames) { $hostVideoChunks / $hostVideoFrames } else { 0 }
$audioKbps = [double](Get-RecordValue -Record $hostAudioFinal -Name "payload_bitrate_kbps")

[object[]]$latencyRecords = @(Get-RecordSeries -Content $clientContent -Marker "remote_latency ")
[object[]]$clockRecords = @(Get-RecordSeries -Content $clientContent -Marker "remote_clock_sync_client samples=")
[object[]]$audioArrivalWindowRecords = @(Get-RecordSeries -Content $clientContent -Marker "remote_audio_arrival_window ")
$latencyFields = @(
    "total_ms",
    "event_to_send_ms",
    "input_network_ms",
    "host_receive_to_apply_ms",
    "host_apply_to_frame_ms",
    "frame_to_encode_ms",
    "encode_ms",
    "video_network_ms",
    "receive_span_ms",
    "decode_ms",
    "upload_ms",
    "present_call_ms"
)
$latencyStatistics = [ordered]@{}
foreach ($field in $latencyFields) {
    $latencyStatistics[$field] = Get-FieldStatistics -Records $latencyRecords -Field $field
}
$clockStatistics = [ordered]@{
    note = "Statistics use logged smoothed checkpoints, not every clock sample."
    rtt_ms = Get-FieldStatistics -Records $clockRecords -Field "rtt_ms"
    jitter_ms = Get-FieldStatistics -Records $clockRecords -Field "jitter_ms"
    uncertainty_ms = Get-FieldStatistics -Records $clockRecords -Field "uncertainty_ms"
}
$audioArrivalSamplesValue = Get-RecordValue -Record $clientFinal `
                                              -Name "audio_arrival_samples" `
                                              -Default $null
$audioArrivalMeasured = $null -ne $audioArrivalSamplesValue
$audioArrivalStatistics = [ordered]@{
    measured = $audioArrivalMeasured
    resolutionMs = if ($audioArrivalMeasured) { 1 } else { $null }
    samples = if ($audioArrivalMeasured) { [long]$audioArrivalSamplesValue } else { 0 }
    average = Get-RecordValue -Record $clientFinal -Name "audio_arrival_average_ms" -Default $null
    p50 = Get-RecordValue -Record $clientFinal -Name "audio_arrival_p50_ms" -Default $null
    p95 = Get-RecordValue -Record $clientFinal -Name "audio_arrival_p95_ms" -Default $null
    p99 = Get-RecordValue -Record $clientFinal -Name "audio_arrival_p99_ms" -Default $null
    maximum = Get-RecordValue -Record $clientFinal -Name "audio_max_arrival_gap_ms" -Default $null
}
$audioArrivalWindows = @(
    foreach ($record in $audioArrivalWindowRecords) {
        [pscustomobject][ordered]@{
            startSeconds = [Math]::Round([double](Get-RecordValue $record "start_ms") / 1000, 3)
            endSeconds = [Math]::Round([double](Get-RecordValue $record "end_ms") / 1000, 3)
            samples = [long](Get-RecordValue $record "samples")
            averageMs = Get-RecordValue $record "average_ms" -Default $null
            p50Ms = Get-RecordValue $record "p50_ms" -Default $null
            p95Ms = Get-RecordValue $record "p95_ms" -Default $null
            p99Ms = Get-RecordValue $record "p99_ms" -Default $null
            maximumMs = Get-RecordValue $record "maximum_ms" -Default $null
        }
    }
)

$videoDropPercent = if ($clientVideoFrames + $videoDropped) {
    $videoDropped * 100 / ($clientVideoFrames + $videoDropped)
}
else { 0 }
$videoSupersededPercent = if ($clientVideoFrames) {
    $videoSuperseded * 100 / $clientVideoFrames
}
else { 0 }
$videoPresentedPercent = if ($clientVideoFrames) {
    $videoPresented * 100 / $clientVideoFrames
}
else { 0 }
$audioDropPercent = if ($clientAudioPackets + $audioDropped) {
    $audioDropped * 100 / ($clientAudioPackets + $audioDropped)
}
else { 0 }
$audioUnderflowsPerMinute = if ($clientAudioSeconds) {
    $audioUnderflows * 60 / $clientAudioSeconds
}
else { 0 }

$observations = [System.Collections.Generic.List[string]]::new()
$hostHash = [string](Get-RecordValue -Record $hostMetadata -Name "executableSHA256" -Default "")
$clientHash = [string](Get-RecordValue -Record $clientMetadata -Name "executableSHA256" -Default "")
if ($hostHash -and $clientHash -and $hostHash -ne $clientHash) {
    $observations.Add("Host and client executable hashes differ.")
}
if ($audioUnderflows -gt 0) {
    $observations.Add("Client recorded $([long]$audioUnderflows) audio underflows.")
}
if ($audioDropped -gt 0) {
    $observations.Add("Client recorded $([long]$audioDropped) missing audio packets.")
}
if ($videoDropped -gt 0) {
    $observations.Add("Client recorded $([long]$videoDropped) dropped video frames.")
}
if ($videoSupersededPercent -gt 0.5) {
    $observations.Add("Completed video superseded before presentation exceeded the 0.5% target.")
}
$totalLatencyMaximum = Get-RecordValue -Record $latencyStatistics["total_ms"] -Name "maximum" -Default 0
if ([double]$totalLatencyMaximum -gt 100) {
    $observations.Add("At least one logged input-to-present sample exceeded 100 ms.")
}
if ($latencyRecords.Count -eq 0) {
    $observations.Add("No detailed remote_latency samples were available.")
}
if (-not $audioArrivalMeasured) {
    $observations.Add("Audio inter-arrival percentiles are unavailable in this older log; only the session maximum is recorded.")
}
elseif ([double]$audioArrivalStatistics.p99 -gt 60) {
    $observations.Add("Audio inter-arrival p99 exceeded the current 60 ms Wi-Fi target.")
}

if ([string]::IsNullOrWhiteSpace($TestLabel)) {
    $hostComputer = [string](Get-RecordValue -Record $hostMetadata -Name "computer" -Default "host")
    $clientComputer = [string](Get-RecordValue -Record $clientMetadata -Name "computer" -Default "client")
    $TestLabel = "$hostComputer-host-to-$clientComputer-client"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $safeLabel = ($TestLabel -replace '[^A-Za-z0-9._-]+', '-').Trim('-')
    $outputId = "$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')-$safeLabel"
    $OutputDirectory = Join-Path $PSScriptRoot "build\regression\summaries\$outputId"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$resolvedOutput = (Resolve-Path -LiteralPath $OutputDirectory).Path

$summary = [ordered]@{
    schemaVersion = 3
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    testLabel = $TestLabel
    inputs = [ordered]@{
        hostLog = $hostInput.LogPath
        clientLog = $clientInput.LogPath
    }
    build = [ordered]@{
        hostSourceCommit = Get-RecordValue -Record $hostMetadata -Name "sourceCommit" -Default "unknown"
        clientSourceCommit = Get-RecordValue -Record $clientMetadata -Name "sourceCommit" -Default "unknown"
        hostExecutableSha256 = $hostHash
        clientExecutableSha256 = $clientHash
        executableHashesMatch = ($hostHash -and $hostHash -eq $clientHash)
    }
    machines = [ordered]@{
        host = $hostMetadata
        client = $clientMetadata
    }
    connection = [ordered]@{
        protocol = Get-RecordValue -Record $connection -Name "protocol"
        udpPort = Get-RecordValue -Record $connection -Name "listening_udp_port"
        sessionId = Get-RecordValue -Record $connection -Name "session"
        firstAcceptedInputSequence = Get-RecordValue -Record $clientActive -Name "first_sequence" -Default 0
        timeoutEvents = ([regex]::Matches($hostContent, 'remote_input_host client_timeout')).Count
    }
    durationsSeconds = [ordered]@{
        clientActiveEstimate = [Math]::Round($clientActiveSeconds, 3)
        clientVideo = [Math]::Round($clientVideoSeconds, 3)
        clientAudio = [Math]::Round($clientAudioSeconds, 3)
        hostVideo = [Math]::Round($hostVideoSeconds, 3)
        hostAudio = [Math]::Round($hostAudioSeconds, 3)
        shutdownTailVideoEstimate = [Math]::Round([Math]::Max(0, $hostVideoSeconds - $clientVideoSeconds), 3)
        shutdownTailAudioEstimate = [Math]::Round([Math]::Max(0, $hostAudioSeconds - $clientAudioSeconds), 3)
    }
    hostCounters = [ordered]@{
        input = $hostInputFinal
        video = $hostVideoFinal
        audio = $hostAudioFinal
        clock = $hostClockFinal
    }
    clientCounters = $clientFinal
    presentation = [ordered]@{
        backend = Get-RecordValue -Record $clientPresentation -Name "remote_client_presentation" -Default "unknown"
        filter = Get-RecordValue -Record $clientPresentation -Name "filter" -Default "unknown"
        vsync = Get-RecordValue -Record $clientPresentation -Name "vsync" -Default "not-recorded"
        swapInterval = Get-RecordValue -Record $clientPresentation -Name "swap_interval" -Default "not-recorded"
        framesPresented = [long]$videoPresented
        framesPresentedMeasured = $videoPresentedMeasured
        presentCallAverageMs = Get-RecordValue -Record $clientFinal -Name "present_call_average_ms" -Default "not-recorded"
        presentCallMaximumMs = Get-RecordValue -Record $clientFinal -Name "present_call_maximum_ms" -Default "not-recorded"
        slowPresentCalls = Get-RecordValue -Record $clientFinal -Name "present_call_slow" -Default "not-recorded"
        receiveSpanAverageMs = Get-RecordValue -Record $clientFinal -Name "receive_span_average_ms" -Default "not-recorded"
        receiveSpanMaximumMs = Get-RecordValue -Record $clientFinal -Name "receive_span_maximum_ms" -Default "not-recorded"
        slowReceiveSpans = Get-RecordValue -Record $clientFinal -Name "receive_span_slow" -Default "not-recorded"
    }
    derived = [ordered]@{
        videoDropPercent = [Math]::Round($videoDropPercent, 4)
        videoSupersededPercent = [Math]::Round($videoSupersededPercent, 4)
        videoPresentedPercent = [Math]::Round($videoPresentedPercent, 4)
        videoChunksPerFrame = [Math]::Round($videoChunksPerFrame, 3)
        audioDropPercent = [Math]::Round($audioDropPercent, 4)
        audioUnderflowsPerMinute = [Math]::Round($audioUnderflowsPerMinute, 3)
        audioFramesTrimmedMs = [Math]::Round($audioFramesTrimmed * 1000 / 48000, 3)
        clockResponsePercent = if ($clockPings) { [Math]::Round($clockPongs * 100 / $clockPings, 3) } else { 0 }
        estimatedVideoMbps = [Math]::Round($videoMbps, 3)
        reportedAudioMbps = [Math]::Round($audioKbps / 1000, 3)
        estimatedMediaMbpsBeforeOverhead = [Math]::Round($videoMbps + $audioKbps / 1000, 3)
    }
    latencyStatistics = $latencyStatistics
    loggedClockCheckpointStatistics = $clockStatistics
    audioArrivalStatistics = $audioArrivalStatistics
    audioArrivalWindows = @($audioArrivalWindows)
    timedEventWindows = @(Get-TimedEventWindows -Content $clientContent)
    observations = @($observations)
}

$jsonPath = Join-Path $resolvedOutput "summary.json"
$markdownPath = Join-Path $resolvedOutput "summary.md"
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$totalLatency = $latencyStatistics["total_ms"]
$markdown = [System.Collections.Generic.List[string]]::new()
$markdown.Add("# SameBoy Link test summary - $TestLabel")
$markdown.Add("")
$markdown.Add("Generated: $($summary.generatedAtUtc)")
$markdown.Add("Schema version: $($summary.schemaVersion)")
$markdown.Add("")
$markdown.Add("## Build and connection")
$markdown.Add("")
$markdown.Add("| Measurement | Result |")
$markdown.Add("|---|---:|")
$markdown.Add("| Host / Client | $((Get-RecordValue -Record $hostMetadata -Name 'computer' -Default 'unknown')) / $((Get-RecordValue -Record $clientMetadata -Name 'computer' -Default 'unknown')) |")
$markdown.Add("| Recorded roles | $((Get-RecordValue -Record $hostMetadata -Name 'role' -Default 'unknown')) / $((Get-RecordValue -Record $clientMetadata -Name 'role' -Default 'unknown')) |")
$markdown.Add("| Network medium | $((Get-RecordValue -Record $hostMetadata -Name 'networkMedium' -Default 'unknown')) / $((Get-RecordValue -Record $clientMetadata -Name 'networkMedium' -Default 'unknown')) |")
$markdown.Add("| Link speed | $((Get-RecordValue -Record $hostMetadata -Name 'linkSpeed' -Default 'unknown')) / $((Get-RecordValue -Record $clientMetadata -Name 'linkSpeed' -Default 'unknown')) |")
$markdown.Add("| Executable hashes match | $($summary.build.executableHashesMatch) |")
$markdown.Add("| Protocol / UDP port / session | $($summary.connection.protocol) / $($summary.connection.udpPort) / $($summary.connection.sessionId) |")
$markdown.Add("| Estimated active duration | $(Format-Number $summary.durationsSeconds.clientActiveEstimate) s |")
$markdown.Add("| Estimated media bitrate before overhead | $(Format-Number $summary.derived.estimatedMediaMbpsBeforeOverhead) Mbit/s |")
$markdown.Add("| Client presentation / VSync | $($summary.presentation.backend) / $($summary.presentation.vsync) |")
$markdown.Add("")
$markdown.Add("## Transport counters")
$markdown.Add("")
$markdown.Add("| Measurement | Result |")
$markdown.Add("|---|---:|")
$markdown.Add("| Host input accepted / rejected / stale | $((Get-RecordValue $hostInputFinal 'accepted')) / $((Get-RecordValue $hostInputFinal 'rejected')) / $((Get-RecordValue $hostInputFinal 'stale')) |")
$markdown.Add("| Client video completed / dropped / rejected | $([long]$clientVideoFrames) / $([long]$videoDropped) / $((Get-RecordValue $clientFinal 'video_rejected')) |")
$markdown.Add("| Client video actually presented | $([long]$videoPresented) ($(Format-Number $videoPresentedPercent 2)%) |")
$markdown.Add("| Client video superseded | $([long]$videoSuperseded) ($(Format-Number $videoSupersededPercent 2)%) |")
$markdown.Add("| Present call average / maximum / slow | $($summary.presentation.presentCallAverageMs) ms / $($summary.presentation.presentCallMaximumMs) ms / $($summary.presentation.slowPresentCalls) |")
$markdown.Add("| Host video datagrams per frame | $(Format-Number $summary.derived.videoChunksPerFrame 2) |")
$markdown.Add("| Host burst average / maximum / slow | $((Get-RecordValue $hostVideoFinal 'average_burst_us' -Default 'not-recorded')) us / $((Get-RecordValue $hostVideoFinal 'maximum_burst_us' -Default 'not-recorded')) us / $((Get-RecordValue $hostVideoFinal 'slow_bursts' -Default 'not-recorded')) |")
$markdown.Add("| Client receive span average / maximum / slow | $($summary.presentation.receiveSpanAverageMs) ms / $($summary.presentation.receiveSpanMaximumMs) ms / $($summary.presentation.slowReceiveSpans) |")
$markdown.Add("| Client audio received / dropped / stale | $([long]$clientAudioPackets) / $([long]$audioDropped) / $((Get-RecordValue $clientFinal 'audio_stale')) |")
$markdown.Add("| Client audio underflows / trims | $([long]$audioUnderflows) / $((Get-RecordValue $clientFinal 'audio_trims')) |")
$markdown.Add("| Maximum audio arrival gap | $(Format-Number (Get-RecordValue $clientFinal 'audio_max_arrival_gap_ms')) ms |")
$markdown.Add("| Host clock pings / pongs | $([long]$clockPings) / $([long]$clockPongs) |")
$markdown.Add("")
$markdown.Add("## Audio packet inter-arrival")
$markdown.Add("")
$markdown.Add("Percentiles use a bounded 1 ms histogram in the client; the maximum and average retain microsecond-derived precision.")
$markdown.Add("")
$markdown.Add("| Samples | Average | p50 | p95 | p99 | Maximum |")
$markdown.Add("|---:|---:|---:|---:|---:|---:|")
$markdown.Add("| $($audioArrivalStatistics.samples) | $(Format-Number $audioArrivalStatistics.average) ms | $(Format-Number $audioArrivalStatistics.p50) ms | $(Format-Number $audioArrivalStatistics.p95) ms | $(Format-Number $audioArrivalStatistics.p99) ms | $(Format-Number $audioArrivalStatistics.maximum) ms |")
$markdown.Add("")
$markdown.Add("### Ten-second audio arrival windows")
$markdown.Add("")
if ($audioArrivalWindows.Count -eq 0) {
    $markdown.Add("No windowed audio inter-arrival telemetry was available.")
}
else {
    $markdown.Add("| Window | Samples | Average | p50 | p95 | p99 | Maximum |")
    $markdown.Add("|---:|---:|---:|---:|---:|---:|---:|")
    foreach ($window in $audioArrivalWindows) {
        $markdown.Add("| $(Format-Number $window.startSeconds)-$(Format-Number $window.endSeconds) s | $($window.samples) | $(Format-Number $window.averageMs) ms | $(Format-Number $window.p50Ms) ms | $(Format-Number $window.p95Ms) ms | $(Format-Number $window.p99Ms) ms | $(Format-Number $window.maximumMs) ms |")
    }
}
$markdown.Add("")
$markdown.Add("## Logged input-to-present latency")
$markdown.Add("")
$markdown.Add("| Samples | Average | p50 | p95 | p99 | Maximum |")
$markdown.Add("|---:|---:|---:|---:|---:|---:|")
$markdown.Add("| $($totalLatency.samples) | $(Format-Number $totalLatency.average) ms | $(Format-Number $totalLatency.p50) ms | $(Format-Number $totalLatency.p95) ms | $(Format-Number $totalLatency.p99) ms | $(Format-Number $totalLatency.maximum) ms |")
$markdown.Add("")
$markdown.Add("## Timed event windows")
$markdown.Add("")
if ($summary.timedEventWindows.Count -eq 0) {
    $markdown.Add("No timed underflow, sequence-gap, incomplete-frame or target-change events were logged.")
}
else {
    $markdown.Add("| Window | Event | Count |")
    $markdown.Add("|---:|---|---:|")
    foreach ($window in $summary.timedEventWindows) {
        $markdown.Add("| $($window.startSeconds)-$($window.endSeconds) s | $($window.event) | $($window.count) |")
    }
}
$markdown.Add("")
$markdown.Add("## Observations")
$markdown.Add("")
foreach ($observation in $summary.observations) {
    $markdown.Add("- $observation")
}
$markdown | Set-Content -LiteralPath $markdownPath -Encoding UTF8

Write-Host "[PASS] SameBoy Link log summary"
Write-Host "Label: $TestLabel"
Write-Host "Active media estimate: $(Format-Number $clientActiveSeconds) s"
Write-Host "Video dropped/superseded: $([long]$videoDropped) / $([long]$videoSuperseded)"
Write-Host "Audio dropped/underflows: $([long]$audioDropped) / $([long]$audioUnderflows)"
Write-Host "Audio arrival average/p95/p99/max: $(Format-Number $audioArrivalStatistics.average) / $(Format-Number $audioArrivalStatistics.p95) / $(Format-Number $audioArrivalStatistics.p99) / $(Format-Number $audioArrivalStatistics.maximum) ms"
Write-Host "Latency average/p95/max: $(Format-Number $totalLatency.average) / $(Format-Number $totalLatency.p95) / $(Format-Number $totalLatency.maximum) ms"
Write-Host "JSON: $jsonPath"
Write-Host "Markdown: $markdownPath"
