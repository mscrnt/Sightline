<#
.SYNOPSIS
    Build and run the presentation-policy self-test (#68).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\presentationtest.ps1

    Compiles src/platform/sl_presentation.c with -DSL_PRESENTATION_SELFTEST
    beside the settings store it reads, and runs it. The policy is the layer
    the coherence design decision lives in, so what is pinned here is the
    decision itself:

      * the three names, and no name outside the range;
      * THE THREE MAPPINGS - ORIGINAL -> textures ORIGINAL + no world assets,
        COMMUNITY HD -> textures COMMUNITY + no world assets, XBLA ->
        textures XBLA + the xbla world-asset set;
      * that no presentation resolves to another presentation's set, which is
        the per-provider fallback contract as a property of the table;
      * an invalid value resolving exactly as ORIGINAL;
      * the canonical value: one store row, out-of-range sets refused, and no
        copy held anywhere - a write straight to the row is seen by the policy;
      * F5 cycling exactly 0 -> 1 -> 2 -> 0, and a full cycle being a no-op;
      * the backward step both editors' LEFT takes;
      * and the coordination itself: one step moves the texture provider only,
        the next moves BOTH.

    NO CONFIG FILE IS READ OR WRITTEN: the store is left INACTIVE, so sets
    change memory only. Nothing is installed, nothing is prepared, no ROM is
    opened - this runs on CI as it stands.

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
    Write-Error "presentationtest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'presentationtest.exe'

$rc = 0
$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra', '-DSL_PRESENTATION_SELFTEST',
            '-o', $exe, 'src/platform/sl_presentation.c', 'src/platform/sl_settings.c')
    if ($LASTEXITCODE -ne 0) { throw "presentationtest.ps1: compile failed (exit $LASTEXITCODE)" }
    # The store's own diagnostics go to stderr; cmd owns the redirect
    # (PowerShell 5.1 would turn them into error records).
    & cmd /c "`"$exe`" 2>nul"
    if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}

exit $rc
