<#
.SYNOPSIS
    Build and run the background draw SEAM fixture - the REAL sl_gfx_dl.c
    driving the REAL sl_gfx_bgtf.c, offline (v0.4).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\bgtfseamtest.ps1

    WHY THIS EXISTS BESIDE bgtftest.ps1. That fixture links the candidate module
    alone, so it proves what the module does when it is called and nothing about
    whether it is called correctly. Three of the seam's properties live entirely
    in src/gfx/sl_gfx_dl.c and are invisible to a module-only fixture:

      - WHICH VERTEX SOURCE each mesh draws from. The candidate switches only
        the position array, and the meshes it does not admit - four-image,
        pulled, coplanar, far shell - draw through their own paths. Leaving a
        previous mesh's prepass pool named is what makes one mesh's indices
        rasterise against another mesh's positions at another depth.
      - WHETHER COVERAGE IS GATED ON THE ACTUAL SOURCE. The admission flag
        stays true across the whole walk, including the excluded meshes and the
        draws whose prepass refused.
      - WHAT A REFUSAL LEAVES BEHIND. A prepass that refuses mid-walk must put
        the original per-mesh source back, not merely return -1.

    THE PRODUCT TRANSLATION UNIT IS COMPILED IN, NOT REIMPLEMENTED.
    tools/profiling/native/test_sl_gfx_bgtf_seam.c #includes
    src/gfx/sl_gfx_dl.c, which is what reaches bg_draw_pass, bggpu_select,
    bgtf_cpu_source, bg_draw_mesh_run and bgv_vertex_pointer - all file-static -
    without editing src/. Nothing in the fixture restates their logic: a copied
    selection algorithm would agree with itself and say nothing about the code
    that ships.

    HOW THE REAL HEADERS ARE KEPT AND THE DRIVER STILL REPLACED. -D_GDI32_ makes
    mingw's WINGDIAPI empty (wingdi.h:13-16), so GL/gl.h's entry points are
    ordinary symbols rather than imports from opengl32 and
    tools/profiling/native/sl_gfx_seamgl.c is what the link resolves them to.
    The real headers supply every type and token, so nothing is transcribed and
    nothing can drift; and APIENTRY is windows.h's own __stdcall here, so the
    calling convention the module's header records a measurement about is
    exercised rather than flattened.

    LINKED FOR REAL, not mocked: src/gfx/sl_gfx_dl.c, src/gfx/sl_gfx_bgtf.c,
    sl_gfx_worldasset.c (so the far-range coefficients, the near-cut
    classification, the coplanar rules and the far plane are the product's),
    sl_gfx_bggpu.c, sl_gfx_tex.c, sl_gfx_texprov.c, sl_gfx_charskin.c,
    sl_gfx_skybatch.c, sl_bgsnap_write.c, and src/platform's bg_worker,
    worker_pool, perf, display, presentation and settings.

    SYNTHETIC: the GL, and the frame inputs (the view matrix, the render origin,
    the level and visibility scales, the fog environment, the frame clock) and
    the asset, which is a hand-built struct because nothing ROM-derived may
    enter this tree. Those are inputs to the pass, not part of it.

    THE FIVE SCENARIOS. A served mesh, then a candidate-INELIGIBLE one, then a
    served mesh again, with each draw attributed by the buffer its POSITION
    pointer named WHEN IT RAN; a fully filtered mesh, identified by its index
    span rather than by a draw count, submitting nothing and counting nothing; a
    prepass allocation that is refused AFTER one has succeeded, so one mesh of
    an admitted walk falls back mid-walk; a two-range frame whose first range
    takes a pool per coefficient pair; and the FAR1 transition inside that same
    walk. Then the walk's exit.

    WHAT IS NOT COVERED. The prepass program's own arithmetic (no GLSL compiler
    and no GPU in this process), any image, any frame time, and the menu or
    settings integration - the candidate is selected only by SL_BG_TF_CLIP at
    launch, so there is no row for this fixture to move.

    THE ENVIRONMENT IS PART OF THE SCENARIO, and each variable is here for a
    measured reason rather than as a convenience:

      SL_BG_TF_CLIP=1   selects the candidate; nothing else does
      SL_BG_GPU_XFORM=1 the candidate is reached INSIDE the transform admission
      SL_PERF_BG_FAR=1  a two-range walk is otherwise refused (far_admit), and
                        without it neither FAR0 pair nor the FAR1 transition is
                        reachable at all
      SL_BG_FOGLESS=0   the far clamp - and so `nfar`, and so `farpass` - cannot
                        fire on a fogless walk
      SL_BG_HIDE=4      mesh 4 is the filtered one

    THE TWO CONTROLS EACH GET THEIR OWN RUNS, because each switch is parsed once
    for the launch. SL_BG_TF_RASTER_CPU keeps the prepass and sends the raster
    back to the original position stream; SL_BG_TF_STATE_ONLY additionally omits
    the prepass's own glDrawArrays and is meaningful only with both of the
    others. The runs are rastercpu, rcpualone, stateonly, soalone and somis - the
    last two being the switch alone and the switch without the original-raster
    control, neither of which may change what the pass admits or draws.

    AND THE EMPTY-DRAW STATE SELECTOR GETS ONE RUN PER ROW, for the same reason:
    SL_BG_TF_EMPTY_STATE is parsed once for the launch, so one process can observe
    one row. The rows run on the zero-count arm, which needs
    SL_BG_TF_ZERO_COUNT=1 with both switches above, and each one asserts for
    itself that its empty draw carried that row's conditions AT the call, that the
    raster read the ORIGINAL stream, that the mode's work is completely accounted
    and that no coverage was claimed. Three of the rows differ from the others
    only in WHERE two glUseProgram calls sit, which no state read after the draw
    can recover, so the seam's own per-draw records are what separate them.

    Seven refusal runs follow the rows - an unreadable value, each missing coupled
    switch, each of the three older exclusive controls requested alongside, and the
    selector alone - because "the mode never silently runs another row" is a
    property of a launch and not of a variable. Every row's draw-sequence digest
    is compared against the original-raster control's below: the raster is the
    original stream on all of them, so the indexed draws must be the same ones in
    the same order.

    Exits 0 when the fixture passes and every mutant fails, 1 otherwise.

.PARAMETER Msys2Root
    Where MSYS2 lives, if it is not in one of the usual places. The
    SL_MSYS2_ROOT environment variable does the same thing.

.PARAMETER OutDir
    Where the objects and the executable go. Defaults to build\win32 beside the
    other fixtures; point it elsewhere to keep a run out of the build tree.
#>
[CmdletBinding()]
param([string]$Msys2Root, [string]$OutDir)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $OutDir) { $OutDir = Join-Path $repo 'build\win32' }

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
    Write-Error "bgtfseamtest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

$objDir = Join-Path $OutDir 'bgtfseam-obj'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

# The SAME flag list src/gfx takes in build.ps1, plus -D_GDI32_ for the reason
# above. -w rather than -Wall: this compiles a 25,000-line product translation
# unit whose warnings are the product's own business and not this fixture's.
$flags = @('-m32', '-O2', '-w', '-mno-ms-bitfields',
           '-Werror=implicit-function-declaration', '-msse2', '-mfpmath=sse',
           '-fno-strict-aliasing', '-D_GDI32_', '-DSDL_MAIN_HANDLED',
           '-Itools/profiling/native', '-Isrc/gfx', '-Isrc/platform',
           "-I$(Join-Path $mingwBin '..\include\SDL2' | Resolve-Path)")

