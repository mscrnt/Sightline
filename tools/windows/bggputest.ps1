<#
.SYNOPSIS
    Build and run the background GPU vertex transform's offline fixture (v0.4).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\bggputest.ps1

    src/gfx/sl_gfx_bggpu.c compiled against the SYNTHETIC GL in
    tools/profiling/native/sl_gfx_bggpu_testgl.c - no window, no native
    graphics API, no renderer, no game - and linked against the REAL policy,
    the REAL settings store and the REAL far-range arithmetic:

      src/platform/sl_perf.c       the OPTIMIZED RENDERING row and the policy
                                   that resolves it (-DSL_PERF_TEST, so the
                                   fixture can drop the cached environment
                                   answers; no SL_*_SELFTEST is defined
                                   anywhere here, which would collide mains)
      src/platform/sl_settings.c   the store the row lives in, pointed at a
                                   scratch SL_CONFIG under %TEMP% by the
                                   fixture, never the player's own config
      src/gfx/sl_gfx_worldasset.c  sl_wa_far_plane and sl_wa_far_range, so the
                                   depth coefficients are the ones
                                   src/gfx/sl_gfx_dl.c computes rather than
                                   numbers written into a test

    WHAT IS CHECKED. The row driving the transform and an off row leaving the
    capability UNTRIED rather than latching it off for the launch; a real
    failure (a withheld entry point, a refused compile, a refused link, each of
    the eight uniforms withheld in turn) latching UNAVAILABLE and never being
    retried, with only sl_bggpu_invalidate clearing it; context recreation -
    invalidate makes no GL call, drops the program, and the next walk
    re-resolves against the new context, with a positive control proving the
    stale-name watchdog can fire; the walk's eight uniform uploads and the
    depth state it leaves behind; the two-range far frame's depth sequence,
    where the uniform follows the MESH and the RANGE inside one binding; and
    the policy's PRECEDENCE rule driven through the module - a menu press ends
    an inherited SL_BG_GPU_XFORM / SL_BG_GPU_BYPASS in both directions and
    PARKS the selection rather than rebuilding the program, while the two
    switches that reach the far admission are kept apart: the EXPLICIT arm
    SL_PERF_BG_FAR survives a press in both directions, because a qualification
    run binds it on purpose, and an inherited SL_BG_GPU_XFORM=1 admits the far
    walk only BEFORE a press - a press ends it, so a variable the session
    happened to inherit cannot put the unqualified two-range walk back on the
    GPU behind a menu that was just set.

    WHAT IS NOT. The vertex program's own depth arithmetic is never executed:
    there is no GLSL compiler and no GPU in this process, and the stub only
    records that the program text was handed to glShaderSource. The far-depth
    expressions' clamp and ranges as a driver evaluates them, and the
    depth-buffer ordering that results, remain unqualified. One case compares a
    hand transcription of that GLSL with the CPU reference and says so in its
    own name.

    HOW THE MODULE'S GL INCLUDES ARE REPLACED. sl_gfx_bggpu.c has no test hook
    in its include block, and src/ is not edited to add one: the stub header is
    injected ahead of that one translation unit with -include, and it defines
    the host headers' own include guards so <windows.h> and <GL/gl.h> expand to
    nothing. Only that translation unit is compiled that way - the store and
    the worker sources need the real windows.h - which is why each is compiled
    to its own object here instead of in one command.

    Exits 0 when every check passes, 1 otherwise. Same toolchain ladder as
    build.ps1 / worldassettest.ps1.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing.
#>
[CmdletBinding()]
param([string]$Msys2Root, [string]$OutDir)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# -OutDir sends this fixture's executable and objects somewhere other than
# build\win32. The default is build\win32 exactly as before, so a run that does
# not pass it writes what it has always written. It exists because re-running
# this fixture to check a regression otherwise writes into the directory the
# project's pinned binaries live in, and a check that disturbs the thing it is
# checking is not a check. Nothing here deletes or restores anything: the
# directory is created if absent and written into, which is why an isolated
# -OutDir is enough to make the run non-destructive.
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
    Write-Error "bggputest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

$objDir = Join-Path $OUT 'bggputest-obj'
New-Item -ItemType Directory -Force -Path $OUT | Out-Null
New-Item -ItemType Directory -Force -Path $objDir | Out-Null
$exe = Join-Path $OUT 'bggputest.exe'

# The mock set's own flags (tools/profiling/check.py, native_mocks).
$flags = @('-m32', '-O2', '-Wall', '-Wextra', '-Werror=implicit-function-declaration',
           '-DSL_PERF_TEST')
$inc   = @('-Isrc/gfx', '-Isrc/platform', '-Itools/profiling/native')
# Injected ahead of the module under test only; see the note above.
$shim  = @('-include', 'tools/profiling/native/sl_gfx_bggpu_testgl.h')

# (source, extra flags). The module under test is the only one that gets the
# shim: the store and the worker pool include the real windows.h.
$units = @(
    @{ src = 'src/gfx/sl_gfx_bggpu.c';                        extra = $shim },
    @{ src = 'tools/profiling/native/sl_gfx_bggpu_testgl.c';  extra = @() },
    @{ src = 'tools/profiling/native/test_sl_gfx_bggpu.c';    extra = $inc },
    @{ src = 'src/platform/sl_perf.c';                        extra = @() },
    @{ src = 'src/platform/sl_settings.c';                    extra = @() },
    @{ src = 'src/platform/sl_presentation.c';                extra = @() },
    @{ src = 'src/platform/sl_worker_pool.c';                 extra = @() },
    @{ src = 'src/gfx/sl_gfx_worldasset.c';                   extra = @() }
)

$rc = 0
$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    $objs = @()
    foreach ($u in $units) {
        $obj = Join-Path $objDir ((Split-Path -Leaf $u.src) -replace '\.c$', '.o')
        & $CC @($flags + $u.extra + @('-c', $u.src, '-o', $obj))
        if ($LASTEXITCODE -ne 0) { throw "bggputest.ps1: $($u.src) failed to compile (exit $LASTEXITCODE)" }
        $objs += $obj
    }
    & $CC @($flags + @('-o', $exe) + $objs)
    if ($LASTEXITCODE -ne 0) { throw "bggputest.ps1: link failed (exit $LASTEXITCODE)" }
    # The module's own refusal lines go to stderr; cmd owns the redirect
    # (PowerShell 5.1 would turn them into error records). The verdict is on
    # stdout.
    & cmd /c "`"$exe`" 2>nul"
    if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}

exit $rc
