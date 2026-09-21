<#
.SYNOPSIS
    Build and run the headless keyboard / mouse / pointer translation test.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\inputtest.ps1

    The Windows counterpart of tools/native/inputtest.sh, and the same idea:
    it links the REAL src/platform/sl_input.c against the stub SDL and stub
    front-end queries in tools/native/inputtest.c, so what is asserted is the
    shipping translation and not a second copy of it. No window, no ROM, no
    pointer, no compositor - which is the point, because every attempt to drive
    this path with a real pointer has produced contradictory readings.

    IT EXITS 1 ON A CLEAN TREE TODAY, and that is a finding rather than a
    breakage in this script. tools/native/inputtest.c was last updated at
    fbcc3a48; src/platform/sl_input.c has three commits after it, two of which
    changed behaviour the harness pins - the per-platform mouse sign default
    (7936cbd0, B-071) and the removal of the mouse-becomes-the-stick aim
    routing (911603cc). 14 of 80 checks fail for those two reasons, all of them
    in the gameplay mouse-look sections. Making them match the binary's current
    output would be a re-baseline of an owner-validated hand measurement, which
    project rule 1 says is its own commit touching nothing else.
    Recorded as B-096 in docs/backlog.md. 2026-09-17 (#38): the harness links
    again (two link stubs it had rotted past) and gained the native action
    section, 12 checks, all passing; the pre-existing failures are unchanged
    in kind (22 of 117 now: the 14 above plus the 8 watch-stick checks that
    predate SL_WATCH_STICK = 70, 76e9404e). 2026-09-18 (#42): the SPRINT
    section, 8 checks, all passing; 22 of 125 pre-existing. 2026-09-18 (#46
    owner replay): the INVERT MOUSE Y precedence cases - SL_MOUSE_INVERT
    against an ACTIVE store (the config governs, the env is ignored, the
    setter writes, the consumer's sign follows the state) and an INACTIVE
    one (the env seeds) - run as two extra PROCESSES before the main run,
    9 + 5 checks, all passing; the main run stays 225 with the 22 above.
    2026-09-19 (#50): the MOUSE SENSITIVITY / SCOPED SENSITIVITY case
    (mouse-sens) runs as a third process - the default identity against the
    cc3a418a measurement, the step grid and clamps, lower / higher, the
    scoped percent under the game's scope predicate only, Invert Y composing,
    the watch pointer and the pad sticks unaffected. 2026-09-20 (#63 round
    6): the modern-pad case grew from 96 to 124 checks (the d-pad's four
    registry sources, the bumpers' scope-aware cycle and its stale rule,
    the held zoom level, the re-seeded presets); the main run is 219 with
    the same 22 B-096 failures (the wheel-onto-INTERACT case became two).
    2026-09-20 (#51): the CONTROLLER TUNING cases (pad-tune, and pad-tune-
    invert with SL_LOOK_INVERT=1) run as two more processes - the default
    identity against the 04b92554 transfer table, the look sensitivity and
    the two deadzones, the four stick layouts, the negative controls.
    2026-09-20 (#56): the HOLD / TOGGLE case (hold-toggle) runs as one more
    process - the HOLD identity against the 68fd4d1e table, the CROUCH and
    SPRINT toggle latches, the menu / watch isolation, the mode change, the
    resets, the remap and the negatives.

    Having no Windows runner is exactly why that rot went unnoticed, which is
    the case for this script existing rather than against it.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing. Same resolution
    order as build.ps1.
#>
[CmdletBinding()]
param([string]$Msys2Root)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$OUT  = Join-Path $repo 'build\win32'

# Same toolchain ladder as build.ps1: MSYS2 is a TOOLCHAIN PROVIDER only, and
# PATH is set for this script's children and restored on the way out.
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
    Write-Error "inputtest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

# SDL2 headers only - the harness DEFINES the SDL entry points it needs, so
# nothing links against the real library. -Dmain=SDL_main is stripped for the
# same reason build.ps1 strips it: it would rewrite this file's own main.
if ($env:SL_SDL_CFLAGS) {
    $sdlRaw = $env:SL_SDL_CFLAGS
} else {
    $pkgconf = $null
    foreach ($nm in @('pkgconf.exe', 'pkg-config.exe')) {
        $cand = Join-Path $mingwBin $nm
        if (Test-Path $cand) { $pkgconf = $cand; break }
    }
    if ($null -eq $pkgconf) {
        Write-Error "inputtest.ps1: no pkgconf/pkg-config in $mingwBin. Run setup.ps1."
    }
    $sdlRaw = (& $pkgconf --cflags sdl2) -join ' '
    if ($LASTEXITCODE -ne 0 -or -not $sdlRaw) {
        Write-Error "inputtest.ps1: pkgconf could not describe sdl2. Run setup.ps1."
    }
}
$SDL_CFLAGS = @($sdlRaw -split '\s+' |
                Where-Object { $_ -ne '' -and $_ -ne '-Dmain=SDL_main' })
$SDL_CFLAGS += '-DSDL_MAIN_HANDLED'

New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$exe = Join-Path $OUT 'inputtest.exe'

$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    # The decomp's own string routines (src/str.c) OVERRIDE the C library's in
    # the native link, and its strncpy writes n + 1 bytes for a short source
    # (Rare's, kept). Linked here too, so the harness runs the same strcpy /
    # strncpy / strcmp / strncmp the game runs - measured 2026-09-18 (#46): a
    # one-byte overflow that only the game showed. Compiled on its own against
    # the N64 include tree (str.h wants ultra64.h), which must not reach the
    # host-header files below.
    $strObj = Join-Path $OUT 'inputtest_str.o'
    & $CC @('-m32', '-O0', '-w', '-mno-ms-bitfields', '-Isrc', '-Iinclude',
            '-c', 'src/str.c', '-o', $strObj)
    if ($LASTEXITCODE -ne 0) { throw "inputtest.ps1: compile of src/str.c failed (exit $LASTEXITCODE)" }
    & $CC (@('-m32', '-O0', '-g', '-Wall', '-Wno-unused-parameter',
             '-mno-ms-bitfields', '-o', $exe,
             'tools/native/inputtest.c', 'src/platform/sl_input.c',
             'src/platform/sl_action.c', 'src/platform/sl_bindings.c',
             'src/platform/sl_bindings_editor.c', 'src/platform/sl_settings.c',
             $strObj) + $SDL_CFLAGS)
    if ($LASTEXITCODE -ne 0) { throw "inputtest.ps1: compile failed (exit $LASTEXITCODE)" }
    # The INVERT MOUSE Y precedence cases run in their OWN processes first
    # (the settings store is a once-only singleton and read_env runs once per
    # process): a failure there is the exit code; the main run follows either
    # way so its 22 pre-existing B-096 failures stay visible and unchanged.
    $rcCases = 0
    foreach ($case in @('mouse-invert-store', 'mouse-invert-nostore', 'mouse-sens', 'modern-pad', 'pad-tune', 'pad-tune-invert', 'hold-toggle')) {
        $env:INPUTTEST_CASE = $case
        & $exe
        if ($LASTEXITCODE -ne 0) { $rcCases = $LASTEXITCODE }
    }
    Remove-Item Env:INPUTTEST_CASE -ErrorAction SilentlyContinue
    & $exe
    $rc = $LASTEXITCODE
    if ($rcCases -ne 0) {
        Write-Host "inputtest.ps1: an INVERT MOUSE Y / MOUSE SENSITIVITY case FAILED"
        $rc = $rcCases
    }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}
exit $rc
