[CmdletBinding()]
param(
    [string]$DestinationRoot = $PSScriptRoot,

    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$Label
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = $PSScriptRoot
$runtimeSource = Join-Path $repositoryRoot "build\bin\SDL"
$buildScript = Join-Path $repositoryRoot "build-windows.ps1"

if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    throw "build-windows.ps1 was not found beside this script."
}

$sourceCommit = (& git -C $repositoryRoot rev-parse --short=12 HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($sourceCommit)) {
    throw "Could not determine the source commit."
}
$sourceDirty = -not [string]::IsNullOrWhiteSpace(
    (& git -C $repositoryRoot status --porcelain) -join "`n"
)

if ([string]::IsNullOrWhiteSpace($Label)) {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $dirtySuffix = if ($sourceDirty) { "-dirty" } else { "" }
    $Label = "$timestamp-$sourceCommit$dirtySuffix"
}

Write-Host "[RUN ] Clean optimized PCM-only Windows build"
& $buildScript -Configuration release -DisableOpus -Clean
Write-Host "[PASS] Optimized Windows build"

# These are the complete non-debug runtime assets. Opus is intentionally
# excluded because PCM is the stable Remote Play baseline and avoids one DLL.
$runtimeItems = @(
    "sameboy.exe",
    "SDL2.dll",
    "dmg_boot.bin",
    "mgb_boot.bin",
    "cgb0_boot.bin",
    "cgb_boot.bin",
    "agb_boot.bin",
    "sgb_boot.bin",
    "sgb2_boot.bin",
    "background.bmp",
    "Shaders",
    "Palettes",
    "LICENSE"
)

foreach ($item in $runtimeItems) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeSource $item))) {
        throw "Required portable runtime item '$item' is missing below $runtimeSource."
    }
}

New-Item -ItemType Directory -Path $DestinationRoot -Force | Out-Null
$resolvedDestinationRoot = (Resolve-Path -LiteralPath $DestinationRoot).ProviderPath
$archiveName = "SameBoyLink-$Label-windows-x64.zip"
$archivePath = Join-Path $resolvedDestinationRoot $archiveName
if (Test-Path -LiteralPath $archivePath) {
    throw "Portable archive already exists: $archivePath"
}

$stagingRoot = Join-Path $repositoryRoot "build\portable-staging"
New-Item -ItemType Directory -Path $stagingRoot -Force | Out-Null
$resolvedStagingRoot = (Resolve-Path -LiteralPath $stagingRoot).ProviderPath
$stagingName = ".staging-$PID-$([Guid]::NewGuid().ToString('N'))"
$stagingDirectory = Join-Path $resolvedStagingRoot $stagingName
$packageDirectory = Join-Path $stagingDirectory "SameBoyLink"
New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null

try {
    foreach ($item in $runtimeItems) {
        Copy-Item -LiteralPath (Join-Path $runtimeSource $item) `
                  -Destination $packageDirectory `
                  -Recurse
    }

    $forbiddenItems = @(
        "prefs.bin",
        "libopus-0.dll",
        "registers.sym",
        "sameboy_debugger.txt"
    )
    foreach ($item in $forbiddenItems) {
        if (Test-Path -LiteralPath (Join-Path $packageDirectory $item)) {
            throw "Non-portable or debug item '$item' entered the package."
        }
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $packageDirectory,
        $archivePath,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $true
    )

    $expectedPaths = @(
        Get-ChildItem -LiteralPath $packageDirectory -File -Recurse |
            ForEach-Object {
                "SameBoyLink/" + $_.FullName.Substring($packageDirectory.Length + 1).Replace('\', '/')
            } |
            Sort-Object
    )
    $archive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $actualPaths = @(
            $archive.Entries |
                Where-Object { -not [string]::IsNullOrEmpty($_.Name) } |
                ForEach-Object { $_.FullName.Replace('\', '/') } |
                Sort-Object
        )
    }
    finally {
        $archive.Dispose()
    }
    if (($expectedPaths -join "`n") -ne ($actualPaths -join "`n")) {
        throw "Portable archive verification failed: archive contents do not match staging."
    }

    $uncompressedBytes = (
        Get-ChildItem -LiteralPath $packageDirectory -File -Recurse |
            Measure-Object -Property Length -Sum
    ).Sum
    $archiveFile = Get-Item -LiteralPath $archivePath
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()

    Write-Host "[PASS] Portable archive created and verified" -ForegroundColor Green
    Write-Host "Archive:      $archivePath"
    Write-Host "Files inside: $($expectedPaths.Count)"
    Write-Host "Uncompressed: $([Math]::Round($uncompressedBytes / 1MB, 2)) MiB"
    Write-Host "ZIP size:     $([Math]::Round($archiveFile.Length / 1MB, 2)) MiB"
    Write-Host "SHA-256:      $archiveHash"
    Write-Host "Source:       $sourceCommit$(if ($sourceDirty) { ' (dirty)' } else { '' })"
}
catch {
    if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
        Remove-Item -LiteralPath $archivePath -Force
    }
    throw
}
finally {
    $resolvedStagingDirectory = [System.IO.Path]::GetFullPath($stagingDirectory)
    $safeStagingPrefix = $resolvedStagingRoot.TrimEnd('\') + '\'
    if ($resolvedStagingDirectory.StartsWith(
            $safeStagingPrefix,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedStagingDirectory)) {
        Remove-Item -LiteralPath $resolvedStagingDirectory -Recurse -Force
    }
}
