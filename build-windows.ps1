[CmdletBinding()]
param(
    [ValidateSet("debug", "release", "native_release")]
    [string]$Configuration = "debug",

    [string[]]$Target = @("sdl"),

    [ValidateRange(1, 256)]
    [int]$Jobs = [Environment]::ProcessorCount,

    [string]$SdlRoot,

    [string]$OpusRoot,

    [switch]$DisableOpus,

    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-Tool {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }

    throw "Could not find $Name. See build-faq.md for the Windows prerequisites."
}

function Add-EnvironmentPath {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [string]$Value
    )

    $currentValue = [Environment]::GetEnvironmentVariable($Name, "Process")
    if ([string]::IsNullOrWhiteSpace($currentValue)) {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
    else {
        [Environment]::SetEnvironmentVariable($Name, "$currentValue;$Value", "Process")
    }
}

$repositoryRoot = $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot "Makefile") -PathType Leaf)) {
    throw "Run this script from a SameBoy checkout; Makefile was not found beside the script."
}

$vswhere = Resolve-Tool -Name "vswhere.exe" -Candidates @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
)

$vsInstallPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($vsInstallPath)) {
    throw "Visual Studio 2022 C++ Build Tools were not found. See build-faq.md."
}

$vsDevCmd = Join-Path $vsInstallPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) {
    throw "VsDevCmd.bat was not found below $vsInstallPath."
}

$developerEnvironmentCommand = "call `"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
$developerEnvironment = & $env:ComSpec /d /s /c $developerEnvironmentCommand
if ($LASTEXITCODE -ne 0) {
    throw "Visual Studio's developer environment could not be initialized."
}

foreach ($line in $developerEnvironment) {
    $separator = $line.IndexOf("=")
    if ($separator -gt 0) {
        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        [Environment]::SetEnvironmentVariable($name, $value, "Process")
    }
}

$clang = Resolve-Tool -Name "clang.exe" -Candidates @(
    "$env:ProgramFiles\LLVM\bin\clang.exe"
)

$make = Resolve-Tool -Name "make.exe" -Candidates @(
    "C:\msys64\usr\bin\make.exe",
    "C:\devkitPro\msys2\usr\bin\make.exe"
)
$msysBin = Split-Path -Parent $make

foreach ($requiredTool in @("sh.exe", "cp.exe", "mkdir.exe", "rm.exe", "touch.exe", "sed.exe", "grep.exe", "ls.exe", "which.exe")) {
    if (-not (Test-Path -LiteralPath (Join-Path $msysBin $requiredTool) -PathType Leaf)) {
        throw "$requiredTool was not found beside $make. Install GNU Make and the MSYS command-line tools."
    }
}

$rgbdsDirectories = @(
    "C:\msys64\mingw64\bin",
    "C:\msys64\ucrt64\bin"
)

$rgbasmCommand = Get-Command "rgbasm.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($rgbasmCommand) {
    $rgbdsDirectories = @((Split-Path -Parent $rgbasmCommand.Source)) + $rgbdsDirectories
}

$rgbdsBin = $null
foreach ($candidate in $rgbdsDirectories | Select-Object -Unique) {
    $hasAllTools = @("rgbasm.exe", "rgblink.exe", "rgbfix.exe", "rgbgfx.exe") |
        ForEach-Object { Test-Path -LiteralPath (Join-Path $candidate $_) -PathType Leaf }
    if ($hasAllTools -notcontains $false) {
        $rgbdsBin = (Resolve-Path -LiteralPath $candidate).Path
        break
    }
}

if (-not $rgbdsBin) {
    throw "RGBDS was not found. See build-faq.md for installation instructions."
}

if ([string]::IsNullOrWhiteSpace($SdlRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:SAMEBOY_SDL2_ROOT)) {
        $SdlRoot = $env:SAMEBOY_SDL2_ROOT
    }
    else {
        $SdlRoot = "C:\SDL2"
    }
}

$SdlRoot = [System.IO.Path]::GetFullPath($SdlRoot)
$sdlInclude = Join-Path $SdlRoot "include"
$sdlLib = Join-Path $SdlRoot "lib\x64"

