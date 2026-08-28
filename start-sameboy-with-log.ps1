[CmdletBinding()]
param(
    [string]$LogRoot,

    [string]$TestLabel,

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

function ConvertTo-SafeFileComponent {
    param(
        [AllowNull()][string]$Value,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Fallback
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $Fallback
    }
    $safeValue = ($Value -replace '[^A-Za-z0-9._-]+', '-').Trim('-')
    if ([string]::IsNullOrWhiteSpace($safeValue)) {
        return $Fallback
    }
    return $safeValue
}

function Resolve-DefaultLogRoot {
    param([AllowNull()][string]$RequestedRoot)

    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return $RequestedRoot
    }

    $environmentRoot = [Environment]::GetEnvironmentVariable("SAMEBOY_LINK_LOG_ROOT", "Process")
    if (-not [string]::IsNullOrWhiteSpace($environmentRoot)) {
        return $environmentRoot
    }

    $currentDirectory = Get-Item -LiteralPath $PSScriptRoot
    while ($null -ne $currentDirectory) {
        if ($currentDirectory.Name -ieq "SAME-LINKTEST") {
            return Join-Path $currentDirectory.FullName "LOGS"
        }
        $currentDirectory = $currentDirectory.Parent
    }

    $sharedTestRoot = "M:\SAME-LINKTEST"
    if (Test-Path -LiteralPath $sharedTestRoot -PathType Container) {
        return Join-Path $sharedTestRoot "LOGS"
    }

    $documentsDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments)
    if ([string]::IsNullOrWhiteSpace($documentsDirectory)) {
        $documentsDirectory = $env:TEMP
    }
    return Join-Path $documentsDirectory "SameBoy-Link-Logs"
}

function Get-RequestedRole {
    param([string[]]$Arguments)

    if ($Arguments -contains "--remote-input-host") {
        return "host"
    }
    if ($Arguments -contains "--remote-input-client") {
        return "client"
    }
    if ($Arguments -contains "--local-link") {
        return "local"
    }
    return "menu"
}

function Get-DetectedRole {
    param(
        [Parameter(Mandatory)][string[]]$LogPaths,
        [Parameter(Mandatory)][string]$RequestedRole
    )

    $contentParts = foreach ($path in $LogPaths) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Get-Content -LiteralPath $path -Raw
        }
    }
    $content = $contentParts -join "`n"
    if ($content -match 'remote_input_host(?:\s|_)') {
        return "host"
    }
    if ($content -match 'remote_input_client(?:\s|_)' -or
        $content -match 'remote_client diagnostic_log=enabled') {
        return "client"
    }
    if ($content -match 'local_link connected' -or $RequestedRole -eq "local") {
        return "local"
    }
    if ($RequestedRole -eq "host" -or $RequestedRole -eq "client") {
        return $RequestedRole
    }
    return "idle"
}

function Find-RemoteClientProcess {
    param(
        [Parameter(Mandatory)][int]$LauncherProcessId,
        [AllowEmptyCollection()][int[]]$ExistingProcessIds = @()
    )

    try {
        $child = Get-CimInstance Win32_Process `
            -Filter "ParentProcessId = $LauncherProcessId" `
            -ErrorAction Stop |
            Where-Object { $_.Name -ieq "sameboy.exe" } |
            Select-Object -First 1
        if ($null -ne $child) {
            return Get-Process -Id ([int]$child.ProcessId) -ErrorAction Stop
        }
    }
    catch {
        # Fall back to the process snapshot below when CIM is unavailable.
    }

    return Get-Process -Name "sameboy" -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Id -ne $LauncherProcessId -and
            $ExistingProcessIds -notcontains $_.Id
        } |
        Sort-Object StartTime -Descending |
        Select-Object -First 1
}

