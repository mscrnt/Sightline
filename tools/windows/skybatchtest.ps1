<#
.SYNOPSIS
    Build and run the skydome batch module's offline mock tests (v0.4, #316/#474).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\skybatchtest.ps1

    src/gfx/sl_gfx_skybatch.c compiled with -DSL_SKYBATCH_TEST against the
    SYNTHETIC GL in tools/profiling/native/sl_gfx_skybatch_testgl.c - no window,
    no native graphics API, no game and no GPU, and nothing linked against
    opengl32 or SDL. The OPTIMIZED RENDERING row, its policy
    (src/platform/sl_perf.c) and the settings store (src/platform/sl_settings.c)
    are LINKED IN RATHER THAN MOCKED, so the row the tests drive is the row the
    game reads, with its real default and its real precedence against the
    developer switches; the store is pointed at a scratch file under %TEMP%
    through SL_CONFIG, so no player configuration is opened.
    src/gfx/sl_gfx_worldasset.c and src/platform/sl_presentation.c are linked in
    too: the shade, the screen fraction and the cloud cast a prepared record
    carries are the shipped ones.

    One mode per process, because three pieces of module state have PROCESS
    lifetime by construction and no reset exists for them - the atexit
    registration, the terminal snapshot it prints and the once-per-process float
    carrier marker:

      row        the row drives the path: OFF answers 0, opens no pass and makes
                 ZERO GL calls of any kind; ON serves a cloud mesh; a plain mesh
                 is declined with no GL call; an empty mesh answers served and
                 draws nothing; the ten counters
      flip       OFF -> ON -> OFF -> ON over the SAME asset pointer: each ON pass
                 opens and serves with a nonzero index count, each OFF pass
                 submits nothing, and no refusal is counted
      carrier    SL_SKY_BATCH=1 with no carrier switch uploads 36-byte records;
                 the row then uploads 48-byte records with a GL_FLOAT colour on
                 the SAME asset, acquiring staging for a topology built without
                 it; the byte and div spellings; and the row never resolving
                 anything but prod, in either position and with any unrecognised
                 spelling
      latch      one coherent carrier per pass: the row moving between two mesh
                 draws of an open pass changes neither the record size nor the
                 stride, and the NEXT pass is the one that sees it
      allocfail  an unallocatable float staging refuses the pass before any
                 submission and leaves the whole saved client-array state, the
                 bindings and the current values exactly as found
      context    a context change re-resolves, re-creates and succeeds, with no
                 call using a name from before it (the stub refuses and records
                 one); and a shutdown with a pass open puts the state back and
                 deletes only the two names the module owns
      bracket    nonzero foreign bindings in both user slots and three arrays, a
                 client unit other than 0, arbitrary sizes, types, strides and
                 pointers, and every array the module does not set left enabled,
                 driven through a pass: the whole recorded state is identical
                 before and after, and no draw ran with a foreign array enabled
      nosync     no synchronisation point and no shader: the stub declares none,
                 so a call would not link, and the loader was never asked for one
      attr       once per UNIQUE REFERENCED VERTEX, counted from the inside, and
                 the writing pass inside the recorded capacities
      precedence an explicit menu choice governs an inherited subsystem switch,
                 driven through the real module rather than through the policy's
                 own answer: with SL_SKY_BATCH=1 in the environment and the row
                 written straight to the store, the pass serves 36-byte records;
                 a press ON moves the SAME asset to 48-byte records with a
                 GL_FLOAT colour, because the row selects prod only; a press OFF
                 makes ZERO GL calls and zero loader calls, which is the defect
                 that left the indexed path running behind a menu reading OFF;
                 and an inherited div carrier is served while the row is
                 untouched but moves to the prod table on a press
      marker     the float carrier marker once per process across three passes
                 and a context change; this run's stderr is also required to
                 carry exactly one marker line and exactly one terminal `final`
                 line, which is written by an atexit handler and so cannot be
                 observed from inside main

    Exits 0 when every check passes, 1 otherwise. Same toolchain ladder as
    build.ps1 / worldassettest.ps1.

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
    Write-Error "skybatchtest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'skybatchtest.exe'

# The flags tools/profiling/check.py uses for the profiling mock pair, mirrored:
# -DSL_SKYBATCH_TEST selects the module's stub-GL branch and its test hooks, and
# -DSL_PERF_TEST is what makes sl_perf_test_reset() exist - the module calls it
# from its own test hook, so the declaration has to be visible in every unit.
# None of the SL_*_SELFTEST macros is defined: each of those files carries its
# own main, and two mains do not link.
$flags = @('-m32', '-O2', '-Wall', '-Wextra', '-Werror=implicit-function-declaration',
           '-DSL_SKYBATCH_TEST', '-DSL_PERF_TEST',
           '-I' + (Join-Path $repo 'src\gfx'),
           '-I' + (Join-Path $repo 'tools\profiling\native'))
$sources = @('tools/profiling/native/test_sl_gfx_skybatch.c',
             'tools/profiling/native/sl_gfx_skybatch_testgl.c',
             'src/gfx/sl_gfx_skybatch.c',
             'src/gfx/sl_gfx_worldasset.c',
             'src/platform/sl_presentation.c',
             'src/platform/sl_perf.c',
             'src/platform/sl_settings.c')

$modes = @('row', 'flip', 'carrier', 'latch', 'allocfail', 'context',
           'bracket', 'nosync', 'attr', 'precedence', 'marker')

$rc = 0
$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    & $CC @($flags + @('-o', $exe) + $sources)
    if ($LASTEXITCODE -ne 0) { throw "skybatchtest.ps1: compile failed (exit $LASTEXITCODE)" }

    # The module's own diagnostics (the topology line, the armed line, the
    # census, the float carrier marker, the terminal snapshot) go to stderr;
    # cmd owns the redirect, because PowerShell 5.1 would turn them into error
    # records. The verdict lines are on stdout.
    foreach ($mode in $modes) {
        if ($mode -eq 'marker') { continue }
        & cmd /c "`"$exe`" $mode 2>nul"
        if ($LASTEXITCODE -ne 0) { $rc = 1 }
    }

    # The marker mode's stderr is the measurement: the float carrier marker and
    # the terminal snapshot are both once-per-process, and the snapshot is
    # written by an atexit handler, so only the captured stream can count them.
    $err = Join-Path $OUT 'skybatchtest-marker-stderr.txt'
    if (Test-Path $err) { Remove-Item $err -Force }
    & cmd /c "`"$exe`" marker 2>`"$err`""
    if ($LASTEXITCODE -ne 0) { $rc = 1 }
    $lines = if (Test-Path $err) { @(Get-Content $err) } else { @() }
    $markerLines = @($lines | Where-Object { $_ -like '*skybatch: float colour carrier*' })
    $finalLines  = @($lines | Where-Object { $_ -like '*skybatch: final items*' })
    if ($markerLines.Count -ne 1) {
        Write-Host "FAIL: the float carrier marker appeared $($markerLines.Count) time(s) in one process, expected 1"
        $rc = 1
    }
    if ($finalLines.Count -ne 1) {
        Write-Host "FAIL: the terminal snapshot line appeared $($finalLines.Count) time(s) in one process, expected 1"
        $rc = 1
    }
    if ($markerLines.Count -eq 1 -and $markerLines[0] -notlike '*carrier prod*') {
        Write-Host "FAIL: the row's marker names a carrier other than prod: $($markerLines[0])"
        $rc = 1
    }
    if ($rc -eq 0) { Write-Host '  marker: one float carrier line and one terminal snapshot line per process' }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}
exit $rc
