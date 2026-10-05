<#
.SYNOPSIS
    Load the Velox Studio development environment into the current session.

.DESCRIPTION
    Dot-source this file before configuring or building:

        . .\scripts\env.ps1

    It locates the Visual Studio toolchain (MSVC + Windows SDK), the CMake and
    Ninja builds that ship with Visual Studio, the bootstrapped Qt 6
    installation and the bootstrapped Vulkan SDK, then exports them into the
    process environment so that CMake presets can consume them:

        VELOX_VS_ROOT     Visual Studio installation root
        VELOX_QT_DIR      Qt 6 msvc2022_64 prefix (contains lib/cmake/Qt6)
        VULKAN_SDK        LunarG Vulkan SDK root (headers, lib, glslc, layers)
        VELOX_CMAKE       cmake.exe
        VELOX_NINJA       ninja.exe
        VELOX_BUILD_DIR   out-of-tree build root (kept outside OneDrive)

.PARAMETER QtVersion
    Qt version prefix installed by scripts/bootstrap-deps.ps1. Default 6.8.3.

.PARAMETER BuildRoot
    Root directory for out-of-tree builds. Defaults to <drive>:\velox-build.
#>
[CmdletBinding()]
param(
    [string] $QtVersion = '6.8.3',
    [string] $BuildRoot = '',
    [switch] $Quiet
)

$ErrorActionPreference = 'Stop'

# Default parameter expressions cannot reliably read $PSScriptRoot when the
# script is dot-sourced, so the build root is resolved here instead.
if (-not $BuildRoot) {
    $drive = [System.IO.Path]::GetPathRoot((Resolve-Path $PSScriptRoot).Path)
    $BuildRoot = Join-Path $drive 'velox-build'
}

function Write-Info { param([string]$Message) if (-not $Quiet) { Write-Host "  $Message" -ForegroundColor DarkGray } }
function Write-Found { param([string]$What, [string]$Path) if (-not $Quiet) { Write-Host "  [ok] $What`n         $Path" -ForegroundColor Green } }
function Write-Miss { param([string]$What, [string]$Hint) if (-not $Quiet) { Write-Host "  [!!] $What not found - $Hint" -ForegroundColor Yellow } }

# ---------------------------------------------------------------------------
# Visual Studio (MSVC + Windows SDK)
#
# v143 / MSVC 14.4x from Visual Studio 2022 is preferred because it is the
# exact toolset Qt's official win64_msvc2022_64 binaries were built with.
# ---------------------------------------------------------------------------
$vsCandidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\Community',
    'C:\Program Files\Microsoft Visual Studio\2022\Community',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise',
    'C:\Program Files\Microsoft Visual Studio\18\Community'
)

$vsRoot = $null
foreach ($candidate in $vsCandidates) {
    if (Test-Path (Join-Path $candidate 'VC\Auxiliary\Build\vcvars64.bat')) { $vsRoot = $candidate; break }
}
if (-not $vsRoot) {
    # Fall back to the official Visual Studio locator if present.
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsRoot) { $vsRoot = $vsRoot.Trim() }
    }
}
if (-not $vsRoot) { throw 'No Visual Studio installation with the C++ tools was found. Install the "Desktop development with C++" workload.' }

$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
if (-not $env:VSCMD_ARG_TGT_ARCH) {
    Write-Info "importing MSVC environment from $vcvars"
    $captured = cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $captured) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
        }
    }
}
$env:VELOX_VS_ROOT = $vsRoot
Write-Found 'Visual Studio' $vsRoot

# ---------------------------------------------------------------------------
# CMake and Ninja (shipped with Visual Studio, otherwise taken from PATH)
# ---------------------------------------------------------------------------
function Resolve-Tool {
    param([string]$Name, [string[]]$Candidates)
    foreach ($c in $Candidates) { if ($c -and (Test-Path $c)) { return $c } }
    $onPath = (Get-Command $Name -ErrorAction SilentlyContinue).Source
    if ($onPath) { return $onPath }
    return $null
}

