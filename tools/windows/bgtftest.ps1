<#
.SYNOPSIS
    Build and run the background GPU clip prepass's offline fixture (v0.4).

.DESCRIPTION
    Run from an ordinary Windows PowerShell prompt at the repository root:

        .\tools\windows\bgtftest.ps1

    src/gfx/sl_gfx_bgtf.c compiled against the SYNTHETIC GL in
    tools/profiling/native/sl_gfx_bgtf_testgl.c - no window, no native graphics
    API, no renderer, no game - and linked against the REAL far-range
    arithmetic:

      src/gfx/sl_gfx_worldasset.c  sl_wa_far_range, so every FAR0 coefficient
                                   pair in the fixture is the one
                                   src/gfx/sl_gfx_dl.c computes rather than a
                                   number written into a test
      src/platform/sl_settings.c   the store sl_gfx_worldasset.c links against,
                                   pointed at a scratch SL_CONFIG under %TEMP%
                                   by the fixture, never the player's own config
      src/platform/sl_presentation.c, src/platform/sl_worker_pool.c
                                   the rest of that one link closure

    THE MODULE UNDER TEST HAS NO POLICY DEPENDENCY, which is why
    src/platform/sl_perf.c is absent here and present in bggputest.ps1: the
    candidate's selection is the SL_BG_TF_CLIP environment switch alone, with no
    settings row and no menu item, so a player session cannot reach it.

    WHAT IS CHECKED. Canonical output indexing - the prepass draws GL_POINTS
    over [0, nverts) from the pass's own vertex buffer at the item's stride, one
    output per input vertex, so the original indices index the output - asserted
    as the WHOLE submission in order rather than as a call count; the
    vertex-count ceiling refusing an absurd count before anything is allocated,
    and the largest accepted count still having a byte size that does not wrap;
    the reuse key, where an identical key inside one walk is served with no GL
    call at all and a new walk generation never reuses a previous walk's pool
    even when the transform compares equal; the three depth cases, with TWO
    DISTINCT FAR0 COEFFICIENT PAIRS TAKING DIFFERENT POOLS (one shared
    first-range buffer would hand a blended mesh the opaque mesh's
    coefficients) while FAR1 and SINGLE, which read neither coefficient, do not
    split the pool; program-0 raster routing, where the clip binding leaves the
    fixed pipeline current, points the position array at the pool as four
    floats, and leaves GL_ARRAY_BUFFER back at 0 so the pass's later client
    pointers are host addresses and not offsets; rasterizer discard bracketing
    the prepass and the unbind putting back the program, both buffer bindings
    and the indexed transform-feedback binding; every capability failure - each
    of the twenty-three entry points withheld in turn, a shader or program name
    of 0, a refused compile, a refused link, each of the eight uniforms withheld
    in turn - latching UNAVAILABLE and never being retried until
    sl_bgtf_invalidate; ALLOCATION ACCEPTANCE, where a buffer name of 0, an
    initial store that installs less than requested, a store that leaves the
    object empty, a growth that installs less, and a GL_BUFFER_SIZE query that
    writes nothing each REFUSE the draw, latch, leave no slot bindable, count no
    coverage and still put back the program, the array binding, the indexed
    transform-feedback binding and rasterizer discard - with the honest path
    beside them, querying exactly once per allocation and never per draw;
    context recreation, where invalidate makes no GL call and drops the program
    and the pool names, with a positive control proving the stale-name watchdog
    can fire; slot accounting, where a free slot and a PREVIOUS walk's slot are
    refills and only displacing a current-walk key is an eviction, and where a
    slot index held over from a previous walk is refused by the binding; the
    binding answering the POOL'S DEPTH rather than a success flag, and a bind
    counting a bind and NOT a draw; submissions counted per nonempty run at the
    draw seam, so two runs off one bind are two submissions and an empty run is
    none; and counter truthfulness, where nine refusals raise `refusals` and
    leave `prepasses`, `vertices`, `raster_draws` and `raster_indices` untouched.

    WHAT THIS SCRIPT DOES NOT COVER, and why there is a second one. This fixture
    does not link src/gfx/sl_gfx_dl.c, so it cannot observe the real mesh source
    selection or its restoration: the gate on the candidate's coverage
    (`g_bgtf_src`) and the restoration through the pass's own vertex-pointer
    helper live in that translation unit. tools/windows/bgtfseamtest.ps1 drives
    the real one.

    THE SELECTION IS A SEPARATE RUN PER SETTING, and that is a consequence of
    the module rather than a preference: sl_bgtf_opt_in parses SL_BG_TF_CLIP
    once into a static and sl_bgtf_invalidate deliberately does not re-read it,
    because the switch belongs to the launch and not to a context. One process
    can therefore observe one value, so this script runs the same binary in
    `env` mode once with the variable unset, once set to "0", once to "1" and
    once to an unrecognised "2" - which must be OFF and reported exactly once -
    plus one `envempty` run that sets an empty value in-process and reports
    which branch it reached. An OFF selection must leave the state UNTRIED (0)
    rather than latching UNAVAILABLE (-1), and must compile nothing.

    WHAT IS NOT CHECKED. The prepass program's own arithmetic is never
    executed: there is no GLSL compiler and no GPU in this process, and the stub
    only records that the program text was handed to glShaderSource. What the
    program computes, whether a driver's transform feedback writes the pool in
    the canonical order these indices then read, and whether the resulting image
    matches the original path are all unqualified.

    NO DIRECT READ-BACK IS PROVEN BY THE LINK, not by a counter. The synthetic
    GL defines no symbol named glFinish, glFlush, glMapBuffer, glUnmapBuffer,
    glGetBufferSubData or glReadPixels and no fence, sync or query entry point.
    A DIRECT call to one from the module would be an undefined reference over
    the whole translation unit and this fixture would not build. The production
    buffer diagnostic resolves glGetBufferSubData through sl_gl_proc inside its
    one capture, so the stub hands it back from a function whose own name is
    tfs_* - and only when a scenario asks for it.

    glGetError IS CALLED DIRECTLY AND SO IS A SYMBOL HERE, which is a change of
    contract rather than a relaxation. It is a GL 1.0 core export, and
    wglGetProcAddress is documented to answer NULL for core functions - so
    resolving it left the error state permanently NOT OBSERVABLE on a capable
    driver, and a gate that will not accept an unobservable error state refused
    every capture. It is not a read-back or a synchronisation point, so the
    link-time absence of those is untouched. "An off launch consumes no GL
    error" is therefore carried by the stub's call counter, which the off run
    requires to be zero, rather than by the absence of the symbol.

    THE PRODUCTION BUFFER DIAGNOSTIC (SL_BG_TF_DUMP), one scenario per run of
    this binary. The switch is parsed once into a static and the ONE capture
    attempt is spent for the life of the process, both on purpose, so a single
    process can observe one selector and one attempt. The scenarios are:

      off/zero       the diagnostic ABSENT or "0": nothing is resolved - in
                     particular glGetBufferSubData is never asked for - nothing
                     is allocated, no readback happens, no GL error is consumed,
                     no file is created, and the GL transcript the offer and the
                     capture add is empty. The driver HAS the entry point in
                     these runs, so the case is about the switch.
      disarm-*       a selector that cannot be read exactly - junk in a field, a
                     dmode outside 0/1/2, a missing field, trailing text, a
                     wildcard with junk after it - or one whose provenance is
                     absent, short, uppercase or all zeros: the diagnostic does
                     NOT arm, because recording the first draw that happens by
                     is the wrong answer to a mistake in the control.
      sel            every rejection that costs no attempt - an adjacent clock
                     in any field, another mesh, material or item, a stale walk
                     generation, a wrong-depth pool, FAR0's OTHER coefficient
                     pair, a slot index outside the cache, a missing CPU clip or
                     raw-depth array - and then ONE capture, whose record is
                     byte for byte the pool's own contents, whose CPU pool holds
                     x, y and w from the clip array and z from the UNCLAMPED
                     zraw, and whose indices are the run's exact submitted
                     order. Then a second matching draw and a later walk, both
                     of which capture nothing.
      clock          the mapping, literally: a screenshot reading read 140 /
                     frame 279 / cloud 139 is the DRAW-SITE tuple 139:139:139,
                     so 140, 279 and every adjacent value in every field are
                     refused and only 139:139:139 selects - with the pumped
                     frame 279 recorded beside it and never selected on.
      far0           the CPU z for FAR0, with the draw's own coefficient BITS in
                     the record. The CPU payload's z is clip[2] in EVERY range -
                     the unclamped depth is never substituted into it - and the
                     sidecar states that component by component.
      ulp, far1z     THE TWO POOLS DISTINCT, which is what makes a record
                     evidence. Every other complete scenario installs the pool
                     EQUAL to the CPU reference, so a GPU payload that received
                     the CPU bytes would read as correct and the comparison
                     would report zero either way. These install a pool that
                     differs in exactly ONE uint32 of one submitted vertex - one
                     ULP in x, and the unclamped depth's own bits in z - and
                     each payload is asserted bitwise against its own source,
                     with a fixed expected mismatch count, index position and
                     component in and out of process. far1z is the one that
                     catches a CPU z taken from zraw: the substitution would
                     make the two AGREE and the count would be 0.
                     AN UNEQUAL RECORD IS STILL SEALED COMPLETE. The seal is
                     about whether the bytes were obtained and written, never
                     about whether they agree.
      one failure per run, each a DISTINCT named outcome that spends the attempt
      and prohibits a retry: the readback entry point missing, a GL error raised
      across the read, a read that writes nothing, a short read whose tail is
      unwritten, a read that writes zeros, an index past the vertex count, an
      empty run, an index count over the maximum, a record already on disk, an
      existing record THIS PROCESS CANNOT READ (held open with read sharing
      denied - the case a probe-then-truncate writer destroyed), a directory
      that does not exist, the final sidecar name already taken, and - in a
      separately linked executable whose C library allocation, stdio and
      exclusive-create primitive are WRAPPED - an allocation that fails, a short
      write, a failed flush, a failed close, a path that becomes occupied
      CONCURRENTLY, and the same three write failures landing on the SIDECAR
      rather than on a payload. Each occupied path is asserted BYTE FOR BYTE
      unchanged, not merely present: a writer that truncated and rewrote the
      same length would pass a length check.

    THE SIDECAR IS STAGED AND THEN PUBLISHED, and three of those runs are about
    nothing else. The sidecar has to state whether the record is complete, so
    that statement is composed before the sidecar's own write can fail - which
    once meant a full, parseable, digest-matching object saying complete: true
    could survive a close that failed while the runtime reported failure. So it
    is written to <base>.json.staging and moved to <base>.json only after a
    successful write AND flush AND close. The three sidecar-failure runs require
    that no sidecar exists at the final name, and the close-failure run also
    requires that the UNPUBLISHED staging file DOES say complete: true - which is
    what makes the name, and not the text, the seal.

    THE RECORD IS THEN VERIFIED OUT OF PROCESS by
    tools/windows/bgtfdump-verify.py, which checks every declared length and
    sha256 against the files, the declared layout and index span, the absence of
    an unpublished staging file, and the sidecar's seal - and, on a record whose
    pools compare equal, PROVES the comparison contract bites by requiring it to
    report one ULP in each component, swapped components, a signed-zero bit
    difference, an unwritten tail and a changed index, on in-memory copies. On
    the distinct-pool records it is instead required to report the exact
    expected mismatch count.

    HOW THE MODULE'S GL INCLUDES ARE REPLACED. sl_gfx_bgtf.c has no test hook in
    its include block, and src/ is not edited to add one: the stub header is
    injected ahead of that one translation unit with -include, and it defines
    the host headers' own include guards so <windows.h> and <GL/gl.h> expand to
    nothing. Only that translation unit is compiled that way - the store, the
    presentation policy, the worker pool and the world-asset provider need the
    real windows.h - which is why each is compiled to its own object here
    instead of in one command.

    Exits 0 when every run passes, 1 otherwise. Same toolchain ladder as
    build.ps1 / bggputest.ps1.

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
    Write-Error "bgtftest.ps1: no mingw32 toolchain found. Run tools\windows\setup.ps1."
}
$CC = Join-Path $mingwBin 'i686-w64-mingw32-gcc.exe'

