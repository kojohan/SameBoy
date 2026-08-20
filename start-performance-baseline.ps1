[CmdletBinding()]
param(
    [ValidateSet(
        "desktop-eth-host-laptop-eth-client",
        "desktop-eth-host-laptop-wifi-client",
        "laptop-wifi-host-desktop-eth-client"
    )]
    [string]$Setup,

    [int]$RunNumber,

    [switch]$DescribeOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$setups = @(
    [pscustomobject]@{
        Number = 1
        Id = "desktop-eth-host-laptop-eth-client"
        Description = "Desktop Ethernet host -> Laptop Ethernet client"
    },
    [pscustomobject]@{
        Number = 2
        Id = "desktop-eth-host-laptop-wifi-client"
        Description = "Desktop Ethernet host -> Laptop Wi-Fi client"
    },
    [pscustomobject]@{
        Number = 3
        Id = "laptop-wifi-host-desktop-eth-client"
        Description = "Laptop Wi-Fi host -> Desktop Ethernet client"
    }
)

if ([string]::IsNullOrWhiteSpace($Setup)) {
    Write-Host "SameBoy Link - five-minute performance baseline"
    Write-Host "Choose the same setup and run number on both computers."
    Write-Host ""
    foreach ($candidate in $setups) {
        Write-Host "$($candidate.Number). $($candidate.Description)"
    }
    Write-Host ""
    $choiceText = Read-Host "Setup (1-3)"
    $choice = 0
    if (-not [int]::TryParse($choiceText, [ref]$choice)) {
        throw "Setup must be a number from 1 to 3."
    }
    $selected = $setups | Where-Object { $_.Number -eq $choice }
}
else {
    $selected = $setups | Where-Object { $_.Id -eq $Setup }
}

if ($null -eq $selected) {
    throw "Unknown performance setup."
}

if (-not $RunNumber) {
    $runText = Read-Host "Repetition (1-3)"
    if (-not [int]::TryParse($runText, [ref]$RunNumber)) {
        throw "Repetition must be a number from 1 to 3."
    }
}
if ($RunNumber -lt 1 -or $RunNumber -gt 3) {
    throw "Repetition must be a number from 1 to 3."
}

$testLabel = "baseline-$($selected.Id)-r$RunNumber"
$logger = Join-Path $PSScriptRoot "start-sameboy-with-log.ps1"
if (-not (Test-Path -LiteralPath $logger -PathType Leaf)) {
    throw "Logging launcher not found beside this script: $logger"
}

Write-Host ""
Write-Host "Setup: $($selected.Description)"
Write-Host "Run:   $RunNumber of 3"
Write-Host "Label: $testLabel"
Write-Host ""
Write-Host "Use Link > Host on the named host and Link > Join on the named client."
Write-Host "Play the side-scroll test for at least five minutes after gameplay appears."
Write-Host "Close the client first, then close the host normally so final counters are saved."
Write-Host ""

if ($DescribeOnly) {
    exit 0
}

& $logger -TestLabel $testLabel
exit $LASTEXITCODE