$cmakeCandidates = @(
    (Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'),
    'C:\Program Files\CMake\bin\cmake.exe'
)
$ninjaCandidates = @(
    (Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'),
    'C:\Program Files\CMake\bin\ninja.exe'
)

$cmake = Resolve-Tool -Name 'cmake' -Candidates $cmakeCandidates
$ninja = Resolve-Tool -Name 'ninja' -Candidates $ninjaCandidates

if (-not $cmake) { throw 'cmake.exe was not found. Install CMake or use a Visual Studio installation that bundles it.' }
$env:VELOX_CMAKE = $cmake
$cmakeBinDir = Split-Path $cmake -Parent
Write-Found 'CMake' $cmake

if ($ninja) {
    $env:VELOX_NINJA = $ninja
    $ninjaBinDir = Split-Path $ninja -Parent
    Write-Found 'Ninja' $ninja
} else {
    Write-Miss 'Ninja' 'install it or use the Visual Studio generator'
}

# ---------------------------------------------------------------------------
# Qt 6 - installed by scripts/bootstrap-deps.ps1
# ---------------------------------------------------------------------------
$qtRoot = Join-Path $env:LOCALAPPDATA 'Programs\Qt'
$qtDir  = Join-Path $qtRoot "$QtVersion\msvc2022_64"

if (-not (Test-Path (Join-Path $qtDir 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    # Accept any installed version when the requested one is absent.
    $fallback = $null
    if (Test-Path $qtRoot) {
        $fallback = Get-ChildItem $qtRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'msvc2022_64' } |
            Where-Object { Test-Path (Join-Path $_ 'lib\cmake\Qt6\Qt6Config.cmake') } |
            Select-Object -First 1
    }
    if ($fallback) { $qtDir = $fallback }
}

if (Test-Path (Join-Path $qtDir 'lib\cmake\Qt6\Qt6Config.cmake')) {
    $env:VELOX_QT_DIR = $qtDir
    $env:Qt6_DIR      = Join-Path $qtDir 'lib\cmake\Qt6'
    $env:PATH         = (Join-Path $qtDir 'bin') + ';' + $env:PATH
    Write-Found 'Qt' $qtDir
} else {
    Write-Miss 'Qt' "run scripts/bootstrap-deps.ps1 (looked in $qtRoot)"
}

# ---------------------------------------------------------------------------
# Vulkan SDK - installed by scripts/bootstrap-deps.ps1
# ---------------------------------------------------------------------------
$vulkanRoot = Join-Path $env:LOCALAPPDATA 'VeloxStudio\VulkanSDK'
$vulkanDir  = $null
if (Test-Path $vulkanRoot) {
    $vulkanDir = Get-ChildItem $vulkanRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'Include\vulkan\vulkan.h') } |
        Sort-Object Name -Descending | Select-Object -First 1 |
        ForEach-Object { $_.FullName }
}

# Discard any inherited (and possibly stale) VULKAN_SDK pointing at a deleted
# SDK before exporting the one we actually verified.
$env:VULKAN_SDK = $null

if ($vulkanDir) {
    $env:VULKAN_SDK = $vulkanDir
    $env:PATH = @(
        (Join-Path $vulkanDir 'Bin'),
        (Join-Path $vulkanDir 'Lib'),
        $env:PATH
    ) -join ';'
    # Validation layers are loaded by explicit path, so no registry changes
    # (which would require administrator rights) are needed.
    $env:VK_LAYER_PATH = Join-Path $vulkanDir 'Bin'
    Write-Found 'Vulkan SDK' $vulkanDir
} else {
    Write-Miss 'Vulkan SDK' 'run scripts/bootstrap-deps.ps1'
}

# ---------------------------------------------------------------------------
# Build directory (kept out of the OneDrive-synced source tree)
# ---------------------------------------------------------------------------
$env:VELOX_BUILD_DIR = $BuildRoot
Write-Info "build root: $BuildRoot"

if (-not $Quiet) {
    Write-Host ''
    Write-Host '  Velox Studio environment ready.' -ForegroundColor Magenta
    Write-Host ''
}