[CmdletBinding()]
param(
    [string]$CacheRoot = (Join-Path $env:LOCALAPPDATA "SameBoy Link\Builds"),

    [switch]$Wait,

    [switch]$PassThru,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$SameBoyArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$sharedBuildDirectory = $PSScriptRoot
$manifestPath = Join-Path $sharedBuildDirectory "build-manifest.json"
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Build manifest not found beside the launcher: $manifestPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or
    [string]::IsNullOrWhiteSpace($manifest.buildId) -or
    [string]::IsNullOrWhiteSpace($manifest.executable)) {
    throw "Unsupported or incomplete SameBoy Link build manifest."
}
if ($manifest.buildId -notmatch '^[A-Za-z0-9._-]+$') {
    throw "Unsafe build ID in the shared manifest."
}

function Resolve-ManifestRelativePath {
    param(
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$RelativePath
    )

    $normalized = $RelativePath.Replace('/', '\')
    if ([System.IO.Path]::IsPathRooted($normalized) -or
        $normalized.Split('\') -contains '..') {
        throw "Unsafe relative path in build manifest: $RelativePath"
    }
    return Join-Path $Root $normalized
}

function Test-CachedBuild {
    param(
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][object[]]$Entries
    )

    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        return $false
    }
    foreach ($entry in $Entries) {
        $path = Resolve-ManifestRelativePath -Root $Directory -RelativePath ([string]$entry.path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            return $false
        }
        if ((Get-Item -LiteralPath $path).Length -ne [int64]$entry.bytes) {
            return $false
        }
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne [string]$entry.sha256) {
            return $false
        }
    }
    return $true
}

$entries = @($manifest.files)
if ($entries.Count -eq 0) {
    throw "The shared build manifest contains no files."
}

New-Item -ItemType Directory -Path $CacheRoot -Force | Out-Null
$resolvedCacheRoot = (Resolve-Path -LiteralPath $CacheRoot).Path
$cacheDirectory = Join-Path $resolvedCacheRoot ([string]$manifest.buildId)

if (-not (Test-CachedBuild -Directory $cacheDirectory -Entries $entries)) {
    if (Test-Path -LiteralPath $cacheDirectory) {
        throw "Local cache exists but does not match the signed file list: $cacheDirectory"
    }

    $stagingDirectory = Join-Path $resolvedCacheRoot "$($manifest.buildId).partial-$PID"
    if (Test-Path -LiteralPath $stagingDirectory) {
        throw "Staging directory already exists: $stagingDirectory"
    }
    New-Item -ItemType Directory -Path $stagingDirectory | Out-Null

    foreach ($entry in $entries) {
        $relativePath = [string]$entry.path
        $sourcePath = Resolve-ManifestRelativePath -Root $sharedBuildDirectory -RelativePath $relativePath
        $destinationPath = Resolve-ManifestRelativePath -Root $stagingDirectory -RelativePath $relativePath
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "Shared build file is missing: $sourcePath"
        }
        $destinationParent = Split-Path -Parent $destinationPath
        New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath

        $copiedHash = (Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($copiedHash -ne [string]$entry.sha256) {
            throw "Hash verification failed while caching $relativePath."
        }
    }

    Copy-Item -LiteralPath $manifestPath `
              -Destination (Join-Path $stagingDirectory "build-manifest.json")
    Move-Item -LiteralPath $stagingDirectory -Destination $cacheDirectory
    Write-Host "Cached and verified SameBoy Link build $($manifest.buildId)."
}
else {
    Write-Host "Using verified local cache for SameBoy Link build $($manifest.buildId)."
}

$localExecutable = Resolve-ManifestRelativePath -Root $cacheDirectory `
                                                -RelativePath ([string]$manifest.executable)
if (-not (Test-Path -LiteralPath $localExecutable -PathType Leaf)) {
    throw "Cached SameBoy executable is missing: $localExecutable"
}

$startParameters = @{
    FilePath = $localExecutable
    WorkingDirectory = $cacheDirectory
    PassThru = $true
}
if ($SameBoyArguments.Count -gt 0) {
    $startParameters.ArgumentList = $SameBoyArguments
}
$process = Start-Process @startParameters
Write-Host "Started SameBoy Link build $($manifest.buildId) as PID $($process.Id)."
Write-Host "Local executable: $localExecutable"
if ($Wait) {
    $process.WaitForExit()
    exit $process.ExitCode
}
if ($PassThru) {
    Write-Output $process
}