$objDir = Join-Path $OutDir 'bgtftest-obj'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $objDir | Out-Null
$exe = Join-Path $OutDir 'bgtftest.exe'

# The mock set's own flags (tools/profiling/check.py, native_mocks). No
# SL_*_SELFTEST is defined anywhere here, which would collide mains.
$flags = @('-m32', '-O2', '-Wall', '-Wextra', '-Werror=implicit-function-declaration')
$inc   = @('-Isrc/gfx', '-Isrc/platform', '-Itools/profiling/native')
# Injected ahead of the module under test only; see the note above.
$shim  = @('-include', 'tools/profiling/native/sl_gfx_bgtf_testgl.h')

# (source, extra flags). The module under test is the only one that gets the
# shim: the store, the presentation policy, the worker pool and the world-asset
# provider include the real windows.h.
$units = @(
    @{ src = 'src/gfx/sl_gfx_bgtf.c';                        extra = $shim },
    @{ src = 'tools/profiling/native/sl_gfx_bgtf_testgl.c';  extra = @() },
    @{ src = 'tools/profiling/native/test_sl_gfx_bgtf.c';    extra = $inc },
    @{ src = 'src/gfx/sl_gfx_worldasset.c';                  extra = @() },
    @{ src = 'src/platform/sl_settings.c';                   extra = @() },
    @{ src = 'src/platform/sl_presentation.c';               extra = @() },
    @{ src = 'src/platform/sl_worker_pool.c';                extra = @() }
)

