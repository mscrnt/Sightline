<#
.SYNOPSIS
    Sample the original cartridge ROM's boot sequence, frame by frame.

.DESCRIPTION
    The reference oracle. Boots YOUR OWN GoldenEye (U) ROM under the same
    parallel_n64 libretro core the trace harness uses, advances it one video
    frame at a time, and records both the game's own counters and what is
    actually on screen.

    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\rom-reference.ps1                  # 1400 frames
        .\tools\windows\rom-reference.ps1 -Frames 3000     # through the cast roll
        .\tools\windows\rom-reference.ps1 -Control         # prove the probe varies

    WHY THIS EXISTS. The front-end timing has been "corrected" three times by
    reasoning from threshold constants in front.c and title.c, and three times
    the owner re-tested and found it still wrong. "How long is the Legal screen
    on screen" is not answerable from a constant: the visible span includes
    screen switches and loading that no counter in the source describes. So
    this measures the original rather than deriving it.

    Nothing is patched. The ROM that runs is byte-identical to your cartridge
    dump, and state is read from RDRAM at addresses resolved from the matching
    build's link map.

    Output is a CSV written OUTSIDE the repository. The samples are ROM-derived
    and must never be committed, so a path inside the working tree is refused
    rather than left to a .gitignore.

.PARAMETER Frames
    Bounded frame count. 1400 reaches the eye intro; 3000 covers the whole boot
    through the cast roll. Default 1400.

.PARAMETER Control
    Report every distinct value each field took, so a field that never varied
    is NAMED rather than quietly passing. An instrument that cannot fail is
    worthless; this is how this one is made to fail visibly.

.PARAMETER Out
    CSV destination. Defaults to %TEMP%\sightline-oracle\boot-<timestamp>.csv.

.PARAMETER Rom
    ROM to boot. Defaults to $env:SL_ROM, then baserom.u.z64 in the repo.

.PARAMETER Core
    parallel_n64 libretro core. Defaults to $env:SL_LIBRETRO_CORE, then
    %LOCALAPPDATA%\sightline\libretro\parallel_n64_libretro.dll.

    The core is an EXTERNAL dependency and is never committed. Get one with:

        $dir = "$env:LOCALAPPDATA\sightline\libretro"
        New-Item -ItemType Directory -Force $dir | Out-Null
        $url = 'https://buildbot.libretro.com/nightly/windows/x86_64/latest/parallel_n64_libretro.dll.zip'
        Invoke-WebRequest $url -OutFile "$dir\core.zip"
        Expand-Archive "$dir\core.zip" -DestinationPath $dir -Force
        Remove-Item "$dir\core.zip"

    Match the core's architecture to your Python: a win64 DLL needs 64-bit
    Python. ctypes reports a confusing "not a valid Win32 application" on a
    mismatch, which reads like a corrupt download rather than a wrong build.
#>
[CmdletBinding()]
param(
    [int]$Frames = 1400,
    [switch]$Control,
    [string]$Out,
    [string]$Rom,
    [string]$Core
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $Rom)  { $Rom  = $env:SL_ROM }
if (-not $Rom)  { $Rom  = Join-Path $repo 'baserom.u.z64' }
if (-not (Test-Path $Rom)) {
    Write-Host "sightline: no ROM at '$Rom'."
    Write-Host '  Point SL_ROM at your own GoldenEye (U) copy. Nothing ROM-derived'
    Write-Host '  ships in this repository, so there is no fallback to fall back to.'
    exit 1
}
$Rom = (Resolve-Path $Rom).Path

if (-not $Core) { $Core = $env:SL_LIBRETRO_CORE }
if (-not $Core) { $Core = Join-Path $env:LOCALAPPDATA 'sightline\libretro\parallel_n64_libretro.dll' }
if (-not (Test-Path $Core)) {
    Write-Host "sightline: no libretro core at '$Core'."
    Write-Host '  The core is an external dependency and is never committed.'
    Write-Host '  Get one (see the -Core help in this script):'
    Write-Host '    Get-Help .\tools\windows\rom-reference.ps1 -Detailed'
    exit 1
}
$Core = (Resolve-Path $Core).Path

# The link map comes from 'make matching'. Its addresses are what makes this
# read DEFINED game state instead of guessing at memory, so a missing map is a
# hard stop rather than a degraded run.
$map = Join-Path $repo 'build\u\ge007.u.map'
if (-not (Test-Path $map)) {
    Write-Host "sightline: no link map at '$map'. Build it with 'make matching'."
    exit 1
}

$python = $env:SL_PYTHON
if (-not $python) { $python = Join-Path $repo '.venv\Scripts\python.exe' }
if (-not (Test-Path $python)) {
    $sys = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $sys) {
        Write-Host 'sightline: no Python. Run .\tools\windows\setup.ps1.'
        exit 1
    }
    $python = $sys.Source
}

$probe = Join-Path $repo 'tools\trace\bootprobe.py'
$probeArgs = @($probe, '--rom', $Rom, '--map', $map, '--core', $Core,
               '--frames', $Frames)
if ($Out)     { $probeArgs += @('--out', $Out) }
if ($Control) { $probeArgs += '--control' }

& $python $probeArgs
exit $LASTEXITCODE
