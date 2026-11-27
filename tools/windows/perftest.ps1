<#
.SYNOPSIS
    Build and run the OPTIMIZED RENDERING policy self-test.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\perftest.ps1

    Compiles src/platform/sl_perf.c together with the real settings store
    (src/platform/sl_settings.c) with -DSL_PERF_SELFTEST -DSL_PERF_TEST and
    runs the fixture inside it: the row's default (ON in a player session),
    its write-on-change and read-back, an invalid set refused, the
    SL_PERF_OPT launch override and the set that ends it, the inactive-store
    semantics that keep trace replay / headless / matching runs on their own
    paths, each subsystem switch governing its own subsystem only, the
    far-walk arm, and the carrier the row selects (prod, never byte or div).

    The scratch config lands under %TEMP% through SL_CONFIG; the player's own
    config is never opened. Nothing is installed, no ROM is opened, no window
    and no GL context exists.

    Exits 0 when every check passes, 1 otherwise. Same toolchain ladder as
    build.ps1 / settingstest.ps1.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing.
#>
[CmdletBinding()]
param([string]$Msys2Root, [string]$OutDir)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# -OutDir sends this fixture's executable somewhere other than build\win32.
# The default is build\win32 exactly as before. The scratch config this fixture
# writes already lands under %TEMP% through SL_CONFIG, so the executable is the
# only thing it puts in the repository, and an isolated -OutDir makes the whole
# run leave the pinned binaries' directory alone. Nothing here deletes or
# restores anything.
if (-not $OutDir) { $OutDir = Join-Path $repo 'build\win32' }
$OUT  = $OutDir

if (-not $Msys2Root) { $Msys2Root = $env:SL_MSYS2_ROOT }
$roots = @()
if ($Msys2Root) { $roots += $Msys2Root }
$roots += @('C:\msys64', 'C:\msys2', "$env:SystemDrive\msys64")
$mingwBin = $null
foreach ($r in $roots) {
    $cand = Join-Path $r 'mingw32\bin'
    if (Test-Path (Join-Path $cand 'i686-w64-mingw32-gcc.exe')) { $mingwBin = $cand; break }
}
if ($null -eq $mingwBin) {
    Write-Error "perftest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'perftest.exe'

$rc = 0
$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra',
            '-DSL_PERF_SELFTEST', '-DSL_PERF_TEST',
            '-o', $exe, 'src/platform/sl_perf.c', 'src/platform/sl_settings.c')
    if ($LASTEXITCODE -ne 0) { throw "perftest.ps1: compile failed (exit $LASTEXITCODE)" }
    # The store's own diagnostics go to stderr; cmd owns the redirect
    # (PowerShell 5.1 would turn them into error records).
    & cmd /c "`"$exe`" 2>nul"
    if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}

exit $rc