function Copy-VerifiedLogDirectory {
    param(
        [Parameter(Mandatory)][string]$SourceDirectory,
        [Parameter(Mandatory)][string]$DestinationDirectory
    )

    if (Test-Path -LiteralPath $DestinationDirectory) {
        throw "Shared log destination already exists: $DestinationDirectory"
    }
    $destinationParent = Split-Path -Parent $DestinationDirectory
    $destinationName = Split-Path -Leaf $DestinationDirectory
    $copyingDirectory = Join-Path $destinationParent ".copying-$destinationName"
    if (Test-Path -LiteralPath $copyingDirectory) {
        throw "Shared log staging destination already exists: $copyingDirectory"
    }

    New-Item -ItemType Directory -Path $copyingDirectory | Out-Null
    Get-ChildItem -LiteralPath $SourceDirectory -Force |
        Copy-Item -Destination $copyingDirectory -Recurse -Force

    $sourceFiles = @(Get-ChildItem -LiteralPath $SourceDirectory -File -Recurse -Force)
    foreach ($sourceFile in $sourceFiles) {
        $relativePath = $sourceFile.FullName.Substring($SourceDirectory.Length).TrimStart('\')
        $copiedFile = Join-Path $copyingDirectory $relativePath
        if (-not (Test-Path -LiteralPath $copiedFile -PathType Leaf)) {
            throw "Shared log copy is missing: $relativePath"
        }
        if ((Get-Item -LiteralPath $copiedFile).Length -ne $sourceFile.Length) {
            throw "Shared log size verification failed: $relativePath"
        }
        $sourceHash = (Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash
        $copiedHash = (Get-FileHash -LiteralPath $copiedFile -Algorithm SHA256).Hash
        if ($sourceHash -ne $copiedHash) {
            throw "Shared log hash verification failed: $relativePath"
        }
    }

    Move-Item -LiteralPath $copyingDirectory -Destination $DestinationDirectory
}

function Test-UsableNetworkAdapter {
    param([AllowNull()][object]$Adapter)

    if ($null -eq $Adapter -or $Adapter.Status -ne "Up") {
        return $false
    }
    $linkSpeed = [string]$Adapter.LinkSpeed
    return -not [string]::IsNullOrWhiteSpace($linkSpeed) -and
           $linkSpeed -notmatch '^\s*0(?:[.,]0+)?(?:\s|$)'
}

function Get-PrimaryNetworkMetadata {
    $metadata = [ordered]@{
        Interface = "unknown"
        Description = "unknown"
        Medium = "unknown"
        LinkSpeed = "unknown"
        IPv4 = "unknown"
        Gateway = "unknown"
    }

    try {
        $defaultRoute = $null
        $adapter = $null
        $addresses = @()
        $routes = @(Get-NetRoute -AddressFamily IPv4 `
                                -DestinationPrefix "0.0.0.0/0" `
                                -PolicyStore ActiveStore `
                                -ErrorAction Stop |
            Sort-Object @{ Expression = {
                [long]$_.RouteMetric + [long]$_.InterfaceMetric
            } })
        foreach ($route in $routes) {
            $candidate = Get-NetAdapter -InterfaceIndex $route.InterfaceIndex `
                                        -ErrorAction SilentlyContinue
            if (-not (Test-UsableNetworkAdapter -Adapter $candidate)) {
                continue
            }
            $candidateAddresses = @(Get-NetIPAddress -InterfaceIndex $candidate.ifIndex `
                                                     -AddressFamily IPv4 `
                                                     -ErrorAction SilentlyContinue |
                Where-Object { $_.IPAddress -notlike "169.254.*" } |
                Select-Object -ExpandProperty IPAddress)
            if ($candidateAddresses.Count -eq 0) {
                continue
            }
            $defaultRoute = $route
            $adapter = $candidate
            $addresses = $candidateAddresses
            break
        }
        if ($null -eq $adapter) {
            $connectedInterfaces = @(Get-NetIPInterface -AddressFamily IPv4 `
                                                        -ConnectionState Connected `
                                                        -ErrorAction Stop |
                Sort-Object InterfaceMetric)
            foreach ($interface in $connectedInterfaces) {
                $candidate = Get-NetAdapter -InterfaceIndex $interface.InterfaceIndex `
                                            -ErrorAction SilentlyContinue
                if (-not (Test-UsableNetworkAdapter -Adapter $candidate)) {
                    continue
                }
                $candidateAddresses = @(Get-NetIPAddress -InterfaceIndex $candidate.ifIndex `
                                                         -AddressFamily IPv4 `
                                                         -ErrorAction SilentlyContinue |
                    Where-Object { $_.IPAddress -notlike "169.254.*" } |
                    Select-Object -ExpandProperty IPAddress)
                if ($candidateAddresses.Count -eq 0) {
                    continue
                }
                $adapter = $candidate
                $addresses = $candidateAddresses
                break
            }
        }
        if ($null -eq $adapter) {
            return [pscustomobject]$metadata
        }

        $mediaDescription = "$($adapter.MediaType) $($adapter.InterfaceDescription) $($adapter.Name)"
        $medium = if ($mediaDescription -match '802\.11|Wi-?Fi|Wireless') {
            "Wi-Fi"
        }
        elseif ($mediaDescription -match '802\.3|Ethernet') {
            "Ethernet"
        }
        else {
            [string]$adapter.MediaType
        }
        $metadata.Interface = [string]$adapter.Name
        $metadata.Description = [string]$adapter.InterfaceDescription
        $metadata.Medium = $medium
        $metadata.LinkSpeed = [string]$adapter.LinkSpeed
        $metadata.IPv4 = if ($addresses.Count) { $addresses -join "," } else { "unknown" }
        if ($null -ne $defaultRoute -and -not [string]::IsNullOrWhiteSpace([string]$defaultRoute.NextHop)) {
            $metadata.Gateway = [string]$defaultRoute.NextHop
        }
    }
    catch {
        $metadata.Description = "unavailable: $($_.Exception.Message -replace '[\r\n]+', ' ')"
    }
    return [pscustomobject]$metadata
}

$LogRoot = Resolve-DefaultLogRoot -RequestedRoot $LogRoot
New-Item -ItemType Directory -Path $LogRoot -Force | Out-Null
$resolvedLogRoot = (Resolve-Path -LiteralPath $LogRoot).ProviderPath
$localAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
if ([string]::IsNullOrWhiteSpace($localAppData)) {
    $localAppData = $env:TEMP
}
$localStagingRoot = Join-Path $localAppData "SameBoy Link\LogStaging"
New-Item -ItemType Directory -Path $localStagingRoot -Force | Out-Null
$resolvedLocalStagingRoot = (Resolve-Path -LiteralPath $localStagingRoot).ProviderPath
$startedAt = [DateTime]::UtcNow
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$safeComputer = ConvertTo-SafeFileComponent -Value $env:COMPUTERNAME -Fallback "unknown-pc"
$safeLabel = ConvertTo-SafeFileComponent -Value $TestLabel -Fallback ""
$launchId = [Guid]::NewGuid().ToString("N").Substring(0, 8)
$requestedRole = Get-RequestedRole -Arguments $SameBoyArguments
$pendingNameParts = @($timestamp, $safeComputer, "pending")
if (-not [string]::IsNullOrWhiteSpace($safeLabel)) {
    $pendingNameParts += $safeLabel
}
$pendingNameParts += $launchId
$logDirectory = Join-Path $resolvedLocalStagingRoot ($pendingNameParts -join "-")
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

$executableHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
$argumentText = if ($SameBoyArguments.Count -eq 0) { "(none; configure Host/Join in the Link menu)" } else { $SameBoyArguments -join " " }
$network = Get-PrimaryNetworkMetadata
$labelText = if ([string]::IsNullOrWhiteSpace($TestLabel)) { "(none)" } else { $TestLabel }

@(
    "SameBoy Link diagnostic run"
    "Launch ID: $launchId"
    "Started UTC: $($startedAt.ToString('o'))"
    "Computer: $env:COMPUTERNAME"
    "Windows user: $env:USERNAME"
    "Requested role: $requestedRole"
    "Test label: $labelText"
    "Network interface: $($network.Interface)"
    "Network description: $($network.Description)"
    "Network medium: $($network.Medium)"
    "Link speed: $($network.LinkSpeed)"
    "Local IPv4: $($network.IPv4)"
    "Default gateway: $($network.Gateway)"
    "Log storage during run: local"
    "Shared log root: $resolvedLogRoot"
    "Executable: $executable"
    "Executable SHA256: $executableHash"
    "Source commit: $sourceCommit"
    "Dirty source: $sourceDirty"
    "Arguments: $argumentText"
) | Set-Content -LiteralPath $infoPath -Encoding UTF8

Write-Host "SameBoy startas med diagnostikloggning."
Write-Host "Starta Host eller Join fran Link-menyn som vanligt."
Write-Host "Avsluta SameBoy normalt efter testet sa att slutstatistiken kommer med."
Write-Host "Gemensam loggrot: $resolvedLogRoot"
Write-Host "Lokal aktiv loggmapp: $logDirectory"
Write-Host "Loggar kopieras till den gemensamma roten forst efter avslut."
Write-Host ""

$exitCode = 1
$parentExitCode = 1
$clientProcessId = 0
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
    }
    if ($SameBoyArguments.Count -gt 0) {
        $startParameters.ArgumentList = $SameBoyArguments
    }

    $existingSameBoyProcessIds = @(Get-Process -Name "sameboy" -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Id)
    $process = Start-Process @startParameters
    $process.WaitForExit()
    $parentExitCode = if ($null -eq $process.ExitCode) { 0 } else { $process.ExitCode }
    $exitCode = $parentExitCode

    $clientProcess = $null
    for ($attempt = 0; $attempt -lt 20 -and $null -eq $clientProcess; $attempt++) {
        $clientProcess = Find-RemoteClientProcess -LauncherProcessId $process.Id `
                                                  -ExistingProcessIds $existingSameBoyProcessIds
        if ($null -eq $clientProcess) {
            Start-Sleep -Milliseconds 100
        }
    }
    if ($null -ne $clientProcess) {
        $clientProcessId = $clientProcess.Id
        Write-Host "Remote Client upptacktes som PID $clientProcessId. Loggningen fortsatter tills klienten stangs."
        $clientProcess.WaitForExit()
        if ($null -ne $clientProcess.ExitCode) {
            $exitCode = $clientProcess.ExitCode
        }
    }
}
catch {
    $_ | Out-String | Set-Content -LiteralPath $stderrPath -Encoding UTF8
    $exitCode = 1
}
finally {
    [Environment]::SetEnvironmentVariable("SAMEBOY_LINK_CLIENT_STDOUT", $previousClientStdout, "Process")
    [Environment]::SetEnvironmentVariable("SAMEBOY_LINK_CLIENT_STDERR", $previousClientStderr, "Process")

    $endedAt = [DateTime]::UtcNow
    $role = Get-DetectedRole -LogPaths @($stderrPath, $stdoutPath, $clientStderrPath, $clientStdoutPath) `
                             -RequestedRole $requestedRole
    @(
        "Role: $role"
        "Ended UTC: $($endedAt.ToString('o'))"
        "Duration seconds: $([Math]::Round(($endedAt - $startedAt).TotalSeconds, 3))"
        "Parent exit code: $parentExitCode"
        "Remote client PID: $clientProcessId"
        "Exit code: $exitCode"
    ) | Add-Content -LiteralPath $infoPath -Encoding UTF8

    $finalNameParts = @($timestamp, $safeComputer, $role)
    if (-not [string]::IsNullOrWhiteSpace($safeLabel)) {
        $finalNameParts += $safeLabel
    }
    $finalNameParts += $launchId
    $finalName = $finalNameParts -join "-"
    $localFinalDirectory = Join-Path $resolvedLocalStagingRoot $finalName
    $sharedFinalDirectory = Join-Path $resolvedLogRoot $finalName
    try {
        Move-Item -LiteralPath $logDirectory -Destination $localFinalDirectory -ErrorAction Stop
        $logDirectory = $localFinalDirectory
    }
    catch {
        $renameWarning = $_.Exception.Message -replace '[\r\n]+', ' '
        Add-Content -LiteralPath $infoPath -Value "Folder rename warning: $renameWarning" -Encoding UTF8
    }

    $stdoutPath = Join-Path $logDirectory "sameboy-stdout.log"
    $stderrPath = Join-Path $logDirectory "sameboy-stderr.log"
    $clientStdoutPath = Join-Path $logDirectory "sameboy-client-stdout.log"
    $clientStderrPath = Join-Path $logDirectory "sameboy-client-stderr.log"
    $combinedPath = Join-Path $logDirectory "sameboy-session.log"
    $infoPath = Join-Path $logDirectory "session-info.txt"
    @(
        "Local staging directory: $logDirectory"
        "Shared log directory: $sharedFinalDirectory"
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

    try {
        Copy-VerifiedLogDirectory -SourceDirectory $logDirectory `
                                  -DestinationDirectory $sharedFinalDirectory
        $sharedCombinedPath = Join-Path $sharedFinalDirectory "sameboy-session.log"
        $localDirectoryToRemove = (Resolve-Path -LiteralPath $logDirectory).ProviderPath
        $safeLocalPrefix = $resolvedLocalStagingRoot.TrimEnd('\') + '\'
        if (-not $localDirectoryToRemove.StartsWith($safeLocalPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove log directory outside local staging root: $localDirectoryToRemove"
        }
        Remove-Item -LiteralPath $localDirectoryToRemove -Recurse -Force
        $logDirectory = $sharedFinalDirectory
        $combinedPath = $sharedCombinedPath
    }
    catch {
        $copyError = $_.Exception.Message -replace '[\r\n]+', ' '
        Add-Content -LiteralPath $infoPath -Value "Shared copy error: $copyError" -Encoding UTF8
        Add-Content -LiteralPath $combinedPath -Value "`r`nShared copy error: $copyError"
        Write-Warning "Kunde inte kopiera loggen till NAS: $copyError"
        Write-Warning "Den lokala loggen finns kvar: $logDirectory"
        $exitCode = 1
    }
}

Write-Host ""
Write-Host "Testet ar avslutat. Roll och unik launch ar sparade i namnet."
Write-Host "Skicka denna fil:"
Write-Host $combinedPath -ForegroundColor Green
exit $exitCode