# The runs, in order. Each is a separate process because the module parses
# SL_BG_TF_CLIP once per launch (see the note above). `switch` is $null for a
# variable that must be ABSENT, and `rcpu` / `sonly` are the same for
# SL_BG_TF_RASTER_CPU and SL_BG_TF_STATE_ONLY.
#
# ---- THE STATE-ONLY CONTROL'S RUNS (SL_BG_TF_STATE_ONLY) -----------------
#
# ONE SETTING PER LAUNCH, because that switch is parsed once into a static like
# the other two. The control keeps the whole prepass bracket and omits ONE call,
# the prepass's own glDrawArrays, so the claim is a DIFFERENCE between launches
# and no single process can state it:
#
#   so-ref    the same scenario with the control ABSENT: one counted draw over
#             nverts vertices, a written pool, a successful bind.
#   so-only   the control SELECTED: zero draws, nothing transformed, no pool
#             written, and the slot the prepass returned REFUSED by the binding.
#
# Each prints `SOSETUP calls=N digest=...` over its prepass transcript with the
# DRAWARRAYS records EXCLUDED, and the two are required to be EQUAL below. That
# equality is what establishes that the program, the uniforms, the pool and its
# verification, the input position pointer, the dropped element binding,
# rasterizer discard and the transform-feedback bracket are the same calls with
# the same arguments in the same order - a claim neither run can make about
# itself. A digest that included the draw could never show it.
#
# THE COUPLING IS PART OF THE ANSWER, and the four grammar runs are about that:
# the control answers 1 only with SL_BG_TF_CLIP=1 AND SL_BG_TF_RASTER_CPU=1, so
# the two miscombination runs require the answer 0, require the launch to behave
# as the arm those two switches selected, and require the reason to be reported
# exactly once rather than once a draw.
$runs = @(
    @{ name = 'main';        switch = '1';  args = @();            must = 0 },

    # ---- THE SELECTION RUNS, AND WHY THREE OF THEM NAME NO ARM -----------
    #
    # `arreal` is this run's SL_BG_TF_AUX_REAL: 'ABSENT' removes the key for
    # that run, and anything else (including the field being omitted) takes the
    # table's own OFF token, set per run below.
    #
    # THE TABLE-WIDE OFF TOKEN CANNOT REACH THE PRODUCTION HALF, which is a
    # fact about the selection rather than a preference here. The off token is
    # a DEVELOPER selection of the main-context prepass, and the production
    # route refuses any launch that named an arm at all - so a run that asks
    # mode env to prove the row's own selection is ready would be asserting it
    # under the one setting that excludes it. Measured: with the token set
    # table-wide, env-unset, env-two and env-empty each failed six checks whose
    # subject is the production route, and clearing it for those three alone
    # left one failure, which is item 3's missing auxiliary capability.
    #
    # ONLY THE THREE PRODUCTION RUNS CLEAR IT. env-zero is the explicit
    # developer OFF and env-one is the developer ON; both are about the
    # MAIN-CONTEXT prepass this table was written for, so both keep the token
    # and keep that diagnostic coverage exactly as it was.
    @{ name = 'env-unset';   switch = $null; args = @('env', '0'); must = 0;
       arreal = 'ABSENT' },
    @{ name = 'env-zero';    switch = '0';  args = @('env', '0');  must = 0 },
    @{ name = 'env-one';     switch = '1';  args = @('env', '1');  must = 0 },
    @{ name = 'env-two';     switch = '2';  args = @('env', '0');  must = 0;
       arreal = 'ABSENT' },
    @{ name = 'env-empty';   switch = '1';  args = @('envempty');  must = 0;
       arreal = 'ABSENT' },

    # the state-only switch's grammar, with both required switches present
    @{ name = 'so-env-unset'; switch = '1'; rcpu = '1'; sonly = $null;
       args = @('envso', '0'); must = 0 },
    @{ name = 'so-env-zero';  switch = '1'; rcpu = '1'; sonly = '0';
       args = @('envso', '0'); must = 0 },
    @{ name = 'so-env-one';   switch = '1'; rcpu = '1'; sonly = '1';
       args = @('envso', '1'); must = 0 },
    @{ name = 'so-env-two';   switch = '1'; rcpu = '1'; sonly = '2';
       args = @('envso', '0'); must = 0 },

    # and the two MISCOMBINATIONS, which must answer 0 and change nothing
    @{ name = 'so-no-clip';   switch = $null; rcpu = '1'; sonly = '1';
       args = @('envso', '0'); must = 0 },
    @{ name = 'so-no-rcpu';   switch = '1'; rcpu = $null; sonly = '1';
       args = @('envso', '0'); must = 0 },

    # the two arms of the comparison
    @{ name = 'so-ref';  switch = '1'; rcpu = '1'; sonly = $null;
       args = @('so', 'ref');  must = 0 },
    @{ name = 'so-only'; switch = '1'; rcpu = '1'; sonly = '1';
       args = @('so', 'only'); must = 0 },

    # ---- THE POSITION-ONLY-INPUT CONTROL (SL_BG_TF_POS_INPUT) -------------
    #
    # It keeps the prepass draw and its transform-feedback writes and narrows
    # the CLIENT ARRAYS that draw dereferences to the position array the program
    # reads. One setting per launch, as for the other three switches.
    #
    # The grammar runs below say what EACH of the two exclusive controls must
    # answer, because the exclusivity is a fact about the pair: `pi` is the
    # expected answer from SL_BG_TF_POS_INPUT and `so` from
    # SL_BG_TF_STATE_ONLY, and on the ambiguous launch both must be 0 with
    # neither silently winning.
    @{ name = 'pi-env-unset'; switch = '1'; rcpu = '1'; pin = $null;
       args = @('envpi', '0', '0'); must = 0 },
    @{ name = 'pi-env-zero';  switch = '1'; rcpu = '1'; pin = '0';
       args = @('envpi', '0', '0'); must = 0 },
    @{ name = 'pi-env-one';   switch = '1'; rcpu = '1'; pin = '1';
       args = @('envpi', '1', '0'); must = 0 },
    @{ name = 'pi-env-two';   switch = '1'; rcpu = '1'; pin = '2';
       args = @('envpi', '0', '0'); must = 0 },

    # the two MISCOMBINATIONS, which must answer 0 and change nothing
    @{ name = 'pi-no-clip';   switch = $null; rcpu = '1'; pin = '1';
       args = @('envpi', '0', '0'); must = 0 },
    @{ name = 'pi-no-rcpu';   switch = '1'; rcpu = $null; pin = '1';
       args = @('envpi', '0', '0'); must = 0 },

    # AND THE AMBIGUITY. Both exclusive controls asked for: both answer 0, the
    # combination is reported once, and the launch is the ordinary candidate arm.
    @{ name = 'pi-ambiguous'; switch = '1'; rcpu = '1'; pin = '1'; sonly = '1';
       args = @('envpi', '0', '0'); must = 0 },

    # the control's own scenarios: the isolation, the cadence and the refusal,
    # then the three ways the required state cannot be accounted for
    @{ name = 'pi-main';     switch = '1'; rcpu = '1'; pin = '1';
       args = @('pi', 'main');    must = 0 },
    @{ name = 'pi-noapi';    switch = '1'; rcpu = '1'; pin = '1';
       args = @('pi', 'noapi');   must = 0 },
    @{ name = 'pi-nocount';  switch = '1'; rcpu = '1'; pin = '1';
       args = @('pi', 'nocount'); must = 0 },
    @{ name = 'pi-toomany';  switch = '1'; rcpu = '1'; pin = '1';
       args = @('pi', 'toomany'); must = 0 },

    # ---- THE COORDINATE-SET DOMAIN AND THE VALIDATED SELECTOR -------------
    #
    # THE DOMAIN. glClientActiveTexture selects among texture COORDINATE SETS
    # (GL_MAX_TEXTURE_COORDS), and the OpenGL 2.1 spec allows the conventional
    # fixed-function unit count (GL_MAX_TEXTURE_UNITS) to be SMALLER - sections
    # 2.7 the coordinate sets, 2.8 the selector's range over them, 2.11.2 the
    # conventional units kept apart. `pi coords` reports two units and four
    # coordinate sets, enables a coordinate array on set 3 - above the unit
    # count - and inherits the selector on set 3, so a sweep bounded by the unit
    # count would leave that array feeding the real prepass draw.
    @{ name = 'pi-coords';   switch = '1'; rcpu = '1'; pin = '1';
       args = @('pi', 'coords');  must = 0 },

    # THE SELECTOR THE RESTORE DEPENDS ON, validated before any selector or
    # array-state mutation. Each of the three rejected answers must leave the
    # context unchanged, cost not one selector, disable or enable call, count a
    # truthful refusal and keep the real draw; the fourth is the boundary that
    # must be ACCEPTED.
    @{ name = 'pi-selnone';  switch = '1'; rcpu = '1'; pin = '1';
       args = @('pi', 'selnone'); must = 0 },
    @{ name = 'pi-selbad';   switch = '1'; rcpu = '1'; pin = '1';
       args = @('pi', 'selbad');  must = 0 },
    @{ name = 'pi-selover';  switch = '1'; rcpu = '1'; pin = '1';
       args = @('pi', 'selover'); must = 0 },
    @{ name = 'pi-selmax';   switch = '1'; rcpu = '1'; pin = '1';
       args = @('pi', 'selmax');  must = 0 }
)

# THE MUTANTS: the same unmodified src/gfx/sl_gfx_bgtf.c against a fake driver
# with ONE injected fault, each of which MUST make the suite fail. A suite that
# passes on the first build has proved nothing until it is shown able to fail,
# and a mutant that passed would name an assertion that does not bite. The
# faults are described above sl_gfx_bgtf_testgl.c's mutant block; the property
# each one is expected to break is named here.
$mutants = @(
    @{ name = 'drawrange'; define = 'BGTF_MUT_DRAWRANGE';
       breaks = 'canonical order: the prepass range no longer starts at vertex 0' },
    @{ name = 'vpsize';    define = 'BGTF_MUT_VPSIZE';
       breaks = 'the raster position array is three floats, not a clip position' },
    @{ name = 'arrbuf';    define = 'BGTF_MUT_ARRBUF';
       breaks = 'GL_ARRAY_BUFFER is left bound, so later client pointers are offsets' },
    @{ name = 'locshift';  define = 'BGTF_MUT_LOCSHIFT';
       breaks = 'the transform uploads land at the wrong uniform locations' },
    @{ name = 'nodiscard'; define = 'BGTF_MUT_NODISCARD';
       breaks = 'the prepass draw runs without rasterizer discard' },
    # THE ALLOCATION MUTANT, for the behaviour this round added. The fake
    # driver's GL_BUFFER_SIZE reports what was REQUESTED rather than what was
    # installed - a driver whose metadata lies - so every short, empty or
    # unverifiable allocation reads as accepted. It must break the whole
    # allocation block: without it those assertions could pass against a module
    # that never looked.
    @{ name = 'allocok';   define = 'BGTF_MUT_ALLOCOK';
       breaks = 'GL_BUFFER_SIZE reports the request, so an undersized or empty pool is stamped valid and drawn from' },
    # THE CPU-FOR-GPU SUBSTITUTION. The scenario installs the CPU reference's
    # bytes into the pool instead of the distinct pattern it meant to install -
    # which is what a record whose GPU payload received the CPU buffer would
    # look like. Every assertion over the GPU payload reads its expectation from
    # d_expect_gpu, which this mutant does NOT touch, so the exact byte
    # comparison, the in-fixture mismatch count and the out-of-process
    # --expect-mismatches must all fail. It runs in `dump ulp` mode rather than
    # the main suite, because the main suite has no capture in it.
    @{ name = 'gpuiscpu';  define = 'BGTF_MUT_GPUISCPU';
       args = @('dump', 'ulp'); armdump = $true;
       breaks = 'the GPU payload receives the CPU reference bytes, so a record of the CPU buffer written twice would read as a measurement of the pool' },
    # THE STATE-ONLY CONTROL'S TWO. Both run in that control's own modes, because
    # the main suite contains no state-only prepass.
    #
    # SONODRAW is the one that keeps "zero draws" from being an arrangement: it
    # stops the fake driver counting glDrawArrays at all, and the REFERENCE arm
    # requires exactly one counted draw over nverts vertices. A counter that
    # never moves would otherwise satisfy the state-only arm's zero.
    @{ name = 'sonodraw'; define = 'BGTF_MUT_SONODRAW';
       args = @('so', 'ref'); clip = '1'; rcpu = '1'; sonly = $null;
       breaks = 'the prepass draw is not counted, so a zero draw count would mean nothing on either arm' },
    # SONOTFBIND makes the transform-feedback binding - part of the setup the
    # control is required to KEEP - unobservable, so the bracket could have lost
    # it without the state-only arm noticing.
    @{ name = 'sonotfbind'; define = 'BGTF_MUT_SONOTFBIND';
       args = @('so', 'only'); clip = '1'; rcpu = '1'; sonly = '1';
       breaks = 'the indexed transform-feedback binding is not counted, so a setup that lost it would still pass the state-only arm' },
    # THE POSITION-ONLY-INPUT CONTROL'S THREE, each the shape of one way an
    # exact restoration can go wrong. All run in that control's own mode, which
    # is the only one with an isolated draw in it.
    @{ name = 'pinorestore'; define = 'BGTF_MUT_PI_NORESTORE';
       args = @('pi', 'main'); clip = '1'; rcpu = '1'; pin = '1';
       breaks = 'one disabled array is never put back, so the state after the draw differs from the state before it - which is what "restore EXACTLY" has to exclude' },
    @{ name = 'piblind';     define = 'BGTF_MUT_PI_BLINDENABLE';
       args = @('pi', 'main'); clip = '1'; rcpu = '1'; pin = '1';
       breaks = 'the restore enables the whole set rather than the set that was found, so an array that was OFF comes back ON and the following raster gets an array the pass never asked for' },
    @{ name = 'piwrongunit'; define = 'BGTF_MUT_PI_WRONGUNIT';
       args = @('pi', 'main'); clip = '1'; rcpu = '1'; pin = '1';
       breaks = 'the per-unit selector lands one unit away, so a save of unit 1 restores unit 2 - the defect a single-unit fixture could not catch at all' },
    # AND THREE FOR THE BOUNDED DOMAIN AND THE VALIDATED SELECTOR. Each runs in
    # the mode whose assertions it is aimed at, because the main suite reports
    # the full coordinate domain and a legal selector and so contains neither
    # condition.
    @{ name = 'piunitsdomain'; define = 'BGTF_MUT_PI_UNITSDOMAIN';
       args = @('pi', 'coords'); clip = '1'; rcpu = '1'; pin = '1';
       breaks = 'GL_MAX_TEXTURE_COORDS answers the smaller conventional unit count, so the sweep is bounded by GL_MAX_TEXTURE_UNITS again and the coordinate array on set 3 survives into the real prepass draw' },
    @{ name = 'piselsane';     define = 'BGTF_MUT_PI_SELSANE';
       args = @('pi', 'selbad'); clip = '1'; rcpu = '1'; pin = '1';
       breaks = 'an illegal GL_CLIENT_ACTIVE_TEXTURE answer is repaired into a legal one before the module sees it, so a module that never validated the saved selector would be indistinguishable from one that did' },
    @{ name = 'pisaveplausible'; define = 'BGTF_MUT_PI_SAVEPLAUSIBLE';
       args = @('pi', 'selnone'); clip = '1'; rcpu = '1'; pin = '1';
       breaks = 'a GL_CLIENT_ACTIVE_TEXTURE query that writes NOTHING instead writes a plausible unit, which is what a save seeded with 0 rather than an impossible sentinel would read as - the no-answer refusal becomes unreachable' }
)

