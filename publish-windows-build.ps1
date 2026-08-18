[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$DestinationRoot,

    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$Label,

    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = $PSScriptRoot
$runtimeSource = Join-Path $repositoryRoot "build\bin\SDL"
$sharedLauncherSource = Join-Path $repositoryRoot "run-shared-windows-build.ps1"
$loggingLauncherSources = @(
    (Join-Path $repositoryRoot "start-sameboy-with-log.cmd"),
    (Join-Path $repositoryRoot "start-sameboy-with-log.ps1")
)

if (-not $SkipBuild) {
    Write-Host "[RUN ] Windows debug build"
    & (Join-Path $repositoryRoot "build-windows.ps1")
    Write-Host "[PASS] Windows debug build"
}

$executableSource = Join-Path $runtimeSource "sameboy.exe"
if (-not (Test-Path -LiteralPath $executableSource -PathType Leaf)) {
    throw "SameBoy executable not found at $executableSource. Run without -SkipBuild first."
}
if (-not (Test-Path -LiteralPath $sharedLauncherSource -PathType Leaf)) {
    throw "Shared-build launcher not found at $sharedLauncherSource."
}
foreach ($loggingLauncherSource in $loggingLauncherSources) {
    if (-not (Test-Path -LiteralPath $loggingLauncherSource -PathType Leaf)) {
        throw "Logging launcher not found at $loggingLauncherSource."
    }
}

$requiredRuntimeItems = @(
    "sameboy.exe",
    "SDL2.dll",
    "Shaders",
    "Palettes",
    "background.bmp"
)
foreach ($item in $requiredRuntimeItems) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeSource $item))) {
        throw "Required runtime item '$item' is missing below $runtimeSource."
    }
}

$sourceCommit = (git -C $repositoryRoot rev-parse --short=12 HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($sourceCommit)) {
    throw "Could not determine the source commit."
}
$sourceDirty = -not [string]::IsNullOrWhiteSpace((git -C $repositoryRoot status --porcelain))
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($Label)) {
    $dirtySuffix = if ($sourceDirty) { "-dirty" } else { "" }
    $buildId = "$timestamp-$sourceCommit$dirtySuffix"
}
else {
    $buildId = $Label
}

New-Item -ItemType Directory -Path $DestinationRoot -Force | Out-Null
$resolvedDestinationRoot = (Resolve-Path -LiteralPath $DestinationRoot).Path
$buildsRoot = Join-Path $resolvedDestinationRoot "builds"
New-Item -ItemType Directory -Path $buildsRoot -Force | Out-Null
$publishDirectory = Join-Path $buildsRoot $buildId
if (Test-Path -LiteralPath $publishDirectory) {
    throw "Build destination already exists: $publishDirectory"
}
New-Item -ItemType Directory -Path $publishDirectory | Out-Null

try {
    Get-ChildItem -LiteralPath $runtimeSource -Force |
        Where-Object { $_.Name -ne "prefs.bin" } |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName `
                      -Destination $publishDirectory `
                      -Recurse
    }
    Copy-Item -LiteralPath $sharedLauncherSource -Destination $publishDirectory
    foreach ($loggingLauncherSource in $loggingLauncherSources) {
        Copy-Item -LiteralPath $loggingLauncherSource -Destination $publishDirectory
    }

    $publishedPrefs = Join-Path $publishDirectory "prefs.bin"
    if (Test-Path -LiteralPath $publishedPrefs) {
        throw "prefs.bin must not be present in a shared build."
    }

    $fileEntries = @(
        Get-ChildItem -LiteralPath $publishDirectory -File -Recurse |
            Sort-Object FullName |
            ForEach-Object {
                $relativePath = $_.FullName.Substring($publishDirectory.Length + 1).Replace('\', '/')
                [ordered]@{
                    path = $relativePath
                    bytes = $_.Length
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            }
    )

    $manifest = [ordered]@{
        schemaVersion = 1
        product = "SameBoy Link"
        buildId = $buildId
        configuration = "debug"
        architecture = "x86_64-windows"
        sourceCommit = $sourceCommit
        sourceDirty = $sourceDirty
        createdAtUtc = [DateTime]::UtcNow.ToString("o")
        executable = "sameboy.exe"
        sharedPreferencesIncluded = $false
        files = $fileEntries
    }
    $manifestPath = Join-Path $publishDirectory "build-manifest.json"
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

    foreach ($entry in $fileEntries) {
        $publishedPath = Join-Path $publishDirectory $entry.path.Replace('/', '\')
        $publishedHash = (Get-FileHash -LiteralPath $publishedPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($publishedHash -ne $entry.sha256) {
            throw "Hash verification failed for $($entry.path)."
        }
    }
}
catch {
    [Console]::Error.WriteLine("Publish failed in ${publishDirectory}: $($_.Exception.Message)")
    throw
}

$publishedExecutable = Join-Path $publishDirectory "sameboy.exe"
Write-Host "[PASS] Published verified Windows runtime"
Write-Host "Build ID: $buildId"
Write-Host "Dirty source: $sourceDirty"
Write-Host "Executable: $publishedExecutable"
Write-Host "Manifest: $(Join-Path $publishDirectory 'build-manifest.json')"
