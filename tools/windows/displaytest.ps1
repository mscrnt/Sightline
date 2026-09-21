<#
.SYNOPSIS
    Build and run the native display-shape self-test (#45).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\displaytest.ps1

    Compiles src/platform/sl_display.c together with the real settings store
    (src/platform/sl_settings.c) with -DSL_DISPLAY_SELFTEST and runs the
    fixture inside it: the aspect enum's names and numeric ratios, the
    persisted selection through the store (missing / malformed / out-of-range
    read 4:3), the content-viewport fit for matching, wider and narrower
    framebuffers (pillarbox / letterbox, never a stretch), the 4:3 baseline
    identity at 960x720, the expected 16:9 / 32:9 horizontal expansion, the
    logical <-> physical pointer transforms and the split-screen gate - and,
    since #52, src/platform/sl_window.c's PC display-mode checks over the
    same store (the mode names, the fullscreen / windowed lists deduped and
    bounded from a measured mode table, the step, the unsupported-size
    fallbacks, the request / take / commit seam, the editors' steps). The
    scratch config lands under %TEMP% through SL_CONFIG; the player's own
    config is never opened.

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
    Write-Error "displaytest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'displaytest.exe'

$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    & $CC @('-m32', '-O0', '-g', '-Wall', '-Wextra', '-DSL_DISPLAY_SELFTEST',
            '-o', $exe, 'src/platform/sl_display.c', 'src/platform/sl_window.c', 'src/platform/sl_settings.c')
    if ($LASTEXITCODE -ne 0) { throw "displaytest.ps1: compile failed (exit $LASTEXITCODE)" }
    # The store's own diagnostics go to stderr, and PowerShell 5.1 turns a
    # native process's stderr into error records under -ErrorAction Stop.
    # cmd owns the redirect instead; the verdict line is on stdout.
    & cmd /c "`"$exe`" 2>nul"
    $rc = $LASTEXITCODE
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}
exit $rc