# ---- THE PRODUCTION BUFFER DIAGNOSTIC'S SCENARIOS ------------------------
#
# ONE PER PROCESS, and that is the module's shape rather than a preference: the
# selector is parsed once into a static and the ONE capture attempt is spent for
# the life of the process on purpose, so a failure cannot retry at another draw,
# frame or slot. See the comment-based help above for what each one is.
#
#   dump    the SL_BG_TF_DUMP value, or $null for ABSENT
#   sha     'real' supplies the executable's and the module source's own
#           sha256; 'none', 'short', 'upper' and 'zero' are the four ways a
#           supplied provenance is refused
#   dir     'fresh' makes an empty scratch directory; 'missing' names one that
#           does not exist, which is how a create failure is produced from a
#           real condition rather than from a hook in the product
#   verify  how bgtfdump-verify.py is asked about the result
#   wrap    needs the WRAPPED executable, whose C library allocation and stdio
#           are replaced at the link
$dumpSel  = '139:139:139:29:29:*:fixtureitem'
$dumpOne  = '139:139:139:29:29:2'
$dumpFar0 = '139:139:139:29:29:1'

$dumpRuns = @(
    # the switch off, with the entry point present in the driver
    @{ name = 'dump-absent'; dump = $null;  sha = 'real'; args = @('dump', 'off');
       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-zero';   dump = '0';    sha = 'real'; args = @('dump', 'off');
       dir = 'fresh'; verify = 'none' },

    # a control that cannot be read exactly, and a provenance that cannot be
    # accepted: neither arms, because recording the first draw that happens by
    # is the wrong answer to a mistake
    @{ name = 'dump-bad-field';    dump = '139:139:garbage:29:29:2'; sha = 'real';
       args = @('dump', 'disarm', 'a field that is not a decimal or a wildcard');
       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-bad-dmode';    dump = '139:139:139:29:29:3'; sha = 'real';
       args = @('dump', 'disarm', 'a dmode outside SL_BGGPU_DEPTH_*');
       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-short-selector'; dump = '139:139:139:29:29'; sha = 'real';
       args = @('dump', 'disarm', 'a selector with only five fields');
       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-trailing-junk'; dump = '139:139:139:29:29:2x'; sha = 'real';
       args = @('dump', 'disarm', 'trailing text after a field');
       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-star-junk';    dump = '139:139:139:29:*7:2'; sha = 'real';
       args = @('dump', 'disarm', 'a wildcard with a digit after it');
       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-wrap-field';   dump = '139:139:4294967296:29:29:2'; sha = 'real';
       args = @('dump', 'disarm', 'a field that would wrap 32 bits');
       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-no-sha';       dump = $dumpOne; sha = 'none';
       args = @('dump', 'disarm', 'no provenance supplied');
       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-short-sha';    dump = $dumpOne; sha = 'short';
       args = @('dump', 'disarm', 'a provenance that is not 64 characters');
       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-upper-sha';    dump = $dumpOne; sha = 'upper';
       args = @('dump', 'disarm', 'a provenance in uppercase hex');
       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-zero-sha';     dump = $dumpOne; sha = 'zero';
       args = @('dump', 'disarm', 'an all-zero provenance, which is what an unset field looks like');
       dir = 'fresh'; verify = 'none' },

    # selection, the clock mapping, and FAR0's reading of the CPU z
    @{ name = 'dump-sel';   dump = $dumpSel;  sha = 'real'; args = @('dump', 'sel');
       dir = 'fresh'; verify = 'complete' },
    @{ name = 'dump-clock'; dump = $dumpOne;  sha = 'real'; args = @('dump', 'clock');
       dir = 'fresh'; verify = 'complete' },
    @{ name = 'dump-far0';  dump = $dumpFar0; sha = 'real'; args = @('dump', 'far0');
       dir = 'fresh'; verify = 'complete' },

    # THE TWO POOLS DISTINCT, which is what makes a record evidence rather than
    # an arrangement that cannot fail. The scenarios above install the pool
    # EQUAL to the CPU reference, so a GPU file that received the CPU bytes
    # would read as correct and the comparison would report zero either way.
    # These two differ in exactly one uint32 of one submitted vertex, the
    # fixture asserts each payload bitwise against its own source, and the
    # verifier is required to report exactly ONE mismatch. An unequal record is
    # still sealed COMPLETE: the seal is about whether the bytes were obtained
    # and written, never about whether they agree.
    #
    # --prove-contract is NOT passed here, and that is arithmetic rather than
    # caution: the corruption battery tests "one more differing pattern than
    # before" and is only meaningful on a baseline of zero.
    @{ name = 'dump-ulp';   dump = $dumpOne;  sha = 'real'; args = @('dump', 'ulp');
       dir = 'fresh'; verify = 'complete'; mismatches = 1; prove = $false },
    # The one that catches a CPU payload whose z came from the unclamped depth
    # instead of the clip array: the pool holds zraw's bits at that vertex, so a
    # substituted CPU z would make the two AGREE and the count would be 0.
    @{ name = 'dump-far1z'; dump = $dumpOne;  sha = 'real'; args = @('dump', 'far1z');
       dir = 'fresh'; verify = 'complete'; mismatches = 1; prove = $false },
    # A slot sized for a LARGER earlier item, taken by a smaller one: the
    # readback's length must be the ACTIVE count and not the storage, or the
    # previous item's bytes enter the record.
    @{ name = 'dump-capacity'; dump = $dumpOne; sha = 'real';
       args = @('dump', 'capacity'); dir = 'fresh'; verify = 'complete' },

    # one named failure per run, each spending the attempt and prohibiting a retry
    # The entry point is missing, so the refusal lands before any byte exists:
    # there is nothing to preserve and nothing is written.
    @{ name = 'dump-noapi';       dump = $dumpOne; sha = 'real';
       args = @('dump', 'noapi');       dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-readerr';     dump = $dumpOne; sha = 'real';
       args = @('dump', 'readerr');     dir = 'fresh'; verify = 'incomplete' },
    @{ name = 'dump-readnothing'; dump = $dumpOne; sha = 'real';
       args = @('dump', 'readnothing'); dir = 'fresh'; verify = 'incomplete' },
    @{ name = 'dump-readpartial'; dump = $dumpOne; sha = 'real';
       args = @('dump', 'readpartial'); dir = 'fresh'; verify = 'incomplete' },
    @{ name = 'dump-readzero';    dump = $dumpOne; sha = 'real';
       args = @('dump', 'readzero');    dir = 'fresh'; verify = 'incomplete' },
    @{ name = 'dump-badindex';    dump = $dumpOne; sha = 'real';
       args = @('dump', 'badindex');    dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-nocount';     dump = $dumpOne; sha = 'real';
       args = @('dump', 'nocount');     dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-overflow';    dump = $dumpOne; sha = 'real';
       args = @('dump', 'overflow');    dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-exists';      dump = $dumpOne; sha = 'real';
       args = @('dump', 'exists');      dir = 'fresh'; verify = 'skip' },
    @{ name = 'dump-nodir';       dump = $dumpOne; sha = 'real';
       args = @('dump', 'nodir');       dir = 'missing'; verify = 'none' },

    # ---- THE EXCLUSIVE-CREATION CASES ----------------------------------
    #
    # An EXISTING FILE THIS PROCESS CANNOT READ. The fixture holds it open with
    # read sharing denied, which is the condition a probe-then-truncate writer
    # lost to: the read probe failed, the path was taken for free, and the
    # truncating open that followed destroyed the record. The exclusive create
    # never asks about readability, and the bytes are asserted unchanged.
    @{ name = 'dump-unreadable';  dump = $dumpOne; sha = 'real';
       args = @('dump', 'unreadable'); dir = 'fresh'; verify = 'skip' },
    # THE FINAL SIDECAR NAME ALREADY TAKEN: the payloads are written, the
    # staging sidecar is written whole, and the publication refuses without
    # touching what is there.
    @{ name = 'dump-sidecarexists'; dump = $dumpOne; sha = 'real';
       args = @('dump', 'sidecarexists'); dir = 'fresh'; verify = 'skip' },

    # the host-I/O failures, in the WRAPPED executable
    @{ name = 'dump-allocfail';  dump = $dumpOne; sha = 'real'; wrap = $true;
       args = @('dump', 'allocfail');  dir = 'fresh'; verify = 'none' },
    @{ name = 'dump-shortwrite'; dump = $dumpOne; sha = 'real'; wrap = $true;
       args = @('dump', 'shortwrite'); dir = 'fresh'; verify = 'skip' },
    @{ name = 'dump-flushfail';  dump = $dumpOne; sha = 'real'; wrap = $true;
       args = @('dump', 'flushfail');  dir = 'fresh'; verify = 'skip' },
    @{ name = 'dump-closefail';  dump = $dumpOne; sha = 'real'; wrap = $true;
       args = @('dump', 'closefail');  dir = 'fresh'; verify = 'skip' },
    # A PATH THAT BECOMES OCCUPIED CONCURRENTLY. The wrapped create primitive
    # makes the file appear immediately before delegating, so it comes into
    # existence inside the window a probe would already have declared empty.
    @{ name = 'dump-race';       dump = $dumpOne; sha = 'real'; wrap = $true;
       args = @('dump', 'race');       dir = 'fresh'; verify = 'skip' },
    # THE THREE SIDECAR-WRITE FAILURES, which are what the publication contract
    # exists for. Three payload writes are let through so the fault lands on the
    # staging sidecar; none of the three may leave a sidecar at the final name,
    # and the close-failure case is the one whose staging file DOES say
    # complete: true - the proof that the name and not the text is the seal.
    @{ name = 'dump-sidecarshortwrite'; dump = $dumpOne; sha = 'real'; wrap = $true;
       args = @('dump', 'sidecarshortwrite'); dir = 'fresh'; verify = 'skip' },
    @{ name = 'dump-sidecarflushfail';  dump = $dumpOne; sha = 'real'; wrap = $true;
       args = @('dump', 'sidecarflushfail');  dir = 'fresh'; verify = 'skip' },
    @{ name = 'dump-sidecarclosefail';  dump = $dumpOne; sha = 'real'; wrap = $true;
       args = @('dump', 'sidecarclosefail');  dir = 'fresh'; verify = 'skip' }
)