# The fixture's own translation unit is the one that #includes sl_gfx_dl.c, so
# sl_gfx_dl.c does NOT appear here: compiling it separately as well would define
# every one of its symbols twice.
#
# src/gfx/sl_gfx_bgstate.c IS ONE OF THEM, and its absence was a link failure
# rather than a choice: src/gfx/sl_gfx_bgtf.c calls that module's arming and
# phase hooks (sl_bgstate_armed, sl_bgstate_reuse, sl_bgstate_post_draw_raw,
# sl_bgstate_post_restore) unconditionally, so no executable of this fixture can
# be produced without it. The product source is compiled unmodified; the probe it
# implements stays off, because nothing here sets the variable that arms it.
$units = @(
    'tools/profiling/native/test_sl_gfx_bgtf_seam.c',
    'tools/profiling/native/sl_gfx_seamgl.c',
    'src/gfx/sl_gfx_bgtf.c',
    'src/gfx/sl_gfx_bgstate.c',
    'src/gfx/sl_gfx_worldasset.c',
    'src/gfx/sl_gfx_tex.c',
    'src/gfx/sl_gfx_texprov.c',
    'src/gfx/sl_gfx_bggpu.c',
    'src/gfx/sl_bgsnap_write.c',
    'src/gfx/sl_gfx_charskin.c',
    'src/gfx/sl_gfx_skybatch.c',
    'src/platform/sl_bg_worker.c',
    'src/platform/sl_worker_pool.c',
    'src/platform/sl_perf.c',
    'src/platform/sl_display.c',
    'src/platform/sl_presentation.c',
    'src/platform/sl_settings.c'
) | Where-Object { Test-Path (Join-Path $repo $_) }

# THE MUTANTS: the same unmodified product sources against a fake driver with
# ONE injected fault, each of which MUST make the fixture fail. The faults are
# described above sl_gfx_seamgl.c's mutant block; the property each one is
# expected to break is named here. Only the fixture's own two translation units
# are recompiled for a mutant - the product's objects are the ones built above,
# from unmodified source.
$mutants = @(
    @{ name = 'countall'; define = 'BGTFSEAM_MUT_COUNTALL';
       breaks = 'every draw is credited to the candidate, which is what the g_bgtf_src gate exists to prevent: excluded, filtered, refused and CPU-fallback meshes would all read as coverage' },
    @{ name = 'ptrstick'; define = 'BGTFSEAM_MUT_PTRSTICK';
       breaks = 'the position pointer never moves after the first draw, so a restoration that happened is invisible and a stale source cannot be told from a restored one' },
    @{ name = 'allocok';  define = 'BGTFSEAM_MUT_ALLOCOK';
       breaks = 'GL_BUFFER_SIZE reports the request, so a pool that was never allocated is stamped valid and the prepass-refusal scenario never refuses' }
)

# The scenario's environment. See the note above for why each one is here.
#
# SL_BG_TF_AUX_REAL NAMES THE OFF TOKEN, which is what selects the prepass this
# fixture is about. Every scenario here reads the MAIN GL context's prepass
# through the real sl_gfx_dl.c - which mesh drew from which pool, what a refusal
# put back, whether coverage was gated on the actual source - and with the
# opt-in switch given and no arm named, an opted-in launch no longer reaches
# that prepass: it reaches the OPT-IN CANDIDATE, the auxiliary-context arm,
# whose second GL context this fixture's synthetic driver does not provide, so
# every served draw would be refused to the CPU raster path and every scenario
# would read zero prepasses. The off token is the developer control the
# candidate keeps for exactly this. The candidate's own behaviour is driven in
# the module fixture, one process per combination (mode 'optin').
$envs = @{
    SL_BG_TF_CLIP    = '1'
    SL_BG_TF_AUX_REAL = 'OFF'
    SL_BG_GPU_XFORM  = '1'
    SL_PERF_BG_FAR   = '1'
    SL_BG_FOGLESS    = '0'
    SL_BG_HIDE       = '4'
}

# ---- THE PRODUCTION BUFFER DIAGNOSTIC THROUGH THE REAL SEAM --------------
#
# ONE SCENARIO PER RUN, because SL_BG_TF_DUMP is a launch-time selector parsed
# once and the ONE capture attempt is spent for the life of the process. What
# only this fixture can observe is placement and gating: the readback sits after
# the prepass's transform feedback ended and before the SELECTED indexed draw,
# a mesh the pass binds a pool for but whose run is empty is never captured, and
# a candidate-ineligible mesh is never offered anything at all.
#
# THE SELECTOR'S CLOCKS ARE THE DRAW SITE'S. This fixture's sl_record_index()
# and cloud phase are 0 and its g_dl_frame advances once per walk, so the first
# walk of a run is frame 1. That is the same three counters the product's
# snapshot seam passes, which is the point: a selector built from a
# screenshot's own read and frame would name a different draw.
#
#   mode    the fixture's argument
#   dump    SL_BG_TF_DUMP; mesh 0 is the first candidate-eligible mesh, mesh 5
#           is the one with no indices, mesh 1 is the four-image mesh
#   must    the expected exit
#   verify  how bgtfdump-verify.py is asked about the result, out of process:
#           'mismatches36' requires a sealed COMPLETE record whose pools differ
#           in EXACTLY 36 component patterns, 'none' that no record exists.
#
# THIRTY-SIX IS THE POINT OF VERIFYING THE SEAM AT ALL. This shim's readback
# writes a deterministic function of the byte offset rather than anything like a
# clip position, so the two payloads must differ in every one of the nine
# submitted indices' four component patterns. Asking for a count of 0 - or
# asking for no count, which is what an invocation without --expect-mismatches
# does - would pass just as happily on a record whose GPU payload had received
# the CPU buffer. --prove-contract is NOT passed: the corruption battery tests
# "one more differing pattern than before" and is only meaningful on a baseline
# of zero.
#
# AND AN UNEQUAL RECORD IS A VALID RECORD. These runs require complete AND 36.
$seamDumpRuns = @(
    @{ name = 'dump-capture';  mode = 'dumpon';
       dump = '0:1:0:0:0:2';  must = 0; verify = 'mismatches36';
       what = 'ONE readback, after the transform feedback ended and before the selected draw, bindings and pointer preserved' },
    @{ name = 'dump-empty-run'; mode = 'dumpempty';
       dump = '0:1:0:5:0:2';  must = 0; verify = 'none';
       what = 'a mesh the pass BINDS a pool for but whose run is empty is never captured - a bind is not a draw' },
    @{ name = 'dump-excluded';  mode = 'dumpexcluded';
       dump = '0:1:0:1:1:2';  must = 0; verify = 'none';
       what = 'a candidate-ineligible mesh is never offered, so a CPU-sourced draw cannot be reported as a TF capture' }
)

