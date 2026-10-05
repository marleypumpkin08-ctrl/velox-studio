<#
.SYNOPSIS
    Bootstrap the Velox Studio build dependencies without administrative rights.

.DESCRIPTION
    Installs, into user-scoped locations only:
      * Qt 6.8.3 (win64_msvc2022_64) plus the qtimageformats and qtsvg modules,
        fetched from the official Qt mirror through aqtinstall (run via uv).
      * The official LunarG Vulkan SDK (headers, import libraries, glslc,
        validation layers) extracted from the upstream ZIP archive.

    CMake 3.31, Ninja 1.12 and MSVC v143 are already provided by Visual Studio
    2022 Build Tools and are not installed here.

    Every step is idempotent: an already-satisfied dependency is detected and
    left untouched. Pass -Force to reinstall.

.EXAMPLE
    pwsh -File scripts/bootstrap-deps.ps1
    pwsh -File scripts/bootstrap-deps.ps1 -QtVersion 6.9.3
#>
[CmdletBinding()]
param(
    [string] $QtVersion   = '6.8.3',
    [string] $QtHost      = 'windows',
    [string] $QtTarget    = 'desktop',
    [string] $QtArch      = 'win64_msvc2022_64',
    # Base Qt packages (QtCore/QtGui/QtWidgets/QtNetwork/QtSvg/...) are pulled in
    # unconditionally. Only genuine *addons* belong here - verified against
    # 'aqt list-qt windows desktop --modules 6.8.3 win64_msvc2022_64'.
    # Note: qtsvg is NOT an addon for 6.8.x, it ships inside the base package.
    [string[]] $QtModules = @('qtimageformats'),
    [string] $QtRoot      = (Join-Path $env:LOCALAPPDATA 'Programs\Qt'),
    [string] $VeloxRoot   = (Join-Path $env:LOCALAPPDATA 'VeloxStudio'),
    [switch] $SkipQt,
    [switch] $SkipVulkan,
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'   # far faster without the progress bar

function Write-Step  { param([string]$m) Write-Host "`n==> $m" -ForegroundColor Cyan }
function Write-Ok    { param([string]$m) Write-Host "    [ ok ] $m" -ForegroundColor Green }
function Write-Skip  { param([string]$m) Write-Host "    [skip] $m" -ForegroundColor DarkGray }
function Write-Warn2 { param([string]$m) Write-Host "    [warn] $m" -ForegroundColor Yellow }

$script:BootstrapLog = Join-Path $VeloxRoot 'logs\bootstrap.log'
New-Item -ItemType Directory -Force -Path (Split-Path $script:BootstrapLog) | Out-Null

# NOTE: the parameter must not be called $Args or $ArgsList - $Args is a
# PowerShell automatic variable and shadowing it silently drops the arguments.
function Invoke-Logged {
    param(
        [Parameter(Mandatory)][string]   $Exe,
        [Parameter(Mandatory)][string[]] $ArgumentList
    )
    Write-Host "    > $Exe $($ArgumentList -join ' ')" -ForegroundColor DarkCyan
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'   # native tools legitimately write to stderr
    try {
        & $Exe @ArgumentList 2>&1 | Tee-Object -FilePath $script:BootstrapLog -Append
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) { throw "'$Exe' failed with exit code $code" }
}

Write-Host "Velox Studio - dependency bootstrap (no admin required)" -ForegroundColor Magenta
Write-Host "  Qt         : $QtVersion $QtArch"
Write-Host "  Qt root    : $QtRoot"
Write-Host "  Velox root : $VeloxRoot"
Write-Host "  Log        : $script:BootstrapLog"

# ----------------------------------------------------------------------------
# 1. Locate uv (used to run aqtinstall in an isolated environment)
# ----------------------------------------------------------------------------
Write-Step 'Locating uv'
$uv = (Get-Command uv -ErrorAction SilentlyContinue).Source
if (-not $uv) {
    $candidate = Join-Path $env:USERPROFILE '.local\bin\uv.exe'
    if (Test-Path $candidate) { $uv = $candidate }
}
if (-not $uv) { throw "uv was not found on PATH. Install it from https://docs.astral.sh/uv/ and re-run." }
Write-Ok "uv -> $uv"

# Pull in the MSVC environment (dumpbin/lib) that generating vulkan-1.lib needs.
$envScript = Join-Path $PSScriptRoot 'env.ps1'
if (Test-Path $envScript) {
    . $envScript -Quiet
}

# ----------------------------------------------------------------------------
# 2. Qt 6.8.3 (win64_msvc2022_64)
# ----------------------------------------------------------------------------
$qtPrefix = Join-Path $QtRoot "$QtVersion\msvc2022_64"
$qtConfig = Join-Path $qtPrefix 'lib\cmake\Qt6\Qt6Config.cmake'

if ($SkipQt) {
    Write-Skip 'Qt (requested)'
} elseif ((Test-Path $qtConfig) -and -not $Force) {
    Write-Skip "Qt $QtVersion already present at $qtPrefix"
} else {
    Write-Step "Installing Qt $QtVersion ($QtArch) via aqtinstall"
    New-Item -ItemType Directory -Force -Path $QtRoot | Out-Null

    # aqtinstall runs from an isolated uv-managed environment so it never
    # pollutes the system Python.
    $aqtArgs = @(
        'tool', 'run', '--from', 'aqtinstall', 'aqt',
        'install-qt', $QtHost, $QtTarget, $QtVersion, $QtArch,
        '--outputdir', $QtRoot
    )
    foreach ($m in $QtModules) {
        if ($m) { $aqtArgs += @('-m', $m) }
    }
    Invoke-Logged -Exe $uv -ArgumentList $aqtArgs

    if (-not (Test-Path $qtConfig)) { throw "Qt installation finished but $qtConfig is missing." }
    Write-Ok "Qt $QtVersion installed at $qtPrefix"
}

# ----------------------------------------------------------------------------
# 3. LunarG Vulkan SDK
# ----------------------------------------------------------------------------
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Expand-VeloxArchive {
    param([string]$Zip, [string]$Destination)
    # .NET's ZipFile cannot read the ZIP64 archives LunarG publishes, so
    # bsdtar (shipped with Windows 10+) is preferred, then 7-Zip.
    $bsdtar = (Get-Command tar.exe -ErrorAction SilentlyContinue).Source
    if ($bsdtar) {
        & $bsdtar -xf $Zip -C $Destination
        if ($LASTEXITCODE -eq 0) { return }
    }
    $sevenZip = 'C:\Program Files\7-Zip\7z.exe'
    if (Test-Path $sevenZip) {
        & $sevenZip x -y "-o$Destination" $Zip | Out-Null
        if ($LASTEXITCODE -eq 0) { return }
    }
    throw "No archive extractor could open '$Zip' (it is a ZIP64 archive). Install 7-Zip or run on Windows 10+."
}

function Get-VulkanVersionFromBinaries {
    param([string]$BinDir)
    # The bundled vulkaninfoSDK.exe carries the SDK version in its PE headers.
    $probe = Join-Path $BinDir 'vulkaninfoSDK.exe'
    if (Test-Path $probe) {
        $v = (Get-Item $probe).VersionInfo.FileVersion
        if ($v) { return $v.Trim() }
    }
    return $null
}

# Generates vulkan-1.lib from the system Vulkan loader so the headers can be
# linked. Requires the MSVC toolchain on PATH (scripts/env.ps1 provides it).
function New-VulkanImportLibrary {
    param([string]$SdkDir)

    $dumpbin = (Get-Command dumpbin.exe -ErrorAction SilentlyContinue).Source
    $libtool = (Get-Command lib.exe -ErrorAction SilentlyContinue).Source
    $loader  = Join-Path $env:SystemRoot 'System32\vulkan-1.dll'

    if (-not $dumpbin -or -not $libtool) {
        Write-Warn2 'dumpbin/lib are not on PATH; run scripts/env.ps1 first. Skipping vulkan-1.lib.'
        return $false
    }
    if (-not (Test-Path $loader)) {
        Write-Warn2 "The Vulkan loader was not found at $loader."
        return $false
    }

    $exportsFile = Join-Path $env:TEMP 'velox-vk-exports.txt'
    & $dumpbin /nologo /exports $loader 2>&1 | Out-File -Encoding ascii $exportsFile

    $names = Get-Content $exportsFile |
        ForEach-Object {
            if ($_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$') { $Matches[1] }
        } |
        Where-Object { $_ -like 'vk*' } |
        Sort-Object -Unique

    if (-not $names -or $names.Count -eq 0) {
        Write-Warn2 'No Vulkan exports were found in the Vulkan loader.'
        return $false
    }

    New-Item -ItemType Directory -Force -Path (Join-Path $SdkDir 'Lib') | Out-Null
    $defFile = Join-Path $env:TEMP 'vulkan-1.def'
    Set-Content -Path $defFile -Value (@('LIBRARY vulkan-1.dll', 'EXPORTS') + $names) -Encoding ascii

    & $libtool /nologo /def:$defFile /machine:x64 /out:"$(Join-Path $SdkDir 'Lib\vulkan-1.lib')" 2>&1 | Out-Null
    Remove-Item $exportsFile, $defFile -Force -ErrorAction SilentlyContinue

    if (Test-Path (Join-Path $SdkDir 'Lib\vulkan-1.lib')) {
        Write-Ok "generated vulkan-1.lib from the system loader ($($names.Count) symbols)"
        return $true
    }
    Write-Warn2 'lib.exe did not produce vulkan-1.lib.'
    return $false
}

$vulkanRoot = Join-Path $VeloxRoot 'VulkanSDK'
$vulkanUrl  = 'https://sdk.lunarg.com/sdk/download/latest/windows/vulkan-sdk.zip'

if ($SkipVulkan) {
    Write-Skip 'Vulkan SDK (requested)'
} else {
    $existing = $null
    if (Test-Path $vulkanRoot) {
        $existing = Get-ChildItem $vulkanRoot -Directory -ErrorAction SilentlyContinue |
                    Where-Object { Test-Path (Join-Path $_.FullName 'Include\vulkan\vulkan.h') } |
                    Sort-Object Name -Descending | Select-Object -First 1
    }

    if ($existing -and -not $Force) {
        Write-Skip "Vulkan SDK already present at $($existing.FullName)"
    } else {
        Write-Step 'Downloading the LunarG Vulkan SDK binary payload'
        New-Item -ItemType Directory -Force -Path $vulkanRoot | Out-Null
        $zip = Join-Path $env:TEMP 'velox-vulkan-sdk.zip'

        Invoke-WebRequest -Uri $vulkanUrl -OutFile $zip -UseBasicParsing
        $sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
        Write-Ok "downloaded $sizeMb MB"

        Write-Step 'Extracting the SDK payload'
        $staging = Join-Path $vulkanRoot '.staging'
        if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $staging | Out-Null
        Expand-VeloxArchive -Zip $zip -Destination $staging
        Remove-Item $zip -Force -ErrorAction SilentlyContinue

        $ver = Get-VulkanVersionFromBinaries -BinDir (Join-Path $staging 'Bin')
        if (-not $ver) { throw 'Could not determine the Vulkan SDK version from the payload.' }

        $dest = Join-Path $vulkanRoot $ver
        if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
        Move-Item $staging $dest -Force
        Write-Ok "Vulkan SDK $ver binaries installed"

        # 3a. Headers matching this exact SDK version, from the Khronos
        #     Vulkan-Headers repository (tag names mirror the SDK version).
        Write-Step "Fetching Vulkan-Headers for $ver"
        $headersZip = Join-Path $env:TEMP 'velox-vkh.zip'
        $headersUrl = "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-$ver.zip"
        Invoke-WebRequest -Uri $headersUrl -OutFile $headersZip -UseBasicParsing

        $headersStaging = Join-Path $env:TEMP "velox-vkh-$([guid]::NewGuid().ToString('N'))"
        New-Item -ItemType Directory -Force -Path $headersStaging | Out-Null
        Expand-VeloxArchive -Zip $headersZip -Destination $headersStaging

        $headersSrc = Get-ChildItem $headersStaging -Directory | Select-Object -First 1
        Copy-Item (Join-Path $headersSrc.FullName 'include\vulkan') (Join-Path $dest 'Include\vulkan') -Recurse -Force
        Copy-Item (Join-Path $headersSrc.FullName 'include\vk_video') (Join-Path $dest 'Include\vk_video') -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item $headersStaging -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item $headersZip -Force -ErrorAction SilentlyContinue

        if (-not (Test-Path (Join-Path $dest 'Include\vulkan\vulkan.h'))) {
            throw "Vulkan headers were not installed into $dest\Include\vulkan"
        }
        Write-Ok 'Vulkan headers installed'

        # 3b. Import library generated from the system Vulkan loader, so the
        #     build never depends on the SDK installer being run.
        Write-Step 'Generating the Vulkan import library'
        [void](New-VulkanImportLibrary -SdkDir $dest)

        if (-not (Test-Path (Join-Path $dest 'Bin\glslc.exe'))) {
            Write-Warn2 "Bin\glslc.exe is missing - shader compilation will fail."
        }
        Write-Ok "Vulkan SDK $ver ready at $dest"
    }
}

# Safety net: the payload may already be installed while the generated import
# library is missing (for example when the SDK was fetched without the MSVC
# environment available). Regenerate it whenever it is absent.
if (-not $SkipVulkan -and (Test-Path $vulkanRoot)) {
    $installed = Get-ChildItem $vulkanRoot -Directory -ErrorAction SilentlyContinue |
                 Where-Object { Test-Path (Join-Path $_.FullName 'Include\vulkan\vulkan.h') } |
                 Sort-Object Name -Descending | Select-Object -First 1
    if ($installed -and -not (Test-Path (Join-Path $installed.FullName 'Lib\vulkan-1.lib'))) {
        Write-Step 'Generating the missing Vulkan import library'
        [void](New-VulkanImportLibrary -SdkDir $installed.FullName)
    }
}

Write-Step 'Bootstrap complete'
Write-Host "    Qt     : $qtPrefix"
Write-Host "    Vulkan : $vulkanRoot"
Write-Host 'Source scripts/env.ps1 (or use the CMake presets) to consume these toolchains.' -ForegroundColor Magenta