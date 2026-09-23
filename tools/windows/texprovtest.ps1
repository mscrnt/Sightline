<#
.SYNOPSIS
    Build and run the texture provider self-test (#47).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\texprovtest.ps1

    Compiles src/gfx/sl_gfx_texprov.c with -DSL_TEXPROV_SELFTEST (plus the
    real settings store for the set names; the store stays inactive - the
    test drives the policy through its own setting) and runs the fixture
    inside it: the SLTX header parser against every
    rejection (magic, version, id, N64 and replacement dimensions including
    an overflowing product, flags, payload), the loader against a truncated
    file, a missing file and a file carrying the wrong embedded id, the
    identity side table (registration, pool reset, re-registration), the
    provider policy (ORIGINAL never looks up; COMMUNITY and XBLA each fall
    back to ORIGINAL and never to each other; a switch bumps the generation),
    the negative cache, and the resident budget's LRU eviction and reload.

    Every fixture is SYNTHETIC - small asymmetric RGBA patterns the test
    writes itself under %TEMP%\sl_texprov_selftest and removes - and the
    root is pointed at them through SL_TEXPACK_ROOT. No pack, no ROM and no
    asset is read; the player's own pack folder is never opened.

    Exits 0 when every check passes, 1 otherwise. Same toolchain ladder as
    build.ps1 / settingstest.ps1.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing.
#>
[CmdletBinding()]
param([string]$Msys2Root)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$OUT  = Join-Path $repo 'build\win32'

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
    Write-Error "texprovtest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'texprovtest.exe'

$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra', '-DSL_TEXPROV_SELFTEST',
            '-o', $exe, 'src/gfx/sl_gfx_texprov.c', 'src/platform/sl_settings.c')
    if ($LASTEXITCODE -ne 0) { throw "texprovtest.ps1: compile failed (exit $LASTEXITCODE)" }
    # The provider's own diagnostics (INVALID lines, the summary) go to
    # stderr; cmd owns the redirect (PowerShell 5.1 would turn them into
    # error records). The verdict line is on stdout.
    & cmd /c "`"$exe`" 2>nul"
    $rc = $LASTEXITCODE
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}
exit $rc