$rc = 0
$savedPath = $env:PATH
$savedCwd  = (Get-Location).Path
$savedEnv  = @{}
foreach ($k in $envs.Keys) { $savedEnv[$k] = [Environment]::GetEnvironmentVariable($k) }
foreach ($k in @('SL_BG_TF_DUMP', 'SL_BG_TF_DUMP_DIR', 'SL_BG_TF_DUMP_EXE_SHA',
                 'SL_BG_TF_DUMP_SRC_SHA', 'SL_BG_TF_RASTER_CPU',
                 'SL_BG_TF_STATE_ONLY', 'SL_BG_TF_POS_INPUT',
                 # the three the empty-draw rows below set, saved with the rest
                 # so this script leaves the caller's environment as it found it
                 'SL_BG_TF_ZERO_COUNT', 'SL_BG_TF_EMPTY_STATE',
                 # and the one the post-reset arms below set, for the same
                 # reason
                 'SL_BG_TF_POST_RESET',
                 # and the one the neutral-program runs below set, likewise
                 'SL_BG_TF_NEUTRAL_PROG',
                 # and the one the auxiliary-context runs below set, likewise
                 'SL_BG_TF_AUX_EMPTY',
                 'SL_BG_TF_PRIVATE_VAO')) {
    $savedEnv[$k] = [Environment]::GetEnvironmentVariable($k)
}
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    foreach ($k in $envs.Keys) { Set-Item -Path "Env:$k" -Value $envs[$k] }
    # THE DIAGNOSTIC IS ABSENT for the main run and for every mutant: those are
    # about the candidate, and an armed diagnostic in them would be a second
    # thing happening in the same walk.
    foreach ($k in @('SL_BG_TF_DUMP', 'SL_BG_TF_DUMP_DIR', 'SL_BG_TF_DUMP_EXE_SHA',
                     'SL_BG_TF_DUMP_SRC_SHA')) {
        Remove-Item "Env:$k" -ErrorAction SilentlyContinue
    }
    # AND SO IS THE POST-RESET BOUNDARY CONTROL, which the arms below set
    # per run. Removing it here means the main run, the dump runs and every
    # mutant measure the path with no arm selected whatever the caller's own
    # environment held - the default-off case asserts exactly that, and a
    # leaked value would make it assert the opposite.
    Remove-Item 'Env:SL_BG_TF_POST_RESET' -ErrorAction SilentlyContinue
    # AND SO IS THE NEUTRAL-PROGRAM CONTROL, which the runs below set per run,
    # for the same reason: the main run, the dump runs and every mutant must
    # measure the path with it absent whatever the caller's own environment
    # held, and the default-off case asserts exactly that.
    Remove-Item 'Env:SL_BG_TF_NEUTRAL_PROG' -ErrorAction SilentlyContinue
    # AND SO IS THE AUXILIARY-CONTEXT CONTROL, which the runs below set per run,
    # for the same reason again - and with one of its own: that control creates a
    # second GL context, so a leaked value would have the main run, the dump runs
    # and every mutant measuring a path with an extra context in it, which is a
    # difference no counter of theirs would report.
    Remove-Item 'Env:SL_BG_TF_AUX_EMPTY' -ErrorAction SilentlyContinue

    $objs = @()
    foreach ($u in $units) {
        $obj = Join-Path $objDir ((Split-Path -Leaf $u) -replace '\.c$', '.o')
        & $CC @($flags + @('-c', $u, '-o', $obj))
        if ($LASTEXITCODE -ne 0) { throw "bgtfseamtest.ps1: $u failed to compile (exit $LASTEXITCODE)" }
        $objs += $obj
    }
    $exe = Join-Path $OutDir 'bgtfseamtest.exe'
    & $CC @($flags + @('-o', $exe) + $objs)
    if ($LASTEXITCODE -ne 0) { throw "bgtfseamtest.ps1: link failed (exit $LASTEXITCODE)" }

    Write-Output ""
    Write-Output "=== run seam: the REAL sl_gfx_dl.c driving the REAL sl_gfx_bgtf.c"
    # cmd owns the redirect: PowerShell 5.1 would turn the pass's own stderr
    # lines into error records. The verdict is on stdout.
    $err = Join-Path $objDir 'stderr-seam.txt'
    $out = Join-Path $objDir 'stdout-seam.txt'
    & cmd /c "`"$exe`" 1>`"$out`" 2>`"$err`""
    $code = $LASTEXITCODE
    if (Test-Path $out) { Get-Content $out | ForEach-Object { Write-Output $_ } }
    Write-Output "--- exit $code (expected 0)"
    if ($code -ne 0) { $rc = 1 }

    # ---- the production buffer diagnostic's scenarios --------------------
    $dumpRoot = Join-Path $OutDir 'bgtfseamdump'
    New-Item -ItemType Directory -Force -Path $dumpRoot | Out-Null
    $realExeSha = (Get-FileHash -Algorithm SHA256 -Path $exe).Hash.ToLowerInvariant()
    $realSrcSha = (Get-FileHash -Algorithm SHA256 -Path (Join-Path $repo 'src\gfx\sl_gfx_bgtf.c')).Hash.ToLowerInvariant()
    $verifier = Join-Path $repo 'tools\windows\bgtfdump-verify.py'
    $python = Join-Path (Split-Path -Parent (Split-Path -Parent $repo)) 'sightline\.venv\Scripts\python.exe'
    if (-not (Test-Path $python)) { $python = 'D:\Projects\sightline\.venv\Scripts\python.exe' }
    foreach ($d in $seamDumpRuns) {
        Write-Output ""
        Write-Output "=== seam dump $($d.name): SL_BG_TF_DUMP='$($d.dump)' - $($d.what)"
        $runDir = Join-Path $dumpRoot $d.name
        if (Test-Path $runDir) { Remove-Item -Recurse -Force $runDir }
        New-Item -ItemType Directory -Force -Path $runDir | Out-Null
        $env:SL_BG_TF_DUMP = $d.dump
        $env:SL_BG_TF_DUMP_DIR = $runDir
        $env:SL_BG_TF_DUMP_EXE_SHA = $realExeSha
        $env:SL_BG_TF_DUMP_SRC_SHA = $realSrcSha
        $derr = Join-Path $objDir "stderr-$($d.name).txt"
        $dout = Join-Path $objDir "stdout-$($d.name).txt"
        & cmd /c "`"$exe`" `"$($d.mode)`" 1>`"$dout`" 2>`"$derr`""
        $code = $LASTEXITCODE
        if (Test-Path $dout) { Get-Content $dout | ForEach-Object { Write-Output $_ } }
        Write-Output "--- exit $code (expected $($d.must))"
        if ($code -ne $d.must) { $rc = 1 }
        if (Test-Path $derr) {
            Get-Content $derr | Select-Object -First 12 | ForEach-Object { Write-Output "    stderr: $_" }
        }
        Write-Output "--- record files: $(@(Get-ChildItem -Path $runDir -File -ErrorAction SilentlyContinue).Count)"

        # THE RECORD, VERIFIED OUT OF PROCESS. The fixture asserts the payload
        # bytes from inside; this asks the same question from outside, through
        # the reader a capture would actually be read with, and binds the
        # expected mismatch count rather than accepting any.
        if ((Test-Path $verifier) -and (Test-Path $python)) {
            $vlog = Join-Path $objDir "verify-$($d.name).txt"
            $vargs = @('-I', '-B', $verifier, '--dir', $runDir)
            if ($d.verify -eq 'mismatches36') {
                $vargs += '--expect-complete'
                $vargs += '--expect-mismatches'; $vargs += '36'
            }
            if ($d.verify -eq 'none') { $vargs += '--expect-none' }
            $env:PYTHONDONTWRITEBYTECODE = '1'
            & cmd /c "`"$python`" $(($vargs | ForEach-Object { '"' + $_ + '"' }) -join ' ') 1>`"$vlog`" 2>&1"
            $vcode = $LASTEXITCODE
            if (Test-Path $vlog) { Get-Content $vlog | ForEach-Object { Write-Output "    verify: $_" } }
            Write-Output "--- verify exit $vcode (expected 0)"
            if ($vcode -ne 0) { $rc = 1 }
        }
    }
    foreach ($k in @('SL_BG_TF_DUMP', 'SL_BG_TF_DUMP_DIR', 'SL_BG_TF_DUMP_EXE_SHA',
                     'SL_BG_TF_DUMP_SRC_SHA')) {
        Remove-Item "Env:$k" -ErrorAction SilentlyContinue
    }

    # ---- THE ORIGINAL-RASTER CONTROL (SL_BG_TF_RASTER_CPU) ----------------
    #
    # ONE RUN PER SETTING, because the control's switch is parsed once for the
    # launch exactly like the candidate's. Two runs are needed, and a third
    # thing - the comparison between them - is what neither run can state:
    #
    #   rastercpu   the control WITH the candidate: the prepass runs and the
    #               raster reads the original position stream.
    #   rcpualone   the control WITHOUT the candidate, which must do nothing at
    #               all. This is what keeps it from becoming a second way in.
    #
    # THE SHAPE COMPARISON. The fixture prints a digest over every indexed
    # draw's element buffer, element offset, count, program and colour/UV
    # pointer shapes, with the POSITION source deliberately EXCLUDED. Equality
    # between the ordinary arm's digest and the control's is what establishes
    # that only the position source changed: the colour and UV arrays, the
    # element buffer, the index order and the per-range draw decomposition are
    # the same draws in the same order. A digest that included the position
    # source could never show that, and no single run can compare itself to
    # another launch's.
    #
    # ---- AND THE STATE-ONLY CONTROL (SL_BG_TF_STATE_ONLY) ----------------
    #
    # It keeps the whole prepass bracket - program, uniforms, pool and its
    # verification, input position pointer, element binding, rasterizer discard,
    # transform-feedback begin/end - and omits ONE call, the prepass's own
    # glDrawArrays. It is meaningful only WITH SL_BG_TF_CLIP=1 and
    # SL_BG_TF_RASTER_CPU=1, and the module's own answer is coupled to both, so
    # three runs are needed:
    #
    #   stateonly   effective: the setup runs, ZERO glDrawArrays are issued, no
    #               pool is bound and no coverage is claimed, and the raster
    #               reads the ORIGINAL four-float stream with the control's own
    #               CPU counts rising.
    #   soalone     SL_BG_TF_CLIP absent: nothing at all happens.
    #   somis       SL_BG_TF_RASTER_CPU absent: not effective, so the ORDINARY
    #               candidate arm runs - prepass draws, bound pools and counted
    #               coverage - which is what says the switch cannot half-apply
    #               or change product admission.
    #
    # The shape digest is collected from all of them and compared below: the
    # state-only arm must produce the same draw sequence as the ordinary arm,
    # since only the prepass's draw is gone and the position source is excluded
    # from the digest by construction.
    $rcpuRuns = @(
        @{ name = 'rastercpu'; mode = 'rastercpu'; rcpu = '1'; clip = '1'; sonly = $null; must = 0;
           what = 'the prepass RUNS, the raster reads the ORIGINAL stream, and that draw is not TF coverage' },
        @{ name = 'rcpualone'; mode = 'rcpualone'; rcpu = '1'; clip = $null; sonly = $null; must = 0;
           what = 'the control ALONE enables nothing and skips no CPU work' },
        @{ name = 'stateonly'; mode = 'stateonly'; rcpu = '1'; clip = '1'; sonly = '1'; must = 0;
           what = 'the prepass SETUP runs with ZERO vertex draws, nothing is bound or claimed, and the raster still reads the ORIGINAL four-float stream' },
        @{ name = 'soalone';   mode = 'soalone';   rcpu = '1'; clip = $null; sonly = '1'; must = 0;
           what = 'the state-only control ALONE enables nothing and skips no CPU work' },
        @{ name = 'somis';     mode = 'somis';     rcpu = $null; clip = '1'; sonly = '1'; must = 0;
           what = 'selected WITHOUT the original-raster control it is not effective, and the ORDINARY candidate arm runs unchanged' },
        #
        # ---- AND THE POSITION-ONLY-INPUT CONTROL (SL_BG_TF_POS_INPUT) -----
        #
        # It keeps the prepass draw and its transform-feedback writes and
        # narrows the CLIENT ARRAYS that draw dereferences to the position array
        # the program reads, putting back exactly what it found. Two runs:
        #
        #   posinput  effective: real draws doing real transform-feedback work,
        #             reading POSITIONS ONLY, with the pass's own arrays and
        #             client unit back before the ORIGINAL raster reads them.
        #   piboth    both exclusive controls asked for at once: NEITHER applies
        #             and the launch is the ordinary original-raster arm, which
        #             is what keeps an ambiguous launch from silently running
        #             one of the two experiments.
        @{ name = 'posinput'; mode = 'posinput'; rcpu = '1'; clip = '1'; sonly = $null; pin = '1'; must = 0;
           what = 'the prepass draw RUNS and reads POSITIONS ONLY, and the pass arrays and unit are back before the original raster' },
        @{ name = 'piboth';   mode = 'piboth';   rcpu = '1'; clip = '1'; sonly = '1'; pin = '1'; must = 0;
           what = 'both exclusive controls asked for: neither applies and the ordinary original-raster arm runs' }
    )
    $shapes = @{}
    # The ordinary arm's digest comes from the main run already executed above.
    if (Test-Path $out) {
        $m0 = (Get-Content $out | Where-Object { $_ -match 'SHAPE draws=\d+ digest=' } | Select-Object -Last 1)
        if ($m0 -match 'draws=(\d+) digest=([0-9a-f]+)') {
            $shapes['ordinary'] = @{ draws = $Matches[1]; digest = $Matches[2] }
        }
    }
    foreach ($r in $rcpuRuns) {
        $clipShown  = if ($null -eq $r.clip)  { 'ABSENT' } else { $r.clip }
        $rcpuShown  = if ($null -eq $r.rcpu)  { 'ABSENT' } else { $r.rcpu }
        $sonlyShown = if ($null -eq $r.sonly) { 'ABSENT' } else { $r.sonly }
        Write-Output ""
        Write-Output "=== seam control $($r.name): SL_BG_TF_RASTER_CPU=$rcpuShown, SL_BG_TF_CLIP=$clipShown, SL_BG_TF_STATE_ONLY=$sonlyShown - $($r.what)"
        if ($null -eq $r.rcpu) { Remove-Item Env:SL_BG_TF_RASTER_CPU -ErrorAction SilentlyContinue }
        else { $env:SL_BG_TF_RASTER_CPU = $r.rcpu }
        if ($null -eq $r.sonly) { Remove-Item Env:SL_BG_TF_STATE_ONLY -ErrorAction SilentlyContinue }
        else { $env:SL_BG_TF_STATE_ONLY = $r.sonly }
        if ($null -eq $r.pin) { Remove-Item Env:SL_BG_TF_POS_INPUT -ErrorAction SilentlyContinue }
        else { $env:SL_BG_TF_POS_INPUT = $r.pin }
        if ($null -eq $r.clip) { Remove-Item Env:SL_BG_TF_CLIP -ErrorAction SilentlyContinue }
        else { $env:SL_BG_TF_CLIP = $r.clip }
        $cerr = Join-Path $objDir "stderr-$($r.name).txt"
        $cout = Join-Path $objDir "stdout-$($r.name).txt"
        & cmd /c "`"$exe`" `"$($r.mode)`" 1>`"$cout`" 2>`"$cerr`""
        $code = $LASTEXITCODE
        if (Test-Path $cout) { Get-Content $cout | ForEach-Object { Write-Output $_ } }
        Write-Output "--- exit $code (expected $($r.must))"
        if ($code -ne $r.must) { $rc = 1 }
        if (Test-Path $cerr) {
            Get-Content $cerr | Select-Object -First 12 | ForEach-Object { Write-Output "    stderr: $_" }
        }
        if (Test-Path $cout) {
            $ml = (Get-Content $cout | Where-Object { $_ -match 'SHAPE draws=\d+ digest=' } | Select-Object -Last 1)
            if ($ml -match 'draws=(\d+) digest=([0-9a-f]+)') {
                $shapes[$r.name] = @{ draws = $Matches[1]; digest = $Matches[2] }
            }
        }
        # The candidate's switch is put back for whatever runs next.
        $env:SL_BG_TF_CLIP = $envs['SL_BG_TF_CLIP']
        Remove-Item Env:SL_BG_TF_RASTER_CPU -ErrorAction SilentlyContinue
        Remove-Item Env:SL_BG_TF_STATE_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:SL_BG_TF_POS_INPUT -ErrorAction SilentlyContinue
    }

    Write-Output ""
    Write-Output "=== the draw sequence's shape ACROSS the arms (position source excluded)"
    foreach ($k in @('ordinary', 'rastercpu', 'rcpualone', 'stateonly', 'soalone',
                     'somis', 'posinput', 'piboth')) {
        if ($shapes.ContainsKey($k)) {
            Write-Output "    $k : draws=$($shapes[$k].draws) digest=$($shapes[$k].digest)"
        } else {
            Write-Output "    $k : NO DIGEST REPORTED"
            $rc = 1
        }
    }
    if ($shapes.ContainsKey('ordinary') -and $shapes.ContainsKey('rastercpu')) {
        if ($shapes['ordinary'].digest -eq $shapes['rastercpu'].digest) {
            Write-Output "--- EQUAL: only the position source differs between the ordinary arm and the control"
        } else {
            Write-Output "--- DIFFER: the control moved more than the position source - colour, UV, element buffer, index order or the per-range decomposition changed"
            $rc = 1
        }
    }
    # THE STATE-ONLY ARM AGAINST THE ORIGINAL-RASTER CONTROL, which is the
    # comparison the chained test is being prepared for: those two arms differ
    # only in whether the prepass's vertex draw executed, so the sequence of
    # INDEXED draws - colour, UV, element buffer, index order, per-range
    # decomposition - must be identical. The position source is excluded from the
    # digest by construction and is the ORIGINAL stream on both arms anyway,
    # which each run asserts for itself.
    if ($shapes.ContainsKey('rastercpu') -and $shapes.ContainsKey('stateonly')) {
        if ($shapes['rastercpu'].digest -eq $shapes['stateonly'].digest) {
            Write-Output "--- EQUAL: the state-only arm draws the same indexed sequence as the original-raster control - only the prepass's own vertex draw is gone"
        } else {
            Write-Output "--- DIFFER: omitting the prepass draw changed the raster draw sequence, which it must not"
            $rc = 1
        }
    }
    # THE POSITION-ONLY-INPUT ARM AGAINST THE ORIGINAL-RASTER CONTROL. Those two
    # differ only in which CLIENT ARRAYS the prepass draw dereferenced, so the
    # sequence of indexed draws - colour, UV, element buffer, index order,
    # per-range decomposition - must be identical. Each run asserts for itself
    # that the arrays came back before the raster read them; this says the raster
    # sequence is the same one.
    if ($shapes.ContainsKey('rastercpu') -and $shapes.ContainsKey('posinput')) {
        if ($shapes['rastercpu'].digest -eq $shapes['posinput'].digest) {
            Write-Output "--- EQUAL: narrowing the prepass draw's client inputs changed nothing about the raster draw sequence"
        } else {
            Write-Output "--- DIFFER: the position-only-input control moved more than the prepass draw's client arrays"
            $rc = 1
        }
    }
    # AND AN AMBIGUOUS LAUNCH IS THE ORIGINAL-RASTER ARM, stated as the digest:
    # with both exclusive controls asked for, neither applies.
    if ($shapes.ContainsKey('rastercpu') -and $shapes.ContainsKey('piboth')) {
        if ($shapes['rastercpu'].digest -eq $shapes['piboth'].digest) {
            Write-Output "--- EQUAL: an ambiguous launch draws the original-raster control's sequence, so neither exclusive control applied"
        } else {
            Write-Output "--- DIFFER: an ambiguous launch changed the draw sequence, so one of the exclusive controls half-applied"
            $rc = 1
        }
    }
    # AND THE MISCOMBINED ARM IS THE ORDINARY ONE, stated as the digest too: it
    # must match the ordinary candidate run rather than either control.
    if ($shapes.ContainsKey('ordinary') -and $shapes.ContainsKey('somis')) {
        if ($shapes['ordinary'].digest -eq $shapes['somis'].digest) {
            Write-Output "--- EQUAL: a miscombined state-only launch draws the ordinary candidate arm's sequence"
        } else {
            Write-Output "--- DIFFER: a miscombined state-only launch changed the draw sequence, so the switch half-applied"
            $rc = 1
        }
    }

    # ---- THE EMPTY-DRAW STATE SELECTOR'S ROWS (SL_BG_TF_EMPTY_STATE) -------
    #
    # ONE ROW PER RUN, because that selector is parsed once for the launch like
    # every switch above it. The rows run on the zero-count arm, so each needs
    # SL_BG_TF_ZERO_COUNT=1 with SL_BG_TF_CLIP=1 and SL_BG_TF_RASTER_CPU=1; the
    # module refuses the selector without them, which the miscombination runs
    # below are about.
    #
    # WHY THIS LOOP IS SEPARATE FROM THE ONE ABOVE rather than more columns in
    # it: that loop's runs are the controls the rows are EXCLUSIVE with, and
    # widening it would mean every one of those runs also carried this
    # selector's keys. Keeping them apart leaves each block setting only what
    # its own subject needs.
    #
    # WHAT EACH ROW ASSERTS IS THE FIXTURE'S BUSINESS, not this runner's: the
    # empty draw's conditions AT the call, the original raster stream, the
    # complete served-mesh account and the absence of coverage. What the runner
    # adds is the comparison no single launch can make - every row's indexed
    # draw sequence against the original-raster control's - and the fact that
    # each row was reached in a process of its own.
    #
    #   es   SL_BG_TF_EMPTY_STATE, or $null for ABSENT
    #   zc   SL_BG_TF_ZERO_COUNT, likewise
    #   shape  $true when this run must report a digest equal to the
    #          original-raster control's. The refusal runs are not compared:
    #          they are the ordinary arm their own switches selected, and which
    #          arm that is differs per run.
    $esRuns = @(
        @{ name = 'esA'; mode = 'esA'; clip = '1'; rcpu = '1'; zc = '1'; es = 'A'; shape = $true;
           what = 'T0 D1 shader: the begin/end pair omitted together, discard and the program untouched' },
        @{ name = 'esB'; mode = 'esB'; clip = '1'; rcpu = '1'; zc = '1'; es = 'B'; shape = $true;
           what = 'T1 D0 shader: the pair kept, discard off for the empty draw alone' },
        @{ name = 'esC'; mode = 'esC'; clip = '1'; rcpu = '1'; zc = '1'; es = 'C'; shape = $true;
           what = 'T0 D0 shader: neither the pair nor discard at the empty draw' },
        @{ name = 'esE'; mode = 'esE'; clip = '1'; rcpu = '1'; zc = '1'; es = 'E'; shape = $true;
           what = 'T0 D1 program 0: a switch to 0 before the empty draw and the rebind after it' },
        @{ name = 'esR'; mode = 'esR'; clip = '1'; rcpu = '1'; zc = '1'; es = 'R'; shape = $true;
           what = 'T0 D1 shader: the switch pair BEFORE the draw, so the pair happens and the shader is current at the call' },
        @{ name = 'esS'; mode = 'esS'; clip = '1'; rcpu = '1'; zc = '1'; es = 'S'; shape = $true;
           what = 'T0 D1 program 0 LEFT current: the one row with no rebind between its draw and the common cleanup' },
        @{ name = 'esQ'; mode = 'esQ'; clip = '1'; rcpu = '1'; zc = '1'; es = 'Q'; shape = $true;
           what = 'T0 D1 shader: the switch pair AFTER the draw' },

        # AND THE REFUSALS, one reason per launch. Each must leave the selector
        # OFF, count exactly one refusal, and run whatever arm its own switches
        # selected - which is what "no row is ever silently substituted" means.
        @{ name = 'esbad';     mode = 'esbad';     clip = '1'; rcpu = '1'; zc = '1'; es = 'Z';
           what = 'an unreadable value with the arm properly coupled: refused, and the launch is the ORDINARY zero-count arm' },
        @{ name = 'esmiszc';   mode = 'esmiszc';   clip = '1'; rcpu = '1'; zc = $null; es = 'A';
           what = 'a valid row without the zero-count arm: there is no empty draw for a condition to hold at' },
        @{ name = 'esmisrcpu'; mode = 'esmisrcpu'; clip = '1'; rcpu = $null; zc = '1'; es = 'A';
           what = 'a valid row and a zero-count request without the original-raster control: the arm beneath the row is itself refused' },
        @{ name = 'esmixso';   mode = 'esmixso';   clip = '1'; rcpu = '1'; zc = '1'; es = 'A'; sonly = '1';
           what = 'a valid row alongside the state-only control, decided from the RAW request' },
        @{ name = 'esmixpi';   mode = 'esmixpi';   clip = '1'; rcpu = '1'; zc = '1'; es = 'A'; pin = '1';
           what = 'a valid row alongside the position-only input' },
        @{ name = 'esmixpv';   mode = 'esmixpv';   clip = '1'; rcpu = '1'; zc = '1'; es = 'A'; pvao = '1';
           what = 'a valid row alongside the private vertex array' },
        @{ name = 'esalone';   mode = 'esalone';   clip = $null; rcpu = $null; zc = $null; es = 'A';
           what = 'the selector ALONE enables nothing at all' }
    )
    foreach ($r in $esRuns) {
        $esShown = if ($null -eq $r.es) { 'ABSENT' } else { $r.es }
        $zcShown = if ($null -eq $r.zc) { 'ABSENT' } else { $r.zc }
        Write-Output ""
        Write-Output "=== seam empty-draw row $($r.name): SL_BG_TF_EMPTY_STATE=$esShown, SL_BG_TF_ZERO_COUNT=$zcShown - $($r.what)"
        foreach ($p in @(@('SL_BG_TF_CLIP', $r.clip), @('SL_BG_TF_RASTER_CPU', $r.rcpu),
                         @('SL_BG_TF_ZERO_COUNT', $r.zc), @('SL_BG_TF_EMPTY_STATE', $r.es),
                         @('SL_BG_TF_STATE_ONLY', $r.sonly), @('SL_BG_TF_POS_INPUT', $r.pin),
                         @('SL_BG_TF_PRIVATE_VAO', $r.pvao))) {
            if ($null -eq $p[1]) { Remove-Item "Env:$($p[0])" -ErrorAction SilentlyContinue }
            else { Set-Item -Path "Env:$($p[0])" -Value $p[1] }
        }
        $eerr = Join-Path $objDir "stderr-$($r.name).txt"
        $eout = Join-Path $objDir "stdout-$($r.name).txt"
        & cmd /c "`"$exe`" `"$($r.mode)`" 1>`"$eout`" 2>`"$eerr`""
        $code = $LASTEXITCODE
        if (Test-Path $eout) { Get-Content $eout | ForEach-Object { Write-Output $_ } }
        Write-Output "--- exit $code (expected 0)"
        if ($code -ne 0) { $rc = 1 }
        if (Test-Path $eerr) {
            Get-Content $eerr | Select-Object -First 12 | ForEach-Object { Write-Output "    stderr: $_" }
        }
        if ($r.shape -and (Test-Path $eout)) {
            $el = (Get-Content $eout | Where-Object { $_ -match 'SHAPE draws=\d+ digest=' } | Select-Object -Last 1)
            if ($el -match 'draws=(\d+) digest=([0-9a-f]+)') {
                $shapes[$r.name] = @{ draws = $Matches[1]; digest = $Matches[2] }
            } else {
                Write-Output "--- NO DIGEST REPORTED by a row that must report one"
                $rc = 1
            }
        }
        # Everything this run set goes back to the launch default for whatever
        # runs next; the candidate's own switch is restored, the rest removed.
        $env:SL_BG_TF_CLIP = $envs['SL_BG_TF_CLIP']
        foreach ($k in @('SL_BG_TF_RASTER_CPU', 'SL_BG_TF_ZERO_COUNT',
                         'SL_BG_TF_EMPTY_STATE', 'SL_BG_TF_STATE_ONLY',
                         'SL_BG_TF_POS_INPUT', 'SL_BG_TF_PRIVATE_VAO')) {
            Remove-Item "Env:$k" -ErrorAction SilentlyContinue
        }
    }

    # ---- THE POST-RESET BOUNDARY CONTROL (SL_BG_TF_POST_RESET) ------------
    #
    # ONE ARM PER LAUNCH, because the selector is parsed once for the process,
    # and the three arms run on the REAL NONZERO transform path - the candidate
    # alone, with the original-raster control, the zero-count arm and every
    # empty-draw row ABSENT. That is the path a shipping question is about and
    # the one no empty-draw row reaches; the fixture's own case refuses a launch
    # whose environment is not it.
    #
    # WHAT THIS RUNNER ADDS over the per-launch cases: that each arm was reached
    # in a process of its own, and the comparison no single launch can make -
    # every arm's indexed draw sequence against the ORDINARY arm's. The boundary
    # adds calls after the prepass and before the raster, so what the pass draws
    # must be untouched.
    #
    #   pr      SL_BG_TF_POST_RESET, or $null for ABSENT
    #   clip    SL_BG_TF_CLIP, likewise
    #   shape   $true when this run must report a digest equal to the ordinary
    #           arm's. The refusal runs are not compared: they are the ordinary
    #           arm their own switches selected, and which arm that is differs
    #           per run.
    $prRuns = @(
        @{ name = 'prEMPTY'; mode = 'prEMPTY'; clip = '1'; pr = 'POST_PROGRAM0_EMPTY'; shape = $true;
           what = 'exactly one glDrawArrays(GL_POINTS, 0, 0) at the boundary, under program 0, with no glUseProgram added' },
        @{ name = 'prFINISH'; mode = 'prFINISH'; clip = '1'; pr = 'POST_FINISH'; shape = $true;
           what = 'exactly one glFinish at the boundary and no draw' },
        @{ name = 'prBOTH'; mode = 'prBOTH'; clip = '1'; pr = 'BOTH'; shape = $true;
           what = 'the empty draw and then the glFinish, in that fixed order' },

        # AND THE REFUSALS, one reason per launch. Each must leave the arm OFF,
        # count exactly one refusal, issue no glFinish, print no line of this
        # control's, and run whatever arm its own switches selected.
        @{ name = 'prbad';    mode = 'prbad';    clip = '1'; pr = 'FINISH';
           what = 'an unreadable value with the candidate selected: refused, and the launch is the ORDINARY candidate arm' },
        @{ name = 'prmixso';  mode = 'prmixso';  clip = '1'; pr = 'BOTH'; sonly = '1'; rcpu = '1';
           what = 'an arm alongside the state-only control, which OMITS the prepass draw this boundary is defined to follow - structural, not attribution' },
        @{ name = 'prmixpi';  mode = 'prmixpi';  clip = '1'; pr = 'POST_FINISH'; pin = '1'; rcpu = '1';
           what = 'an arm alongside the position-only input: a second experiment on the same prepass' },
        @{ name = 'prmixpv';  mode = 'prmixpv';  clip = '1'; pr = 'POST_PROGRAM0_EMPTY'; pvao = '1'; rcpu = '1';
           what = 'an arm alongside the private vertex array: likewise' },
        @{ name = 'pralone';  mode = 'pralone';  clip = $null; pr = 'BOTH';
           what = 'the selector ALONE enables nothing at all, so there is no prepass and no boundary' },

        # AND ONE COMPOSED LAUNCH, which is the only run that has both this
        # control's line and an empty-draw row's in one census. The control is
        # independent of that mode by design, so the row's whole case must pass
        # unchanged beside the arm's. It is not compared to the ordinary arm's
        # digest: it is a zero-count launch and those draw the original-raster
        # control's sequence, which the row block below compares separately.
        @{ name = 'prcompose'; mode = 'prcompose'; clip = '1'; rcpu = '1'; zc = '1';
           es = 'R'; pr = 'BOTH';
           what = 'row R composed with arm BOTH: both lines in one census, and the row unchanged' },

        # ---- AND THE NEUTRAL-PROGRAM CONTROL (SL_BG_TF_NEUTRAL_PROG) -------
        #
        # ONE RUN PER SETTING, because its switch is parsed once for the process
        # and the program it creates is deleted inside the case. It runs on the
        # ZERO-COUNT arm, so it needs SL_BG_TF_CLIP=1, SL_BG_TF_RASTER_CPU=1 and
        # SL_BG_TF_ZERO_COUNT=1; the module refuses it without them, which the
        # miscombination run below is about.
        #
        # They are in THIS loop rather than a sixth one because the loop already
        # sets every key any of them needs, including this control's own. What
        # the runner adds over the per-launch cases is that the control was
        # reached in a process of its own, and the comparison no single launch
        # can make - its indexed draw sequence against the ORIGINAL-RASTER
        # control's, which is what every zero-count launch must draw.
        @{ name = 'npon'; mode = 'npon'; clip = '1'; rcpu = '1'; zc = '1'; np = '1';
           shape = $true;
           what = 'the one empty draw under a newly linked program with no feedback varyings, row A conditions at the call, and teardown deleting exactly that program' },

        # AND THE REFUSALS, one reason per launch. Each must leave the control
        # off, count exactly one refusal, create no program, print no line of
        # its own, and run whatever arm its own switches selected.
        @{ name = 'npmixes';  mode = 'npmixes';  clip = '1'; rcpu = '1'; zc = '1'; np = '1'; es = 'A';
           what = 'an empty-draw row also requested, which varies the program at the SAME call' },
        @{ name = 'npmixpr';  mode = 'npmixpr';  clip = '1'; rcpu = '1'; zc = '1'; np = '1'; pr = 'BOTH';
           what = 'a post-reset arm also requested, whose guard is about which program the bracket last made current' },
        @{ name = 'npmixso';  mode = 'npmixso';  clip = '1'; rcpu = '1'; zc = '1'; np = '1'; sonly = '1';
           what = 'the state-only control also requested, which OMITS the draw there would be a program to bind for' },
        @{ name = 'npmixpi';  mode = 'npmixpi';  clip = '1'; rcpu = '1'; zc = '1'; np = '1'; pin = '1';
           what = 'the position-only input also requested' },
        @{ name = 'npmixpv';  mode = 'npmixpv';  clip = '1'; rcpu = '1'; zc = '1'; np = '1'; pvao = '1';
           what = 'the private vertex array also requested' },
        @{ name = 'npmiszc';  mode = 'npmiszc';  clip = '1'; rcpu = '1'; zc = $null; np = '1';
           what = 'the zero-count arm absent: there is no EMPTY draw for a second program to be bound at' },
        @{ name = 'npalone';  mode = 'npalone';  clip = $null; np = '1';
           what = 'the switch ALONE enables nothing at all, so there is no prepass and no program' },

        # ---- AND THE AUXILIARY-CONTEXT CONTROL (SL_BG_TF_AUX_EMPTY) --------
        #
        # ONE RUN PER SETTING, for the reason the neutral-program runs above are
        # one per setting and one of this control's own: its switch is parsed
        # once for the process AND the second GL context it creates is destroyed
        # inside the case, so a second setting in the same process would be
        # driving a control that had already torn its context down.
        #
        # THE CONTEXT HERE IS THE SYNTHETIC BACKEND'S. tools/profiling/native/
        # sl_gfx_seamgl.c supplies the four context functions, so no window and
        # no platform call happens in this fixture - what it checks is the
        # module's SEQUENCE and per-context placement over a real walk.
        @{ name = 'axon'; mode = 'axon'; clip = '1'; rcpu = '1'; zc = '1'; ax = '1';
           shape = $true;
           what = 'every empty draw of the walk issued on ONE second object-sharing context with row A conditions there, the MAIN context issuing none, and teardown destroying exactly that context' },

        # AND TWO REFUSALS. The module fixture drives all eight exclusions one
        # per launch; these two are the ones worth driving against the real
        # caller - the control closest to this one, and the missing arm.
        @{ name = 'axmixnp';  mode = 'axmixnp';  clip = '1'; rcpu = '1'; zc = '1'; ax = '1'; np = '1';
           what = 'the neutral-program control also requested, which changes WHICH PROGRAM the same call runs under' },
        @{ name = 'axmiszc';  mode = 'axmiszc';  clip = '1'; rcpu = '1'; zc = $null; ax = '1';
           what = 'the zero-count arm absent: there is no EMPTY draw to relocate to a second context' }
    )
    foreach ($r in $prRuns) {
        $prShown = if ($null -eq $r.pr) { 'ABSENT' } else { $r.pr }
        $npShown = if ($null -eq $r.np) { 'ABSENT' } else { $r.np }
        $axShown = if ($null -eq $r.ax) { 'ABSENT' } else { $r.ax }
        Write-Output ""
        Write-Output "=== seam run $($r.name): SL_BG_TF_POST_RESET=$prShown, SL_BG_TF_NEUTRAL_PROG=$npShown, SL_BG_TF_AUX_EMPTY=$axShown - $($r.what)"
        foreach ($p in @(@('SL_BG_TF_CLIP', $r.clip), @('SL_BG_TF_RASTER_CPU', $r.rcpu),
                         @('SL_BG_TF_ZERO_COUNT', $r.zc), @('SL_BG_TF_EMPTY_STATE', $r.es),
                         @('SL_BG_TF_POST_RESET', $r.pr),
                         @('SL_BG_TF_NEUTRAL_PROG', $r.np),
                         @('SL_BG_TF_AUX_EMPTY', $r.ax),
                         @('SL_BG_TF_STATE_ONLY', $r.sonly), @('SL_BG_TF_POS_INPUT', $r.pin),
                         @('SL_BG_TF_PRIVATE_VAO', $r.pvao))) {
            if ($null -eq $p[1]) { Remove-Item "Env:$($p[0])" -ErrorAction SilentlyContinue }
            else { Set-Item -Path "Env:$($p[0])" -Value $p[1] }
        }
        $perr = Join-Path $objDir "stderr-$($r.name).txt"
        $pout = Join-Path $objDir "stdout-$($r.name).txt"
        & cmd /c "`"$exe`" `"$($r.mode)`" 1>`"$pout`" 2>`"$perr`""
        $code = $LASTEXITCODE
        if (Test-Path $pout) { Get-Content $pout | ForEach-Object { Write-Output $_ } }
        Write-Output "--- exit $code (expected 0)"
        if ($code -ne 0) { $rc = 1 }
        if (Test-Path $perr) {
            Get-Content $perr | Select-Object -First 12 | ForEach-Object { Write-Output "    stderr: $_" }
        }
        if ($r.shape -and (Test-Path $pout)) {
            $pl = (Get-Content $pout | Where-Object { $_ -match 'SHAPE draws=\d+ digest=' } | Select-Object -Last 1)
            if ($pl -match 'draws=(\d+) digest=([0-9a-f]+)') {
                $shapes[$r.name] = @{ draws = $Matches[1]; digest = $Matches[2] }
            } else {
                Write-Output "--- NO DIGEST REPORTED by an arm that must report one"
                $rc = 1
            }
        }
        # Everything this run set goes back to the launch default for whatever
        # runs next; the candidate's own switch is restored, the rest removed.
        $env:SL_BG_TF_CLIP = $envs['SL_BG_TF_CLIP']
        foreach ($k in @('SL_BG_TF_RASTER_CPU', 'SL_BG_TF_ZERO_COUNT',
                         'SL_BG_TF_EMPTY_STATE', 'SL_BG_TF_POST_RESET',
                         'SL_BG_TF_NEUTRAL_PROG', 'SL_BG_TF_AUX_EMPTY',
                         'SL_BG_TF_STATE_ONLY', 'SL_BG_TF_POS_INPUT',
                         'SL_BG_TF_PRIVATE_VAO')) {
            Remove-Item "Env:$k" -ErrorAction SilentlyContinue
        }
    }

    # EVERY ARM DRAWS THE ORDINARY ARM'S SEQUENCE. The arms differ from the
    # ordinary candidate run only in the calls added at the boundary, which is
    # after the prepass and before the raster, so the indexed draws must be the
    # same ones in the same order. An arm that moved them would have changed what
    # the pass draws, which no arm may do. The reference is the MAIN run, because
    # these arms are the real nonzero path and not the original-raster control.
    Write-Output ""
    Write-Output "=== the draw sequence's shape ACROSS the post-reset arms (position source excluded)"
    if ($shapes.ContainsKey('ordinary')) {
        foreach ($k in @('prEMPTY', 'prFINISH', 'prBOTH')) {
            if (-not $shapes.ContainsKey($k)) {
                Write-Output "    $k : NO DIGEST REPORTED"
                $rc = 1
                continue
            }
            Write-Output "    $k : draws=$($shapes[$k].draws) digest=$($shapes[$k].digest)"
            if ($shapes[$k].digest -ne $shapes['ordinary'].digest) {
                Write-Output "    --- DIFFER from the ordinary arm: $k changed the raster draw sequence, which no arm may do"
                $rc = 1
            }
        }
        Write-Output "    ordinary (the reference): draws=$($shapes['ordinary'].draws) digest=$($shapes['ordinary'].digest)"
    } else {
        Write-Output "    the ordinary arm reported no digest, so the post-reset arms cannot be compared against it"
        $rc = 1
    }

    # EVERY ROW DRAWS THE ORIGINAL-RASTER CONTROL'S SEQUENCE. The rows differ
    # from that control only in the prepass's own empty call and the state around
    # it, and the position source is excluded from the digest by construction -
    # so the indexed draws must be the same ones in the same order. A row that
    # moved them would have changed what the pass draws, which no row may do.
    Write-Output ""
    Write-Output "=== the draw sequence's shape ACROSS the empty-draw rows (position source excluded)"
    if ($shapes.ContainsKey('rastercpu')) {
        foreach ($k in @('esA', 'esB', 'esC', 'esE', 'esR', 'esS', 'esQ')) {
            if (-not $shapes.ContainsKey($k)) {
                Write-Output "    $k : NO DIGEST REPORTED"
                $rc = 1
                continue
            }
            Write-Output "    $k : draws=$($shapes[$k].draws) digest=$($shapes[$k].digest)"
            if ($shapes[$k].digest -ne $shapes['rastercpu'].digest) {
                Write-Output "    --- DIFFER from the original-raster control: row $k changed the raster draw sequence, which no row may do"
                $rc = 1
            }
        }
        Write-Output "    rastercpu (the reference): draws=$($shapes['rastercpu'].draws) digest=$($shapes['rastercpu'].digest)"
    } else {
        Write-Output "    the original-raster control reported no digest, so the rows cannot be compared against it"
        $rc = 1
    }

    # AND THE NEUTRAL-PROGRAM CONTROL DRAWS THE ORIGINAL-RASTER CONTROL'S
    # SEQUENCE TOO. It is a zero-count launch, so that control is the right
    # reference and not the ordinary arm: it differs from it only in the
    # prepass's own empty call and the program bound for it, and the position
    # source is excluded from the digest by construction. A control that moved
    # the indexed draws would have changed what the pass draws, which it may
    # not do.
    Write-Output ""
    Write-Output "=== the draw sequence's shape under the neutral-program control (position source excluded)"
    if ($shapes.ContainsKey('rastercpu')) {
        if (-not $shapes.ContainsKey('npon')) {
            Write-Output "    npon : NO DIGEST REPORTED"
            $rc = 1
        } else {
            Write-Output "    npon : draws=$($shapes['npon'].draws) digest=$($shapes['npon'].digest)"
            if ($shapes['npon'].digest -ne $shapes['rastercpu'].digest) {
                Write-Output "    --- DIFFER from the original-raster control: the neutral-program control changed the raster draw sequence, which it may not do"
                $rc = 1
            } else {
                Write-Output "    --- EQUAL: binding a second program for the prepass's empty draw changed nothing about the raster draw sequence"
            }
        }
        Write-Output "    rastercpu (the reference): draws=$($shapes['rastercpu'].draws) digest=$($shapes['rastercpu'].digest)"
    } else {
        Write-Output "    the original-raster control reported no digest, so the neutral-program run cannot be compared against it"
        $rc = 1
    }

    # AND THE SAME COMPARISON FOR THE AUXILIARY-CONTEXT CONTROL, which is the one
    # that matters most for it: that control takes a call AWAY from the main
    # context, so if the relocation disturbed the picture's commands at all it
    # would show up as a different raster draw sequence here. Equal means the
    # indexed draws the caller issued are the same ones in the same order as the
    # original-raster control's. It is NOT a claim about any pixel - this fixture
    # has no GPU and no image - only about the commands.
    Write-Output ""
    Write-Output "=== the draw sequence's shape under the auxiliary-context control (position source excluded)"
    if ($shapes.ContainsKey('rastercpu')) {
        if (-not $shapes.ContainsKey('axon')) {
            Write-Output "    axon : NO DIGEST REPORTED"
            $rc = 1
        } else {
            Write-Output "    axon : draws=$($shapes['axon'].draws) digest=$($shapes['axon'].digest)"
            if ($shapes['axon'].digest -ne $shapes['rastercpu'].digest) {
                Write-Output "    --- DIFFER from the original-raster control: the auxiliary-context control changed the raster draw sequence, which it may not do"
                $rc = 1
            } else {
                Write-Output "    --- EQUAL: moving the prepass's empty draw to a second context changed nothing about the raster draw sequence"
            }
        }
        Write-Output "    rastercpu (the reference): draws=$($shapes['rastercpu'].draws) digest=$($shapes['rastercpu'].digest)"
    } else {
        Write-Output "    the original-raster control reported no digest, so the auxiliary-context run cannot be compared against it"
        $rc = 1
    }

    foreach ($m in $mutants) {
        Write-Output ""
        Write-Output "=== mutant $($m.name) ($($m.define)): must FAIL - $($m.breaks)"
        $mobjs = @()
        foreach ($u in $units) {
            $leaf = (Split-Path -Leaf $u) -replace '\.c$', ''
            if ($u -like 'tools/profiling/native/*') {
                $obj = Join-Path $objDir "$leaf-$($m.name).o"
                & $CC @($flags + @("-D$($m.define)", '-c', $u, '-o', $obj))
                if ($LASTEXITCODE -ne 0) { throw "bgtfseamtest.ps1: mutant $($m.name): $u failed to compile (exit $LASTEXITCODE)" }
            } else {
                $obj = Join-Path $objDir "$leaf.o"
            }
            $mobjs += $obj
        }
        $mexe = Join-Path $OutDir "bgtfseamtest_$($m.name).exe"
        & $CC @($flags + @('-o', $mexe) + $mobjs)
        if ($LASTEXITCODE -ne 0) { throw "bgtfseamtest.ps1: mutant $($m.name) link failed (exit $LASTEXITCODE)" }
        $merr = Join-Path $objDir "stderr-mutant-$($m.name).txt"
        $mout = Join-Path $objDir "stdout-mutant-$($m.name).txt"
        & cmd /c "`"$mexe`" 1>`"$mout`" 2>`"$merr`""
        $code = $LASTEXITCODE
        $line = ''
        $first = $null
        if (Test-Path $mout) {
            $line  = (Get-Content $mout | Where-Object { $_ -match '^\d+ cases' })
            $first = (Get-Content $mout | Where-Object { $_ -match '^\s+FAIL' } | Select-Object -First 1)
        }
        Write-Output "--- exit $code (expected 1, a PASS here would be the fixture not biting)"
        Write-Output "--- $line"
        if ($first) { Write-Output "--- first failure:$first" }
        if ($code -ne 1) {
            Write-Output "--- MUTANT SURVIVED: the assertion for '$($m.breaks)' does not bite"
            $rc = 1
        }
    }
} finally {
    Set-Location $savedCwd
    $env:PATH = $savedPath
    foreach ($k in $savedEnv.Keys) {
        if ($null -eq $savedEnv[$k]) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
        else { Set-Item -Path "Env:$k" -Value $savedEnv[$k] }
    }
}

Write-Output ""
Write-Output "bgtfseamtest.ps1: $(if ($rc -eq 0) { 'the fixture passed and every mutant failed' } else { 'FAILED' })"
exit $rc
