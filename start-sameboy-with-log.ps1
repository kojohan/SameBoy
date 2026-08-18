[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$SameBoyArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$buildDirectory = $PSScriptRoot
$executable = Join-Path $buildDirectory "sameboy.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "sameboy.exe hittades inte bredvid loggskriptet: $executable"
}

$documentsDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments)
if ([string]::IsNullOrWhiteSpace($documentsDirectory)) {
    $documentsDirectory = $env:TEMP
}
$logRoot = Join-Path $documentsDirectory "SameBoy-Link-Logs"
$runId = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$logDirectory = Join-Path $logRoot $runId
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

$stdoutPath = Join-Path $logDirectory "sameboy-stdout.log"
$stderrPath = Join-Path $logDirectory "sameboy-stderr.log"
$clientStdoutPath = Join-Path $logDirectory "sameboy-client-stdout.log"
$clientStderrPath = Join-Path $logDirectory "sameboy-client-stderr.log"
$combinedPath = Join-Path $logDirectory "sameboy-session.log"
$infoPath = Join-Path $logDirectory "session-info.txt"

$sourceCommit = "unknown"
$sourceDirty = "unknown"
$manifestPath = Join-Path $buildDirectory "build-manifest.json"
if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    try {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $sourceCommit = [string]$manifest.sourceCommit
        $sourceDirty = [string]$manifest.sourceDirty
    }
    catch {
        $sourceCommit = "unreadable manifest"
    }
}

$startedAt = [DateTime]::UtcNow
$executableHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
$argumentText = if ($SameBoyArguments.Count -eq 0) { "(none; configure Host/Join in the Link menu)" } else { $SameBoyArguments -join " " }

@(
    "SameBoy Link diagnostic run"
    "Started UTC: $($startedAt.ToString('o'))"
    "Computer: $env:COMPUTERNAME"
    "Windows user: $env:USERNAME"
    "Executable: $executable"
    "Executable SHA256: $executableHash"
    "Source commit: $sourceCommit"
    "Dirty source: $sourceDirty"
    "Arguments: $argumentText"
) | Set-Content -LiteralPath $infoPath -Encoding UTF8

Write-Host "SameBoy startas med diagnostikloggning."
Write-Host "Starta Host eller Join fran Link-menyn som vanligt."
Write-Host "Avsluta SameBoy normalt efter testet sa att slutstatistiken kommer med."
Write-Host "Loggmapp: $logDirectory"
Write-Host ""

$exitCode = 1
$previousClientStdout = [Environment]::GetEnvironmentVariable("SAMEBOY_LINK_CLIENT_STDOUT", "Process")
$previousClientStderr = [Environment]::GetEnvironmentVariable("SAMEBOY_LINK_CLIENT_STDERR", "Process")
try {
    [Environment]::SetEnvironmentVariable("SAMEBOY_LINK_CLIENT_STDOUT", $clientStdoutPath, "Process")
    [Environment]::SetEnvironmentVariable("SAMEBOY_LINK_CLIENT_STDERR", $clientStderrPath, "Process")

    $startParameters = @{
        FilePath = $executable
        WorkingDirectory = $buildDirectory
        RedirectStandardOutput = $stdoutPath
        RedirectStandardError = $stderrPath
        PassThru = $true
        Wait = $true
    }
    if ($SameBoyArguments.Count -gt 0) {
        $startParameters.ArgumentList = $SameBoyArguments
    }

    $process = Start-Process @startParameters
    $exitCode = $process.ExitCode
}
catch {
    $_ | Out-String | Set-Content -LiteralPath $stderrPath -Encoding UTF8
    $exitCode = 1
}
finally {
    [Environment]::SetEnvironmentVariable("SAMEBOY_LINK_CLIENT_STDOUT", $previousClientStdout, "Process")
    [Environment]::SetEnvironmentVariable("SAMEBOY_LINK_CLIENT_STDERR", $previousClientStderr, "Process")

    $endedAt = [DateTime]::UtcNow
    @(
        "Ended UTC: $($endedAt.ToString('o'))"
        "Duration seconds: $([Math]::Round(($endedAt - $startedAt).TotalSeconds, 3))"
        "Exit code: $exitCode"
    ) | Add-Content -LiteralPath $infoPath -Encoding UTF8

    Get-Content -LiteralPath $infoPath | Set-Content -LiteralPath $combinedPath -Encoding UTF8
    Add-Content -LiteralPath $combinedPath -Value "`r`n===== STDERR ====="
    if (Test-Path -LiteralPath $stderrPath) {
        Get-Content -LiteralPath $stderrPath | Add-Content -LiteralPath $combinedPath
    }
    Add-Content -LiteralPath $combinedPath -Value "`r`n===== STDOUT ====="
    if (Test-Path -LiteralPath $stdoutPath) {
        Get-Content -LiteralPath $stdoutPath | Add-Content -LiteralPath $combinedPath
    }
    Add-Content -LiteralPath $combinedPath -Value "`r`n===== REMOTE CLIENT STDERR ====="
    if (Test-Path -LiteralPath $clientStderrPath) {
        Get-Content -LiteralPath $clientStderrPath | Add-Content -LiteralPath $combinedPath
    }
    Add-Content -LiteralPath $combinedPath -Value "`r`n===== REMOTE CLIENT STDOUT ====="
    if (Test-Path -LiteralPath $clientStdoutPath) {
        Get-Content -LiteralPath $clientStdoutPath | Add-Content -LiteralPath $combinedPath
    }
}

Write-Host ""
Write-Host "Testet ar avslutat. Skicka denna fil:"
Write-Host $combinedPath -ForegroundColor Green
exit $exitCode