$rc = 0
$savedPath   = $env:PATH
$savedCwd    = (Get-Location).Path
$savedSwitch = $env:SL_BG_TF_CLIP
$savedRcpu   = $env:SL_BG_TF_RASTER_CPU
$savedSonly  = $env:SL_BG_TF_STATE_ONLY
$savedPin    = $env:SL_BG_TF_POS_INPUT
$savedDump   = $env:SL_BG_TF_DUMP
$savedDumpDir = $env:SL_BG_TF_DUMP_DIR
$savedExeSha = $env:SL_BG_TF_DUMP_EXE_SHA
$savedSrcSha = $env:SL_BG_TF_DUMP_SRC_SHA
$savedArReal = $env:SL_BG_TF_AUX_REAL
$savedArSync = $env:SL_BG_TF_AUX_SYNC
try {
    $env:PATH = "$mingwBin;$env:PATH"
    Set-Location $repo
    # THE DIAGNOSTIC IS ABSENT FOR EVERY RUN THAT IS NOT ONE OF ITS OWN. The
    # main suite and the selection runs are about the candidate, and an armed
    # diagnostic in them would be a second thing happening.
    Remove-Item Env:SL_BG_TF_DUMP -ErrorAction SilentlyContinue
    Remove-Item Env:SL_BG_TF_DUMP_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:SL_BG_TF_DUMP_EXE_SHA -ErrorAction SilentlyContinue
    Remove-Item Env:SL_BG_TF_DUMP_SRC_SHA -ErrorAction SilentlyContinue
    # THE MAIN-CONTEXT PREPASS IS SELECTED BY NAME FOR EVERY RUN OF THIS TABLE.
    #
    # Every run below is about the prepass this module issues on the MAIN GL
    # context - its program, its pool, its bindings, its refusals, its
    # diagnostics - and with the opt-in switch given and no arm named, that is
    # no longer what an opted-in launch reaches: it reaches the OPT-IN CANDIDATE,
    # which is the auxiliary-context arm. The off token is the developer control
    # the candidate keeps for exactly this, so naming it here keeps all of these
    # runs measuring what they were written to measure rather than silently
    # becoming launches of another arm.
    #
    # THE SYNCHRONISATION KEY IS CLEARED for the same reason it is set nowhere
    # below: it belongs to the auxiliary arms, and an inherited value would
    # refuse an arm these runs do not use.
    #
    # THE CANDIDATE'S OWN LAUNCHES ARE NOT IN THIS TABLE. They are one process
    # per combination with their own environment - mode 'optin' - because the
    # arbiter decides once per launch; see the opt-in block in
    # tools/profiling/native/test_sl_gfx_bgtf.c.
    $env:SL_BG_TF_AUX_REAL = 'OFF'
    Remove-Item Env:SL_BG_TF_AUX_SYNC -ErrorAction SilentlyContinue
    $objs = @()
    foreach ($u in $units) {
        $obj = Join-Path $objDir ((Split-Path -Leaf $u.src) -replace '\.c$', '.o')
        & $CC @($flags + $u.extra + @('-c', $u.src, '-o', $obj))
        if ($LASTEXITCODE -ne 0) { throw "bgtftest.ps1: $($u.src) failed to compile (exit $LASTEXITCODE)" }
        $objs += $obj
    }
    & $CC @($flags + @('-o', $exe) + $objs)
    if ($LASTEXITCODE -ne 0) { throw "bgtftest.ps1: link failed (exit $LASTEXITCODE)" }

    # The state-only arms' setup digests, keyed by run name. Collected from what
    # each run actually printed and compared after the loop; a run that reports
    # none is a failure, because a missing digest would otherwise read as
    # agreement.
    $soShapes = @{}

    foreach ($r in $runs) {
        if ($null -eq $r.switch) { $shown = '(unset)' } else { $shown = "'" + $r.switch + "'" }
        if ($null -eq $r.rcpu)  { $rcpuShown  = '(unset)' } else { $rcpuShown  = "'" + $r.rcpu  + "'" }
        if ($null -eq $r.sonly) { $sonlyShown = '(unset)' } else { $sonlyShown = "'" + $r.sonly + "'" }
        if ($null -eq $r.pin)   { $pinShown   = '(unset)' } else { $pinShown   = "'" + $r.pin   + "'" }

        # THE ARM TOKEN IS PER RUN, and applied here rather than once before the
        # table, so that the three production runs can name no arm at all while
        # every other run keeps the main-context diagnostic's off token.
        if ($r.arreal -eq 'ABSENT') {
            Remove-Item Env:SL_BG_TF_AUX_REAL -ErrorAction SilentlyContinue
            $arShown = '(absent)'
        } else {
            $env:SL_BG_TF_AUX_REAL = 'OFF'
            $arShown = "'OFF'"
        }
        # The synchronisation key belongs to the auxiliary arms and is cleared
        # for this whole table; it is reported so the line states the launch
        # rather than leaving a reader to assume it.
        if ($null -eq $env:SL_BG_TF_AUX_SYNC -or $env:SL_BG_TF_AUX_SYNC -eq '') {
            $syncShown = '(absent)'
        } else {
            $syncShown = "'" + $env:SL_BG_TF_AUX_SYNC + "'"
        }
        Write-Output ""
        Write-Output "=== run $($r.name): SL_BG_TF_CLIP=$shown SL_BG_TF_RASTER_CPU=$rcpuShown SL_BG_TF_STATE_ONLY=$sonlyShown SL_BG_TF_POS_INPUT=$pinShown SL_BG_TF_AUX_REAL=$arShown SL_BG_TF_AUX_SYNC=$syncShown"
        if ($null -eq $r.switch) { Remove-Item Env:SL_BG_TF_CLIP -ErrorAction SilentlyContinue }
        else { $env:SL_BG_TF_CLIP = $r.switch }
        if ($null -eq $r.rcpu) { Remove-Item Env:SL_BG_TF_RASTER_CPU -ErrorAction SilentlyContinue }
        else { $env:SL_BG_TF_RASTER_CPU = $r.rcpu }
        if ($null -eq $r.sonly) { Remove-Item Env:SL_BG_TF_STATE_ONLY -ErrorAction SilentlyContinue }
        else { $env:SL_BG_TF_STATE_ONLY = $r.sonly }
        if ($null -eq $r.pin) { Remove-Item Env:SL_BG_TF_POS_INPUT -ErrorAction SilentlyContinue }
        else { $env:SL_BG_TF_POS_INPUT = $r.pin }

        # The module's own refusal and selection lines go to stderr. cmd owns
        # the redirect (PowerShell 5.1 would turn them into error records), and
        # the stderr of the unrecognised-value run is KEPT, because "reported
        # once" is a property of it. The verdict is on stdout, which is captured
        # rather than left on the console because the state-only arms' digests
        # are read back out of it.
        $err = Join-Path $objDir "stderr-$($r.name).txt"
        $out = Join-Path $objDir "stdout-$($r.name).txt"
        $argline = ($r.args | ForEach-Object { """$_""" }) -join ' '
        & cmd /c "`"$exe`" $argline 1>`"$out`" 2>`"$err`""
        $code = $LASTEXITCODE
        if (Test-Path $out) { Get-Content $out | ForEach-Object { Write-Output $_ } }
        Write-Output "--- exit $code (expected $($r.must))"
        if ($code -ne $r.must) { $rc = 1 }

        if ($r.name -eq 'so-ref' -or $r.name -eq 'so-only') {
            # THE FIRST digest of the run, which is the one the shared scenario
            # printed for its first prepass window. Both arms print exactly one.
            if (Test-Path $out) {
                $sl = (Get-Content $out | Where-Object { $_ -match 'SOSETUP calls=\d+ digest=' } | Select-Object -First 1)
                if ($sl -match 'calls=(\d+) digest=([0-9a-f]+)') {
                    $soShapes[$r.name] = @{ calls = $Matches[1]; digest = $Matches[2] }
                }
            }
            # AND THE DRAW COUNTS, read from the fixture's own report rather
            # than inferred: the reference arm must have counted one prepass
            # draw and the state-only arm none.
            $wantDraws = if ($r.name -eq 'so-only') { 0 } else { 1 }
            $drawLine = ''
            if (Test-Path $out) {
                $drawLine = (Get-Content $out | Where-Object { $_ -match 'glDrawArrays calls:' } | Select-Object -First 1)
            }
            Write-Output "--- expected prepass draws on this arm: $wantDraws"
            if ($drawLine) { Write-Output "--- fixture reported:$drawLine" }
        }

        if ($r.name -eq 'so-no-clip' -or $r.name -eq 'so-no-rcpu') {
            # A MISCOMBINATION IS REPORTED ONCE, not once per draw. The scenario
            # reads the control 65 times and serves several draws, so a per-call
            # or per-draw report would show more than one line here.
            $n = 0
            if (Test-Path $err) {
                $n = @(Select-String -Path $err -SimpleMatch 'needs BOTH SL_BG_TF_CLIP=1' -AllMatches).Count
            }
            Write-Output "--- 'needs BOTH' lines on stderr: $n (expected 1)"
            if ($n -ne 1) { $rc = 1 }
            Get-Content $err | ForEach-Object { Write-Output "    stderr: $_" }
        }

        if ($r.name -eq 'pi-no-clip' -or $r.name -eq 'pi-no-rcpu') {
            # A MISCOMBINATION IS REPORTED ONCE, not once per draw.
            $n = 0
            if (Test-Path $err) {
                $n = @(Select-String -Path $err -SimpleMatch 'SL_BG_TF_POS_INPUT=1 needs BOTH' -AllMatches).Count
            }
            Write-Output "--- 'POS_INPUT needs BOTH' lines on stderr: $n (expected 1)"
            if ($n -ne 1) { $rc = 1 }
            Get-Content $err | ForEach-Object { Write-Output "    stderr: $_" }
        }

        if ($r.name -eq 'pi-ambiguous') {
            # THE EXCLUSIVITY IS REPORTED ONCE, AND IT IS THE ONLY THING
            # REPORTED: a launch asking for both experiments must not also be
            # told that either one is miscombined, because neither is - they are
            # refused for being asked together. A coupling line here would mean
            # the arbiter ran the ambiguity test after the coupling ones and
            # reported a reason that is not the reason.
            $n = 0
            $m = 0
            if (Test-Path $err) {
                $n = @(Select-String -Path $err -SimpleMatch 'MUTUALLY EXCLUSIVE' -AllMatches).Count
                $m = @(Select-String -Path $err -SimpleMatch 'needs BOTH SL_BG_TF_CLIP=1' -AllMatches).Count
            }
            Write-Output "--- 'MUTUALLY EXCLUSIVE' lines on stderr: $n (expected 1)"
            if ($n -ne 1) { $rc = 1 }
            Write-Output "--- and coupling-complaint lines: $m (expected 0)"
            if ($m -ne 0) { $rc = 1 }
            Get-Content $err | ForEach-Object { Write-Output "    stderr: $_" }
        }

        if ($r.name -eq 'pi-env-two') {
            $n = 0
            if (Test-Path $err) {
                $n = @(Select-String -Path $err -SimpleMatch 'SL_BG_TF_POS_INPUT="2" is not 0 or 1' -AllMatches).Count
            }
            Write-Output "--- 'SL_BG_TF_POS_INPUT is not 0 or 1' lines on stderr: $n (expected 1)"
            if ($n -ne 1) { $rc = 1 }
        }

        if ($r.name -eq 'so-env-two') {
            # The same "reported once" property for an unrecognised VALUE of the
            # state-only switch, which is a different line from the coupling's.
            $n = 0
            if (Test-Path $err) {
                $n = @(Select-String -Path $err -SimpleMatch 'SL_BG_TF_STATE_ONLY="2" is not 0 or 1' -AllMatches).Count
            }
            Write-Output "--- 'SL_BG_TF_STATE_ONLY is not 0 or 1' lines on stderr: $n (expected 1)"
            if ($n -ne 1) { $rc = 1 }
        }

        if ($r.name -eq 'env-two') {
            # AN UNRECOGNISED VALUE IS REPORTED ONCE, not once per walk. The
            # fixture calls sl_bgtf_opt_in 65 times in env mode, so a per-call
            # report would show 65 lines here.
            $n = 0
            if (Test-Path $err) {
                $n = @(Select-String -Path $err -SimpleMatch 'is not 0 or 1' -AllMatches).Count
            }
            Write-Output "--- 'is not 0 or 1' lines on stderr: $n (expected 1)"
            if ($n -ne 1) { $rc = 1 }
            Get-Content $err | ForEach-Object { Write-Output "    stderr: $_" }
        }
    }

    # ---- THE SETUP TRANSCRIPT ACROSS THE TWO STATE-ONLY ARMS --------------
    #
    # The digest covers every recorded prepass call with its arguments, in order,
    # with the DRAWARRAYS records EXCLUDED. Equality is what establishes that the
    # control omits that ONE call and changes nothing else about the bracket;
    # excluding it is why the comparison can say so at all. Neither run can make
    # this claim about itself, because the switch is parsed once per launch.
    Write-Output ""
    Write-Output "=== the prepass SETUP transcript ACROSS the arms (the prepass draw excluded)"
    foreach ($k in @('so-ref', 'so-only')) {
        if ($soShapes.ContainsKey($k)) {
            Write-Output "    $k : calls=$($soShapes[$k].calls) digest=$($soShapes[$k].digest)"
        } else {
            Write-Output "    $k : NO DIGEST REPORTED"
            $rc = 1
        }
    }
    if ($soShapes.ContainsKey('so-ref') -and $soShapes.ContainsKey('so-only')) {
        if ($soShapes['so-ref'].digest -eq $soShapes['so-only'].digest) {
            Write-Output "--- EQUAL: the state-only control omits the prepass draw and nothing else - same program, uniforms, pool and its verification, input position pointer, element binding, rasterizer discard and transform-feedback bracket, in the same order with the same arguments"
        } else {
            Write-Output "--- DIFFER: the state-only control moved more than the prepass draw"
            $rc = 1
        }
        if ($soShapes['so-ref'].calls -ne $soShapes['so-only'].calls) {
            Write-Output "--- COUNT DIFFERS: the two setup transcripts hold a different number of non-draw calls ($($soShapes['so-ref'].calls) vs $($soShapes['so-only'].calls))"
            $rc = 1
        }
    }

    # The two extra switches are absent for every run that is not one of the
    # state-only control's own: the diagnostic's scenarios and the mutants are
    # about the candidate, and a control selected in them would be a second thing
    # happening in the same walk.
    Remove-Item Env:SL_BG_TF_RASTER_CPU -ErrorAction SilentlyContinue
    Remove-Item Env:SL_BG_TF_STATE_ONLY -ErrorAction SilentlyContinue
    Remove-Item Env:SL_BG_TF_POS_INPUT -ErrorAction SilentlyContinue

    # THE OFF TOKEN IS PUT BACK FOR EVERYTHING BELOW, because the per-run
    # handling above may have left it absent on the last selection run. The
    # diagnostic scenarios and the mutants are all about the MAIN-CONTEXT
    # prepass and were written under the token, so leaving a cleared key to
    # leak into them would silently re-point them at the production arm - a
    # second thing happening in runs whose subject is something else.
    $env:SL_BG_TF_AUX_REAL = 'OFF'

    # ---- the production buffer diagnostic's scenarios --------------------
    #
    # A SECOND EXECUTABLE, linked with the C library's allocation and stdio
    # WRAPPED, for the four host-I/O failures. The product's object is the one
    # built above, from unmodified source: only the shim is recompiled, with
    # BGTF_WRAP_IO, and the wrap flags are a link-time substitution. There is no
    # test hook anywhere in src/.
    $wrapExe = Join-Path $OutDir 'bgtftest_wrapio.exe'
    $wrapFlags = @('-Wl,--wrap=malloc', '-Wl,--wrap=fwrite',
                   '-Wl,--wrap=fflush', '-Wl,--wrap=fclose')
    $wrapObjs = @()
    foreach ($u in $units) {
        $leaf = (Split-Path -Leaf $u.src) -replace '\.c$', ''
        if ($u.src -eq 'tools/profiling/native/sl_gfx_bgtf_testgl.c') {
            $obj = Join-Path $objDir "$leaf-wrapio.o"
            & $CC @($flags + $u.extra + @('-DBGTF_WRAP_IO', '-c', $u.src, '-o', $obj))
            if ($LASTEXITCODE -ne 0) { throw "bgtftest.ps1: the wrapped shim failed to compile (exit $LASTEXITCODE)" }
        } else {
            $obj = Join-Path $objDir "$leaf.o"
        }
        $wrapObjs += $obj
    }
    & $CC @($flags + @('-o', $wrapExe) + $wrapObjs + $wrapFlags)
    if ($LASTEXITCODE -ne 0) { throw "bgtftest.ps1: the wrapped executable failed to link (exit $LASTEXITCODE)" }

    # THE PROVENANCE IS REAL. The executable's own sha256 and the module
    # source's, so the validator is exercised against values a launcher would
    # actually supply rather than against a placeholder.
    $realExeSha = (Get-FileHash -Algorithm SHA256 -Path $exe).Hash.ToLowerInvariant()
    $realSrcSha = (Get-FileHash -Algorithm SHA256 -Path (Join-Path $repo 'src\gfx\sl_gfx_bgtf.c')).Hash.ToLowerInvariant()
    $dumpRoot = Join-Path $OutDir 'bgtfdump'
    New-Item -ItemType Directory -Force -Path $dumpRoot | Out-Null
    $verifier = Join-Path $repo 'tools\windows\bgtfdump-verify.py'
    $python = Join-Path (Split-Path -Parent (Split-Path -Parent $repo)) 'sightline\.venv\Scripts\python.exe'
    if (-not (Test-Path $python)) { $python = 'D:\Projects\sightline\.venv\Scripts\python.exe' }

    $env:SL_BG_TF_CLIP = '1'
    foreach ($d in $dumpRuns) {
        Write-Output ""
        if ($null -eq $d.dump) { $shown = '(unset)' } else { $shown = "'" + $d.dump + "'" }
        Write-Output "=== dump run $($d.name): SL_BG_TF_DUMP=$shown sha=$($d.sha) dir=$($d.dir)"

        $runDir = Join-Path $dumpRoot $d.name
        if ($d.dir -eq 'missing') {
            # A directory that does not exist, so the create failure is a real
            # condition and not an injected one.
            if (Test-Path $runDir) { Remove-Item -Recurse -Force $runDir }
            $env:SL_BG_TF_DUMP_DIR = (Join-Path $runDir 'absent')
        } else {
            if (Test-Path $runDir) { Remove-Item -Recurse -Force $runDir }
            New-Item -ItemType Directory -Force -Path $runDir | Out-Null
            $env:SL_BG_TF_DUMP_DIR = $runDir
        }

        if ($null -eq $d.dump) { Remove-Item Env:SL_BG_TF_DUMP -ErrorAction SilentlyContinue }
        else { $env:SL_BG_TF_DUMP = $d.dump }

        switch ($d.sha) {
            'real'  { $env:SL_BG_TF_DUMP_EXE_SHA = $realExeSha
                      $env:SL_BG_TF_DUMP_SRC_SHA = $realSrcSha }
            'short' { $env:SL_BG_TF_DUMP_EXE_SHA = $realExeSha.Substring(0, 63)
                      $env:SL_BG_TF_DUMP_SRC_SHA = $realSrcSha }
            'upper' { $env:SL_BG_TF_DUMP_EXE_SHA = $realExeSha.ToUpperInvariant()
                      $env:SL_BG_TF_DUMP_SRC_SHA = $realSrcSha }
            'zero'  { $env:SL_BG_TF_DUMP_EXE_SHA = ('0' * 64)
                      $env:SL_BG_TF_DUMP_SRC_SHA = $realSrcSha }
            default { Remove-Item Env:SL_BG_TF_DUMP_EXE_SHA -ErrorAction SilentlyContinue
                      Remove-Item Env:SL_BG_TF_DUMP_SRC_SHA -ErrorAction SilentlyContinue }
        }

        $runExe = if ($d.wrap) { $wrapExe } else { $exe }
        $err = Join-Path $objDir "stderr-$($d.name).txt"
        $out = Join-Path $objDir "stdout-$($d.name).txt"
        $argline = ($d.args | ForEach-Object { '"' + $_ + '"' }) -join ' '
        & cmd /c "`"$runExe`" $argline 1>`"$out`" 2>`"$err`""
        $code = $LASTEXITCODE
        if (Test-Path $out) { Get-Content $out | ForEach-Object { Write-Output $_ } }
        Write-Output "--- exit $code (expected 0)"
        if ($code -ne 0) { $rc = 1 }
        if (Test-Path $err) {
            Get-Content $err | Select-Object -First 12 | ForEach-Object { Write-Output "    stderr: $_" }
        }

        # ---- THE SPENT-OFFER BANNER, read from what the run actually said ---
        #
        # WHY THIS IS CHECKED HERE AND NOT IN THE FIXTURE. The property is about
        # a line on stderr, and a process cannot read back what it printed. The
        # runner owns the redirect, so this is the only place the claim is
        # measurable.
        #
        # THE DEFECT IT GUARDS. The shared rejection wording ends "Nothing is
        # recorded and the attempt is NOT spent". That is true of every refusal
        # that costs nothing and FALSE after a COMPLETE record, where later
        # matching draws are declined precisely BECAUSE the attempt was spent.
        # Measured: a run that sealed a complete record printed its
        # PUBLISHED/COMPLETE line and then that one, and a reader gating on the
        # absence of failure words read a successful capture as a failure.
        #
        # ONLY THE SPENT-OFFER REASON IS FORBIDDEN, and that distinction is
        # measured rather than assumed. A run that seals a record may legitimately
        # have refused earlier draws for real reasons - a stale or another mesh's
        # slot, a count or pointer the record needs - and for those the shared
        # wording is TRUE: nothing was recorded and no attempt was spent. The
        # dump-sel run prints exactly those two before its capture succeeds. A
        # check that forbade every refusal line in a complete run would therefore
        # demand the removal of messages that must be kept, so it tests for the
        # one reason that was suppressed and leaves the rest alone.
        #
        # BOTH DIRECTIONS ARE ASSERTED, because blanket silence would also be
        # wrong: a sealed run must still carry its publication line, and a
        # withheld record must still say so in its own words. A one-sided check
        # would pass on a module that had simply gone quiet.
        if (Test-Path $err) {
            $etext = (Get-Content $err -Raw)
            if ($null -eq $etext) { $etext = '' }
            $saysSpentOffer = $etext -match 'already been spent'
            if ($d.verify -eq 'complete') {
                if ($saysSpentOffer) {
                    Write-Output "--- BANNER FAIL: a COMPLETE record's run printed the spent-offer refusal line, whose wording claims the attempt is not spent"
                    $rc = 1
                } elseif ($etext -notmatch 'COMPLETE') {
                    Write-Output "--- BANNER FAIL: a COMPLETE record's run did not report its own publication - the suppression silenced more than the one line"
                    $rc = 1
                } else {
                    Write-Output "--- banner: published COMPLETE, and no misleading spent-offer line; other real refusals preserved"
                }
            } elseif ($d.verify -eq 'incomplete') {
                # A REAL FAILURE MUST STILL REJECT IN ITS OWN WORDS. These
                # scenarios withhold the seal, so the run has to report
                # something rather than fall silent with the banner change.
                if ($etext -match '#95') {
                    Write-Output "--- banner: a withheld record still reports its own refusal"
                } else {
                    Write-Output "--- BANNER FAIL: a withheld record reported nothing at all"
                    $rc = 1
                }
            }
        }

        # THE RECORD, VERIFIED OUT OF PROCESS. 'complete' requires a sealed
        # record AND requires the comparison contract to report every minimal
        # corruption; 'none' requires that no record exists at all; 'incomplete'
        # requires a record whose seal is withheld; 'skip' is for the outcomes
        # whose payload is deliberately partial, which the fixture itself
        # asserts and which has no digest to agree with.
        if ($d.verify -ne 'skip' -and (Test-Path $verifier) -and (Test-Path $python)) {
            $vlog = Join-Path $objDir "verify-$($d.name).txt"
            $vargs = @('-I', '-B', $verifier, '--dir', $env:SL_BG_TF_DUMP_DIR)
            if ($d.verify -eq 'complete') {
                # A SEALED RECORD AND A FIXED EXPECTED MISMATCH COUNT. The
                # default is 0 - the scenarios that install the pool equal to
                # the reference - and the distinct-pool scenarios pass their own
                # count instead. An unequal record is still COMPLETE: the seal
                # is about whether the bytes were obtained and written, never
                # about whether they agree, so acceptance must not depend on
                # equality.
                $mis = 0
                if ($null -ne $d.mismatches) { $mis = $d.mismatches }
                $vargs += '--expect-complete'
                $vargs += '--expect-mismatches'; $vargs += "$mis"
                # THE CORRUPTION BATTERY ONLY MEANS SOMETHING ON A BASELINE OF
                # ZERO: it tests "one more differing pattern than before", so on
                # a record that already differs a new difference cannot raise
                # the count and the battery would report its own arithmetic.
                if ($null -eq $d.prove -or $d.prove) { $vargs += '--prove-contract' }
            }
            if ($d.verify -eq 'incomplete') { $vargs += '--expect-incomplete' }
            if ($d.verify -eq 'none')       { $vargs += '--expect-none' }
            $env:PYTHONDONTWRITEBYTECODE = '1'
            & cmd /c "`"$python`" $(($vargs | ForEach-Object { '"' + $_ + '"' }) -join ' ') 1>`"$vlog`" 2>&1"
            $vcode = $LASTEXITCODE
            if (Test-Path $vlog) { Get-Content $vlog | ForEach-Object { Write-Output "    verify: $_" } }
            Write-Output "--- verify exit $vcode (expected 0)"
            if ($vcode -ne 0) { $rc = 1 }
        }
    }

    # The mutants. Only the shim and the fixture are recompiled; the module's
    # own object is the one built above, from unmodified source.
    Remove-Item Env:SL_BG_TF_DUMP -ErrorAction SilentlyContinue
    Remove-Item Env:SL_BG_TF_DUMP_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:SL_BG_TF_DUMP_EXE_SHA -ErrorAction SilentlyContinue
    Remove-Item Env:SL_BG_TF_DUMP_SRC_SHA -ErrorAction SilentlyContinue
    $env:SL_BG_TF_CLIP = '1'
    foreach ($m in $mutants) {
        Write-Output ""
        Write-Output "=== mutant $($m.name) ($($m.define)): must FAIL - $($m.breaks)"
        $mobjs = @()
        foreach ($u in $units) {
            $leaf = (Split-Path -Leaf $u.src) -replace '\.c$', ''
            if ($u.src -like 'tools/profiling/native/*') {
                $obj = Join-Path $objDir "$leaf-$($m.name).o"
                & $CC @($flags + $u.extra + @("-D$($m.define)", '-c', $u.src, '-o', $obj))
                if ($LASTEXITCODE -ne 0) { throw "bgtftest.ps1: mutant $($m.name): $($u.src) failed to compile (exit $LASTEXITCODE)" }
            } else {
                $obj = Join-Path $objDir "$leaf.o"
            }
            $mobjs += $obj
        }
        $mexe = Join-Path $OutDir "bgtftest_$($m.name).exe"
        & $CC @($flags + @('-o', $mexe) + $mobjs)
        if ($LASTEXITCODE -ne 0) { throw "bgtftest.ps1: mutant $($m.name) link failed (exit $LASTEXITCODE)" }
        $merr = Join-Path $objDir "stderr-mutant-$($m.name).txt"
        $mout = Join-Path $objDir "stdout-mutant-$($m.name).txt"
        # A MUTANT WITH ITS OWN ARGUMENTS. Most break something the main suite
        # observes; a payload-substitution mutant has to run a CAPTURE, which
        # the main suite contains none of, so it gets the diagnostic's own
        # scenario and the environment that arms it.
        $margline = ''
        if ($null -ne $m.args) { $margline = ($m.args | ForEach-Object { '"' + $_ + '"' }) -join ' ' }
        # A MUTANT WITH ITS OWN ENVIRONMENT. The state-only control's two run in
        # that control's modes, which need their own switches; every other mutant
        # runs with the candidate alone, which is the default set above. The keys
        # are absent from the other records, so this leaves them unchanged.
        if ($null -ne $m.clip)  { $env:SL_BG_TF_CLIP = $m.clip }
        if ($null -ne $m.rcpu)  { $env:SL_BG_TF_RASTER_CPU = $m.rcpu }
        if ($null -ne $m.sonly) { $env:SL_BG_TF_STATE_ONLY = $m.sonly }
        if ($null -ne $m.pin)   { $env:SL_BG_TF_POS_INPUT = $m.pin }
        if ($m.armdump) {
            $mdir = Join-Path $dumpRoot "mutant-$($m.name)"
            if (Test-Path $mdir) { Remove-Item -Recurse -Force $mdir }
            New-Item -ItemType Directory -Force -Path $mdir | Out-Null
            $env:SL_BG_TF_DUMP = $dumpOne
            $env:SL_BG_TF_DUMP_DIR = $mdir
            $env:SL_BG_TF_DUMP_EXE_SHA = $realExeSha
            $env:SL_BG_TF_DUMP_SRC_SHA = $realSrcSha
        }
        & cmd /c "`"$mexe`" $margline 1>`"$mout`" 2>`"$merr`""
        $code = $LASTEXITCODE
        Remove-Item Env:SL_BG_TF_RASTER_CPU -ErrorAction SilentlyContinue
        Remove-Item Env:SL_BG_TF_STATE_ONLY -ErrorAction SilentlyContinue
        Remove-Item Env:SL_BG_TF_POS_INPUT -ErrorAction SilentlyContinue
        $env:SL_BG_TF_CLIP = '1'
        if ($m.armdump) {
            foreach ($k in @('SL_BG_TF_DUMP', 'SL_BG_TF_DUMP_DIR',
                             'SL_BG_TF_DUMP_EXE_SHA', 'SL_BG_TF_DUMP_SRC_SHA')) {
                Remove-Item "Env:$k" -ErrorAction SilentlyContinue
            }
        }
        $line = ''
        if (Test-Path $mout) {
            $line = (Get-Content $mout | Where-Object { $_ -match '^\d+ cases' })
            $first = (Get-Content $mout | Where-Object { $_ -match '^\s+FAIL line' } | Select-Object -First 1)
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
    if ($null -eq $savedSwitch) { Remove-Item Env:SL_BG_TF_CLIP -ErrorAction SilentlyContinue }
    else { $env:SL_BG_TF_CLIP = $savedSwitch }
    if ($null -eq $savedRcpu) { Remove-Item Env:SL_BG_TF_RASTER_CPU -ErrorAction SilentlyContinue }
    else { $env:SL_BG_TF_RASTER_CPU = $savedRcpu }
    if ($null -eq $savedSonly) { Remove-Item Env:SL_BG_TF_STATE_ONLY -ErrorAction SilentlyContinue }
    else { $env:SL_BG_TF_STATE_ONLY = $savedSonly }
    if ($null -eq $savedPin) { Remove-Item Env:SL_BG_TF_POS_INPUT -ErrorAction SilentlyContinue }
    else { $env:SL_BG_TF_POS_INPUT = $savedPin }
    if ($null -eq $savedDump) { Remove-Item Env:SL_BG_TF_DUMP -ErrorAction SilentlyContinue }
    else { $env:SL_BG_TF_DUMP = $savedDump }
    if ($null -eq $savedDumpDir) { Remove-Item Env:SL_BG_TF_DUMP_DIR -ErrorAction SilentlyContinue }
    else { $env:SL_BG_TF_DUMP_DIR = $savedDumpDir }
    if ($null -eq $savedExeSha) { Remove-Item Env:SL_BG_TF_DUMP_EXE_SHA -ErrorAction SilentlyContinue }
    else { $env:SL_BG_TF_DUMP_EXE_SHA = $savedExeSha }
    if ($null -eq $savedSrcSha) { Remove-Item Env:SL_BG_TF_DUMP_SRC_SHA -ErrorAction SilentlyContinue }
    else { $env:SL_BG_TF_DUMP_SRC_SHA = $savedSrcSha }
    if ($null -eq $savedArReal) { Remove-Item Env:SL_BG_TF_AUX_REAL -ErrorAction SilentlyContinue }
    else { $env:SL_BG_TF_AUX_REAL = $savedArReal }
    if ($null -eq $savedArSync) { Remove-Item Env:SL_BG_TF_AUX_SYNC -ErrorAction SilentlyContinue }
    else { $env:SL_BG_TF_AUX_SYNC = $savedArSync }
}

Write-Output ""
Write-Output "bgtftest.ps1: $(if ($rc -eq 0) { 'all runs passed' } else { 'FAILED' })"
exit $rc