foreach ($requiredSdlFile in @(
    (Join-Path $sdlInclude "SDL.h"),
    (Join-Path $sdlLib "SDL2.lib"),
    (Join-Path $sdlLib "SDL2main.lib"),
    (Join-Path $sdlLib "SDL2.dll")
)) {
    if (-not (Test-Path -LiteralPath $requiredSdlFile -PathType Leaf)) {
        throw "SDL2 file not found: $requiredSdlFile. Set -SdlRoot or SAMEBOY_SDL2_ROOT."
    }
}

if (-not $DisableOpus) {
    if ([string]::IsNullOrWhiteSpace($OpusRoot)) {
        if (-not [string]::IsNullOrWhiteSpace($env:SAMEBOY_OPUS_ROOT)) {
            $OpusRoot = $env:SAMEBOY_OPUS_ROOT
        }
        else {
            $OpusRoot = "C:\msys64\mingw64"
        }
    }

    $OpusRoot = [System.IO.Path]::GetFullPath($OpusRoot)
    $opusInclude = Join-Path $OpusRoot "include\opus\opus.h"
    $opusImportLibrary = Join-Path $OpusRoot "lib\libopus.dll.a"
    $opusRuntime = Join-Path $OpusRoot "bin\libopus-0.dll"
    foreach ($requiredOpusFile in @($opusInclude, $opusImportLibrary, $opusRuntime)) {
        if (-not (Test-Path -LiteralPath $requiredOpusFile -PathType Leaf)) {
            throw "Opus file not found: $requiredOpusFile. Install mingw-w64-x86_64-opus, set -OpusRoot/SAMEBOY_OPUS_ROOT, or use -DisableOpus."
        }
    }
}
else {
    $OpusRoot = $null
    $opusRuntime = $null
}

$pathEntries = @(
    (Split-Path -Parent $clang),
    $rgbdsBin,
    $msysBin,
    $env:PATH
)
$env:PATH = ($pathEntries | Where-Object { $_ } | Select-Object -Unique) -join ";"

Add-EnvironmentPath -Name "INCLUDE" -Value $sdlInclude
Add-EnvironmentPath -Name "LIB" -Value $sdlLib

# MSYS must leave rc.exe, cvtres.exe and lld-link.exe arguments in Windows form.
$env:MSYS2_ARG_CONV_EXCL = "*"

Write-Host "SameBoy Windows build"
Write-Host "  Configuration: $Configuration"
Write-Host "  Target:        $($Target -join ', ')"
Write-Host "  Jobs:          $Jobs"
Write-Host "  Visual Studio: $vsInstallPath"
Write-Host "  Clang:         $clang"
Write-Host "  SDL2:          $SdlRoot"
Write-Host "  Opus:          $(if ($DisableOpus) { 'disabled' } else { $OpusRoot })"
Write-Host "  RGBDS:         $rgbdsBin"
Write-Host "  Make:          $make"

Push-Location $repositoryRoot
try {
    if ($Clean) {
        & $make clean
        if ($LASTEXITCODE -ne 0) {
            throw "Cleaning failed with exit code $LASTEXITCODE."
        }
    }

    $opusMakeRoot = if ($DisableOpus) { "" } else { $OpusRoot.Replace("\", "/") }
    $makeArguments = @("-j$Jobs") + $Target + @(
        "CONF=$Configuration",
        "CC=clang",
        "PKG_CONFIG=false",
        "lib=$sdlLib",
        "OPUS_ROOT=$opusMakeRoot"
    )

    & $make @makeArguments
    if ($LASTEXITCODE -ne 0) {
        throw "SameBoy build failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}

$sameBoyExecutable = Join-Path $repositoryRoot "build\bin\SDL\sameboy.exe"
if (($Target -contains "sdl" -or $Target -contains "all") -and (Test-Path -LiteralPath $sameBoyExecutable -PathType Leaf)) {
    if (-not $DisableOpus) {
        Copy-Item -LiteralPath $opusRuntime -Destination (Split-Path -Parent $sameBoyExecutable) -Force
    }
    Write-Host "Build succeeded: $sameBoyExecutable" -ForegroundColor Green
}
else {
    Write-Host "Build succeeded." -ForegroundColor Green
}
