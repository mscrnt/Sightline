<#
.SYNOPSIS
    Build the background pass's GPU depth probe (v0.4). BUILD ONLY by default -
    no GL context, no driver, no game.

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\gpudepthprobe.ps1

    WHAT GAP THIS CLOSES. src/gfx/sl_gfx_bggpu.c computes the background pass's
    clip position AND its depth inside a GLSL vertex program (BGG_VS), with
    three depth cases - SL_BGGPU_DEPTH_SINGLE (the one-range frame's far
    clamp), _FAR0 (the first range, z = k1*w - k2, with the mesh's own
    opaque-or-blend coefficient pair) and _FAR1 (the game's range, unclamped).
    tools/profiling/native/test_sl_gfx_bggpu.c records only that the program
    TEXT reached glShaderSource, and the one arithmetic comparison it makes is
    between a HAND TRANSCRIPTION of those lines and the CPU reference. No
    driver has evaluated the expressions. This builds the setup that lets one
    do so.

    WHAT IT BUILDS, in order.

      1. tools/profiling/native/sl_bggpu_depth_probe_pin.py extracts BGG_VS and
         the module's uniform names out of src/gfx/sl_gfx_bggpu.c, checks their
         SHA-256 against tools/profiling/native/sl_bggpu_depth_probe.pin, and
         emits the text as a generated header under build\win32. A shader-text
         or uniform-list change REFUSES here, so a driver measurement cannot be
         taken under an earlier shader's name. src/ is not edited to expose the
         string, and no retyped or "equivalent" shader is used anywhere.

      2. bggpudepthprobe.exe - the probe with NO GL SIDE. The case table, the
         CPU reference (the same expressions sl_gfx_dl.c's bg_vertex_range and
         its two depth rewrites use, in the same order, exact comparison and no
         tolerance) and the probe's own unit checks. It is RUN: that part needs
         no driver. Its objdump import table is checked to prove opengl32 is
         absent, so this binary cannot reach a GPU even if asked.

      3. bggpudepthprobe-gl.exe - the same probe plus the driver side. Compiled
         and linked, and NOT RUN unless -RunGl is passed. It needs its own
         --gl argument on top of that, so neither the build nor an accidental
         launch touches a driver.

    SYNTHETIC INPUTS ONLY. No ROM is opened, no asset or background bundle is
    read, nothing is installed and nothing is staged. Synthetic probe data is
    not ROM data.

    Exits 0 when the build succeeded and every non-advisory check passed, 1
    otherwise. Same toolchain ladder as build.ps1 / bggputest.ps1.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing.

.PARAMETER PythonExe
    The interpreter for the pin extractor, if the project venv is not beside
    this checkout.

.PARAMETER WritePin
    Rewrite sl_bggpu_depth_probe.pin from what the source now holds. For an
    INTENTIONAL shader change only, and the diff belongs in the same review as
    the change: a measurement taken before it does not describe the shader that
    then ships.

.PARAMETER RunGl
    THE OPT-IN. Run the probe's -gl binary with --gl, which creates a GL context
    and evaluates the pinned shader text on the driver. Not the default.
    Everything else this script does is a compile.

.PARAMETER Outputs
    BUILD THE VERTEX-OUTPUT PROBE INSTEAD OF THE DEPTH PROBE. BGG_VS writes six
    outputs and the depth probe reads one float of one of them; this builds
    bggpuoutprobe.exe / bggpuoutprobe-gl.exe, which capture all six and compare
    every component. See tools/profiling/native/sl_bggpu_out_probe.h.

    SEPARATE BINARY NAMES ON PURPOSE. The depth binaries are not rebuilt and not
    overwritten by this mode, so an earlier measurement's binary stays exactly
    as it was saved. The shader-text pin is shared and still governs both.
#>
[CmdletBinding()]
param([string]$Msys2Root, [string]$PythonExe, [switch]$WritePin, [switch]$RunGl,
      [switch]$Outputs)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$OUT  = Join-Path $repo 'build\win32'

# ---- the toolchain ladder, build.ps1's ------------------------------------
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
    Write-Error "gpudepthprobe.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC      = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'
$OBJDUMP = Join-Path $mingwBin 'objdump.exe'

# ---- the interpreter for the pin extractor --------------------------------
if (-not $PythonExe) {
    foreach ($c in @((Join-Path $repo '.venv\Scripts\python.exe'),
                     (Join-Path (Split-Path -Parent $repo) 'sightline\.venv\Scripts\python.exe'))) {
        if (Test-Path $c) { $PythonExe = $c; break }
    }
}
if (-not $PythonExe) {
    $cmd = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($cmd) { $PythonExe = $cmd.Source }
}
if (-not $PythonExe -or -not (Test-Path $PythonExe)) {
    Write-Error "gpudepthprobe.ps1: no Python found for the pin extractor. Pass -PythonExe."
}

$objDir = Join-Path $OUT 'bggpudepthprobe-obj'
New-Item -ItemType Directory -Force -Path $OUT    | Out-Null
New-Item -ItemType Directory -Force -Path $objDir | Out-Null
$exe   = Join-Path $OUT 'bggpudepthprobe.exe'
$exeGl = Join-Path $OUT 'bggpudepthprobe-gl.exe'

# The mock set's own flags (tools/profiling/check.py, native_mocks), plus the
# two float flags src/gfx is BUILT with (build.ps1's $CFLAGS: -msse2
# -mfpmath=sse). Those two are load-bearing here and not a preference: mingw32
# defaults to x87, which evaluates float expressions in 80 bits, and the CPU
# reference would then differ from both the driver and the shipping build in
# the last bits - which is exactly what this probe compares. -ffp-contract=off
# states the no-fusion rule the GLSL's `precise` states on the other side; on
# i686 with SSE2 and no FMA there is nothing to contract, so it changes no
# instruction today and guards a future toolchain.
$flags = @('-m32', '-O2', '-Wall', '-Wextra', '-Werror=implicit-function-declaration',
           '-DSL_PERF_TEST', '-msse2', '-mfpmath=sse', '-ffp-contract=off')
$inc   = @('-Isrc/gfx', '-Isrc/platform', '-Itools/profiling/native', "-I$objDir")
# Injected ahead of the module under test only - sl_gfx_bggpu.c has no test
# hook in its include block and src/ is not edited to add one. See the long
# note at the top of tools/profiling/native/sl_gfx_bggpu_testgl.h.
$shim  = @('-include', 'tools/profiling/native/sl_gfx_bggpu_testgl.h')

# (source, extra flags). Shared by both binaries: the module under test against
# the synthetic GL, the probe itself, and the REAL policy, store and far-range
# arithmetic - sl_wa_far_plane and sl_wa_far_range compute every coefficient,
# so no number in the probe is hand-written.
$common = @(
    @{ src = 'src/gfx/sl_gfx_bggpu.c';                             extra = $shim },
    @{ src = 'tools/profiling/native/sl_gfx_bggpu_testgl.c';       extra = @() },
    @{ src = 'tools/profiling/native/sl_bggpu_depth_probe.c';      extra = $inc },
    @{ src = 'src/platform/sl_perf.c';                             extra = @() },
    @{ src = 'src/platform/sl_settings.c';                         extra = @() },
    @{ src = 'src/platform/sl_presentation.c';                     extra = @() },
    @{ src = 'src/platform/sl_worker_pool.c';                      extra = @() },
    @{ src = 'src/gfx/sl_gfx_worldasset.c';                        extra = @() }
)

function Invoke-Cc([string[]]$Argv, [string]$What) {
    & $CC @Argv
    if ($LASTEXITCODE -ne 0) { throw "gpudepthprobe.ps1: $What failed (exit $LASTEXITCODE)" }
}

function Compile-Unit($unit, [string]$suffix) {
    $obj = Join-Path $objDir (((Split-Path -Leaf $unit.src) -replace '\.c$', '') + $suffix + '.o')
    Invoke-Cc ($flags + $unit.extra + @('-c', $unit.src, '-o', $obj)) $unit.src
    return $obj
}

$rc = 0
$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo

    # ---- 1. the pin, and the generated header ----------------------------
    $pinScript = 'tools/profiling/native/sl_bggpu_depth_probe_pin.py'
    $pinFile   = 'tools/profiling/native/sl_bggpu_depth_probe.pin'
    $glslHdr   = Join-Path $objDir 'sl_bggpu_depth_probe_glsl.h'
    $pinArgs   = @('-I', '-B', $pinScript, '--source', 'src/gfx/sl_gfx_bggpu.c',
                   '--pin', $pinFile, '--out', $glslHdr)
    if ($WritePin) { $pinArgs += '--write-pin' }
    Write-Host '--- the production shader text, extracted and pinned ---'
    & $PythonExe @pinArgs
    if ($LASTEXITCODE -ne 0) {
        throw ("gpudepthprobe.ps1: the shader-text pin REFUSED (exit $LASTEXITCODE). " +
               "The production shader text or its uniform list changed. Review the " +
               "change, then re-run with -WritePin in the same review; a driver " +
               "measurement taken before the change does not describe the shader " +
               "that now ships.")
    }

    # ---- 2b. the VERTEX-OUTPUT probe, in its own binaries ----------------
    # Built from the same pinned text and the same case table, into NEW names.
    # The depth binaries are not touched in this mode: a measurement's binary
    # stays exactly as it was saved, which is why this is a separate output
    # mode rather than an extra flag on the same executable.
    if ($Outputs) {
        $objDirO = Join-Path $OUT 'bggpuoutprobe-obj'
        New-Item -ItemType Directory -Force -Path $objDirO | Out-Null
        $exeO   = Join-Path $OUT 'bggpuoutprobe.exe'
        $exeOGl = Join-Path $OUT 'bggpuoutprobe-gl.exe'
        $incO   = @('-Isrc/gfx', '-Isrc/platform', '-Itools/profiling/native', "-I$objDir")

        # The depth probe compiles as a LIBRARY here - its case table, CPU
        # reference and pinned-text accessors without its own main or its own
        # self-checks, which the output probe brings its own of. Two mains
        # cannot link; the macro is additive and undefined in the depth build.
        $commonO = @()
        foreach ($u in $common) {
            if ($u.src -eq 'tools/profiling/native/sl_bggpu_depth_probe.c') {
                $commonO += @{ src = $u.src; extra = ($u.extra + @('-DSL_BGDP_NO_MAIN')) }
            } else {
                $commonO += $u
            }
        }
        $commonO += @{ src = 'tools/profiling/native/sl_bggpu_out_probe.c'; extra = $incO }
        # The bounded snapshot reader. Shared by both links: the CPU binary
        # drives it from its own fixtures and the GL binary reads an actual
        # frame through the same function, so there is one parser and not two.
        $commonO += @{ src = 'tools/profiling/native/sl_bgsnap_read.c'; extra = $incO }
        # The record sink. Shared by both links: the CPU binary drives its
        # round-trip, partial, truncation and refusal controls from its own
        # artifacts, and the GL binary writes a real run's records through the
        # same writer, so there is one artifact format and not two.
        $commonO += @{ src = 'tools/profiling/native/sl_bgop_sink.c'; extra = $incO }

        function Compile-UnitO($unit) {
            $obj = Join-Path $objDirO (((Split-Path -Leaf $unit.src) -replace '\.c$', '') + '.o')
            Invoke-Cc ($flags + $unit.extra + @('-c', $unit.src, '-o', $obj)) $unit.src
            return $obj
        }

        Write-Host ''
        Write-Host '--- building bggpuoutprobe.exe (no GL side) ---'
        # Two lists, not one list with its tail sliced off. The shared objects
        # go in both links; the stub objects go in the no-GL link only and the
        # GL object in the GL link only. The earlier form linked everything but
        # the LAST object into the GL binary, which made the stub set a set of
        # exactly one and silently coupled "which objects are stubs" to "which
        # object was compiled last".
        $objsO = @()
        foreach ($u in $commonO) { $objsO += Compile-UnitO $u }
        $stubsO = @()
        foreach ($srcStub in @('tools/profiling/native/sl_bggpu_out_probe_nogl.c',
                               'tools/profiling/native/sl_bgop_snap_nogl.c')) {
            $stubsO += Compile-UnitO @{ src = $srcStub; extra = $incO }
        }
        Invoke-Cc ($flags + @('-o', $exeO) + $objsO + $stubsO) 'the no-GL link'

        $impO = & $OBJDUMP -p $exeO
        if ($LASTEXITCODE -ne 0) { throw "gpudepthprobe.ps1: objdump failed (exit $LASTEXITCODE)" }
        $dllsO = @($impO | Select-String -Pattern 'DLL Name:\s*(\S+)' -AllMatches |
                   ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value })
        Write-Host ("bggpuoutprobe.exe imports: " + ($dllsO -join ' '))
        if ($dllsO.Count -eq 0) { throw "gpudepthprobe.ps1: objdump reported no imports for $exeO" }
        if ($dllsO | Where-Object { $_ -match '(?i)^opengl32' }) {
            throw "gpudepthprobe.ps1: the no-GL output build imports opengl32; it must not be able to reach a driver"
        }

        Write-Host ''
        Write-Host '--- building bggpuoutprobe-gl.exe (driver side; NOT run here) ---'
        # SL_BGGPU_PROBE_OUTPUTS, and ONLY here. The shared GL source's
        # output-only section calls sl_bgop_fields(), which lives in the output
        # probe's object - an object the ordinary DEPTH GL link below does not
        # include. Compiled unconditionally it put an unresolved external into
        # that link and broke the default GL rebuild, which the depth binary
        # being preserved rather than rebuilt had hidden.
        $glObjO = Join-Path $objDirO 'sl_bggpu_depth_probe_gl.o'
        Invoke-Cc ($flags + $incO + @('-DSL_BGGPU_PROBE_GL', '-DSL_BGGPU_PROBE_OUTPUTS',
                   '-c', 'tools/profiling/native/sl_bggpu_depth_probe_gl.c',
                   '-o', $glObjO)) `
                  'tools/profiling/native/sl_bggpu_depth_probe_gl.c'
        Invoke-Cc ($flags + @('-o', $exeOGl) + $objsO + @($glObjO) +
                   @('-lopengl32', '-lgdi32', '-luser32')) 'the GL link'
        $glImpO = & $OBJDUMP -p $exeOGl
        if ($LASTEXITCODE -ne 0) { throw "gpudepthprobe.ps1: objdump failed on the GL output build (exit $LASTEXITCODE)" }
        $glDllsO = @($glImpO | Select-String -Pattern 'DLL Name:\s*(\S+)' -AllMatches |
                     ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value })
        Write-Host ("bggpuoutprobe-gl.exe imports: " + ($glDllsO -join ' '))
        if (-not ($glDllsO | Where-Object { $_ -match '(?i)^opengl32' })) {
            throw "gpudepthprobe.ps1: the GL output build does not import opengl32; its driver side did not link in"
        }

        Write-Host ''
        Write-Host '--- the CPU side: the record layout, the inputs, the associations, the controls ---'
        & cmd /c "`"$exeO`" 2>nul"
        if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }

        if ($RunGl) {
            Write-Host ''
            Write-Host '--- THE DRIVER RUN (opt-in): a GL context is created ---'
            & cmd /c "`"$exeOGl`" --gl 2>nul"
            if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
        } else {
            Write-Host ''
            Write-Host 'BUILD ONLY. No GL context was created, no driver was asked anything.'
            Write-Host 'The driver run is:'
            Write-Host '    .\tools\windows\gpudepthprobe.ps1 -Outputs -RunGl'
            Write-Host ("or the binary directly: `"$exeOGl`" --gl")
        }
    }

    # ---- 2. the probe with NO GL SIDE ------------------------------------
    if (-not $Outputs) {
    Write-Host ''
    Write-Host '--- building bggpudepthprobe.exe (no GL side) ---'
    $objs = @()
    foreach ($u in $common) { $objs += Compile-Unit $u '' }
    $objs += Compile-Unit @{ src = 'tools/profiling/native/sl_bggpu_depth_probe_nogl.c'; extra = $inc } ''
    Invoke-Cc ($flags + @('-o', $exe) + $objs) 'the no-GL link'

    # The safety property, from the binary rather than from a claim: a build
    # that does not import opengl32 cannot create a context. check.py inspects
    # its mock binaries' imports the same way before running them.
    $imports = & $OBJDUMP -p $exe
    if ($LASTEXITCODE -ne 0) { throw "gpudepthprobe.ps1: objdump failed (exit $LASTEXITCODE)" }
    $dlls = @($imports | Select-String -Pattern 'DLL Name:\s*(\S+)' -AllMatches |
              ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value })
    Write-Host ("bggpudepthprobe.exe imports: " + ($dlls -join ' '))
    if ($dlls.Count -eq 0) { throw "gpudepthprobe.ps1: objdump reported no imports for $exe" }
    if ($dlls | Where-Object { $_ -match '(?i)^opengl32' }) {
        throw "gpudepthprobe.ps1: the no-GL build imports opengl32; it must not be able to reach a driver"
    }

    # ---- 3. the probe WITH the driver side, compiled and NOT RUN --------
    Write-Host ''
    Write-Host '--- building bggpudepthprobe-gl.exe (driver side; NOT run here) ---'
    $glObj = Join-Path $objDir 'sl_bggpu_depth_probe_gl.o'
    Invoke-Cc ($flags + $inc + @('-DSL_BGGPU_PROBE_GL', '-c',
               'tools/profiling/native/sl_bggpu_depth_probe_gl.c', '-o', $glObj)) `
              'tools/profiling/native/sl_bggpu_depth_probe_gl.c'
    Invoke-Cc ($flags + @('-o', $exeGl) + $objs[0..($objs.Count - 2)] + @($glObj) +
               @('-lopengl32', '-lgdi32', '-luser32')) 'the GL link'
    $glImports = & $OBJDUMP -p $exeGl
    if ($LASTEXITCODE -ne 0) { throw "gpudepthprobe.ps1: objdump failed on the GL build (exit $LASTEXITCODE)" }
    $glDlls = @($glImports | Select-String -Pattern 'DLL Name:\s*(\S+)' -AllMatches |
                ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value })
    Write-Host ("bggpudepthprobe-gl.exe imports: " + ($glDlls -join ' '))
    if (-not ($glDlls | Where-Object { $_ -match '(?i)^opengl32' })) {
        throw "gpudepthprobe.ps1: the GL build does not import opengl32; its driver side did not link in"
    }

    # ---- 4. the part that needs no driver, run --------------------------
    Write-Host ''
    Write-Host '--- the CPU side: the pinned text, the coefficients, the reference, the controls ---'
    # The module's own refusal lines and the store's diagnostics go to stderr;
    # cmd owns the redirect (PowerShell 5.1 would turn them into error
    # records). The verdict is on stdout.
    & cmd /c "`"$exe`" 2>nul"
    if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }

    # ---- 5. the driver run, only when explicitly asked for --------------
    if ($RunGl) {
        Write-Host ''
        Write-Host '--- THE DRIVER RUN (opt-in): a GL context is created ---'
        & cmd /c "`"$exeGl`" --gl 2>nul"
        if ($LASTEXITCODE -ne 0) { $rc = $LASTEXITCODE }
    } else {
        Write-Host ''
        Write-Host 'BUILD ONLY. No GL context was created, no driver was asked anything.'
        Write-Host 'The driver run is:'
        Write-Host '    .\tools\windows\gpudepthprobe.ps1 -RunGl'
        Write-Host ("or the binary directly: `"$exeGl`" --gl")
        Write-Host 'It creates a hidden-window GL 4.0 compatibility context, compiles the'
        Write-Host 'pinned production text, links it with gl_Position captured through'
        Write-Host 'transform feedback, draws one point per synthetic case with the'
        Write-Host 'rasterizer discarded, and compares each captured depth with the CPU'
        Write-Host 'reference bit for bit. Tens of draws: the context setup dominates, and'
        Write-Host 'the whole run is seconds, not minutes.'
    }
    }   # end: if (-not $Outputs) - the depth probe's own build and run
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
}

exit $rc
