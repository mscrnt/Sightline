<#
.SYNOPSIS
    OPTIMIZED RENDERING live-batch runner: one finite, predeclared arm table
    for the v0.4 performance row, run sequentially in one session, staged
    smallest-first around a MEASURED far case.

.DESCRIPTION
    WHAT THIS IS. A batch of GPU runs that qualify the OPTIMIZED RENDERING row
    (src/platform/sl_perf.h) against the evidence the row's far-depth guard is
    waiting on. Every arm is predeclared: a name, an exact argv, a complete
    environment map, a cwd, its own scratch config and its own fresh copy of a
    pinned initial save, a pose, a frame count, its capture schedule, an
    expected duration, and the named witnesses the checker looks for in that
    arm's own stderr.

    LAUNCHING IS THE DEFAULT. -DryRun is a SWITCH, and it is off unless it is
    given: an invocation with no arguments RUNS the batch and starts GPU
    children. -DryRun prints the complete staged batch and starts nothing.

    THE FIRST FAR CASE IS A MEASURED BOOT RECIPE, NOT A POSE FILE.

      An ENGINE_START_PAD recipe replaces the four-mark prerequisite that used
      to stand between this batch and its first paired comparison. The recipe
      is Runway (stage 35) booted directly at start pad 0 under a pinned RNG
      state, pinned scratch-config bytes and a fresh copy of a pinned initial
      save. The single-player start camera derives eye and heading from pad 0
      (src/game/bondview_r.c:376-400), so the recipe names a scene without
      naming a coordinate, and no coordinate is recorded for it.

      WHAT THE RECIPE IS QUALIFIED FOR, AND ONLY THAT. It is a measured far
      GPU-submission recipe: at this exact recipe the background pass drew 181
      of 181 frames in two depth ranges and the GPU transform submitted 7602
      draw calls / 9690378 indices in a far frame's first range and 6878 /
      9639336 in its game range, with the vertex program compiled and linked.
      It is NOT qualified as a numeric teleport mark, a blended far scene, an
      occluder scene, a movement crossing or a reduction-positive scene, and
      nothing here labels it as any of those. Those cases are declared in the
      batch's not_covered list with the measurement that leaves them open.

      THE RECIPE IS BOUND BY HASH AND THE BATCH REFUSES TO RUN WITHOUT IT. The
      anchor record's own sha256, the scratch-config bytes' sha256 and the
      initial save's sha256 are all pinned below and all verified in preflight.
      A mismatch on any of them stops the batch before a child is started: a
      recipe that is not the measured one measures a different scene under the
      measured one's name.

      THE INITIAL SAVE COMES FROM THE REPOSITORY, FRESH PER ARM. SL_EEPROM_RW
      is READ-WRITE and the engine rewrites it in place, so the save left
      behind in a completed run's directory is an OUTPUT, not the recipe's
      input: the discovery run's own leftover differs from the pinned initial
      image in both length (2048 against 296960 bytes) and content (sha256
      cb38df6dabec791d3a01929de105f98b936751c2901d7e5f9f1431fa9742eb61 against
      the pinned e6e9f1eaaa7968504c3bc9595ed51ff200bcf68f503492173192cf66e998
      08c8). Each arm therefore copies the REPOSITORY file into its own
      directory and the copy's hash is recorded, so no half of a pair can
      inherit the other half's mutated save and no run can be fed an output as
      an input.

    OPTIONAL REAL POSES ARE STILL HONOURED, AND STILL REFUSED SILENTLY NEVER.
    -Marks binds a pose from a mark file whose sha256 and whose own stage are
    recorded and must match the arm's. The cases that genuinely need a pose -
    an occluder, far geometry in the blended class, a camera crossing - are the
    cases the recipe does not qualify, and they stay NOT_COVERED and unrun
    until a mark binds them. A missing mark is never a quiet fall back to a
    start pad.

    SAVED CONTROLS, FOR THE CASES THEY COVER. tools/trace/inputs holds
    <stage>[-sweep-<diff>].{input,eeprom,spec} triples: recorded input streams
    with pinned initial saves, replayed by the native build through SL_INPUT
    (src/platform/sl_ultra_shim.c:1702; SL_INPUT_RECORD is the recorder and
    SL_INPUT_REPLAY is the emulator plugin's variable, neither of which
    replays here). A control supplies a reproducible, hash-pinned viewpoint
    away from the start pad without a mark file, which is what the second-pose
    case needs. It does NOT supply a coordinate: which viewpoint a stream
    reaches at a given moment is not recorded, so a control is used for "a
    second independent pose" and never to claim a named one. The streams carry
    no SEED= in their .spec sidecars, so they were recorded on the engine's
    deterministic boot state, which is the same state this batch pins
    explicitly (src/platform/sl_ultra_shim.c:3340, :3446-3470).

    ACQUISITION IS EXPLICIT, FINITE AND JUSTIFIED, NOT A BLANKET DEMAND.
    -Acquire runs the declared probe arms and nothing else is implied by them.
    The one role this batch cannot measure at the qualified recipe is the CPU
    loop reduction: the reduction predicate admits only fogless walks
    (src/gfx/sl_gfx_dl.c:18388) and the recipe's stage carries a level fog
    table, so the probe is two arms on the two fogless candidates and no more.

    AN UNPROVED ROLE IS REJECTED, NOT FILLED IN WITH THE FAR CANDIDATE.
    A selection is read for the reduction and skydome roles only, and each is
    accepted only when the named candidate's OWN acquisition record carries
    every declared witness - a bound asset root, the declared stage item in a
    LOADED line, the pinned RNG state taken explicitly, no timeout, no shader
    error - AND the role's own measured predicate. A selection that names the
    far candidate for one of those roles, or names a stage whose record fails
    the predicate, is REJECTED by name: that role's dependent arms are
    NOT_COVERED, they are not run, and the reason is recorded. The defect this
    closes was observed: a selection recorded reduction = runway while that
    run's own fogless coverage was null and its reduction count was 0 with the
    statistics reader refusing the walk outright.

    WHAT IT MEASURES, AND WHY THE ARMS ARE SHAPED THIS WAY.

      THE PAIRED ARMS DIFFER IN SL_PERF_OPT ALONE. Every far arm sets
      SL_PERF_BG_FAR=1 in BOTH halves of its pair. Without that the row's own
      guard refuses the two-range walk (sl_perf_bg_far_admit, src/platform/
      sl_perf.c:204), the GPU far path never executes, and a pair compares two
      CPU fallbacks to each other - a comparison that passes whatever the far
      path does. The guard-negative case is a separate arm (phase C) and is the
      only arm that omits SL_PERF_BG_FAR.

      THE OFF HALF CANNOT WITNESS THE VERTEX PROGRAM. The readiness line is
      printed from sl_bggpu_init (src/gfx/sl_gfx_bggpu.c:350), which is reached
      only when the transform is asked for, and sl_bggpu_opt_in ->
      sl_perf_bg_xform returns the row's value (src/platform/sl_perf.c:188-202)
      - so SL_PERF_OPT=0 does not ask and the line cannot appear. GPU_PROGRAM_
      READY is therefore carried by ON halves only. Demanding it of an OFF half
      is a witness whose own arm configuration suppresses it.

      EVERY DEPTH AND BLEND WITNESS IS READ ACROSS THE CAPTURE WINDOW, NOT AT
      THE END. The per-depth census is cumulative over the process, so a
      nonzero terminal reading is also consistent with geometry submitted
      before the window and none inside it. The checker reads the DELTA between
      the census report immediately preceding the arm's first capture and the
      one immediately following its last. Every arm that carries a depth or
      blend witness therefore also captures, so that the bracket exists.

      THE CAPTURE SCHEDULE IS WINDOWED ON THE INPUT RECORD INDEX. The engine
      windows on one counter at a time: SL_SHOT_READ_FIRST/READ_LAST select the
      record index, otherwise SL_SHOT_FIRST/LAST select the frame counter
      (src/gfx/sl_gfx_sdl.c:789-828). The record index advances once per
      retrace (sl_input_vi_advance, src/platform/sl_ultra_shim.c:2287), and
      under SL_VI_CATCHUP=0 the pump takes exactly one retrace per paced period
      (:625 clamps the catch-up to 0), so in level the index takes every
      integer value at successive presents and a declared moment is the moment
      that fires. The frame counter does not: it is incremented BOTH per
      retrace (:1497) and per game-frame boundary (sl_frame_advance, called
      from src/game/frametiming.c:100), so it steps by two per present and the
      parity of its first in-level value is a property of how many retraces the
      load consumed. A schedule declared in frame units is therefore a schedule
      whose first moment is not known before the run; declared in record units
      it is. These arms declare record units and say so in the record.

      THE CENSUS ARMS AND THE NO-CENSUS ARMS MEASURE DIFFERENT THINGS, AND
      NEITHER CAN STAND IN FOR THE OTHER. Measured source facts:
        * The per-depth census, the background pass's per-frame totals, its
          blended share and the fill-query worker line are printed only from
          the census branch of the per-60-frame report (src/gfx/sl_gfx_dl.c
          :23749-23775; sl_sky_report :19513, nf_report :13540).
        * The CPU loop reduction and the background vertex helper are BOTH
          refused while a statistics reader is active: fstats =
          bg_fogtrace_on() || dl_census_on() (src/gfx/sl_gfx_dl.c:18357),
          consumed at :18386 (the reduction) and :17960 (the helper).
      So a census arm cannot measure the reduction or the helper, and the
      reduction has its own inline bounded report (:19307, :19363) that needs
      no census. NO ARM HERE MEASURES A FEATURE WITH A SWITCH THAT DISABLES IT.

      MOVEMENT IS WITHIN ONE RUN, THROUGH THE SYNTHETIC PAD. The viewpoint
      teleport applies once per process and latches (g_tp_done,
      src/native/sl_teleport.c), so a sequence of separate processes is a
      sequence of static poses and not motion. SL_PAD_VIRTUAL attaches an SDL
      virtual pad - mandatory; without it SL_PAD_SCRIPT never applies
      (src/platform/sl_input.c:1443-1466) - and SL_PAD_SCRIPT drives one step
      per line, the left stick moving and the right stick looking, each step
      applied once sl_frames_completed reaches its frame and held until the
      next. The step file is written by this script and pinned by sha256.

      ONE PAIR CANNOT SAY WHICH SUBSYSTEM MOVED A PIXEL, AND -Discriminate
      PREPARES THE TWO ARMS THAT CAN. SL_PERF_OPT selects three things at once -
      the skydome batch path, the carrier that path uploads its cloud colours
      with, and the background GPU vertex transform (src/platform/sl_perf.c
      :152-202) - so a difference between the halves of a pair that differs in
      SL_PERF_OPT alone is not attributable to any one of them. -Discriminate
      runs the TWO COMBINATIONS THE PAIR DOES NOT CONTAIN: the row ON with the
      skydome batch disabled, and the row ON with the GPU transform disabled,
      at the same recipe, RNG state, config bytes, fresh-per-arm pinned save
      and capture grid. The pair's own halves are NOT re-run - they are reused
      as hash-pinned saved captures - and each arm's bytes are compared against
      BOTH of them with no tolerance and one named pixel read out. It replaces
      the staged batch rather than adding to it, it is a DIAGNOSTIC and never a
      qualification, and it asserts nothing about a cause. See $AXIS_MATRIX.

    THE ORDER IS SMALLEST-FIRST AND THE FIRST GROUP IS A GATE. The measured
    far pair runs before anything else. If its two halves' captures are not
    byte-identical, the batch STOPS and preserves: a real image difference
    between the two paths at the one scene where the far path is known to
    submit is a finding about the row, and running the rest of the batch over
    it would spend the window on arms whose premise has just failed.

    WHAT A FAILURE MEANS. A required far case that fails stays FAIL and one
    that was never observed stays NOT_COVERED; neither is satisfied by a
    far-guarded subset, and both leave the batch INCOMPLETE. A -Only run can
    report PASS as a SUBSET DIAGNOSTIC and never reports full acceptance: full
    qualification additionally needs every mandatory arm present, an explicit
    recorded outcome for every manual step pinned to build + arm + frame, and
    the performance row. Outputs are preserved in every case. There is no image
    tolerance, no retention or deletion policy, and no trace re-baseline.

    NOTHING IS REUSED BETWEEN RUNS. Each invocation gets its own attempt
    directory under -Out, which must be fresh and empty; a stale selection, a
    stale capture or a stale check from an earlier attempt can therefore never
    be consumed. A -Only retry refuses to write into an attempt that already
    carries a completed result.

    THE PLAYER'S FILES ARE NOT TOUCHED. Every arm points SL_CONFIG and
    SL_EEPROM_RW at its OWN files inside its own attempt directory. The real
    config at %LOCALAPPDATA%\sightline\config.ini and the real save beside it
    are never read, written, backed up or restored. The installed asset roots
    are bound for READING only: no arm sets a variable that writes under them,
    and the batch records an inventory hash of each root so a change is
    detectable after the fact.

    AD-HOC LAUNCH AND tools/windows/agent-parity.ps1. That file forbids
    starting the executable through an ad-hoc Start-Process for BOOT and
    FRONTEND measurements, so that what is played and what is measured are the
    same execution. No arm here measures boot or the front end: every arm
    direct-boots a stage and measures the background pass in level. The parity
    launch has no level, no capture and no per-arm environment, so it cannot
    express these arms.

.PARAMETER Only
    Arm names to run instead of the whole batch. This makes the run a SUBSET:
    the result can read PASS as a subset diagnostic and never reports full
    acceptance.

.PARAMETER DryRun
    A SWITCH, off by default. When given, print every arm's exact argv,
    environment, capture schedule and expected duration and launch NOTHING. No
    child process of any kind is started in this mode.

.PARAMETER Discriminate
    A SWITCH, off by default. Run the TWO missing combinations INSTEAD of the
    staged batch: the same hash-bound recipe, seed, config bytes, fresh-per-arm
    pinned save and capture grid, the OPTIMIZED RENDERING row held ON in both,
    and exactly one subsystem disabled in each - the skydome batch in one, the
    background GPU transform in the other - so that for the subsystem it does
    not disable, each arm matches the combined path exactly. The saved OFF and
    ON halves are reused as hash-pinned captures and are NOT re-run. Each arm's
    full bytes are compared against both of them with no tolerance and one
    declared pixel read out channel by channel. It carries no pair-identity
    witness and no gate: a difference is the READING being taken, not a failure,
    and which baseline an arm matches is reported without being explained. It
    qualifies nothing and justifies no rendering change.

.PARAMETER Out
    Evidence root. Default build\win32\perflive. Each run creates its own
    attempt directory beneath it.

.PARAMETER Attempt
    Name the attempt directory instead of deriving one from the UTC clock. It
    must not already exist, or must be empty.

.PARAMETER Resume
    An earlier attempt directory to read selected.json from, for the reduction
    and skydome roles. Its selection is re-hashed into this attempt's record
    and re-proved here; nothing of it is overwritten and nothing in it is
    trusted on its own say-so.

.PARAMETER Acquire
    Run the declared acquisition probe. It is OFF by default: the far case is
    measured and needs no discovery, and an acquisition run that is not
    justified by a named missing coverage spends GPU starts for nothing. The
    probe's candidates and the coverage each is there to close are declared in
    $ACQUISITION.

.PARAMETER Control
    The name of a saved control in tools/trace/inputs to replay for the
    second-pose arms. It must be a control of the recipe's own stage: a stream
    recorded on another stage boots another stage, which has no qualified far
    role here. Without it the second-pose arms are NOT_COVERED and are not run.

.PARAMETER Anchor
    The ENGINE_START_PAD anchor record to bind the recipe to. It defaults to
    the path below in this checkout and then in the sibling checkout, and its
    sha256 must equal the pinned value either way.

.PARAMETER ExpectExe
    SHA-256 the executable must have. A mismatch fails the batch before any
    arm runs.

.PARAMETER Marks
    Directory holding mark files for the OPTIONAL real poses. Every mark must
    state the stage it was taken on (its run-dir's lvl<NN>), and that stage
    must match the arm's. A missing or stage-mismatched mark leaves the arms
    that need it NOT_COVERED and unrun, never silently re-posed.

.PARAMETER SeedHex
    The 64-bit RNG STATE both halves of every pair are pinned to, in hex.
    SL_RNG_SEED is the state and not the seed argument
    (src/platform/sl_ultra_shim.c:3345-3351). Default 00000000000f4241, which
    is the state the measured recipe was taken under and the engine's own
    deterministic boot state.

.PARAMETER WorldAssetRoot
.PARAMETER TexpackRoot
.PARAMETER AssetOverrideDir
    The installed asset roots to bind for reading. Each defaults to the
    player-data location the binary's own ladder names
    (%LOCALAPPDATA%\sightline\assets\...), and each must exist: a batch that
    cannot bind the roots cannot exercise the XBLA presentation.

.PARAMETER TimeoutSeconds
    Kill and fail an arm that has not exited by then. Default 420.

.PARAMETER Exe
    The executable every arm launches. Defaults, exactly as before, to
    build\win32\sightline.exe, so a run that does not pass this is byte for
    byte the run it was. It exists because a candidate built into an isolated
    build\diag\<name> cannot otherwise be measured at all: -ExpectExe only
    pins a digest and does not choose a path, and an arm table that can name
    only one path can qualify only one binary.

    The DLL search directory follows the executable rather than the
    repository, because build.ps1 stages SDL2.dll and libwinpthread-1.dll
    beside every binary it links, diagnostic outputs included. An override
    whose runtime came from another directory would be measuring a third
    combination.

    THIS WIDENS WHAT MAY BE LAUNCHED AND NARROWS NOTHING. The frozen-binary
    pin under -Discriminate is unchanged: that diagnostic compares against
    captures a named executable produced, so it still refuses any other one.
#>
[CmdletBinding()]
param(
    [string[]]$Only,
    [switch]$DryRun,
    [switch]$Discriminate,
    [switch]$Candidate,
    [switch]$Warmups,
    [string]$Out,
    [string]$Attempt,
    [string]$Resume,
    [switch]$Acquire,
    [string]$Control,
    [string]$Anchor,
    [string]$ExpectExe,
    [string]$Exe,
    [string]$Marks,
    [string]$SeedHex = '00000000000f4241',
    [string]$WorldAssetRoot,
    [string]$TexpackRoot,
    [string]$AssetOverrideDir,
    [int]$TimeoutSeconds = 420
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# The default is the one this harness has always used; -Exe replaces it and
# nothing else, and $bin follows the executable so a binary's own staged
# runtime is the one it loads.
$exe  = Join-Path $repo 'build\win32\sightline.exe'
if ($Exe) { $exe = $Exe }
$bin  = Split-Path -Parent $exe
if (-not $Out) { $Out = Join-Path $repo 'build\win32\perflive' }

# PRESENTATION. 2 is XBLA: SL_PRESENTATION_XBLA is 2 in
# src/platform/sl_presentation.h:76, and src/platform/sl_settings.c's own row
# self-test asserts presentation=2 is written and read back as XBLA (:1435,
# :1442). The default is ORIGINAL (0, :125), which derives the EMPTY
# world-asset set - the dark picture a config carrying only
# optimized_rendering=1 produces.
$PRESENTATION_XBLA = 2

# The scratch config bytes, identical for every arm, and the same three lines
# the measured recipe was taken under. The file exists so that no arm can fall
# back to the player's own config. Its sha256 is pinned in $RECIPE below and
# verified in preflight.
$SCRATCH_CONFIG = "version=1`npresentation=$PRESENTATION_XBLA`noptimized_rendering=1`n"

# ------------------------------------------------- the measured far recipe
#
# ENGINE_START_PAD. Stage 35 booted directly, start pad 0, the pinned RNG
# state, the pinned config bytes, a fresh copy of the pinned initial save. The
# single-player start camera derives eye and heading from pad 0
# (src/game/bondview_r.c:376-400), which is why a recipe is enough and no
# coordinate is needed - and none is recorded.
#
# QUALIFIED FOR ONE ROLE. measured_far_gpu_submission_recipe, from the
# measurement quoted in $RECIPE.measured. Every other role an earlier batch
# would have wanted from a far pose is UNQUALIFIED here and listed as such, so
# that a pass on these arms cannot be read as covering any of them.
$RECIPE = [ordered]@{
    kind                         = 'ENGINE_START_PAD'
    stage                        = 'runway'
    stage_number                 = 35
    pad_index                    = 0
    source_authority             = 'src/game/bondview_r.c:376-400 - the single-player start camera derives eye and heading from pad 0'
    numeric_coordinates_recorded = $false
    seed_hex                     = '00000000000f4241'
    anchor_relpath               = '.sightline\chat-handoff\artifacts\v040\codex-far-discovery-analysis-r527\runway-startpad-anchor-526.json'
    anchor_sha256                = 'c83708d20a3f2892a8c46d8c80400ee6a83f9bb49708b35668aa738cdbb98474'
    config_sha256                = '02ee9b337ebb1eb77e1a964a548963773fc990b20de2aa1b3e17c875816bd388'
    initial_save_relpath         = 'tools\trace\inputs\runway.eeprom'
    initial_save_sha256          = 'e6e9f1eaaa7968504c3bc9595ed51ff200bcf68f503492173192cf66e99808c8'
    qualified_roles              = @('measured_far_gpu_submission_recipe')
    unqualified_roles            = @('numeric_teleport_mark', 'blended_far_scene', 'occluder_scene',
                                     'movement_crossing', 'reduction_positive')
    measured                     = 'at this recipe the background pass drew 181 of 181 frames in two depth ranges, the vertex program compiled and linked, and the GPU transform submitted 7602 draw calls / 9690378 indices in a far frame''s first range and 6878 / 9639336 in its game range, with 0 calls / 0 indices at the single-range clamp; the float colour carrier printed its prod marker exactly once and the skybatch closed on 209 indexed draws'
    length_deviation             = 'the measurement was taken over 420 pumped frames; these arms run 600 so that a per-60-frame census report brackets the capture window on BOTH sides, which a 420-frame run does not leave room for. The hash-bound elements - stage, pad, RNG state, config bytes, initial save - are unchanged.'
}

# THE TRAP THIS RECIPE AVOIDS, recorded because it is invisible from the file
# names alone. SL_EEPROM_RW is read-write and the engine rewrites the save in
# place, so the eeprom.bin left in a completed run's directory is that run's
# OUTPUT. The discovery run's leftover differs from the pinned initial image in
# length and in content, so taking the initial save from an artifact directory
# would start a "reproduction" from a mutated state.
$SAVE_OUTPUT_TRAP = [ordered]@{
    leftover_sha256 = 'cb38df6dabec791d3a01929de105f98b936751c2901d7e5f9f1431fa9742eb61'
    leftover_bytes  = 2048
    pinned_bytes    = 296960
    rule            = 'the initial save is copied from the repository file named in $RECIPE.initial_save_relpath, fresh for each arm, and its hash is verified before the copy and recorded after it; it is never taken from a run or artifact directory and never shared between the halves of a pair'
}

# ---------------------------------------------------------------- the stages
#
# `item` is the world-asset item path the stage's own background is provided
# under, which the LOADED line names. It is the preflight witness that the run
# reached the stage the arm declared. Stage 9's item is `bunker`: the source
# carries background/bunker and skydome/bunker for Bunker 1 and background/
# bunker2 for Bunker 2 (src/gfx/sl_gfx_worldasset.c:5074-5077;
# docs/world-assets.md:5186-5190), and a tree-wide grep for the string
# "bunker1" over every .c and .h in this checkout returns nothing - the alias
# `bunker1` is the LEVEL name used by the save and stream files
# (tools/trace/inputs/bunker1.*) and the preparation flags, never the item.
$STAGES = @(
    @{ name = 'runway'; stage = 35; item = 'runway'; why =
       'FarFog 15000 at visibility scale 1.0 against a replacement reaching w 46889.7; measured far GPU submission at the ENGINE_START_PAD recipe above' },
    @{ name = 'surface'; stage = 36; item = 'surface'; why =
       'FarFog 2500 with a blended panorama shell; measured at the same recipe shape as a PANORAMA FALLBACK - 181 far frames, 0 GPU submissions in every depth mode, 181 panorama refusals, the program not asked. That is intentional fallback evidence, not a shader failure and not far coverage.' },
    @{ name = 'surface2'; stage = 43; item = 'surface2'; why =
       'the same environment family and far plane as surface, a second pose population for the same transition' },
    @{ name = 'bunker1'; stage = 9; item = 'bunker'; why =
       'fogless table (15..10000) with an exterior panorama and a cloud sheet 14000 world units out; a fogless walk is the only class the CPU loop reduction admits' },
    @{ name = 'silo'; stage = 20; item = 'silo'; why =
       'fogless table, wholly underground, no panorama shell marked and no skydome asset: the reduction candidate with the fewest competing refusals' },
    @{ name = 'dam'; stage = 33; item = 'dam'; why =
       'far plane 75000 at visibility scale 0.2 and no vertex past it at any pose measured: the documented far-NEGATIVE control, which must read zero far frames' }
)

# ------------------------------------------------------- the saved controls
#
# Recorded input streams with pinned initial saves, replayed by the native
# build through SL_INPUT. Each entry pins the stream, its .spec sidecar and the
# initial save by sha256. WHAT A CONTROL SUPPLIES: a reproducible viewpoint
# away from the start pad, and movement within one run, with no mark file and
# no invented coordinate. WHAT IT DOES NOT SUPPLY: a named viewpoint. Which
# pose a stream reaches at a given moment is not recorded anywhere, so a
# control is used for "a second independent pose" and never to claim a
# particular one.
$SAVED_CONTROLS = @(
    @{ name = 'runway'; stage = 'runway'; stage_number = 35
       input_relpath  = 'tools\trace\inputs\runway.input'
       input_sha256   = 'c36935c860d3e5b6288e6ff59e541d55788dbc6f9aa36c7c38dd919f96a9beff'
       spec_relpath   = 'tools\trace\inputs\runway.spec'
       spec_sha256    = 'a72e363148fd7483f05d21c7e5061b09a3ebb870db5d9770631a5d6dd8ed28cf'
       save_relpath   = 'tools\trace\inputs\runway.eeprom'
       save_sha256    = 'e6e9f1eaaa7968504c3bc9595ed51ff200bcf68f503492173192cf66e99808c8'
       covers         = 'a second, non-start-pad viewpoint on the recipe''s OWN stage, reproducible from a pinned stream rather than a mark file' }
)

# Controls that exist and are NOT used here, with the reason, so that "use the
# saved controls" cannot be read as a claim that all of them apply. Each of
# these is a frozen test contract for another stage; a stream recorded on
# another stage boots that stage, which carries no qualified far role in this
# batch.
$CONTROLS_OFF_STAGE = @('frigate', 'cradle', 'statue', 'facility')

# ------------------------------------------------------------- acquisition
#
# EXPLICIT, FINITE, AND JUSTIFIED BY A NAMED MISSING COVERAGE. -Acquire runs
# exactly these arms. Nothing else in the batch depends on a discovery run, and
# nothing here demands a mark file.
$ACQUISITION = @(
    @{ stage = 'silo'; role = 'reduction'; frames = 600; seconds = 130; justification =
       'the CPU loop reduction cannot be measured at the qualified recipe: the predicate admits only fogless walks (src/gfx/sl_gfx_dl.c:18388) and stage 35 carries a level fog table. Silo is fogless, wholly underground, marks no panorama shell and has no skydome asset, so it is the candidate with the fewest competing refusals.' },
    @{ stage = 'bunker1'; role = 'reduction'; frames = 600; seconds = 130; justification =
       'the second fogless candidate, for the same missing coverage. Its exterior panorama is a known competing refusal (the predicate refuses a panorama walk outright, :18385), so it is probed beside Silo rather than instead of it.' }
)

# Stages deliberately NOT probed, with the measurement that makes a probe
# pointless, so that a short acquisition list is not mistaken for an oversight.
$ACQUISITION_EXCLUDED = @(
    @{ stage = 'runway'; reason = 'already measured: this is the qualified far recipe. Re-probing it spends a GPU start to re-measure what the anchor record already pins.' }
    @{ stage = 'surface'; reason = 'already measured as a PANORAMA FALLBACK at the same recipe shape - 181 far frames, 0 GPU submissions in all depth modes, 181 panorama refusals, program not asked. A second probe measures the same fallback again.' }
    @{ stage = 'surface2'; reason = 'no role in this batch is open that surface2 could close: the far role is measured and the reduction needs a fogless walk, which this environment family does not provide.' }
    @{ stage = 'dam'; reason = 'the documented far-NEGATIVE control. It can confirm a zero reading, which no open role in this batch needs.' }
)

$MANDATORY_PHASES = @('B', 'C', 'D', 'F', 'G')

# Full qualification needs every mandatory CASE, not one arm per phase: a phase
# letter is satisfied by either half of a pair, and half a pair is not a
# comparison. These names are the cases, and the checker is given them here
# rather than inferring them from whichever arms happened to run.
$MANDATORY_CASES = @('B-depth-off', 'B-depth-on', 'C-guard', 'D-blend-off', 'D-blend-on',
                     'F-move-off', 'F-move-on', 'G4-combined-off', 'G4-combined-on')

# Cases this batch does NOT cover, each with the measurement or source fact
# that makes it uncoverable here. They are declared so that a PASS cannot be
# read as covering them, and so that full qualification cannot be claimed while
# any of them stands.
$NOT_COVERED = @(
    @{ name = 'SINGLE_RANGE_SUBMISSION'; reason =
       'The single-range clamp submitted NOTHING at the one qualified far recipe: every per-depth census report of the measured run read 0 calls / 0 indices at the single-range clamp while the two far columns ran to 7602 / 9690378 and 6878 / 9639336. So no arm here can carry WINDOW_SINGLE_DELTA_POSITIVE or CLAMP_INPUT_PRESENT without declaring a witness its own scene is measured not to produce. A scene at which the single range submits is not among the measured candidates, and none is invented: this stays open until one is measured.' }
    @{ name = 'CLAMP_BRANCH_EXECUTED'; reason =
       'The single-range clamp is a branch inside the vertex program (u_dmode == 0 && u_fogless == 0 && c.w > 0.0 && c.z > c.w, src/gfx/sl_gfx_bggpu.c BGG_VS), and that program is not read back - the module says so at :163-169. The offline fixture under tools/profiling/native has no GLSL compiler and no GPU (tools/windows/bggputest.ps1: "The vertex program''s own depth arithmetic is never executed"), and its one comparison of a hand transcription against the CPU reference is SOURCE-LEVEL ONLY and says so in its own name. What this batch covers is GPU_PROGRAM_READY - a real driver compiled and linked the program - and the windowed FAR0/FAR1 submission deltas. Driver execution of any depth branch remains unqualified, and a GPU depth probe is the vehicle for it, not an arm here.' }
    @{ name = 'BLENDED_FAR_RANGE_GEOMETRY'; reason =
       'Phase D draws the translucent class INSIDE the walk (SL_BG_XLU_LATE=0 in both halves) and reads the per-frame count of background GL draws blended with no alpha reject, which the measured run reported as 26 per frame at this recipe. That establishes that a blended class is drawn and that the two paths still agree on the pixels; it does NOT establish that any geometry in a far range is blended, nor the depth ordering between a blended far draw and anything in front of it. The recipe is not qualified as a blended far scene and is not labelled one.' }
    @{ name = 'OCCLUSION_ORDERING'; reason =
       'The surplus of rasterised triangles over the background pass''s own per-frame count was once used as an occlusion proxy. It is LIMITED EVIDENCE, not a covered case: a surplus establishes that other geometry was rasterised on those frames, not that any of it lies between the eye and the far scenery, and not that depth ordering between the two was correct. No witness here claims it, and the recipe is not qualified as an occluder scene. Closing this needs a pose at which an occluder stands in front of far geometry, which is a mark, not a recipe.' }
    @{ name = 'MOVEMENT_CROSSES_A_TRANSITION'; reason =
       'Phase F moves and turns within one run through SL_PAD_VIRTUAL + SL_PAD_SCRIPT and its steps are witnessed as applied at distinct frames. Whether that traverse CROSSES a far transition - an interval of the census that submitted no far geometry beside one that did - is a reading of the result, not a property of the recipe: the recipe is not qualified as a movement crossing. The batch witness F_TRANSITION_CROSSED reports what the intervals show and is NOT_COVERED when they show no crossing.' }
    @{ name = 'CPU_LOOP_REDUCTION'; reason =
       'The reduction predicate admits only fogless walks (src/gfx/sl_gfx_dl.c:18388) and the qualified recipe''s stage carries a level fog table, so a reduction-POSITIVE reading cannot come from it; the recipe is not qualified as reduction-positive, and the earlier selection that recorded reduction = runway is rejected here by name - that run''s own fogless coverage was null and its reduction count was 0 with the statistics reader refusing the walk. The clean combined arm therefore requires only that the reduction''s own inline report was printed and reported no unsound draw; a positive reduced-walk count needs a proved fogless candidate, which -Acquire probes and which nothing here assumes.' }
    @{ name = 'FILL_QUERY_WORKERS'; reason =
       'At the qualified recipe the fill-query line read 2 workers and 181 batches of 0 PREDICTED traces, with 0 answers computed by a worker, on every report of the measured run. The worker path has nothing to do at this scene, so an arm demanding FILL_WORKERS_DID_WORK here would fail on a property of the scene rather than of the feature. It is not required of any arm and stays open until a scene that predicts traces is measured.' }
    @{ name = 'VERTEX_HELPER_SPLIT'; reason =
       'At the qualified recipe the helper reported itself OFF under SL_BG_VERTEX_WORKER auto with 16 allowed physical cores, 0 walks in two ranges and 1 on the render thread alone, refused by the statistics reader (the census arms'' fstats term, src/gfx/sl_gfx_dl.c:18357 consumed at :17960). Whether it splits on a clean no-census arm is unmeasured, and its admission also depends on a switch this batch does not set. No arm carries a helper witness, and a positive would in any case require the split MINUS the splits whose upper range was recomputed to exceed zero - a milestone line alone establishes nothing about the walks after it.' }
    @{ name = 'SEPARATE_PROCESS_TELEPORT_SEQUENCE'; reason =
       'A sequence of separate processes, each teleported to a different pose, was once used as movement. It is LIMITED EVIDENCE, not a covered case: the teleport latches once per process (g_tp_done, src/native/sl_teleport.c), so such a sequence is a set of static poses with no transition inside any run. It is recorded only so that a reading of the old evidence is not mistaken for coverage.' }
)

# Roles added to $NOT_COVERED while the batch is built, each because a role was
# named but not proved. They are kept separate from the declared list above so
# that a run-time refusal is visibly a refusal and not a standing limitation.
$script:runtimeNotCovered = @()
function Add-NotCovered([string]$name, [string]$reason) {
    $script:runtimeNotCovered += [ordered]@{ name = $name; reason = $reason }
}

# ------------------------------------------------------------------ helpers
function Get-Sha256Lower([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower()
}

function Get-StringSha256([string]$s) {
    $enc = New-Object System.Text.UTF8Encoding($false)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $h = $sha.ComputeHash($enc.GetBytes($s))
    } finally {
        $sha.Dispose()
    }
    return (($h | ForEach-Object { $_.ToString('x2') }) -join '')
}

# No BOM, anywhere. A byte-order mark at the head of a JSON document makes it
# unparseable by a strict reader, and the checker is one.
function Write-TextNoBom([string]$path, [string]$text) {
    $dir = Split-Path -Parent $path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    [IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
}

function Write-JsonNoBom([string]$path, $obj, [int]$depth) {
    Write-TextNoBom $path (($obj | ConvertTo-Json -Depth $depth) + "`n")
}

# An inventory hash of a bound asset root: every file's relative path and
# length, sorted. It is not a content hash of the tree - it is the cheapest
# reading that makes an added, removed or resized file detectable after the
# fact, which is what "bound for reading only" has to be checkable against.
function Get-RootInventory([string]$root) {
    if (-not (Test-Path -LiteralPath $root)) { return $null }
    $lines = New-Object System.Collections.Generic.List[string]
    $full = (Resolve-Path -LiteralPath $root).Path
    foreach ($f in (Get-ChildItem -LiteralPath $full -Recurse -File -ErrorAction SilentlyContinue | Sort-Object FullName)) {
        $rel = $f.FullName.Substring($full.Length).TrimStart('\', '/')
        $lines.Add(("{0}`t{1}" -f $rel, $f.Length))
    }
    return [ordered]@{ path = $full; files = $lines.Count
                       inventory_sha256 = (Get-StringSha256 ($lines -join "`n")) }
}

function Resolve-Python {
    if ($env:SL_PYTHON) {
        if (Test-Path -LiteralPath $env:SL_PYTHON) { return (Resolve-Path -LiteralPath $env:SL_PYTHON).Path }
    }
    $local = Join-Path $repo '.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $local) { return $local }
    # A worktree has no .venv of its own; the interpreter lives in the parent
    # checkout beside it.
    $sibling = Join-Path (Split-Path -Parent $repo) 'sightline\.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $sibling) { return $sibling }
    return $null
}

# A path that may live in this checkout or in the sibling one beside it. The
# anchor record is kept with the artifacts of the run it describes, which is
# not necessarily the worktree the binary was built in.
function Resolve-EitherCheckout([string]$relPath) {
    $here = Join-Path $repo $relPath
    if (Test-Path -LiteralPath $here) { return (Resolve-Path -LiteralPath $here).Path }
    $there = Join-Path (Join-Path (Split-Path -Parent $repo) 'sightline') $relPath
    if (Test-Path -LiteralPath $there) { return (Resolve-Path -LiteralPath $there).Path }
    return $null
}

# THE MARK PARSE is play.ps1's, field for field: the explicit [teleport]
# section first, then the legacy [camera] / [rooms] quantities (play.ps1
# :580-602). On top of that the mark must STATE ITS OWN STAGE, which the
# legacy shape carries only in its run-dir line (...-lvl<NN>). A mark that
# cannot say which stage it is of is refused rather than guessed at: a pose
# applied on the wrong stage lands somewhere nobody chose.
function Read-Mark([string]$path) {
    $resolved = (Resolve-Path -LiteralPath $path).Path
    $text = [IO.File]::ReadAllText($resolved)
    $pose = [ordered]@{ kind = 'teleport'; x = $null; y = $null; z = $null
                        theta = $null; verta = $null; room = '-1'
                        source = $resolved; mark_sha256 = (Get-Sha256Lower $resolved)
                        mark_stage = $null; shape = 'legacy' }
    if ($text -match '(?m)^teleport-pos\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)') {
        $pose.x = $Matches[1]; $pose.y = $Matches[2]; $pose.z = $Matches[3]
        $pose.shape = 'teleport-section'
    }
    if ($text -match '(?m)^teleport-theta\s+(-?\d+(?:\.\d+)?)') { $pose.theta = $Matches[1] }
    if ($text -match '(?m)^teleport-verta\s+(-?\d+(?:\.\d+)?)') { $pose.verta = $Matches[1] }
    if ($text -match '(?m)^teleport-room\s+(-?\d+)')            { $pose.room  = $Matches[1] }
    if ($null -eq $pose.x -and $text -match '(?m)^eye-world-pos\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)') {
        $pose.x = $Matches[1]; $pose.y = $Matches[2]; $pose.z = $Matches[3]
    }
    if ($null -eq $pose.theta -and $text -match '(?m)^look-theta-deg\s+(-?\d+(?:\.\d+)?)') { $pose.theta = $Matches[1] }
    if ($null -eq $pose.verta -and $text -match '(?m)^look-verta-deg\s+(-?\d+(?:\.\d+)?)') { $pose.verta = $Matches[1] }
    if ($text -match '(?m)^teleport-stage\s+(\d+)')  { $pose.mark_stage = $Matches[1] }
    if ($null -eq $pose.mark_stage -and $text -match '-lvl(\d+)') { $pose.mark_stage = $Matches[1] }
    if ($null -eq $pose.x -or $null -eq $pose.theta -or $null -eq $pose.verta) { return $null }
    if ($null -eq $pose.mark_stage) { return $null }
    return $pose
}

# One pose, hash-bound, or an explicit refusal. There is no fall back to a
# start pad here: the arms that ask for a mark are the arms whose case a start
# pad does not cover, and a silent fall back is what let a recipe's scene be
# recorded under a pose's name.
$script:poseRefusals = @()
function Get-BoundPose([string]$markName, [int]$stageNum, [string]$needBy) {
    if (-not $Marks) {
        $script:poseRefusals += "$needBy needs $markName and no -Marks directory was given"
        return $null
    }
    $p = Join-Path $Marks $markName
    if (-not (Test-Path -LiteralPath $p)) {
        $script:poseRefusals += "$needBy needs $markName, absent at $p"
        return $null
    }
    $pose = Read-Mark $p
    if ($null -eq $pose) {
        $script:poseRefusals += "$needBy needs $markName, which does not carry a pose AND a stage: $p"
        return $null
    }
    if ([int]$pose.mark_stage -ne $stageNum) {
        $script:poseRefusals += ("$needBy needs a mark of stage $stageNum; $markName is of stage " +
                                 "$($pose.mark_stage) (sha256 $($pose.mark_sha256))")
        return $null
    }
    return $pose
}

# THE RECIPE'S POSE. This is a POSITIVE, hash-bound declaration and not a fall
# back: it names the engine's own start pad for the stage, cites the code that
# derives the camera from it, and carries the anchor record's hash. It has no
# coordinates because none were recorded and none are invented.
function New-RecipePose {
    return [ordered]@{
        kind = 'engine-start-pad'
        source = ("ENGINE_START_PAD, stage {0} pad {1}; {2}; anchor sha256 {3}" -f `
                  $RECIPE.stage_number, $RECIPE.pad_index, $RECIPE.source_authority, $RECIPE.anchor_sha256)
        mark_sha256 = $null; mark_stage = "$($RECIPE.stage_number)"; shape = 'engine-start-pad'
        x = $null; y = $null; z = $null; theta = $null; verta = $null; room = $null
        anchor_sha256 = $RECIPE.anchor_sha256; pad_index = $RECIPE.pad_index
        qualified_roles = $RECIPE.qualified_roles
        unqualified_roles = $RECIPE.unqualified_roles
    }
}

function New-UnboundPose([string]$why) {
    return [ordered]@{ kind = 'unbound'; source = $why; mark_sha256 = $null
                       mark_stage = $null; shape = 'none'
                       x = $null; y = $null; z = $null; theta = $null; verta = $null; room = $null }
}

# ------------------------------------------------------- the capture schedule
#
# THE GRID IS DECLARED IN RECORD-INDEX UNITS, which is the unit a declared
# moment can be held to. The engine windows on ONE counter: SL_SHOT_READ_FIRST/
# READ_LAST select the input record index, otherwise SL_SHOT_FIRST/LAST select
# the frame counter (src/gfx/sl_gfx_sdl.c:789-802), and the rule is "at least
# EVERY since the last capture" inside the window, never divisibility (:816-824).
#
#   * The record index advances once per retrace (sl_input_vi_advance,
#     src/platform/sl_ultra_shim.c:2287) and SL_VI_CATCHUP=0 clamps the
#     catch-up to zero (:625), so in level it takes every integer value at
#     successive presents and the first moment IS read_first.
#   * The frame counter advances per retrace (:1497) AND per game-frame
#     boundary (sl_frame_advance, src/game/frametiming.c:100), so it steps by
#     two per present; the parity of its first in-level value depends on how
#     many retraces the load consumed, which is not known before the run. A
#     moment declared in frame units is therefore a moment that may be one off,
#     and a schedule that cannot be held to is not a schedule.
#
# WHERE THE NUMBERS COME FROM. The measured recipe ran 420 pumped frames and
# printed its per-60-frame reports at display-list frames 1, 61, 121 and 181,
# so the load consumed about 58 pumped frames and a report falls near record
# indices 59, 119, 179 and 239. The windows below put the captures between the
# second and the fourth of those, so a census report brackets them on both
# sides - which is what the WINDOW_* deltas are read from.
function New-ReadShots([int]$readFirst, [int]$readLast, [int]$every, [string]$purpose) {
    $m = @()
    $f = $readFirst
    while ($f -le $readLast) { $m += $f; $f += $every }
    return [ordered]@{
        gate             = 'read'
        gate_env         = @('SL_SHOT_READ_FIRST', 'SL_SHOT_READ_LAST', 'SL_SHOT_EVERY')
        first            = $readFirst
        last             = $readLast
        every            = $every
        read_first       = $readFirst
        read_last        = $readLast
        expected_moments = $m
        count            = $m.Count
        width            = $script:shotW
        height           = $script:shotH
        purpose          = $purpose
        derivation       = (('the engine captures the first moment inside the window and then every ' +
                             'moment at least EVERY past the last capture (src/gfx/sl_gfx_sdl.c' +
                             ':816-824), and SL_SHOT_READ_FIRST/READ_LAST window on the input record ' +
                             'index (:789-802), which advances once per retrace and takes every ' +
                             'integer value at successive in-level presents under SL_VI_CATCHUP=0. ' +
                             'read_first={0} read_last={1} every={2} is therefore exactly {3} ' +
                             'captures at {4}. The first and last fields repeat the window bounds in ' +
                             'that same unit because the engine windows on one counter at a time.') -f `
                            $readFirst, $readLast, $every, $m.Count, ($m -join ', '))
    }
}

# ------------------------------------------------------------- the arm table
function New-Arm($name, $phase, $stage, $stageNum, $item, $pose, $frames, $extra, $shots, $seconds, $witnesses, $pair, $role, $padSteps, $control, $note) {
    $armDir = Join-Path $script:attemptDir $name
    $cfg    = Join-Path $armDir 'config.ini'
    $save   = Join-Path $armDir 'eeprom.bin'
    $env = [ordered]@{
        SL_ROM                 = $script:rom
        SL_BOOT_LEVEL          = "$stageNum"
        SL_BOOT_DIFFICULTY     = '1'
        SL_BOOT_ELAPSED_FRAMES = '0'
        SL_FRAMES              = "$frames"
        SL_WINDOW              = '1'
        SL_WINDOW_SIZE         = "$($script:shotW)x$($script:shotH)"
        SL_WINDOW_POS          = '5120,-220'
        SL_AUDIO               = '0'
        SL_VI_CATCHUP          = '0'
        SL_RUN                 = '0'
        SL_CHEATS              = 'invincible,invisible,maxammo'
        SL_RNG_SEED            = $script:seed
        SL_CONFIG              = $cfg
        SL_EEPROM_RW           = $save
        SL_WORLDASSET_ROOT     = $script:waRoot
        SL_TEXPACK_ROOT        = $script:tpRoot
        SL_ASSET_OVERRIDE_DIR  = $script:aoDir
    }
    if ($pose.kind -eq 'teleport') {
        $env['SL_TELEPORT']       = '1'
        $env['SL_TELEPORT_X']     = "$($pose.x)"
        $env['SL_TELEPORT_Y']     = "$($pose.y)"
        $env['SL_TELEPORT_Z']     = "$($pose.z)"
        $env['SL_TELEPORT_THETA'] = "$($pose.theta)"
        $env['SL_TELEPORT_VERTA'] = "$($pose.verta)"
        $env['SL_TELEPORT_ROOM']  = "$($pose.room)"
    }
    foreach ($k in $extra.Keys) { $env[$k] = "$($extra[$k])" }
    $padRec = $null
    if ($padSteps) {
        # SL_PAD_VIRTUAL is MANDATORY: without it the script never applies
        # (src/platform/sl_input.c, vpad_attach returns on an empty kind).
        $env['SL_PAD_VIRTUAL'] = 'xbox'
        $env['SL_PAD_SCRIPT']  = $script:padPath
        $padRec = [ordered]@{ path = $script:padPath; sha256 = $script:padSha; steps = $padSteps }
    }
    $ctlRec = $null
    if ($control) {
        # SL_INPUT is the native build's replay variable. An explicit
        # SL_RNG_SEED still wins over the stream's .spec sidecar
        # (src/platform/sl_ultra_shim.c:3446-3470), and these streams carry no
        # SEED= line, so they were recorded on the engine's deterministic boot
        # state - the state this batch pins by name.
        $env['SL_INPUT'] = $control.input_path
        $ctlRec = [ordered]@{
            name = $control.name
            input = $control.input_path;  input_sha256 = $control.input_sha256
            spec  = $control.spec_path;   spec_sha256  = $control.spec_sha256
            save  = $control.save_path;   save_sha256  = $control.save_sha256
            reads = $control.reads
            covers = $control.covers
            note = 'a pinned stream supplies a reproducible viewpoint away from the start pad and movement within one run; it does not record which viewpoint, so no named pose is claimed from it'
        }
    }
    $shotRec = $null
    if ($shots) {
        $env['SL_SHOT']       = (Join-Path $armDir 'shot')
        $env['SL_SHOT_EVERY'] = "$($shots.every)"
        if ($shots.gate -eq 'read') {
            $env['SL_SHOT_READ_FIRST'] = "$($shots.read_first)"
            $env['SL_SHOT_READ_LAST']  = "$($shots.read_last)"
        } else {
            $env['SL_SHOT_FIRST'] = "$($shots.first)"
            $env['SL_SHOT_LAST']  = "$($shots.last)"
        }
        $shotRec = $shots
    }
    return [ordered]@{
        name = $name; phase = $phase; stage = $stage; stage_number = $stageNum
        pose = $pose
        frames = $frames
        shots = $shotRec
        pad_script = $padRec
        control = $ctlRec
        expected_seconds = $seconds
        cwd = $repo
        # THE EXECUTABLE THIS ARM RUNS, recorded per arm. The default is the
        # script-level one, so every existing arm records exactly the binary it
        # has always run; an arm group that needs an isolated runtime (the
        # candidate arms below) overrides this field and its own argv, and
        # Invoke-Arm launches the arm's own exe. Without a per-arm field,
        # Invoke-Arm's one script-level $exe makes "which binary did this arm
        # run" unanswerable from the record whenever a batch mixes runtimes.
        exe = $exe
        argv = @($exe)
        env = $env
        seed_hex = $script:seed
        expect = [ordered]@{ presentation = $PRESENTATION_XBLA; presentation_set = 'xbla'
                             stage_item = $item; worldasset_root = $script:waRoot
                             width = $script:shotW; height = $script:shotH }
        scratch_config = $cfg
        scratch_config_sha256 = $script:cfgSha
        scratch_save = $save
        pinned_eeprom = $script:eepromOf[$stage]
        witnesses = $witnesses
        pair = $pair
        role = $role
        note = $note
    }
}

# The witnesses every arm carries, whatever it measures: the run happened, it
# stayed live, it took the pinned state, and it was looking at the presentation
# and the stage the arm declared. Absence of any of the last four is a FAIL in
# the checker (FAIL_ON_ABSENCE), because a run that cannot confirm what it was
# looking at measured something else.
$PREFLIGHT_WITNESSES = @('EXIT_ZERO', 'NOT_TIMED_OUT', 'LIVENESS', 'SEED_ECHOED',
                         'PRESENTATION_XBLA_ACTIVE', 'ASSET_ROOT_BOUND', 'STAGE_ITEM_LOADED')

# The capture witnesses every capturing arm carries. CAPTURE_SCHEDULE_EXACT is
# the one that makes two identical WRONG schedules fail: names, counts,
# dimensions and payload completeness are all satisfied by a pair that captured
# at the wrong moments in the wrong unit, and the moment-by-moment comparison
# against the declared grid is not.
$CAPTURE_WITNESSES = @('CAPTURE_SET_EXACT', 'CAPTURE_SCHEDULE_EXACT')

# ---- the acquisition probe ------------------------------------------------
function New-AcqArms {
    $arms = @()
    foreach ($a in $ACQUISITION) {
        $st = $script:stageOf[$a.stage]
        # A mark is honoured here if one happens to exist, and its absence is
        # not a blocker: an acquisition probe at the stage's own start pad is a
        # legitimate reading of what that stage does, and it says which it was.
        $pose = $null
        # WAS THE CANDIDATE THERE AT ALL? The fallback below has to say which of
        # two different things happened, and they are not interchangeable: no
        # mark was offered, or a mark WAS offered and was refused. Reporting a
        # refusal as an absence is how a reader of the receipt concludes no mark
        # existed when one did and was rejected for being of another stage.
        $candExisted = $false
        if ($Marks) {
            $cand = Join-Path $Marks ("acq-" + $a.stage + ".txt")
            if (Test-Path -LiteralPath $cand) {
                $candExisted = $true
                $pose = Get-BoundPose ("acq-" + $a.stage + ".txt") $st.stage ("ACQ-" + $a.stage)
            }
        }
        if ($null -eq $pose) {
            $pose = [ordered]@{ kind = 'engine-start-pad'
                # BOTH WORDINGS CARRY THE SAME CLOSING PHRASE. What differs is
                # the first clause - whether a mark was offered at all - and
                # that is the whole point of the distinction. The closing
                # phrase is what says the probe is a start-pad probe rather
                # than a recorded pose, and it is true in both cases, so it is
                # stated in both rather than only in the one a reader happens
                # to hit.
                source = $(if ($candExisted) {
                        ("the stage's own start pad; acq-{0}.txt WAS present and was REFUSED - it is " +
                         "not of stage {1} - and a probe at the start pad is declared as such rather " +
                         "than recorded as a pose") -f $a.stage, $st.stage
                    } else {
                        ("the stage's own start pad; no acq-{0}.txt was present, and a probe at the " +
                         "start pad is declared as such rather than recorded as a pose") -f $a.stage
                    })
                mark_sha256 = $null; mark_stage = "$($st.stage)"; shape = 'engine-start-pad'
                x = $null; y = $null; z = $null; theta = $null; verta = $null; room = $null
                anchor_sha256 = $null; pad_index = 0
                qualified_roles = @(); unqualified_roles = @('numeric_teleport_mark') }
        }
        $arms += New-Arm ("ACQ-" + $a.stage) 'ACQ' $a.stage $st.stage $st.item $pose `
            $a.frames @{ SL_DL_CENSUS = '1'; SL_PERF_OPT = '1'; SL_PERF_BG_FAR = '1' } $null $a.seconds `
            ($PREFLIGHT_WITNESSES + @('CENSUS_REPORT_PRESENT')) $null '' $null $null `
            ("probe for the {0} role. {1} Stage reason: {2}" -f $a.role, $a.justification, $st.why)
    }
    return $arms
}

# ---- the measured far pair, and everything that runs at the same recipe ---
function New-RecipeArms($poses) {
    $arms = @()
    $stage   = $RECIPE.stage
    $stageNo = $RECIPE.stage_number
    $item    = $script:stageOf[$stage].item
    $pose    = New-RecipePose
    $frames  = 600

    # The captures sit between the per-60-frame reports near record indices 119
    # and 239, so the census brackets them on both sides.
    $shots3 = New-ReadShots 140 220 40 'three captures between two census reports at the measured recipe'

    # ---- B: the measured far pair. SL_PERF_OPT alone differs. -------------
    $bOff = @{ SL_DL_CENSUS = '1'; SL_PERF_BG_FAR = '1'; SL_PERF_OPT = '0' }
    $bOn  = @{ SL_DL_CENSUS = '1'; SL_PERF_BG_FAR = '1'; SL_PERF_OPT = '1' }
    $arms += New-Arm 'B-depth-off' 'B' $stage $stageNo $item $pose $frames $bOff $shots3 130 `
        ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('CENSUS_REPORT_PRESENT',
          'FAR_FRAMES_POSITIVE', 'WINDOW_FAR0_DELTA_ZERO', 'WINDOW_FAR1_DELTA_ZERO',
          'CAPTURE_NAMES_MATCH_PAIR')) 'B-depth-on' 'off' $null $null `
        'the original CPU far path at the measured recipe; its window must show no GPU far submission at all. GPU_PROGRAM_READY is NOT asked of this half: with the row off the transform is never asked for, so the readiness line cannot be printed (src/gfx/sl_gfx_bggpu.c:350 is reached only from an opt-in walk).'
    $arms += New-Arm 'B-depth-on' 'B' $stage $stageNo $item $pose $frames $bOn $shots3 130 `
        ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('CENSUS_REPORT_PRESENT', 'GPU_PROGRAM_READY',
          'FAR_FRAMES_POSITIVE', 'WINDOW_FAR0_DELTA_POSITIVE', 'WINDOW_FAR1_DELTA_POSITIVE',
          'WINDOW_FAR_NOT_ADMITTED_DELTA_ZERO', 'CAPTURE_NAMES_MATCH_PAIR',
          'SHOT_MOMENTS_MATCH', 'FRAMES_BYTE_IDENTICAL')) 'B-depth-off' 'on' $null $null `
        'both far ranges must have submitted real draw calls AND indices INSIDE the capture bracket, no walk may have been refused for its far range there, and the pair''s pixels must be identical. The single-range column is NOT asked for: it read 0 calls / 0 indices on every report of the measured run, so a positive single-range witness here would demand what this scene is measured not to produce - see not_covered SINGLE_RANGE_SUBMISSION.'

    # ---- C: the guard is a guard ----------------------------------------
    $arms += New-Arm 'C-guard' 'C' $stage $stageNo $item $pose $frames `
        @{ SL_DL_CENSUS = '1'; SL_PERF_OPT = '1' } $shots3 130 `
        ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('CENSUS_REPORT_PRESENT', 'GPU_PROGRAM_READY',
          'FAR_FRAMES_POSITIVE', 'WINDOW_FAR0_DELTA_ZERO', 'WINDOW_FAR1_DELTA_ZERO',
          'WINDOW_FAR_NOT_ADMITTED_DELTA_POSITIVE', 'CENSUS_SINGLE_MATCHES_REF')) $null '' $null $null `
        'SL_PERF_BG_FAR is absent here and nowhere else; the far frames still happen, so zero FAR0/FAR1 across the window with a positive far-range refusal is the guard refusing them rather than an absence of far frames. CENSUS_SINGLE_MATCHES_REF compares the single-range column against the far-admitted arm''s: both are measured at zero here, so it is a consistency reading and not a positive claim.'

    # ---- D: the translucent class inside the walk, paired identity ------
    # SL_BG_XLU_LATE=0 in BOTH halves draws the translucent class inside the
    # walk instead of at the late draw, which the GPU transform refuses
    # (g_bggpu_r_late - the measured run refused all 181 walks' late draws).
    # The blended reading is the per-frame count of background GL draws blended
    # with no alpha reject, measured at 26 per frame at this recipe.
    $dOff = @{ SL_DL_CENSUS = '1'; SL_PERF_BG_FAR = '1'; SL_BG_XLU_LATE = '0'; SL_PERF_OPT = '0' }
    $dOn  = @{ SL_DL_CENSUS = '1'; SL_PERF_BG_FAR = '1'; SL_BG_XLU_LATE = '0'; SL_PERF_OPT = '1' }
    $arms += New-Arm 'D-blend-off' 'D' $stage $stageNo $item $pose $frames $dOff $shots3 130 `
        ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('CENSUS_REPORT_PRESENT',
          'FAR_FRAMES_POSITIVE', 'WINDOW_FAR0_DELTA_ZERO', 'WINDOW_FAR1_DELTA_ZERO',
          'WINDOW_BLENDED_POSITIVE', 'CAPTURE_NAMES_MATCH_PAIR')) 'D-blend-on' 'off' $null $null `
        'the original CPU far path with the translucent class inside the walk'
    $arms += New-Arm 'D-blend-on' 'D' $stage $stageNo $item $pose $frames $dOn $shots3 130 `
        ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('CENSUS_REPORT_PRESENT', 'GPU_PROGRAM_READY',
          'FAR_FRAMES_POSITIVE', 'WINDOW_FAR0_DELTA_POSITIVE', 'WINDOW_FAR1_DELTA_POSITIVE',
          'WINDOW_BLENDED_POSITIVE', 'CAPTURE_NAMES_MATCH_PAIR',
          'SHOT_MOMENTS_MATCH', 'FRAMES_BYTE_IDENTICAL')) 'D-blend-off' 'on' $null $null `
        'the GPU far path with the translucent class inside the walk. This arm makes NO claim that far-range geometry is blended or that depth ordering against it is correct - see not_covered BLENDED_FAR_RANGE_GEOMETRY and OCCLUSION_ORDERING.'

    # ---- F: movement and turning WITHIN one run -------------------------
    # The whole step file runs in both halves, and the captures straddle the
    # scripted traverse, so each half's window reading is taken while the
    # viewpoint is moving and turning.
    $fShots = New-ReadShots 130 250 40 'four captures straddling the scripted traverse'
    $fOff = @{ SL_DL_CENSUS = '1'; SL_PERF_BG_FAR = '1'; SL_PERF_OPT = '0' }
    $fOn  = @{ SL_DL_CENSUS = '1'; SL_PERF_BG_FAR = '1'; SL_PERF_OPT = '1' }
    $arms += New-Arm 'F-move-off' 'F' $stage $stageNo $item $pose $frames $fOff $fShots 130 `
        ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('CENSUS_REPORT_PRESENT',
          'PAD_SCRIPT_STEPS_APPLIED', 'CAPTURE_NAMES_MATCH_PAIR')) 'F-move-on' 'off' `
        $script:padStepCount $null `
        'the original CPU far path while the synthetic pad moves and turns'
    $arms += New-Arm 'F-move-on' 'F' $stage $stageNo $item $pose $frames $fOn $fShots 130 `
        ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('CENSUS_REPORT_PRESENT', 'GPU_PROGRAM_READY',
          'PAD_SCRIPT_STEPS_APPLIED', 'WINDOW_FAR0_DELTA_POSITIVE',
          'CAPTURE_NAMES_MATCH_PAIR', 'SHOT_MOMENTS_MATCH',
          'FRAMES_BYTE_IDENTICAL')) 'F-move-off' 'on' $script:padStepCount $null `
        'the GPU far path under the same scripted movement. F_TRANSITION_CROSSED is a SEQUENCE witness over this arm''s own capture intervals and is NOT_COVERED when they show no crossing - see not_covered MOVEMENT_CROSSES_A_TRANSITION.'

    # ---- E: a second pose, from a saved control -------------------------
    # Only when -Control names a control of this stage. There is no mark here
    # and no coordinate: the claim is a second independent pose, not a named
    # one.
    if ($poses.control) {
        $c = $poses.control
        $eOff = @{ SL_DL_CENSUS = '1'; SL_PERF_BG_FAR = '1'; SL_PERF_OPT = '0' }
        $eOn  = @{ SL_DL_CENSUS = '1'; SL_PERF_BG_FAR = '1'; SL_PERF_OPT = '1' }
        $arms += New-Arm 'E-pose2-off' 'E' $stage $stageNo $item $pose $frames $eOff $shots3 130 `
            ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('CENSUS_REPORT_PRESENT',
              'INPUT_STREAM_REPLAYED', 'CAPTURE_NAMES_MATCH_PAIR')) 'E-pose2-on' 'off' $null $c `
            'the original CPU far path at the viewpoint the pinned stream reaches'
        $arms += New-Arm 'E-pose2-on' 'E' $stage $stageNo $item $pose $frames $eOn $shots3 130 `
            ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('CENSUS_REPORT_PRESENT', 'GPU_PROGRAM_READY',
              'INPUT_STREAM_REPLAYED', 'CAPTURE_NAMES_MATCH_PAIR', 'SHOT_MOMENTS_MATCH',
              'FRAMES_BYTE_IDENTICAL')) 'E-pose2-off' 'on' $null $c `
            'a second independent viewpoint at which the two paths must still produce identical pixels. Which viewpoint is not recorded anywhere, so none is named.'
    }

    # ---- G4: the clean combined comparison, NO census -------------------
    # CPU workers, the GPU transform, the background bypass and the sky's
    # validated float carrier, all in one run and all without a statistics
    # reader - because the bypass and the vertex helper are refused whenever one
    # is active (src/gfx/sl_gfx_dl.c:18357). The skybatch lines are not
    # census-gated (src/gfx/sl_gfx_skybatch.c:710, :1375), so nothing here needs
    # one.
    $gOff = @{ SL_PERF_OPT = '0'; SL_PERF_BG_FAR = '1' }
    $gOn  = @{ SL_PERF_OPT = '1'; SL_PERF_BG_FAR = '1' }
    $arms += New-Arm 'G4-combined-off' 'G' $stage $stageNo $item $pose $frames $gOff $shots3 130 `
        ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('NO_CENSUS_REQUESTED',
          'SKYBATCH_INDEXED_DRAWS_POSITIVE', 'CAPTURE_NAMES_MATCH_PAIR')) 'G4-combined-on' 'off' $null $null `
        'the row off, clean: the pass that ships. The float carrier marker is NOT asked of this half - the carrier the row selects with the row off is the byte carrier, so a prod marker here would be the wrong reading, not a missing one.'
    $arms += New-Arm 'G4-combined-on' 'G' $stage $stageNo $item $pose $frames $gOn $shots3 130 `
        ($PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES + @('NO_CENSUS_REQUESTED', 'GPU_PROGRAM_READY',
          'SKY_FLOAT_CARRIER_PROD_ONCE', 'SKYBATCH_INDEXED_DRAWS_POSITIVE',
          'REDUCTION_REPORT_PRESENT', 'UNSOUND_DRAWS_ZERO', 'REPAIRS_REPORTED',
          'CAPTURE_NAMES_MATCH_PAIR', 'SHOT_MOMENTS_MATCH', 'FRAMES_BYTE_IDENTICAL')) `
        'G4-combined-off' 'on' $null $null `
        'the row on, clean: CPU workers, the GPU transform, the background bypass and the sky''s prod float carrier in one run, with no statistics reader to refuse any of them. A POSITIVE reduced-walk count is NOT asked for: the reduction admits only fogless walks and this stage carries a level fog table, so what is required is that its own inline report was printed and reported no unsound draw. The carrier marker and the indexed sky draws were both measured present at this recipe.'

    return $arms
}

# ---- the two missing combinations, -Discriminate --------------------------
#
# WHAT THE SAVED PAIR LEFT OPEN. The measured far pair differs in SL_PERF_OPT
# alone, and that one variable selects THREE things at once: the skydome batch
# path, the carrier that path uploads its cloud colours with, and the
# background GPU vertex transform (src/platform/sl_perf.c:152-202). Its two
# halves' captures are byte-identical except one channel of one pixel, so the
# pair establishes that something in the row's selection moves that pixel and
# cannot say WHICH of the three. The two arms below are the two combinations
# the pair does not contain.
#
# THE ROW IS HELD ON IN BOTH ARMS, and that is the whole design. Each arm runs
# the row's own selection and disables ONE subsystem by its own switch, so for
# the OTHER subsystem the arm matches the combined path exactly. An arm pair
# built the other way - the row OFF with one subsystem enabled - would read a
# launch-time diagnostic state rather than the shipped selection.
#
# THE ABSENCES ARE LOAD-BEARING. What a variable does NOT say is as much of the
# declaration as what it says, so each arm's absences are declared with their
# reason and nothing is added for symmetry:
#   * GPU-only leaves SL_BG_GPU_XFORM and SL_BG_GPU_BYPASS absent, so the ON
#     row governs both and the arm's GPU selection IS the combined path's.
#   * GPU-only leaves the carrier absent; with the batch path off it is never
#     latched, and naming it would imply a choice the arm does not make.
#   * Sky-only leaves SL_SKY_BATCH and SL_SKY_BATCH_FLOAT_COLOR absent, so the
#     ON row selects the batch path WITH its PROD carrier. SL_SKY_BATCH=1 alone
#     would select the legacy BYTE carrier instead (sl_perf_sky_carrier,
#     src/platform/sl_perf.c:175-181) - a second change, and the wrong one. An
#     explicit prod would resolve to the same carrier; absence additionally
#     matches the combined arm's own environment key for key.
#
# WHAT WAS VERIFIED IN src/platform/sl_perf.c, and what it buys. Each of
# sl_perf_sky_on (:152), sl_perf_bg_xform (:184) and sl_perf_bg_bypass (:194)
# returns its own switch when that switch names exactly 0 or 1, before falling
# back to the row, and consults it only while s_row_chosen is 0 (:154, :186,
# :196). s_row_chosen is set by sl_perf_opt_set and nothing else (:112), whose
# only callers outside sl_perf.c are the two options menus - a tree-wide grep
# for sl_perf_opt_set over src/ returns src/game/options.c:4398 and
# src/native/sl_front_options.c:1106 and no third site. NO ARM PRESSES A MENU,
# so a single subsystem can be disabled underneath an ON row for the whole
# launch, with the current binary and no rebuild.
#
# EVERY OVERRIDE IS IN THE ARM'S OWN env MAP. Invoke-Arm clears every SL_*
# variable from this process and then fills the child's environment from the
# arm's map alone, so a variable exported in the parent shell reaches no arm.
# The two maps below are therefore the complete statement of each arm.
#
# THE BASELINES ARE SAVED EVIDENCE AND ARE NOT RE-RUN. The OFF and ON halves
# already exist as recorded captures; re-running them would spend two GPU
# starts to re-measure what is already on disk, and would also replace the
# evidence the single-pixel difference was read from. Both halves are bound
# here by the sha256 of every one of their three captures, by the recorded
# moment of each, and by their own saved environment, and a drift in any of
# those stops the run before anything is launched.
#
# THE REDUCTION CANNOT ACT IN EITHER ARM. Its predicate refuses every walk
# while a statistics reader is active (wbox || fstats, src/gfx/sl_gfx_dl.c
# :18398 consumed at :18427) and SL_DL_CENSUS=1 is one in both arms; sky-only
# additionally disables it outright, and the GPU-only arm leaves it exactly as
# the combined path had it.
#
# NO IDENTITY WITNESS, NO GATE, AND NO CAUSE. A difference between an arm and a
# baseline is the reading being taken, so no arm carries FRAMES_BYTE_IDENTICAL
# and the group has no gate. Which baseline an arm matches is reported; what
# produced the pixel is not inferred from it, and if neither or both arms
# match, that is recorded as interaction or nondeterminism UNRESOLVED.
$AXIS_MATRIX = [ordered]@{
    arms = @('X-gpu-only', 'X-sky-only')
    pixel = [ordered]@{ x = 71; y = 113
        convention = 'column x, row y of the image with row 0 the TOP row, which is also the first payload row: the capture writer flips GL''s bottom-left origin as it writes (src/gfx/sl_gfx_sdl.c:861-863)'
        measured = 'the saved pair reads OFF (92,118,143) and ON (92,118,142) at this pixel in all three matching capture pairs: one channel, one step' }
    # THE FROZEN BINARY. The saved baselines were taken with this executable,
    # and a diagnostic that compares new captures against them has to run the
    # same one: a different binary is a different experiment whatever its
    # environment says. No build is needed to separate the two subsystems.
    exe_sha256 = 'b91167532cc5a094e5ccd6bf6f16b0bfdba4dff4112d0df320c7b0116ea4dd8b'
    baseline_relpath = '.sightline\chat-handoff\artifacts\v040\performance-probe-far-pair-r529\native-data\runway-first-pair-529'
    # THE MOMENTS, from the saved halves' own sl_shot lines. Both halves
    # recorded exactly these three, which is what makes them a moment pin for a
    # new arm rather than a description: an arm whose captures are not at these
    # moments is not comparable with these baselines however equal its bytes.
    moments = @(
        [ordered]@{ name = 'shot-00000.ppm'; read = 140; frame = 279; cloud_phase = '139.000' }
        [ordered]@{ name = 'shot-00001.ppm'; read = 180; frame = 359; cloud_phase = '179.000' }
        [ordered]@{ name = 'shot-00002.ppm'; read = 220; frame = 439; cloud_phase = '219.000' }
    )
    baselines = @(
        [ordered]@{ label = 'OFF-529'; arm = 'B-depth-off'
            is = 'the original CPU path: the row OFF, far admitted, census on'
            env = [ordered]@{ SL_PERF_OPT = '0'; SL_PERF_BG_FAR = '1'; SL_DL_CENSUS = '1' }
            captures = @(
                [ordered]@{ name = 'shot-00000.ppm'; sha256 = 'a091c18ea3caa20425f01534442a938201cef78268b30b81b6aa5803b20511bc' }
                [ordered]@{ name = 'shot-00001.ppm'; sha256 = '10925334203a46273ed7311145f787b35cf4322a68c7f8121ed1d680aff838da' }
                [ordered]@{ name = 'shot-00002.ppm'; sha256 = '98c4063d60aee1b4acb69ef71c5ec8b2204ea68d7cdd3b0c606be4386312ab29' }
            ) }
        [ordered]@{ label = 'ON-529'; arm = 'B-depth-on'
            is = 'the combined path: the row ON, which selects the batch path with its PROD carrier AND the GPU transform AND the reduction, far admitted, census on'
            env = [ordered]@{ SL_PERF_OPT = '1'; SL_PERF_BG_FAR = '1'; SL_DL_CENSUS = '1' }
            captures = @(
                [ordered]@{ name = 'shot-00000.ppm'; sha256 = '2e6ef747722a7b29dd319d727f7341f6ce772b1ae1977d63b263d98ced91f77e' }
                [ordered]@{ name = 'shot-00001.ppm'; sha256 = '49c571a014ce308a14f5b69a67789e3ecf8d0b5a57daa0f690b5c88650bef874' }
                [ordered]@{ name = 'shot-00002.ppm'; sha256 = 'ec8ba4eb3b26630ecf797bc200290d0e2f713f9e347500debd9df345eed4833a' }
            ) }
    )
    # The arms' one-axis comparison is against the COMBINED arm, because that
    # is the environment each of them is one disable away from.
    assert_against = 'ON-529'
    constants = [ordered]@{
        SL_PERF_OPT    = 'the row, held ON in both arms: each arm runs the row''s own selection for the subsystem it does not disable'
        SL_PERF_BG_FAR = 'far admission, identical in both arms and in both baselines, so it is not a variable (sl_perf_bg_far_admit, src/platform/sl_perf.c:204)'
        SL_DL_CENSUS   = 'the per-depth census the far-range window deltas are read from; both saved halves set it'
    }
    axes = [ordered]@{
        sky_disable = [ordered]@{ vars = @('SL_SKY_BATCH')
            disables = 'the skydome batch path and with it the PROD carrier upload, returning the sky to the immediate glColor4ub path (sl_skybatch_on -> sl_perf_sky_on, src/platform/sl_perf.c:152)' }
        gpu_disable = [ordered]@{ vars = @('SL_BG_GPU_XFORM', 'SL_BG_GPU_BYPASS')
            disables = 'the background GPU vertex transform and the loop reduction the row selects with it (sl_bggpu_opt_in -> sl_perf_bg_xform :184, sl_perf_bg_bypass :194)' }
    }
    # THE VARIABLES THAT DIFFER BY DESIGN AND ARE NOT AN AXIS. Every arm points
    # at its OWN scratch config, its OWN fresh copy of the pinned initial save
    # and its OWN capture prefix: SL_EEPROM_RW is read-write and the engine
    # rewrites the save in place, so nothing mutable may be shared. The PATHS
    # differ from the baselines' and from each other; what is behind them does
    # not - the config is written from the same pinned bytes and the save is
    # copied from the same pinned repository file, both re-hashed per arm.
    per_arm_vars = [ordered]@{
        SL_CONFIG    = 'this arm''s own scratch config, written from the same pinned bytes the baselines used (sha256 verified in preflight and recorded per arm)'
        SL_EEPROM_RW = 'this arm''s own fresh copy of the same pinned initial save, re-hashed at the source before the copy and recorded after it'
        SL_SHOT      = 'this arm''s own capture prefix, so the captures being compared are this arm''s own'
    }
    comparison = 'each arm''s full P6 bytes against BOTH saved baselines at matching capture moments, whole-file equality with no tolerance, every differing byte decoded to (x, y, channel) with both values, and the declared pixel read out in every capture'
    establishes = @(
        'WHICH of the two combinations the saved pair lacks reproduces which baseline''s bytes, at the one hash-bound recipe and the same three moments, under the row the game ships with ON',
        'that the subsystem each arm disables was really disabled and the other really ran, from that arm''s own census and skybatch lines: the GPU program and positive FAR0/FAR1 deltas with an off skybatch report in the GPU-only arm, zero far submissions with exactly one PROD carrier marker in the sky-only arm',
        'that each arm''s environment differs from the combined baseline''s by exactly one subsystem''s disable, derived from the two environment maps rather than claimed',
        'that each arm captured at the same read, frame and cloud phase as both baselines, derived from its own sl_shot lines'
    )
    establishes_not = @(
        'no cause and no attribution: an arm matching a baseline says which subsystem was selected when the bytes agreed, not which expression, which arithmetic, which upload or which draw produced the pixel. The renderer cause is UNRESOLVED and nothing here changes that',
        'nothing about which draw the pixel belongs to: the position lies on the left mountain in the saved OFF capture, but shared GL state can affect a later draw, so no draw is excluded by the picture',
        'nothing about any other scene, pose, stage, moment or level: the recipe is qualified for one role and this inherits exactly that scope',
        'nothing if neither arm or both arms match a baseline - that outcome is recorded as interaction or nondeterminism UNRESOLVED and is not explained',
        'no qualification, no acceptance and no justification for a rendering change: the batch''s mandatory cases are not attempted and the saved pair''s failure stands unerased'
    )
}

# The class a variable belongs to, or 'unassigned' - which is reported rather
# than hidden, so a variable added to an arm and classified nowhere makes the
# one-axis assertion fail visibly instead of passing silently.
function Get-AxisOfVar([string]$name) {
    foreach ($ax in $AXIS_MATRIX.axes.Keys) {
        if (@($AXIS_MATRIX.axes[$ax].vars) -contains $name) { return $ax }
    }
    if (@($AXIS_MATRIX.constants.Keys) -contains $name) { return 'constant' }
    if (@($AXIS_MATRIX.per_arm_vars.Keys) -contains $name) { return 'per-arm' }
    return 'unassigned'
}

# WHICH AXES MOVE BETWEEN TWO ENVIRONMENTS, computed from the maps themselves -
# the arm's own, and the baseline's as its saved intent.json recorded it. This
# is the assertion the diagnostic rests on, so it is derived and recorded, never
# asserted in prose: every differing variable is listed with its class, and the
# assertion holds only when the BEHAVIOURAL difference is exactly one axis. A
# differing constant, or a variable no class claims, fails it by name; the
# per-arm isolation paths are listed separately because they differ in every
# comparison by design and the bytes behind them are identical and recorded.
function Get-AxisDelta($fromEnv, $toEnv, [string]$fromName, [string]$toName) {
    $keys = @()
    foreach ($k in $fromEnv.Keys) { if ($keys -notcontains $k) { $keys += $k } }
    foreach ($k in $toEnv.Keys)   { if ($keys -notcontains $k) { $keys += $k } }
    $diff = @()
    foreach ($k in $keys) {
        $a = $null
        $b = $null
        if (@($fromEnv.Keys) -contains $k) { $a = "$($fromEnv[$k])" }
        if (@($toEnv.Keys)   -contains $k) { $b = "$($toEnv[$k])" }
        if ($a -ne $b) {
            $diff += [ordered]@{ name = $k; from = $a; to = $b; axis = (Get-AxisOfVar $k) }
        }
    }
    $behave = @($diff | Where-Object { $_.axis -ne 'per-arm' })
    $axes = @()
    foreach ($d in $behave) { if ($axes -notcontains $d.axis) { $axes += $d.axis } }
    return [ordered]@{
        from = $fromName; to = $toName
        env_keys_differing = @($diff | ForEach-Object { $_.name })
        changes = $diff
        axes_moved = $axes
        behavioural_changes = @($behave | ForEach-Object {
            $f = $_.from
            if ($null -eq $f) { $f = '(absent)' }
            $t = $_.to
            if ($null -eq $t) { $t = '(absent)' }
            "$($_.name) $f->$t" })
        per_arm_paths_differing = @($diff | Where-Object { $_.axis -eq 'per-arm' } |
                                    ForEach-Object { $_.name })
        exactly_one_axis = ($axes.Count -eq 1 -and $axes[0] -ne 'constant' -and $axes[0] -ne 'unassigned')
    }
}

# A saved baseline half's directory, in this checkout or the sibling one beside
# it - the artifacts of the run that recorded it are not necessarily in the
# worktree the binary was built in.
function Get-BaselineDir($b) {
    $root = Resolve-EitherCheckout $AXIS_MATRIX.baseline_relpath
    if (-not $root) { return $null }
    $d = Join-Path $root $b.arm
    if (-not (Test-Path -LiteralPath $d)) { return $null }
    return (Resolve-Path -LiteralPath $d).Path
}

# A saved baseline half's OWN environment, read from the intent.json it was
# launched with. The pinned map above says what it is expected to be; this says
# what it was. The comparison between the two is what makes "the arm differs
# from the combined baseline by one disable" a measurement over the real
# environment instead of a claim about a remembered one.
function Get-BaselineEnvMap([string]$dir) {
    $p = Join-Path $dir 'intent.json'
    if (-not (Test-Path -LiteralPath $p)) { return $null }
    $j = Get-Content -LiteralPath $p -Raw | ConvertFrom-Json
    $m = [ordered]@{}
    foreach ($prop in $j.env.PSObject.Properties) { $m[$prop.Name] = "$($prop.Value)" }
    return $m
}

function New-DiscriminateArms {
    $stage   = $RECIPE.stage
    $stageNo = $RECIPE.stage_number
    $item    = $script:stageOf[$stage].item
    $pose    = New-RecipePose
    $frames  = 600

    # THE SAVED PAIR'S OWN GRID, unchanged: three captures between the
    # per-60-frame census reports, at the record indices both saved halves
    # captured at. A capture at another moment is not comparable with them.
    $shots = New-ReadShots 140 220 40 `
        'three captures at the record indices BOTH saved baseline halves captured at (read 140, 180, 220), so each new capture has a baseline capture at the same moment'

    # Each arm's own witnesses say whether the subsystem it disabled was really
    # off and the other really ran. The GPU-only arm must show the vertex
    # program ready and positive far-range submission deltas across its own
    # capture bracket; the sky-only arm must show zero far-range submissions
    # and the PROD carrier marker the batch path prints exactly once. No arm
    # carries FRAMES_BYTE_IDENTICAL: the comparison is the reading, not a
    # requirement on the run, and there is no in-attempt pair to compare with.
    $common = $PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES +
              @('CENSUS_REPORT_PRESENT', 'FAR_FRAMES_POSITIVE')

    $decl = @(
        [ordered]@{ name = 'X-gpu-only'; role = 'gpu-only'; disables = 'sky_disable'
            env = [ordered]@{ SL_PERF_OPT = '1'; SL_PERF_BG_FAR = '1'; SL_DL_CENSUS = '1'
                              SL_SKY_BATCH = '0' }
            absent = [ordered]@{
                SL_BG_GPU_XFORM          = 'absent so the ON row governs the transform: this arm''s GPU selection IS the combined path''s'
                SL_BG_GPU_BYPASS         = 'absent for the same reason; the census refuses the reduction in any case'
                SL_SKY_BATCH_FLOAT_COLOR = 'absent because with the batch path off the carrier is never latched (sl_skybatch_pass_begin refuses at src/gfx/sl_gfx_skybatch.c:1284 before latching at :1294)' }
            witnesses = ($common + @('GPU_PROGRAM_READY', 'WINDOW_FAR0_DELTA_POSITIVE',
                                     'WINDOW_FAR1_DELTA_POSITIVE', 'WINDOW_FAR_NOT_ADMITTED_DELTA_ZERO'))
            sky_expect = 'off'
            note = 'THE GPU TRANSFORM AS THE ROW SELECTS IT, WITH THE ORIGINAL SKY. SL_SKY_BATCH=0 is the only override: the ON row still selects the transform, the reduction and far admission exactly as the combined baseline had them, and the sky returns to the immediate path. Positive FAR0 and FAR1 deltas inside this arm''s own capture bracket are what say the transform really submitted; the skybatch report''s own off phrase and zero indexed draws are what say the batch path did not run. No sky carrier witness is asked of it, because an unlatched carrier cannot print a marker.' }
        [ordered]@{ name = 'X-sky-only'; role = 'sky-only'; disables = 'gpu_disable'
            env = [ordered]@{ SL_PERF_OPT = '1'; SL_PERF_BG_FAR = '1'; SL_DL_CENSUS = '1'
                              SL_BG_GPU_XFORM = '0'; SL_BG_GPU_BYPASS = '0' }
            absent = [ordered]@{
                SL_SKY_BATCH             = 'absent so the ON row selects the batch path: this arm''s sky selection IS the combined path''s'
                SL_SKY_BATCH_FLOAT_COLOR = 'absent so the row selects PROD. SL_SKY_BATCH=1 with no carrier switch would select the legacy BYTE carrier instead (src/platform/sl_perf.c:175-181), which is a second change and the wrong one' }
            witnesses = ($common + @('WINDOW_FAR0_DELTA_ZERO', 'WINDOW_FAR1_DELTA_ZERO',
                                     'SKY_FLOAT_CARRIER_PROD_ONCE', 'SKYBATCH_INDEXED_DRAWS_POSITIVE'))
            sky_expect = 'prod'
            note = 'THE SKYDOME BATCH AND ITS PROD CARRIER AS THE ROW SELECTS THEM, WITH THE ORIGINAL BACKGROUND PATH. SL_BG_GPU_XFORM=0 and SL_BG_GPU_BYPASS=0 are the only overrides, and the sky switches stay absent so the row''s own PROD carrier is what runs. Zero FAR0 and FAR1 deltas across the bracket are what say the transform did not submit; exactly one PROD carrier marker and positive indexed skybatch draws are what say the batch path did. GPU_PROGRAM_READY is NOT asked of it: with the transform never asked for, the readiness line cannot be printed (src/gfx/sl_gfx_bggpu.c:350 is reached only from an opt-in walk).' }
    )

    $arms = @()
    foreach ($d in $decl) {
        $a = New-Arm $d.name 'X' $stage $stageNo $item $pose $frames `
            $d.env $shots 130 $d.witnesses $null $d.role $null $null $d.note
        $a['matrix'] = [ordered]@{
            role = $d.role
            disables = $d.disables
            disabled_by = (@($AXIS_MATRIX.axes[$d.disables].vars | ForEach-Object { "$_=0" }) -join ' ')
            disables_what = $AXIS_MATRIX.axes[$d.disables].disables
            row = 'SL_PERF_OPT=1 - the row is ON, so every selection this arm does not disable is the row''s own'
            absent_by_design = $d.absent
            sky_expect = $d.sky_expect
            compared_against = @($AXIS_MATRIX.baselines | ForEach-Object { $_.label })
            pixel = $AXIS_MATRIX.pixel
            identity_is_a_reading = 'this arm carries no FRAMES_BYTE_IDENTICAL witness and the group has no gate: whether its captures equal a baseline''s is the measurement, not a condition on the run'
        }
        $arms += $a
    }

    # THE ONE-AXIS ASSERTION, DERIVED FROM THE SAVED BASELINE'S OWN
    # ENVIRONMENT. Each arm is asserted against the COMBINED half, which is the
    # environment it is one disable away from; the OFF half is recorded as a
    # reference delta and is NOT a one-axis comparison, because the row differs
    # there too and saying otherwise would be a false claim about the design.
    foreach ($a in $arms) {
        $asserts = @()
        foreach ($b in @($AXIS_MATRIX.baselines)) {
            $env = $script:baseEnvOf[$b.label]
            if ($null -eq $env) {
                $asserts += [ordered]@{
                    from = $b.label; to = $a.name; intent = 'unavailable'
                    asserted = $true; holds = $false
                    changes = @(); axes_moved = @()
                    behavioural_changes = @("the saved half's own intent.json was not read, so no delta could be derived")
                    per_arm_paths_differing = @(); env_keys_differing = @()
                    exactly_one_axis = $false }
                continue
            }
            $dl = Get-AxisDelta $env $a.env $b.label $a.name
            if ($b.label -eq $AXIS_MATRIX.assert_against) {
                $dl['intent'] = 'one-axis-disabled'
                $dl['asserted'] = $true
                $dl['holds'] = ($dl.exactly_one_axis -and
                                @($dl.axes_moved)[0] -eq $a.matrix.disables)
            } else {
                $dl['intent'] = 'reference-delta'
                $dl['asserted'] = $false
                $dl['holds'] = $null
                $dl['why'] = ('the row itself differs from this half, so this is not a one-axis ' +
                              'comparison and is not asserted as one; it is recorded because this ' +
                              'half is one of the two the arm is compared against')
            }
            $asserts += $dl
        }
        $a['axis_assertions'] = $asserts
        $a['axis_assertions_hold'] =
            (@($asserts | Where-Object { $_.asserted -and -not $_.holds }).Count -eq 0)
    }
    return $arms
}

# ================= THE CLIP-PREPASS CANDIDATE ARMS (-Candidate) =============
#
# THREE DEDICATED ARMS AT THE ONE HASH-BOUND RECIPE, run from an ISOLATED
# runtime so that the production build under build\win32 is neither read as the
# candidate nor written by it.
#
# WHY THE SWITCH HAS TO LIVE IN THE ARM MAP. Invoke-Arm clears every SL_*
# variable out of the parent process and then fills the child's environment
# from the arm's own map alone. That is what stops an ambient SL_* reaching a
# measured child - and it also means an arm's selection CANNOT be injected from
# outside the table. A candidate arm whose switch was exported by the caller
# would run with the switch absent and report honest zeros, which reads exactly
# like a candidate that did nothing. So the three maps are declared here.
#
# THE CORRECTNESS BASELINE IS ORIG_CPU - the original CPU / program-0 renderer.
# It is NOT the existing GPU vertex-program arm: that arm is already measured
# to differ from the original renderer at this recipe (one channel of pixel
# (71,113) reading 143 against 142 in the saved halves), so a candidate that
# reproduced the original exactly would have DIFFERED from it and been rejected
# for being right. DIAG_PROG runs in the same batch and is read as a
# diagnostic only; it is never correctness authority.
$TFCLIP_RUNTIME = [ordered]@{
    relpath = 'build\diag\tfclip'
    why = 'an isolated diagnostic runtime: its own sightline.exe and its own staged SDL2 and libwinpthread beside it, so the candidate arms neither load nor disturb the production build under build\win32'
    exe_sha256 = 'c20d69563a6440be58a1533f1ae74401685dc5cc176472d0f564d459d54b8c49'
    exe_bytes = 10998044
    dll_sha256 = [ordered]@{
        'SDL2.dll' = 'e9ea103d1981945670425c1603d7a069d807b85b07866b0a7b2857d7a4753b6f'
        'libwinpthread-1.dll' = '9e27b34c035fe7ccb62a523bd8eefe472ea6616643d31f2d1f6df6aa8b999369'
    }
    dll_rule = 'Windows resolves a DLL beside the executable before the working directory, so an arm whose exe is the diagnostic build loads the diagnostic build''s own staged runtime. The digests are pinned and verified before any arm is launched; they are READ and never written.'
    source_sha256 = [ordered]@{
        'src/gfx/sl_gfx_bgtf.c' = '61bcf2e553fbbef3858d99a101bfe6c18b37f45785c42d6e17bbe5bbc0afb71e'
        'src/gfx/sl_gfx_bgtf.h' = '99926d83c0914e9ccdda445ef810d3d6ae1692cc2c7321a10f094181cdf8f88a'
        'src/gfx/sl_gfx_dl.c'   = '27ec33dfd121cc15ca88872f91bccdf8cc71d0fea6001ea0af4b7901ffdd837d'
        'src/gfx/sl_gfx_sdl.c'  = 'b1f76089ca8d9b923bf54c210f04f23d1bd8d712a0cdc5d87c4513f5652ca1f1'
    }
}

# The witnesses every candidate-group arm carries, and the three maps. Each
# map is complete: what is NOT in it is absent from the child, and the
# `absent` field records why for the two switches whose absence is load-bearing.
$TFCLIP_ARMS = @(
    [ordered]@{
        name = 'ORIG_CPU'; role = 'orig-cpu'
        is = 'THE CORRECTNESS BASELINE. The original CPU / program-0 renderer.'
        env = [ordered]@{ SL_PERF_OPT = '0'; SL_PERF_BG_FAR = '1'; SL_DL_CENSUS = '1'
                          SL_BG_GPU_XFORM = '0'; SL_BG_GPU_BYPASS = '0'; SL_SKY_BATCH = '0' }
        absent = [ordered]@{
            SL_BG_TF_CLIP = 'absent: this arm is the renderer the candidate is judged against, so it must not be able to take the candidate branch at all'
            SL_SKY_BATCH_FLOAT_COLOR = 'absent because with the batch path off the carrier is never latched, so the switch would select nothing'
        }
        authority = 'src/platform/sl_perf.c sl_perf_opt(): with the row unpressed SL_PERF_OPT is read from the environment and wins over the stored row, so this arm is the row OFF whatever the scratch config carries'
        tf_expect = 'off'
    }
    [ordered]@{
        name = 'TF_ON'; role = 'candidate'
        is = 'THE CANDIDATE. The background GPU clip prepass doing the work.'
        env = [ordered]@{ SL_PERF_OPT = '1'; SL_PERF_BG_FAR = '1'; SL_DL_CENSUS = '1'
                          SL_BG_GPU_XFORM = '1'; SL_BG_GPU_BYPASS = '0'; SL_SKY_BATCH = '0'
                          SL_BG_TF_CLIP = '1' }
        absent = [ordered]@{
            SL_SKY_BATCH_FLOAT_COLOR = 'absent for the same reason as in the baseline arm'
        }
        authority = 'src/gfx/sl_gfx_dl.c:18919 - the candidate branch sits inside `if (opt_xform)`, so SL_PERF_OPT=1 and SL_BG_GPU_XFORM=1 are the candidate''s PRECONDITION and not free variables. No pair of arms can differ in SL_BG_TF_CLIP alone, and this group says so rather than claiming a one-variable comparison.'
        tf_expect = 'on'
    }
    [ordered]@{
        name = 'DIAG_PROG'; role = 'diagnostic'
        is = 'DIAGNOSTIC ONLY. The existing GPU vertex-program shader.'
        env = [ordered]@{ SL_PERF_OPT = '1'; SL_PERF_BG_FAR = '1'; SL_DL_CENSUS = '1'
                          SL_BG_GPU_XFORM = '1'; SL_BG_GPU_BYPASS = '0'; SL_SKY_BATCH = '0' }
        absent = [ordered]@{
            SL_BG_TF_CLIP = 'absent: this arm is the EXISTING shader, which is what makes it the diagnostic rather than the candidate'
            SL_SKY_BATCH_FLOAT_COLOR = 'absent for the same reason as in the baseline arm'
        }
        authority = 'NEVER correctness authority. It is already measured to differ from ORIGINAL at this recipe, so a candidate difference from it is uninterpretable on its own, and a candidate match with it would be a match with a known defect.'
        tf_expect = 'off'
    }
)

function New-CandidateArms {
    $stage   = $RECIPE.stage
    $stageNo = $RECIPE.stage_number
    $item    = $script:stageOf[$stage].item
    $pose    = New-RecipePose
    $frames  = 600

    # THE SAVED HALVES' OWN GRID, so each new capture has a saved ORIGINAL
    # capture at the same moment and the three arms are comparable with each
    # other moment for moment as well.
    $shots = New-ReadShots 140 220 40 `
        'the three moments both saved halves recorded (read 140, 180, 220), so every new capture has a saved ORIGINAL capture at the same moment'

    $common = $PREFLIGHT_WITNESSES + $CAPTURE_WITNESSES +
              @('CENSUS_REPORT_PRESENT', 'FAR_FRAMES_POSITIVE')

    # The candidate's own coverage witnesses, read from the #95 clip-prepass
    # line. PROGRAM_COLUMNS_MUST_INVERT is here and
    # WINDOW_FAR0/FAR1_DELTA_POSITIVE are deliberately NOT: a prepass walk
    # leaves gpu_walk 0 and selects pool 0, so the vertex program's own far
    # columns read ZERO when the candidate works and those two witnesses would
    # FAIL on success. The checker refuses them on a candidate arm by name
    # (TF_CANDIDATE_WITNESS_SET_VALID).
    $tfOn = @('TF_STATE_READY', 'TF_PREPASSES_POSITIVE', 'TF_VERTICES_POSITIVE',
              'TF_SUBMITTED_POSITIVE', 'TF_SOURCE_DEPTH_FAR0_POSITIVE',
              'TF_SOURCE_DEPTH_FAR1_POSITIVE', 'TF_WALKS_POSITIVE',
              'TF_ADMITTED_POSITIVE', 'TF_REFUSALS_ARE_NUMBERS_AND_A_FALLBACK_FAILS',
              'TF_REUSES_ARE_NOT_COVERAGE', 'TF_REFILL_AND_EVICTION_READ_SEPARATELY',
              'TF_SOURCE_DEPTH_SINGLE_IS_A_CONSISTENCY_READING',
              'PROGRAM_COLUMNS_MUST_INVERT')
    $tfOff = @('TF_OFF_ARMS_READ_ZERO')

    $arms = @()
    foreach ($d in $TFCLIP_ARMS) {
        $w = $common
        if ($d.tf_expect -eq 'on') { $w = $common + $tfOn } else { $w = $common + $tfOff }
        # The diagnostic arm reads the program's own columns, because on it
        # the program IS the path under observation.
        if ($d.role -eq 'diagnostic') {
            $w = $w + @('GPU_PROGRAM_READY', 'WINDOW_FAR0_DELTA_POSITIVE',
                        'WINDOW_FAR1_DELTA_POSITIVE')
        }
        $a = New-Arm $d.name 'T' $stage $stageNo $item $pose $frames `
            $d.env $shots 130 $w $null $d.role $null $null $d.is
        # THE ISOLATED RUNTIME. The arm's own exe and argv, so Invoke-Arm
        # launches this binary and the record says which one it launched.
        $a['exe'] = $script:tfExe
        $a['argv'] = @($script:tfExe)
        $a['candidate'] = [ordered]@{
            role = $d.role
            is = $d.is
            authority = $d.authority
            absent_by_design = $d.absent
            tf_expect = $d.tf_expect
            runtime = $TFCLIP_RUNTIME.relpath
            runtime_dir = $script:tfBin
            exe = $script:tfExe
            exe_sha256 = $script:tfExeSha
            dll_sha256 = $script:tfDllSha
            correctness_baseline = 'ORIG_CPU'
            diagnostic_only = ($d.role -eq 'diagnostic')
            compared_against = @('the saved ORIGINAL captures at the saved moments',
                                 'the other arms of THIS binary, byte for byte')
            pixel = $AXIS_MATRIX.pixel
        }
        $arms += $a
    }
    return $arms
}

# ============ MATCHED UNSCORED WARMUPS, TWO OPPOSITE-ORDER BLOCKS ==========
#
# The orchestration retained from the counterbalanced repeat series, reused
# here rather than re-derived. Its scene there was the sky repeat series, not
# this recipe, so what is reused is the DESIGN; none of its measured numbers
# is carried over or attributed to this candidate.
#
# WHAT A WARMUP IS. A REAL native start. It is claimed in the same budget as a
# measured arm, it spends a start, it is recorded with role WARMUP and its own
# label, and it is NEVER SCORED. A spent warmup is not a free setup incident
# and is never refunded or re-run. Its captures, its stderr and its timing are
# all retained in its own record and read by no verdict.
#
# WHY IT CANNOT BE SCORED BY ACCIDENT. Warmup records are kept in a SEPARATE
# list and are never written into the batch's `arms`, which is the list the
# witness checker consumes. Isolation by construction rather than by a flag the
# next reader has to notice: a warmup cannot reach a scored verdict even
# through a coding mistake, which is the property the original design bought
# with its private accumulator.
#
# THE CHRONOLOGY IS WHERE THE COUNTERBALANCE LIVES, not the labels. Two blocks
# in OPPOSITE order, one warmup IMMEDIATELY BEFORE each measured arm in that
# arm's OWN condition, and one predeclared settle immediately before EACH
# warmup. Between a warmup and the measured arm it precedes, only the mandatory
# bounded steps below may run; anything else - a probe, an analysis, an extra
# settle, a copy, a conversion, a telemetry or statistics step - is refused
# while a pair is open, and an unknown step is refused outright.
$WARMUP_SETTLE_SECONDS = 60
$PAIR_MANDATORY_STEPS = @('WARMUP_CLEANUP', 'WARMUP_QUALIFICATION',
                          'NEXT_START_ACCOUNTING')
$PAIR_REFUSED_STEPS = @('PROBE', 'ANALYSIS', 'EXTRA_SETTLE', 'COPY', 'CONVERSION',
                        'TELEMETRY', 'CENSUS_PASS', 'STATISTICS_PASS')

$script:warmupRecords = @()
$script:chronology = @()
$script:pairOpen = $null

function Add-Chronology([string]$kind, [string]$label, $detail) {
    $script:chronology += [ordered]@{
        n = ($script:chronology.Count + 1)
        kind = $kind; label = $label
        utc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        detail = $detail
    }
}

# The ONE place that decides what may run between a warmup and its measured
# arm. A step not named mandatory is refused, and an unknown name is refused
# too rather than allowed through as unrecognised.
function Invoke-PairStep([string]$step) {
    if ($PAIR_MANDATORY_STEPS -notcontains $step) {
        $why = "the step '$step' is not one of the mandatory warmup-to-arm steps " +
               "($($PAIR_MANDATORY_STEPS -join ', '))"
        if ($PAIR_REFUSED_STEPS -contains $step) {
            $why = "the step '$step' is explicitly refused while a warmup/arm pair is open"
        }
        Add-Chronology 'STEP_REFUSED' $step $why
        throw "perflive: $why"
    }
    Add-Chronology 'PAIR_STEP' $step 'a mandatory bounded step inside an open warmup/arm pair'
    return $true
}

# A settle NEVER runs inside an open pair: a settle between a warmup and its
# measured arm would undo exactly the preconditioning the warmup exists to
# leave in place.
function Invoke-Settle([int]$seconds, [string]$before) {
    if ($script:pairOpen) {
        $why = ("a settle was asked for while the warmup/arm pair '{0}' was open; " +
                'a settle there undoes the preconditioning the warmup exists to leave ' +
                'in place') -f $script:pairOpen
        Add-Chronology 'SETTLE_REFUSED' $before $why
        throw "perflive: $why"
    }
    Add-Chronology 'SETTLE' $before ("{0} s, predeclared, immediately before the warmup" -f $seconds)
    Write-Host ("  settle    {0} s before the {1} warmup" -f $seconds, $before)
    if (-not $DryRun) { Start-Sleep -Seconds $seconds }
}

# The warmup arm: the measured arm's OWN condition, its own isolated directory,
# its own fresh scratch config and its own fresh copy of the pinned save. Its
# name carries the block and the measured partner, so the record says what it
# preconditioned without a lookup.
function New-WarmupArm($measured, [string]$blockId) {
    $name = ('WARMUP-{0}-{1}' -f $blockId, $measured.name)
    $w = New-Arm $name 'W' $measured.stage $measured.stage_number `
        $measured.expect.stage_item $measured.pose $measured.frames `
        $measured.env $measured.shots $measured.expected_seconds `
        $PREFLIGHT_WITNESSES $null 'warmup' $null $null `
        ('AN UNSCORED MATCHED WARMUP for ' + $measured.name + ', in that arm''s OWN condition')
    if ($measured.exe) { $w['exe'] = $measured.exe; $w['argv'] = @($measured.exe) }
    $w['warmup'] = [ordered]@{
        block = $blockId
        paired_measured_arm = $measured.name
        condition = 'the measured arm''s own env map, unchanged'
        ledger_role = 'WARMUP'
        is_a_real_native_start = $true
        free_setup_incident = $false
        refunded = $false
        timing_retained = $true
        timing_scored = $false
        enters_block_verdict = $false
        enters_series_verdict = $false
        enters_any_aggregate = $false
        credit = 'NONE'
        why = 'a warmup is claimed in the same budget as a measured arm and spends a real start; it is recorded and retained, and it is read by no verdict'
    }
    return $w
}

# THE MEASURED REPEAT. Each of the four measured runs is its own arm with its
# own directory, its own scratch config, its own fresh copy of the pinned save
# and its own capture prefix. An arm object launched twice would write its
# second run's stdout, stderr and captures over its first, which both destroys
# a completed run and leaves the repeated arm with one set of evidence where
# the screen needs two. The CONDITION is identical - the same env map, the same
# executable, the same schedule - and only the isolation paths differ, which is
# the same rule the halves of every pair here already follow.
$script:repeatArms = @()

function New-RepeatArm($measured, [string]$blockId, [string]$order) {
    $name = ('{0}-{1}' -f $measured.name, $blockId)
    $r = New-Arm $name $measured.phase $measured.stage $measured.stage_number `
        $measured.expect.stage_item $measured.pose $measured.frames `
        $measured.env $measured.shots $measured.expected_seconds `
        $measured.witnesses $null $measured.role $null $null `
        ('one measured repeat of ' + $measured.name + ' in block ' + $blockId)
    if ($measured.exe) { $r['exe'] = $measured.exe; $r['argv'] = @($measured.exe) }
    if ($measured.candidate) { $r['candidate'] = $measured.candidate }
    $r['repeat'] = [ordered]@{
        of = $measured.name; block = $blockId; order_label = $order
        condition = 'identical to the arm it repeats: the same env map, the same executable, the same capture schedule'
        differs_only_in = @('SL_CONFIG', 'SL_EEPROM_RW', 'SL_SHOT')
        why = 'the four measured runs are four separate arms so that none of them overwrites another''s evidence, and the repeated arm has the two reports the paired screen needs'
    }
    return $r
}

# ONE PAIR: the settle, the warmup, the mandatory steps, the measured arm -
# interleaved in that order and with nothing else between the warmup and the
# arm. The warmup's record is merged in a FINALLY, so a warmup that spent a
# start and then failed keeps everything a passing one keeps: a spent start
# reported as "not started" is the one failure mode this ordering exists to
# prevent.
function Invoke-WarmupAndArm($measured, [string]$blockId, [string]$order) {
    $repeat = New-RepeatArm $measured $blockId $order
    New-Item -ItemType Directory -Force -Path (Join-Path $script:attemptDir $repeat.name) | Out-Null
    Invoke-Settle $WARMUP_SETTLE_SECONDS $repeat.name
    $w = New-WarmupArm $repeat $blockId
    New-Item -ItemType Directory -Force -Path (Join-Path $script:attemptDir $w.name) | Out-Null
    $script:pairOpen = ('{0}/{1}' -f $blockId, $measured.name)
    try {
        Add-Chronology 'WARMUP' $w.name ('an unscored matched warmup in ' + $measured.name + '''s own condition')
        Invoke-Arm $w
        $null = Invoke-PairStep 'WARMUP_CLEANUP'
        $null = Invoke-PairStep 'WARMUP_QUALIFICATION'
        $null = Invoke-PairStep 'NEXT_START_ACCOUNTING'
    } finally {
        # EVERY PATH. A failed warmup is still a spent start with a record.
        $script:warmupRecords += [ordered]@{
            schema = 'sightline-perflive-warmup/1'
            name = $w.name; block = $blockId
            paired_measured_arm = $measured.name
            warmup = $w.warmup
            run = $w.run
            env = $w.env
            exe = $w.exe
            scratch_config = $w.scratch_config
            scratch_save = $w.scratch_save
        }
        $script:pairOpen = $null
    }
    Add-Chronology 'MEASURED' $repeat.name 'the measured arm, immediately after its own warmup'
    Invoke-Arm $repeat
    $script:repeatArms += $repeat
}

# TWO BLOCKS IN OPPOSITE ORDER. Block 1 runs A then B; block 2 runs B then A.
# The counterbalance is in the chronology, so a block whose labels say A,B
# while its chronology ran B,A is a different experiment and the recorded
# chronology is what a later reader checks.
function Invoke-CounterbalancedBlocks($armA, $armB) {
    $blocks = @(
        [ordered]@{ id = 'blk1'; order = @($armA.name, $armB.name)
                    arms = @($armA, $armB); labels = @('A', 'B') }
        [ordered]@{ id = 'blk2'; order = @($armB.name, $armA.name)
                    arms = @($armB, $armA); labels = @('B', 'A') }
    )
    $done = @()
    foreach ($b in $blocks) {
        Write-Host ''
        Write-Host ("--- BLOCK {0}: {1} ---" -f $b.id, ($b.order -join ' then '))
        Add-Chronology 'BLOCK_OPEN' $b.id ('order ' + ($b.order -join ','))
        for ($i = 0; $i -lt $b.arms.Count; $i++) {
            Invoke-WarmupAndArm $b.arms[$i] $b.id $b.labels[$i]
        }
        Add-Chronology 'BLOCK_CLOSE' $b.id ''
        $done += [ordered]@{ id = $b.id; declared_order = $b.order
                             declared_labels = $b.labels }
    }
    # THE ACTUAL MEASURED SEQUENCE, read back out of the chronology rather than
    # restated from the labels. A,B,B,A is the pattern the paired screen
    # consumes, and a run whose labels say A,B,B,A while its chronology ran
    # something else is a different experiment: the sequence compared here is
    # the arms the runs actually measured, not the letters they carry.
    $actual = @($script:chronology | Where-Object { $_.kind -eq 'MEASURED' } |
                ForEach-Object { $_.label })
    $actualOf = @($script:repeatArms | ForEach-Object { $_.repeat.of })
    $wanted = @(('{0}-blk1' -f $armA.name), ('{0}-blk1' -f $armB.name),
                ('{0}-blk2' -f $armB.name), ('{0}-blk2' -f $armA.name))
    $wantedOf = @($armA.name, $armB.name, $armB.name, $armA.name)
    return [ordered]@{
        schema = 'sightline-perflive-counterbalance/1'
        blocks = $done
        settle_seconds = $WARMUP_SETTLE_SECONDS
        settle_placement = 'ONE_BEFORE_EACH_WARMUP'
        settles = @($script:chronology | Where-Object { $_.kind -eq 'SETTLE' }).Count
        warmups = $script:warmupRecords.Count
        measured_sequence_actual = $actual
        measured_sequence_wanted = $wanted
        measured_sequence_matches = (($actual -join ',') -eq ($wanted -join ','))
        measured_arms_actual = $actualOf
        measured_arms_wanted = $wantedOf
        measured_arms_match_the_pattern = (($actualOf -join ',') -eq ($wantedOf -join ','))
        measured_repeat_arms = @($script:repeatArms | ForEach-Object { $_.name })
        pair_mandatory_steps = $PAIR_MANDATORY_STEPS
        pair_refused_steps = $PAIR_REFUSED_STEPS
        warmups_scored = $false
        warmups_in_arms_list = $false
        rule = 'a warmup is a real native start, claimed in the same budget, recorded with role WARMUP and never scored; one warmup immediately before each measured arm in that arm''s own condition; one predeclared settle immediately before each warmup; two blocks in opposite order; records merged in a finally so a spent start is never reported as not started'
        reused_from = 'the counterbalanced repeat series'' warmup orchestration. The DESIGN is reused; its measured results were taken on other code at another viewpoint and none of them is attributed to this candidate.'
        chronology = $script:chronology
    }
}

# ---- what an arm's own stderr says about its moments and its sky ----------
#
# TWO READINGS THE WITNESS CHECKER CANNOT TAKE, both derived from the arm's own
# stderr and recorded with what they were measured against.
#
#   MOMENT IDENTITY ACROSS RUNS. The checker compares an arm's capture moments
#   against its PAIR in the same attempt, and these arms have no pair: the
#   halves they are compared with were recorded in an earlier run. So the
#   moments are compared against the pinned ones above, which both saved halves
#   recorded identically - name, record index, frame counter and cloud phase.
#   Equal bytes at a different moment would be a coincidence, not a match.
#
#   THE SKY PATH. There is no witness for "the immediate sky path drew", so the
#   skybatch report's own state phrase is read: "off (OPTIMIZED RENDERING is
#   off, or SL_SKY_BATCH says 0)" against "armed" (src/gfx/sl_gfx_skybatch.c
#   :654-668), beside the final snapshot's indexed-draw count (:708-717, the
#   one line carrying the word "final") and the carrier marker (:1375). The
#   GPU-only arm must read off with zero indexed draws and no marker; the
#   sky-only arm must read armed with positive indexed draws and exactly one
#   prod marker.
function Get-ArmReadings($arm) {
    $dir = Join-Path $script:attemptDir $arm.name
    $errPath = Join-Path $dir 'stderr.txt'
    $rec = [ordered]@{
        stderr = $errPath; present = (Test-Path -LiteralPath $errPath)
        shots = @(); moments = [ordered]@{}; sky = [ordered]@{}
    }
    if (-not $rec.present) {
        $rec.moments = [ordered]@{ holds = $false
            findings = @('the arm produced no stderr, so no capture moment could be read') }
        $rec.sky = [ordered]@{ holds = $false
            findings = @('the arm produced no stderr, so no skybatch line could be read') }
        return $rec
    }
    $text = [IO.File]::ReadAllText($errPath)
    $lines = $text -split "`r?`n"

    $shots = @()
    foreach ($ln in $lines) {
        if ($ln -match '^sl_shot: .*?([^\\/]+\.ppm) \((\d+)x(\d+)\) at read=(\d+) frame=(\d+) \(window=(\w+)\) cloud phase (?:\(no layer\) )?(-?[\d.]+)') {
            $shots += [ordered]@{ name = $Matches[1]; width = [int]$Matches[2]; height = [int]$Matches[3]
                                  read = [int]$Matches[4]; frame = [int]$Matches[5]
                                  window = $Matches[6]; cloud_phase = $Matches[7] }
        }
    }
    $rec.shots = $shots
    $findings = @()
    $want = @($AXIS_MATRIX.moments)
    if ($shots.Count -ne $want.Count) {
        $findings += ("this arm logged {0} captures; the baselines recorded {1}" -f $shots.Count, $want.Count)
    }
    for ($i = 0; $i -lt $want.Count; $i++) {
        if ($i -ge $shots.Count) { continue }
        $s = $shots[$i]
        $w = $want[$i]
        if ($s.name -ne $w.name) { $findings += ("capture {0} is named {1}, the baselines' is {2}" -f $i, $s.name, $w.name) }
        if ($s.read -ne $w.read) { $findings += ("{0} is at read={1}, the baselines' at read={2}" -f $s.name, $s.read, $w.read) }
        if ($s.frame -ne $w.frame) { $findings += ("{0} is at frame={1}, the baselines' at frame={2}" -f $s.name, $s.frame, $w.frame) }
        if ("$($s.cloud_phase)" -ne "$($w.cloud_phase)") {
            $findings += ("{0} is at cloud phase {1}, the baselines' at {2}" -f $s.name, $s.cloud_phase, $w.cloud_phase)
        }
    }
    $rec.moments = [ordered]@{
        expected = $want
        measured = @($shots | ForEach-Object { "$($_.name) read=$($_.read) frame=$($_.frame) cloud=$($_.cloud_phase)" })
        holds = ($findings.Count -eq 0)
        findings = $findings
        rule = 'every capture must be at the record index, frame counter and cloud phase both saved halves recorded for that capture name; equal bytes at another moment are not a match'
    }

    # The skybatch state phrase, the final snapshot's indexed draws, the
    # carrier markers. The final line is the one carrying "final" and is the
    # only one whose counters are terminal.
    $states = @()
    foreach ($ln in $lines) {
        if ($ln -match '^#316 skybatch: (off \(|armed)') { $states += $Matches[1].TrimEnd(' (') }
    }
    $finalDraws = $null
    foreach ($ln in $lines) {
        if ($ln -match '^#316 skybatch: final .*indexed-draws (\d+) ') { $finalDraws = [int]$Matches[1] }
    }
    $markers = @()
    foreach ($ln in $lines) {
        if ($ln -match '^#474 skybatch: float colour carrier (\w+)') { $markers += $Matches[1] }
    }
    $sf = @()
    if ($arm.matrix.sky_expect -eq 'off') {
        if (@($states | Where-Object { $_ -eq 'off' }).Count -eq 0) {
            $sf += 'no skybatch report read "off": nothing says the batch path was disabled'
        }
        if (@($states | Where-Object { $_ -eq 'armed' }).Count -gt 0) { $sf += 'a skybatch report read "armed"' }
        if ($null -eq $finalDraws) { $sf += 'no final skybatch snapshot was printed, so the indexed-draw total is unknown' }
        elseif ($finalDraws -ne 0) { $sf += "the final skybatch snapshot counted $finalDraws indexed draws, not 0" }
        if ($markers.Count -ne 0) { $sf += ("a carrier marker was printed: " + ($markers -join ',')) }
    } else {
        if (@($states | Where-Object { $_ -eq 'armed' }).Count -eq 0) {
            $sf += 'no skybatch report read "armed": nothing says the batch path ran'
        }
        if ($null -eq $finalDraws) { $sf += 'no final skybatch snapshot was printed, so the indexed-draw total is unknown' }
        elseif ($finalDraws -le 0) { $sf += 'the final skybatch snapshot counted 0 indexed draws' }
        if ($markers.Count -ne 1 -or $markers[0] -ne 'prod') {
            $sf += ("the carrier markers read " + (($markers -join ',')) + "; exactly one, naming prod, is required")
        }
    }
    $rec.sky = [ordered]@{
        expect = $arm.matrix.sky_expect
        report_states = $states
        final_indexed_draws = $finalDraws
        carrier_markers = $markers
        holds = ($sf.Count -eq 0)
        findings = $sf
        rule = 'the skybatch report''s own state phrase, the final snapshot''s indexed-draw total and the carrier markers, read from this arm''s stderr (src/gfx/sl_gfx_skybatch.c:654-668, :708-717, :1375)'
    }
    return $rec
}

# THE COMPARATOR'S ARGUMENT LIST, BUILT IN ONE PLACE. The dry run prints the
# command and the live run executes it, and a dry run that printed a different
# command from the one that runs is worse than no dry run at all - so both call
# this. Every reference capture is passed with its pinned sha256, so the
# comparator refuses a drifted baseline on its own evidence rather than trusting
# the preflight that already checked it.
function Get-MatrixArgs {
    $cargs = @('--attempt', $script:attemptDir)
    foreach ($n in @($AXIS_MATRIX.arms)) { $cargs += @('--arm', $n) }
    foreach ($b in @($AXIS_MATRIX.baselines)) {
        $d = $script:baseDirOf[$b.label]
        $cargs += @('--baseline', ("{0}={1}" -f $b.label, $d))
        foreach ($c in @($b.captures)) {
            $cargs += @('--pin', ("{0}:{1}={2}" -f $b.label, $c.name, $c.sha256))
        }
    }
    $cargs += @('--pixel', ("{0},{1}" -f $AXIS_MATRIX.pixel.x, $AXIS_MATRIX.pixel.y),
                '--json', (Join-Path $script:attemptDir 'discriminate-matrix.json'))
    return $cargs
}

# ----------------------------------------------- the roles, proved or refused
#
# A ROLE IS PROVED BY ITS OWN CANDIDATE'S ACQUISITION RECORD, NOT BY BEING
# NAMED. Every clause below is a case that a named stage satisfies and a
# measured one does not. The far role is not read from a selection at all: it
# comes from the hash-bound recipe. The defect this closes was a selection that
# recorded reduction = runway - the far candidate - while that run's own
# fogless coverage was null and its reduction count was 0.
function Test-RoleProved($candidates, [string]$stageName, [string]$role, [string]$farStage) {
    $reasons = @()
    if (-not $stageName) {
        return [ordered]@{ proved = $false; stage = $null
                           because = @('the selection names no stage for this role') }
    }
    if ($stageName -eq $farStage) {
        $reasons += ("the selection names the FAR candidate '$stageName' for the $role role, which " +
                     "is the fallback assignment and not a measurement of this role")
    }
    $rec = $null
    foreach ($c in $candidates) { if ("$($c.stage)" -eq $stageName) { $rec = $c } }
    if ($null -eq $rec) {
        $reasons += "no acquisition record for stage '$stageName' is in the selection's candidates"
        return [ordered]@{ proved = $false; stage = $stageName; because = $reasons }
    }
    # The declared acquisition witnesses, re-applied here rather than taken on
    # the selector's word: a bound asset root and the declared presentation
    # set, the declared stage item in a LOADED line, the pinned RNG state taken
    # explicitly, a real zero exit, no timeout and no shader error.
    if (-not $rec.eligible) {
        $reasons += ("the candidate's own acquisition record is INELIGIBLE: " +
                     (@($rec.ineligible_because) -join '; '))
    }
    if ($rec.gpu_program_ready -eq $false -and $role -eq 'skydome') {
        # Not a refusal on its own: the sky pass does not need the background
        # vertex program. Recorded, not counted.
        $reasons += 'note: the candidate did not report the background vertex program ready (not required for this role)'
    }
    if ($role -eq 'reduction') {
        # The predicate admits only fogless walks (src/gfx/sl_gfx_dl.c:18388)
        # and refuses a panorama walk outright (:18385), and there must have
        # been a background walk at all to reduce.
        if ($null -eq $rec.fogless_frames) {
            $reasons += 'fogless coverage is NULL in the candidate''s record: nothing measured a fogless walk, so nothing proves one exists at that stage'
        } elseif ([int]$rec.fogless_frames -le 0) {
            $reasons += "fogless frames = $($rec.fogless_frames); the reduction predicate admits only fogless walks"
        }
        if ($null -eq $rec.bg_frames -or [int]$rec.bg_frames -le 0) {
            $reasons += "background frames = $($rec.bg_frames); there was no background walk to reduce"
        }
        if ($rec.pano_frames -and [int]$rec.pano_frames -gt 0) {
            $reasons += "panorama frames = $($rec.pano_frames); the predicate refuses a panorama walk outright"
        }
    }
    if ($role -eq 'skydome') {
        if ($null -eq $rec.sky_frames -or [int]$rec.sky_frames -le 0) {
            $reasons += "skydome frames = $($rec.sky_frames); a stage that composed no skydome cannot carry the carrier marker"
        }
        if (-not (@($rec.carriers) -contains 'prod')) {
            $reasons += "carriers = $(@($rec.carriers) -join ',' ); the prod carrier marker was not printed"
        }
    }
    $hard = @($reasons | Where-Object { -not $_.StartsWith('note:') })
    return [ordered]@{ proved = ($hard.Count -eq 0); stage = $stageName; because = $reasons
                       record = $rec }
}

# ------------------------------------------------------------------ preflight
Write-Host ''
Write-Host 'perflive: OPTIMIZED RENDERING live batch'
Write-Host "  repo        $repo"
Write-Host "  out         $Out"
if ($DryRun) {
    Write-Host '  mode        DRY RUN (-DryRun given) - nothing is launched'
} else {
    Write-Host '  mode        LIVE - GPU children WILL be started. -DryRun is a switch and is off.'
}

if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host "perflive: no executable at '$exe'. Build first; this runner never builds."
    exit 1
}
$exeSha = Get-Sha256Lower $exe
$sdlSha = Get-Sha256Lower (Join-Path $bin 'SDL2.dll')
$pthSha = Get-Sha256Lower (Join-Path $bin 'libwinpthread-1.dll')
if (-not $sdlSha) { $sdlSha = 'not present' }
if (-not $pthSha) { $pthSha = 'not present' }
Write-Host "  sightline   $exeSha"
Write-Host "  SDL2        $sdlSha"
Write-Host "  pthread     $pthSha"
if ($ExpectExe) {
    if ($ExpectExe.ToLower() -ne $exeSha) {
        Write-Host "perflive: -ExpectExe $($ExpectExe.ToLower()) does not match the executable. Refusing."
        exit 1
    }
    Write-Host '  -ExpectExe  matches'
}

# ---- the candidate group's ISOLATED RUNTIME, pinned ----------------------
#
# The candidate arms do not run the executable above. They run the isolated
# diagnostic build, and its exe, its length and its two staged DLLs are all
# verified HERE - before any arm is launched - so that a drifted runtime stops
# the group instead of producing a result attributed to the wrong bytes. Every
# path in this block READS; nothing under build\diag is written, copied,
# deleted or restored by this runner.
$script:tfBin = Join-Path $repo $TFCLIP_RUNTIME.relpath
$script:tfExe = Join-Path $script:tfBin 'sightline.exe'
$script:tfExeSha = $null
$script:tfDllSha = [ordered]@{}
$script:tfPins = @()
if ($Candidate) {
    Write-Host ''
    Write-Host '--- THE CANDIDATE GROUP''S ISOLATED RUNTIME (read only) ---'
    Write-Host ("  runtime     {0}" -f $script:tfBin)
    if (-not (Test-Path -LiteralPath $script:tfExe)) {
        Write-Host ("perflive: no candidate executable at '{0}'. This runner never builds. Refusing." -f $script:tfExe)
        exit 1
    }
    $script:tfExeSha = Get-Sha256Lower $script:tfExe
    $tfLen = (Get-Item -LiteralPath $script:tfExe).Length
    $script:tfPins += [ordered]@{ what = 'exe'; path = $script:tfExe
                                  expected = $TFCLIP_RUNTIME.exe_sha256
                                  actual = $script:tfExeSha
                                  matches = ($script:tfExeSha -eq $TFCLIP_RUNTIME.exe_sha256) }
    $script:tfPins += [ordered]@{ what = 'exe-bytes'; path = $script:tfExe
                                  expected = "$($TFCLIP_RUNTIME.exe_bytes)"
                                  actual = "$tfLen"
                                  matches = ($tfLen -eq $TFCLIP_RUNTIME.exe_bytes) }
    foreach ($dll in $TFCLIP_RUNTIME.dll_sha256.Keys) {
        $p = Join-Path $script:tfBin $dll
        $h = Get-Sha256Lower $p
        if (-not $h) { $h = 'not present' }
        $script:tfDllSha[$dll] = $h
        $script:tfPins += [ordered]@{ what = $dll; path = $p
                                      expected = $TFCLIP_RUNTIME.dll_sha256[$dll]
                                      actual = $h
                                      matches = ($h -eq $TFCLIP_RUNTIME.dll_sha256[$dll]) }
    }
    # The PRODUCT SOURCE the candidate was built from, pinned beside the
    # binary. A binary whose digest matches tells you which bytes ran; the
    # source pins tell you which bytes they were built from, and a result read
    # against a different tree later is what this prevents.
    foreach ($rel in $TFCLIP_RUNTIME.source_sha256.Keys) {
        $p = Join-Path $repo ($rel -replace '/', '\')
        $h = Get-Sha256Lower $p
        if (-not $h) { $h = 'not present' }
        $script:tfPins += [ordered]@{ what = "source $rel"; path = $p
                                      expected = $TFCLIP_RUNTIME.source_sha256[$rel]
                                      actual = $h
                                      matches = ($h -eq $TFCLIP_RUNTIME.source_sha256[$rel]) }
    }
    foreach ($pin in $script:tfPins) {
        Write-Host ("  {0,-26} {1}  {2}" -f $pin.what, $pin.actual,
                    $(if ($pin.matches) { 'pinned' } else { "DRIFTED (pinned $($pin.expected))" }))
    }
    $drifted = @($script:tfPins | Where-Object { -not $_.matches })
    if ($drifted.Count -gt 0) {
        Write-Host ''
        Write-Host 'perflive: the candidate group''s pins have DRIFTED. Refusing to launch.'
        Write-Host '  A measurement taken against a runtime or a source tree other than the pinned'
        Write-Host '  one is a result about bytes nobody named. Nothing was launched and nothing'
        Write-Host '  under build\diag was written.'
        foreach ($d in $drifted) {
            Write-Host ("  {0}: expected {1}, read {2}" -f $d.what, $d.expected, $d.actual)
        }
        exit 1
    }
    Write-Host '  every pin matches; the candidate arms run this runtime and nothing writes to it'
}

# THE PINNED RNG STATE. SL_RNG_SEED is the 64-bit STATE in hex, not the seed
# argument (src/platform/sl_ultra_shim.c:3345-3351), and it is honoured exactly
# when set (:3351 "use it exactly. Replay, and explicit tests").
if ($SeedHex -notmatch '^[0-9a-fA-F]{1,16}$') {
    Write-Host "perflive: -SeedHex '$SeedHex' is not a 1..16 digit hex state."
    exit 1
}
$script:seed = $SeedHex.ToLower().PadLeft(16, '0')
Write-Host "  RNG state   $($script:seed) (pinned in both halves of every pair)"
if ($script:seed -ne $RECIPE.seed_hex) {
    Write-Host ("perflive: the measured recipe is bound to RNG state {0}; -SeedHex gives {1}." -f `
                $RECIPE.seed_hex, $script:seed)
    Write-Host '  A different state is a different run of the recipe, not the measured one. Refusing.'
    exit 1
}

$rom = $env:SL_ROM
if (-not $rom) {
    Write-Host 'perflive: SL_ROM is unset. Point it at your own GoldenEye (U) copy; nothing ROM-derived ships here.'
    exit 1
}
if (-not (Test-Path -LiteralPath $rom)) {
    Write-Host "perflive: SL_ROM names '$rom', which does not exist."
    exit 1
}
$rom = (Resolve-Path -LiteralPath $rom).Path
$script:rom = $rom

# ---- THE RECIPE'S PINNED HASHES, verified before anything is launched ----
# A recipe that is not the measured one measures a different scene under the
# measured one's name, so every pin is checked here and a mismatch stops the
# batch. The config is a STRING hash of the bytes this script writes, not of a
# file on disk: that is what every arm's config.ini is written from.
$script:cfgSha = Get-StringSha256 $SCRATCH_CONFIG
if (-not $Anchor) { $Anchor = Resolve-EitherCheckout $RECIPE.anchor_relpath }
$savePath = Resolve-EitherCheckout $RECIPE.initial_save_relpath
$pinChecks = @(
    [ordered]@{ what = 'the ENGINE_START_PAD anchor record'; path = $Anchor
                expected = $RECIPE.anchor_sha256; actual = (Get-Sha256Lower $Anchor) }
    [ordered]@{ what = 'the scratch config bytes'; path = '(written by this script)'
                expected = $RECIPE.config_sha256; actual = $script:cfgSha }
    [ordered]@{ what = 'the pinned initial save'; path = $savePath
                expected = $RECIPE.initial_save_sha256; actual = (Get-Sha256Lower $savePath) }
)

# ---- THE SAVED BASELINES AND THE FROZEN BINARY, when the diagnostic runs ----
# The two arms are compared against captures recorded by an earlier run, so
# those captures, the executable that produced them and the environment they
# were produced under are all pins of THIS run: a baseline that has drifted, or
# a different binary, makes every comparison a comparison with something else.
# They are appended to the same list so that the same refusal above covers them
# and nothing is launched when one fails.
$script:baseEnvOf = @{}
$script:baseDirOf = @{}
if ($Discriminate) {
    $pinChecks += [ordered]@{
        what = 'the frozen executable the saved baselines were taken with'
        path = $exe; expected = $AXIS_MATRIX.exe_sha256; actual = $exeSha }
    foreach ($b in @($AXIS_MATRIX.baselines)) {
        $bd = Get-BaselineDir $b
        $script:baseDirOf[$b.label] = $bd
        if (-not $bd) {
            $pinChecks += [ordered]@{
                what = ("the saved {0} baseline's directory" -f $b.label)
                path = (Join-Path "$($AXIS_MATRIX.baseline_relpath)" $b.arm)
                expected = 'present in this checkout or the sibling one'; actual = $null }
            continue
        }
        foreach ($c in @($b.captures)) {
            $cp = Join-Path $bd $c.name
            $pinChecks += [ordered]@{
                what = ("the saved {0} baseline's {1}" -f $b.label, $c.name)
                path = $cp; expected = $c.sha256; actual = (Get-Sha256Lower $cp) }
        }
        # THE BASELINE'S OWN ENVIRONMENT, read from the intent.json it was
        # launched with, and compared against the map pinned in $AXIS_MATRIX.
        # The one-axis assertion is derived from what the baseline ACTUALLY ran
        # with, so a drift between the two would silently change what that
        # assertion is about.
        $bEnv = Get-BaselineEnvMap $bd
        $script:baseEnvOf[$b.label] = $bEnv
        $bad = @()
        if ($null -eq $bEnv) {
            $bad += 'its intent.json was not read'
        } else {
            foreach ($k in $b.env.Keys) {
                $got = $null
                if (@($bEnv.Keys) -contains $k) { $got = "$($bEnv[$k])" }
                if ($got -ne "$($b.env[$k])") { $bad += ("{0}={1}, pinned {2}" -f $k, $got, $b.env[$k]) }
            }
        }
        $pinChecks += [ordered]@{
            what = ("the saved {0} baseline's own environment" -f $b.label)
            path = (Join-Path "$bd" 'intent.json')
            expected = (($b.env.Keys | ForEach-Object { "$_=$($b.env[$_])" }) -join ' ')
            actual = $(if ($bad.Count -eq 0) { (($b.env.Keys | ForEach-Object { "$_=$($b.env[$_])" }) -join ' ') }
                       else { 'DIFFERS: ' + ($bad -join '; ') }) }
    }
}
$pinBad = @()
Write-Host '  recipe pins'
foreach ($p in $pinChecks) {
    $state = 'MATCHES'
    if ($null -eq $p.actual) { $state = 'ABSENT' }
    elseif ($p.actual -ne $p.expected) { $state = 'MISMATCH' }
    if ($state -ne 'MATCHES') { $pinBad += $p }
    Write-Host ("    {0,-8} {1}" -f $state, $p.what)
    Write-Host ("             {0}" -f $p.path)
    Write-Host ("             expected {0}" -f $p.expected)
    Write-Host ("             actual   {0}" -f $p.actual)
}
if ($pinBad.Count -gt 0) {
    Write-Host ''
    Write-Host 'perflive: STOP. The measured far recipe is not bound.'
    Write-Host '  Every pin above must match before the pair runs: the anchor record the recipe is'
    Write-Host '  taken from, the config bytes it was taken under, and the initial save it started'
    Write-Host '  from. A pair run against an unbound recipe records a different scene under the'
    Write-Host '  measured recipe''s name. Nothing is launched.'
    if ($Discriminate) {
        Write-Host '  With -Discriminate the saved baselines, their own environment and the frozen'
        Write-Host '  executable are pins too: the two arms are compared against captures an earlier run'
        Write-Host '  recorded, so a drift in any of them compares this run with something else.'
    }
    exit 1
}
Write-Host ("    note     the initial save is copied from the repository file above, FRESH for each arm." )
Write-Host ("             SL_EEPROM_RW is read-write, so a completed run's own eeprom.bin is that run's")
Write-Host ("             OUTPUT: the discovery run left {0} ({1} bytes), not the pinned" -f `
            $SAVE_OUTPUT_TRAP.leftover_sha256.Substring(0, 16), $SAVE_OUTPUT_TRAP.leftover_bytes)
Write-Host ("             image ({0} bytes). An artifact directory is never the source." -f $SAVE_OUTPUT_TRAP.pinned_bytes)

# THE INSTALLED ASSET ROOTS, BOUND FOR READING. Each defaults to the
# player-data location the binary's own ladder names: SL_TEXPACK_ROOT falls
# back to %LOCALAPPDATA%\sightline\assets\texpacks (src/gfx/sl_gfx_texprov.h
# :25), SL_WORLDASSET_ROOT to <SL_ASSET_OVERRIDE_DIR>\worldassets
# (src/gfx/sl_gfx_worldasset.c:895-903), and SL_ASSET_OVERRIDE_DIR is candidate
# 0 of the override ladder (src/native/sl_asset_override.c:64, :124).
$lad = $env:LOCALAPPDATA
if (-not $lad -and $env:USERPROFILE) { $lad = Join-Path $env:USERPROFILE 'AppData\Local' }
if (-not $AssetOverrideDir) {
    if ($lad) { $AssetOverrideDir = Join-Path $lad 'sightline\assets' }
}
if (-not $WorldAssetRoot) {
    if ($AssetOverrideDir) { $WorldAssetRoot = Join-Path $AssetOverrideDir 'worldassets' }
}
if (-not $TexpackRoot) {
    if ($AssetOverrideDir) { $TexpackRoot = Join-Path $AssetOverrideDir 'texpacks' }
}
$rootsOk = $true
foreach ($r in @(@{ n = 'SL_ASSET_OVERRIDE_DIR'; v = $AssetOverrideDir },
                 @{ n = 'SL_WORLDASSET_ROOT'; v = $WorldAssetRoot },
                 @{ n = 'SL_TEXPACK_ROOT'; v = $TexpackRoot })) {
    if (-not $r.v) {
        Write-Host "perflive: $($r.n) could not be resolved and was not given."
        $rootsOk = $false
        continue
    }
    if (-not (Test-Path -LiteralPath $r.v)) {
        Write-Host "perflive: $($r.n) names '$($r.v)', which does not exist."
        $rootsOk = $false
    }
}
if (-not $rootsOk) {
    Write-Host '  The XBLA presentation cannot be exercised without the installed asset roots.'
    Write-Host '  Supply -AssetOverrideDir / -WorldAssetRoot / -TexpackRoot, or run'
    Write-Host '  tools\windows\prepare-all.ps1 first. Nothing is launched.'
    exit 1
}
$script:aoDir  = (Resolve-Path -LiteralPath $AssetOverrideDir).Path
$script:waRoot = (Resolve-Path -LiteralPath $WorldAssetRoot).Path
$script:tpRoot = (Resolve-Path -LiteralPath $TexpackRoot).Path
Write-Host "  assets      $($script:aoDir)"
Write-Host "  worldassets $($script:waRoot)"
Write-Host "  texpacks    $($script:tpRoot)"

$script:shotW = 640
$script:shotH = 480
$script:stageOf = @{}
foreach ($s in $STAGES) { $script:stageOf[$s.name] = $s }

# THE PINNED INITIAL SAVE, PER STAGE. tools/trace/inputs/<level>.eeprom is a
# pinned initial save image; SL_EEPROM_RW names a read/write save and the shim
# loads it when it is there (src/platform/sl_ultra_shim.c:2346, :2973-2975).
# Each arm gets its OWN FRESH COPY of its stage's image, so no mutable save
# state carries from one half of a pair to the other - the two halves of a pair
# are always on the same stage, so they start from identical bytes.
$script:inputsDir = Join-Path $repo 'tools\trace\inputs'
$script:eepromOf = @{}
foreach ($s in $STAGES) {
    $p = Join-Path $script:inputsDir ($s.name + '.eeprom')
    if (Test-Path -LiteralPath $p) {
        $full = (Resolve-Path -LiteralPath $p).Path
        $script:eepromOf[$s.name] = [ordered]@{ path = $full; sha256 = (Get-Sha256Lower $full)
                                                bytes = (Get-Item -LiteralPath $full).Length }
    } else {
        # Recorded as absent rather than substituted. SL_EEPROM_RW still names
        # a per-arm file, so the run creates its own save on demand and still
        # cannot touch the player's; what is lost is the PINNED starting
        # content, which the record says so about.
        $script:eepromOf[$s.name] = [ordered]@{ path = $null; sha256 = $null; bytes = $null
            absent_reason = "no $($s.name).eeprom under tools\trace\inputs; the arm starts from a save created on demand in its own directory" }
    }
}

# ---- the saved control, if one was asked for ----------------------------
$script:controlRefusals = @()
$script:boundControl = $null
if ($Control) {
    $entry = $null
    foreach ($c in $SAVED_CONTROLS) { if ($c.name -eq $Control) { $entry = $c } }
    if ($null -eq $entry) {
        if ($CONTROLS_OFF_STAGE -contains $Control) {
            $script:controlRefusals += ("-Control '$Control' is a frozen test contract for another " +
                "stage. A stream recorded on another stage boots that stage, which carries no " +
                "qualified far role in this batch, so it cannot stand in for a second pose at the " +
                "recipe's stage.")
        } else {
            $script:controlRefusals += ("-Control '$Control' is not a pinned control here. Pinned: " +
                (($SAVED_CONTROLS | ForEach-Object { $_.name }) -join ', '))
        }
    } elseif ($entry.stage -ne $RECIPE.stage) {
        $script:controlRefusals += ("-Control '$Control' is of stage $($entry.stage), and the recipe's " +
            "stage is $($RECIPE.stage).")
    } else {
        $ip = Resolve-EitherCheckout $entry.input_relpath
        $sp = Resolve-EitherCheckout $entry.spec_relpath
        $vp = Resolve-EitherCheckout $entry.save_relpath
        $bad = @()
        foreach ($t in @(@{ n = 'stream'; p = $ip; e = $entry.input_sha256 },
                         @{ n = 'spec sidecar'; p = $sp; e = $entry.spec_sha256 },
                         @{ n = 'initial save'; p = $vp; e = $entry.save_sha256 })) {
            $a = Get-Sha256Lower $t.p
            if ($null -eq $a) { $bad += "the control's $($t.n) is absent" }
            elseif ($a -ne $t.e) { $bad += "the control's $($t.n) hashes $a, pinned $($t.e)" }
        }
        if ($bad.Count -gt 0) {
            $script:controlRefusals += (("-Control '$Control' is not bound: ") + ($bad -join '; '))
        } else {
            $script:boundControl = [ordered]@{
                name = $entry.name; input_path = $ip; input_sha256 = $entry.input_sha256
                spec_path = $sp; spec_sha256 = $entry.spec_sha256
                save_path = $vp; save_sha256 = $entry.save_sha256
                reads = [int]((Get-Item -LiteralPath $ip).Length / 4)
                covers = $entry.covers
            }
        }
    }
}
if ($script:controlRefusals.Count -gt 0) {
    Write-Host ''
    Write-Host '--- SAVED CONTROL REFUSED ---'
    foreach ($r in $script:controlRefusals) { Write-Host "  $r" }
    Write-Host '  The second-pose arms are NOT_COVERED and are not run.'
}

if (-not $DryRun) {
    $running = @(Get-Process -Name 'sightline' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        Write-Host "perflive: a sightline.exe is already running (pid $($running[0].Id)). Refusing to start."
        Write-Host '  Nothing is killed here. Close it and run again.'
        exit 1
    }
}

$python = Resolve-Python
if (-not $python) {
    Write-Host 'perflive: no interpreter for the checker (SL_PYTHON, .\.venv, or the sibling checkout''s .venv).'
    exit 1
}
$checker = Join-Path $PSScriptRoot 'perflive-check.py'
if (-not (Test-Path -LiteralPath $checker)) {
    Write-Host "perflive: no checker at '$checker'."
    exit 1
}

# ---------------------------------------------------- the attempt directory
# A FRESH directory per invocation. -Out is reused across runs by design, so
# anything that reads a file by name - a selection, a capture, a check - must
# read it from THIS run's directory or not at all.
if (-not $Attempt) {
    $Attempt = 'attempt-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss') + '-' + $exeSha.Substring(0, 8)
}
$script:attemptDir = Join-Path $Out $Attempt
if (Test-Path -LiteralPath $script:attemptDir) {
    $existing = @(Get-ChildItem -LiteralPath $script:attemptDir -Force -ErrorAction SilentlyContinue)
    if ($existing.Count -gt 0) {
        Write-Host "perflive: attempt directory '$($script:attemptDir)' is not empty."
        Write-Host '  A reused directory can hand a later run an earlier run''s captures, selection or'
        Write-Host '  check. Give -Attempt a new name, or remove that directory yourself. Nothing is'
        Write-Host '  deleted here and nothing is launched.'
        exit 1
    }
}
Write-Host "  attempt     $($script:attemptDir)"
if ($Only) { Write-Host "  scope       SUBSET (-Only given): a PASS here is a subset diagnostic, never full acceptance" }
else       { Write-Host '  scope       FULL' }
$script:scope = 'full'
if ($Only) { $script:scope = 'subset' }

# THE DIAGNOSTIC IS A SUBSET BY CONSTRUCTION. It runs two arms of its own
# instead of the batch, so the batch's mandatory cases are not attempted and a
# result from it can never read as acceptance. The mandatory names below are its
# own two, so that a run missing one of them is still reported as incomplete on
# the thing it was measuring.
$script:mandatoryPhases = $MANDATORY_PHASES
$script:mandatoryCases  = $MANDATORY_CASES
$script:censusRefArm    = 'B-depth-on'
if ($Discriminate) {
    $script:scope = 'subset'
    $script:mandatoryPhases = @('X')
    $script:mandatoryCases  = @($AXIS_MATRIX.arms)
    # Inert here: no arm declares CENSUS_SINGLE_MATCHES_REF, which is the one
    # witness that reads it. It names the arm that submits, so that the record
    # cannot point at an arm this run does not contain.
    $script:censusRefArm    = 'X-gpu-only'
    Write-Host '  mode        DISCRIMINATE: the two missing combinations replace the staged batch'
    Write-Host '              the saved OFF and ON halves are REUSED by hash and are not re-run'
    Write-Host '              a difference from a baseline is the reading, not a failure; it qualifies nothing'
}

Write-Host ("  scratch cfg {0} (sha256 {1})" -f `
            (($SCRATCH_CONFIG.TrimEnd("`n") -split "`n") -join ' | '), $script:cfgSha)

# THE PAD STEP FILE. One step per line, <frame> <lx> <ly> <rx> <ry> <lt> <rt>,
# axes in SDL's sense (y+ = down, so ly = -1 is forward) and the RIGHT stick is
# the look pair (src/platform/sl_input.c). Written here and pinned by sha256,
# so the movement an arm performed is a recorded input and not a description.
$PAD_SCRIPT_TEXT = @'
# perflive movement: forward, then forward while turning, then turn, then back.
# <frame> <lx> <ly> <rx> <ry> <lt> <rt>   axes -1..1 (SDL: y+ is down), triggers 0..1
0 0.0 0.0 0.0 0.0 0.0 0.0
60 0.0 -1.0 0.0 0.0 0.0 0.0
150 0.0 -1.0 0.6 0.0 0.0 0.0
240 0.0 0.0 0.6 0.0 0.0 0.0
300 0.0 -1.0 -0.6 0.0 0.0 0.0
390 0.0 0.0 0.0 0.0 0.0 0.0
'@
$PAD_SCRIPT_TEXT = ($PAD_SCRIPT_TEXT -replace "`r`n", "`n")
if (-not $PAD_SCRIPT_TEXT.EndsWith("`n")) { $PAD_SCRIPT_TEXT = $PAD_SCRIPT_TEXT + "`n" }
$script:padStepCount = 6
$script:padPath = Join-Path $script:attemptDir 'padscript\move.txt'
$script:padSha = Get-StringSha256 $PAD_SCRIPT_TEXT

if (-not $DryRun) {
    New-Item -ItemType Directory -Force -Path $script:attemptDir | Out-Null
    Write-TextNoBom $script:padPath $PAD_SCRIPT_TEXT
    $script:padSha = Get-Sha256Lower $script:padPath
}

# ------------------------------------------------------------------- the run
$script:commands = @()

# PER-ARM DURABLE RECEIPTS. The record used to arrive only after a whole phase,
# so an exception or an early exit lost every arm of it. Three writes per arm
# instead: the INTENT before anything is launched, the LAUNCH (pid and start
# time) before any dependent step, and the RECEIPT on normal exit, on exception
# and on early termination alike, from a finally block.
function Write-ArmIntent($arm) {
    $dir = Join-Path $script:attemptDir $arm.name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Write-JsonNoBom (Join-Path $dir 'intent.json') ([ordered]@{
        schema = 'sightline-perflive-arm-intent/1'
        written_utc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        name = $arm.name; phase = $arm.phase; stage = $arm.stage
        argv = $arm.argv; cwd = $arm.cwd; env = $arm.env
        pose = $arm.pose; shots = $arm.shots; pad_script = $arm.pad_script
        control = $arm.control
        expect = $arm.expect; witnesses = $arm.witnesses
        pinned_eeprom = $arm.pinned_eeprom
        seed_hex = $arm.seed_hex; timeout_seconds = $TimeoutSeconds
    }) 8
}

function Write-ArmReceipt($arm, $rec) {
    $dir = Join-Path $script:attemptDir $arm.name
    $doc = [ordered]@{
        schema = 'sightline-perflive-arm-receipt/1'
        written_utc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        name = $arm.name
    }
    # Copied key by key: PowerShell 5.1 defines no + for an OrderedDictionary,
    # and the receipt must keep its declared order.
    foreach ($k in $rec.Keys) { $doc[$k] = $rec[$k] }
    Write-JsonNoBom (Join-Path $dir 'receipt.json') $doc 6
}

function Invoke-Arm($arm) {
    $dir = Join-Path $script:attemptDir $arm.name
    Write-ArmIntent $arm
    $outPath = Join-Path $dir 'stdout.txt'
    $errPath = Join-Path $dir 'stderr.txt'

    # EACH HALF GETS ITS OWN FRESH COPY of the same scratch config bytes and
    # the same pinned initial save, so nothing mutable crosses between halves.
    # The pin is re-verified at the SOURCE before the copy: SL_EEPROM_RW is
    # read-write, so a source that has itself been used as a run's save is an
    # output being fed back in as an input.
    Write-TextNoBom $arm.scratch_config $SCRATCH_CONFIG
    $saveSha = $null
    $savePinOk = $null
    $pin = $arm.pinned_eeprom
    if ($pin -and $pin.path) {
        $srcSha = Get-Sha256Lower $pin.path
        $savePinOk = ($srcSha -eq $pin.sha256)
        if (-not $savePinOk) {
            Write-Host ("  {0,-16} REFUSED: its pinned initial save now hashes {1}, pinned {2}" -f `
                        $arm.name, $srcSha, $pin.sha256)
            $rec = [ordered]@{
                argv = $arm.argv; pid = $null; exit_code = $null; timed_out = $false
                launch_failure = "the pinned initial save '$($pin.path)' hashes $srcSha, pinned $($pin.sha256); the arm was not launched"
                start_utc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
                end_utc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
                stdout = $outPath; stderr = $errPath
                scratch_config_sha256 = (Get-Sha256Lower $arm.scratch_config)
                scratch_save_sha256 = $null; scratch_save_pin_matches = $false
            }
            $script:commands += $rec
            $arm['run'] = $rec
            Write-ArmReceipt $arm $rec
            return
        }
        Copy-Item -LiteralPath $pin.path -Destination $arm.scratch_save -Force
        $saveSha = Get-Sha256Lower $arm.scratch_save
    }

    # The child inherits this process's environment, so the SL_* set is cleared
    # to nothing and then filled from the arm's map alone: no ambient SL_* can
    # reach an arm, and nothing of the caller's is left behind.
    $saved = @{}
    foreach ($v in Get-ChildItem Env: | Where-Object { $_.Name -like 'SL_*' }) {
        $saved[$v.Name] = $v.Value
        Remove-Item -LiteralPath ("Env:" + $v.Name) -ErrorAction SilentlyContinue
    }
    foreach ($k in $arm.env.Keys) { Set-Item -LiteralPath ("Env:" + $k) -Value $arm.env[$k] }

    $start = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    $code = $null
    $timedOut = $false
    $pid_ = $null
    $failure = $null
    $rec = $null
    # The arm's OWN executable when it declares one, and the script-level
    # default otherwise - which is every arm the batch has ever had.
    $armExe = $exe
    if ($arm.exe) { $armExe = $arm.exe }
    try {
        try {
            $p = Start-Process -FilePath $armExe -WorkingDirectory $arm.cwd -PassThru `
                               -RedirectStandardOutput $outPath -RedirectStandardError $errPath
            # Cache the handle BEFORE waiting. Without this read, -PassThru
            # without -Wait leaves .ExitCode empty and a nonzero child exit
            # disappears (measured).
            $null = $p.Handle
            $pid_ = $p.Id
            # THE LAUNCH RECEIPT, before any dependent step. An exception or a
            # kill after this point still leaves the pid and the start time on
            # disk.
            Write-JsonNoBom (Join-Path $dir 'launch.json') ([ordered]@{
                schema = 'sightline-perflive-arm-launch/1'
                name = $arm.name; pid = $pid_; start_utc = $start
                exe = $armExe; exe_sha256 = (Get-Sha256Lower $armExe)
                stdout = $outPath; stderr = $errPath
                scratch_config_sha256 = (Get-Sha256Lower $arm.scratch_config)
                scratch_save_sha256 = $saveSha
                scratch_save_pin_matches = $savePinOk
            }) 4
            if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
                $timedOut = $true
                try { $p.Kill() } catch { }
                $p.WaitForExit()
            }
            $code = $p.ExitCode
        } catch {
            $failure = "$($_.Exception.GetType().FullName): $($_.Exception.Message)"
        }
    } finally {
        foreach ($k in $arm.env.Keys) { Remove-Item -LiteralPath ("Env:" + $k) -ErrorAction SilentlyContinue }
        foreach ($k in $saved.Keys) { Set-Item -LiteralPath ("Env:" + $k) -Value $saved[$k] }
        $end = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        $rec = [ordered]@{
            argv = $arm.argv; pid = $pid_; exit_code = $code; timed_out = $timedOut
            launch_failure = $failure
            exe = $armExe; exe_sha256 = (Get-Sha256Lower $armExe)
            start_utc = $start; end_utc = $end; stdout = $outPath; stderr = $errPath
            scratch_config_sha256 = (Get-Sha256Lower $arm.scratch_config)
            scratch_save_sha256 = $saveSha
            scratch_save_pin_matches = $savePinOk
        }
        $script:commands += $rec
        $arm['run'] = $rec
        Write-ArmReceipt $arm $rec
    }
    $state = 'exit ' + "$code"
    if ($timedOut) { $state = "KILLED after ${TimeoutSeconds}s" }
    if ($failure) { $state = "LAUNCH FAILED: $failure" }
    Write-Host ("  {0,-16} pid {1,-7} {2}" -f $arm.name, "$pid_", $state)
}

function Show-Arm($arm) {
    Write-Host ''
    Write-Host ("[{0}] phase {1}  stage {2} ({3})  frames {4}  ~{5}s" -f `
                $arm.name, $arm.phase, $arm.stage, $arm.stage_number, $arm.frames, $arm.expected_seconds)
    Write-Host ("  pose      {0} ({1})" -f $arm.pose.kind, $arm.pose.source)
    if ($arm.pose.kind -eq 'teleport') {
        Write-Host ("            x={0} y={1} z={2} theta={3} verta={4} room={5}" -f `
                    $arm.pose.x, $arm.pose.y, $arm.pose.z, $arm.pose.theta, $arm.pose.verta, $arm.pose.room)
        Write-Host ("            mark sha256 {0}, stage {1}, shape {2}" -f `
                    $arm.pose.mark_sha256, $arm.pose.mark_stage, $arm.pose.shape)
    }
    if ($arm.pose.unqualified_roles) {
        Write-Host ("            UNQUALIFIED for: {0}" -f (@($arm.pose.unqualified_roles) -join ', '))
    }
    Write-Host ("  argv      {0}" -f ($arm.argv -join ' '))
    Write-Host ("  cwd       {0}" -f $arm.cwd)
    Write-Host ("  config    {0} (sha256 {1})" -f $arm.scratch_config, $arm.scratch_config_sha256)
    Write-Host ("  save      {0}" -f $arm.scratch_save)
    if ($arm.pinned_eeprom.path) {
        Write-Host ("            fresh copy of {0} (sha256 {1})" -f `
                    $arm.pinned_eeprom.path, $arm.pinned_eeprom.sha256)
    }
    Write-Host ("  expect    presentation={0} set={1} item={2} {3}x{4}" -f `
                $arm.expect.presentation, $arm.expect.presentation_set, $arm.expect.stage_item,
                $arm.expect.width, $arm.expect.height)
    if ($arm.shots) {
        Write-Host ("  shots     gate={0} first={1} last={2} every={3} read_first={4} read_last={5}" -f `
                    $arm.shots.gate, $arm.shots.first, $arm.shots.last, $arm.shots.every,
                    $arm.shots.read_first, $arm.shots.read_last)
        Write-Host ("            expected_moments = {0}  count={1}  {2}x{3}" -f `
                    (@($arm.shots.expected_moments) -join ', '), $arm.shots.count,
                    $arm.shots.width, $arm.shots.height)
    } else {
        Write-Host '  shots     none'
    }
    if ($arm.pad_script) {
        Write-Host ("  pad       {0} steps, sha256 {1}" -f $arm.pad_script.steps, $arm.pad_script.sha256)
    }
    if ($arm.control) {
        Write-Host ("  control   {0} ({1} reads), sha256 {2}" -f `
                    $arm.control.name, $arm.control.reads, $arm.control.input_sha256)
    }
    if ($arm.pair) { Write-Host ("  pair      {0} (role {1})" -f $arm.pair, $arm.role) }
    # Only the matrix's arms carry these, and they are the whole of what makes
    # the matrix readable: which environment makes which subsystem active, and
    # the derived assertion that exactly one axis moves between the arms this
    # one is meant to be compared against.
    if ($arm.matrix) {
        Write-Host ("  row       {0}" -f $arm.matrix.row)
        Write-Host ("  disables  {0} ({1})" -f $arm.matrix.disabled_by, $arm.matrix.disables)
        Write-Host ("            {0}" -f $arm.matrix.disables_what)
        Write-Host '  absent BY DESIGN, so the ON row governs what the arm does not disable'
        foreach ($k in $arm.matrix.absent_by_design.Keys) {
            Write-Host ("    {0,-24} {1}" -f $k, $arm.matrix.absent_by_design[$k])
        }
        Write-Host ("  compared  against {0}; pixel ({1},{2})" -f `
                    (@($arm.matrix.compared_against) -join ' and '),
                    $arm.matrix.pixel.x, $arm.matrix.pixel.y)
    }
    if ($arm.axis_assertions) {
        foreach ($as in @($arm.axis_assertions)) {
            $state = 'not asserted'
            if ($as.asserted) {
                $state = 'DOES NOT HOLD'
                if ($as.holds) { $state = 'holds' }
            }
            Write-Host ("  vs {0,-8} {1} ({2}) [{3}]: {4}" -f `
                        $as.from, $as.intent, $state, (@($as.axes_moved) -join '+'),
                        ((@($as.behavioural_changes)) -join ', '))
            Write-Host ("              per-arm paths, differing by design: {0}" -f `
                        ((@($as.per_arm_paths_differing)) -join ', '))
            if ($as.why) { Write-Host ("              {0}" -f $as.why) }
        }
    }
    Write-Host '  env'
    foreach ($k in $arm.env.Keys) { Write-Host ("            {0}={1}" -f $k, $arm.env[$k]) }
    Write-Host ("  witnesses {0}" -f ($arm.witnesses -join ', '))
    Write-Host ("  note      {0}" -f $arm.note)
}

function Invoke-Checker([string[]]$cargs) {
    $argv = @($python, '-I', '-B', $checker) + $cargs
    $log = Join-Path $script:attemptDir 'checker.txt'
    $o = & $python -I -B $checker @cargs 2>&1
    $rc = $LASTEXITCODE
    Add-Content -LiteralPath $log -Value (@("`$ " + ($argv -join ' ')) + $o + "exit $rc") -Encoding UTF8
    $script:commands += [ordered]@{ argv = $argv; exit_code = $rc; stdout = $log; stderr = $log }
    foreach ($l in $o) { Write-Host "  | $l" }
    return $rc
}

$wanted = $null
if ($Only) { $wanted = @($Only) }
function Want($name) {
    if ($null -eq $wanted) { return $true }
    return ($wanted -contains $name)
}

# THE PAIR GATE. The two halves' captures, byte for byte, with no tolerance.
# A real image difference between the CPU far path and the GPU one at the one
# scene where the far path is measured to submit is a FINDING about the row:
# the batch stops and preserves rather than carrying on over it.
function Test-PairIdentical($a, $b) {
    $da = Join-Path $script:attemptDir $a
    $db = Join-Path $script:attemptDir $b
    $fa = @(Get-ChildItem -LiteralPath $da -Filter 'shot-*.ppm' -File -ErrorAction SilentlyContinue | Sort-Object Name)
    $fb = @(Get-ChildItem -LiteralPath $db -Filter 'shot-*.ppm' -File -ErrorAction SilentlyContinue | Sort-Object Name)
    $findings = @()
    if ($fa.Count -eq 0 -or $fb.Count -eq 0) {
        $findings += ("one half wrote no captures: {0} has {1}, {2} has {3}" -f $a, $fa.Count, $b, $fb.Count)
        return [ordered]@{ identical = $false; compared = 0; findings = $findings
                           verdict = 'NOT_COMPARABLE' }
    }
    if ($fa.Count -ne $fb.Count) {
        $findings += ("capture counts differ: {0} wrote {1}, {2} wrote {3}" -f $a, $fa.Count, $b, $fb.Count)
    }
    $n = [Math]::Min($fa.Count, $fb.Count)
    for ($i = 0; $i -lt $n; $i++) {
        if ($fa[$i].Name -ne $fb[$i].Name) {
            $findings += ("name {0} != {1}" -f $fa[$i].Name, $fb[$i].Name)
            continue
        }
        $ha = Get-Sha256Lower $fa[$i].FullName
        $hb = Get-Sha256Lower $fb[$i].FullName
        if ($ha -ne $hb) {
            $findings += ("{0}: {1} != {2}" -f $fa[$i].Name, $ha.Substring(0, 16), $hb.Substring(0, 16))
        }
    }
    $v = 'IDENTICAL'
    if ($findings.Count -gt 0) { $v = 'DIFFERENT' }
    return [ordered]@{ identical = ($findings.Count -eq 0); compared = $n
                       findings = $findings; verdict = $v }
}

# ---- the roles ------------------------------------------------------------
# The FAR role comes from the hash-bound recipe and from nowhere else. The
# reduction and skydome roles come from a selection if one exists, and each is
# re-proved here from the candidate's own acquisition record.
$script:roleProof = [ordered]@{}
$script:roleProof['far'] = [ordered]@{
    proved = $true; stage = $RECIPE.stage; source = 'ENGINE_START_PAD recipe'
    because = @(("bound by hash to the anchor record (sha256 {0}), the config bytes (sha256 {1}) " +
                 "and the initial save (sha256 {2}); {3}") -f `
                $RECIPE.anchor_sha256, $RECIPE.config_sha256, $RECIPE.initial_save_sha256,
                $RECIPE.measured)
    qualified_roles = $RECIPE.qualified_roles
    unqualified_roles = $RECIPE.unqualified_roles
}

$selPath = $null
$sel = $null
$selSha = $null
$selFrom = $null
if ($Resume) {
    $selPath = Join-Path $Resume 'selected.json'
    $selFrom = $Resume
    if (Test-Path -LiteralPath $selPath) {
        $sel = Get-Content -LiteralPath $selPath -Raw | ConvertFrom-Json
        $selSha = Get-Sha256Lower $selPath
    } else {
        Write-Host ''
        Write-Host "perflive: -Resume '$Resume' carries no selected.json; the reduction and skydome"
        Write-Host '  roles stay unproved and their arms are NOT_COVERED.'
    }
}

foreach ($role in @('reduction', 'skydome')) {
    if ($null -eq $sel) {
        $script:roleProof[$role] = [ordered]@{
            proved = $false; stage = $null; source = 'none'
            because = @(("no selection was read, so no candidate was measured for the $role role. " +
                         "It is NOT filled in with the far candidate: that is the fallback " +
                         "assignment this runner rejects by name."))
        }
        continue
    }
    $named = $null
    if ($sel.$role) { $named = "$($sel.$role.stage)" }
    $proof = Test-RoleProved @($sel.candidates) $named $role "$($sel.far.stage)"
    $proof['source'] = $selPath
    $script:roleProof[$role] = $proof
}

Write-Host ''
Write-Host '--- ROLES ---'
foreach ($k in $script:roleProof.Keys) {
    $p = $script:roleProof[$k]
    $state = 'UNPROVED'
    if ($p.proved) { $state = 'PROVED' }
    Write-Host ("  {0,-10} {1,-9} {2}" -f $k, $state, "$($p.stage)")
    foreach ($b in @($p.because)) { Write-Host ("             $b") }
}
foreach ($k in @('reduction', 'skydome')) {
    if (-not $script:roleProof[$k].proved) {
        Add-NotCovered ("ROLE_" + $k.ToUpper() + "_UNPROVED") (
            ("the $k role was not proved, so its dependent arms are NOT_COVERED and were not run. " +
             "Reasons: ") + ((@($script:roleProof[$k].because)) -join '; '))
    }
}
if ($script:controlRefusals.Count -gt 0 -or (-not $Control)) {
    $why = 'no -Control was given'
    if ($script:controlRefusals.Count -gt 0) { $why = (@($script:controlRefusals) -join '; ') }
    Add-NotCovered 'SECOND_POSE' (
        ("the second-pose arms need a viewpoint away from the start pad, which comes from a pinned " +
         "saved control of the recipe's own stage or from a mark. $why, so the arms were not built " +
         "and were not run. No coordinate was invented for them."))
}

# ---- the optional real poses -------------------------------------------
# These bind the cases the recipe does NOT qualify. Their absence leaves those
# cases NOT_COVERED; it never re-poses an arm onto the start pad.
$script:poseRefusals = @()
$optionalPoses = @{
    occluder = (Get-BoundPose 'far-occluder.txt' $RECIPE.stage_number 'the occluder-ordering case')
    blended  = (Get-BoundPose 'far-blended.txt'  $RECIPE.stage_number 'the blended-far-geometry case')
}

# ---- the staged batch -------------------------------------------------
# SMALLEST-FIRST, and the first group is a gate. Each group is run in order and
# the gate decides whether the rest runs at all.
#
# THE ACQUISITION ARMS ARE BUILT BEFORE THE REFUSALS ARE PRINTED, and the order
# is load-bearing rather than incidental. New-AcqArms is the only place that
# asks Get-BoundPose for an acq-<stage>.txt, so it is the only place that can
# append a DISCOVERY-phase refusal to $script:poseRefusals. Printing the list
# before this ran meant a discovery refusal was appended after the one place it
# would have been shown, and so never appeared at all: the dry run reported the
# optional poses' refusals and silently dropped the probes'. It survived into
# arms.json either way, which is why the loss showed only in what a reader of
# the dry run was told.
$armsAcq = @()
if ($Acquire) { $armsAcq = New-AcqArms }

if ($script:poseRefusals.Count -gt 0) {
    Write-Host ''
    Write-Host '--- OPTIONAL POSES NOT BOUND (the cases they would close stay NOT_COVERED) ---'
    Write-Host '    (a DISCOVERY probe''s refused mark is listed here too; the probe then stands at'
    Write-Host '     the stage''s own start pad and its pose record says it was refused, not absent)'
    foreach ($r in $script:poseRefusals) { Write-Host "  $r" }
}

$poses = @{ control = $script:boundControl; occluder = $optionalPoses.occluder
            blended = $optionalPoses.blended }
$armsRecipe = New-RecipeArms $poses

$byName = @{}
foreach ($a in ($armsAcq + $armsRecipe)) { $byName[$a.name] = $a }

$SCHEDULE = @(
    [ordered]@{ group = 'FAR-PAIR'; arms = @('B-depth-off', 'B-depth-on')
                gate = 'pair-images-identical'
                why = 'the measured far case, paired at the hash-bound recipe. It runs FIRST and it is a GATE: if the two halves'' captures differ byte for byte the batch stops and preserves, because a real image difference at the one scene where the far path is measured to submit is a finding and not an obstacle to work around.' }
    [ordered]@{ group = 'GUARD'; arms = @('C-guard'); gate = $null
                why = 'the guard refuses the far ranges when SL_PERF_BG_FAR is absent, at the same recipe and with the same schedule' }
    [ordered]@{ group = 'BLEND'; arms = @('D-blend-off', 'D-blend-on'); gate = $null
                why = 'the translucent class drawn inside the walk in both halves, paired identity, no far-blend or occlusion claim' }
    [ordered]@{ group = 'MOVE'; arms = @('F-move-off', 'F-move-on'); gate = $null
                why = 'movement and turning within one run through the pinned pad script, paired identity' }
    [ordered]@{ group = 'SECOND-POSE'; arms = @('E-pose2-off', 'E-pose2-on'); gate = $null
                why = 'a second independent viewpoint from a pinned saved control; built only when -Control binds one of the recipe''s own stage' }
    [ordered]@{ group = 'COMBINED'; arms = @('G4-combined-off', 'G4-combined-on'); gate = $null
                why = 'the clean comparison: CPU workers, the GPU transform, the background bypass and the sky''s prod float carrier in one run, with no statistics reader to refuse any of them' }
    [ordered]@{ group = 'ACQUIRE'; arms = @($armsAcq | ForEach-Object { $_.name }); gate = $null
                why = 'the declared acquisition probe for the reduction role; built only when -Acquire is given, and finite by declaration' }
)

# ---- the two missing combinations REPLACE the batch, when asked for -------
# Two arms instead of nine, at the same recipe and the same grid, and no gate:
# a difference from a baseline is what this measures. The batch's own arms are
# not built into this schedule, so the run spends exactly TWO GPU starts - the
# saved halves are reused by hash and are not re-run - and makes no attempt at
# the batch's cases, which is recorded as NOT_COVERED below rather than left to
# be inferred from the arm list.
$script:discArms = @()
if ($Discriminate) {
    $script:discArms = New-DiscriminateArms
    $byName = @{}
    foreach ($a in $script:discArms) { $byName[$a.name] = $a }
    $SCHEDULE = @(
        [ordered]@{ group = 'DISCRIMINATE'
                    arms = @($AXIS_MATRIX.arms)
                    gate = $null
                    why = ('the two combinations the saved pair does not contain, at the hash-bound ' +
                           'recipe and the saved pair''s own capture moments: the row ON with the ' +
                           'skydome batch disabled, and the row ON with the background GPU transform ' +
                           'disabled. Each arm is compared against BOTH saved halves. NO GATE: a ' +
                           'difference from a baseline is the reading this group takes, and which ' +
                           'baseline an arm matches is reported rather than explained.') }
    )
    Add-NotCovered 'BATCH_CASES_NOT_ATTEMPTED' (
        ('-Discriminate ran the two missing combinations instead of the staged batch, so none of ' +
         'the batch''s mandatory cases ({0}) was attempted and nothing here is a qualification of ' +
         'the OPTIMIZED RENDERING row or a justification for a rendering change. The saved pair''s ' +
         'own failure stands unerased and is not re-run.') -f ($MANDATORY_CASES -join ', '))
    Add-NotCovered 'DISCRIMINATE_SCOPE' (
        'what this diagnostic does NOT establish, declared so that a row of it cannot be read as ' +
        'more than it is: ' + ((@($AXIS_MATRIX.establishes_not)) -join '; '))
}

# ---- the candidate group REPLACES the batch, when asked for --------------
# Three arms instead of nine, at the same recipe and the same grid, from the
# isolated diagnostic runtime. The decisive comparison is an exact A/B between
# two arms of the SAME binary, with both arms also checked against the saved
# ORIGINAL captures; the diagnostic arm is read and never decides anything.
$script:candArms = @()
if ($Candidate) {
    $script:candArms = New-CandidateArms
    $byName = @{}
    foreach ($a in $script:candArms) { $byName[$a.name] = $a }
    $SCHEDULE = @(
        [ordered]@{ group = 'CANDIDATE'
                    arms = @($TFCLIP_ARMS | ForEach-Object { $_.name })
                    gate = $null
                    why = ('the background GPU clip prepass at the hash-bound recipe and the ' +
                           'saved halves'' own capture moments, from the isolated diagnostic ' +
                           'runtime: the ORIGINAL CPU / program-0 baseline, the candidate, and ' +
                           'the existing vertex-program shader as a diagnostic. NO GATE IS ' +
                           'DECLARED HERE: the image judgement is made by the chained control, ' +
                           'which orders it baseline-oracle first, then candidate-against-' +
                           'ORIGINAL, then the exact same-binary A/B.') }
    )
    Add-NotCovered 'BATCH_CASES_NOT_ATTEMPTED_CANDIDATE' (
        ('-Candidate ran the three clip-prepass arms instead of the staged batch, so none of ' +
         'the batch''s mandatory cases ({0}) was attempted and nothing here is a qualification ' +
         'of the OPTIMIZED RENDERING row.') -f ($MANDATORY_CASES -join ', '))
    Add-NotCovered 'CANDIDATE_RECOMPUTES_CPU_POSITIONS' (
        'THE CANDIDATE CLAIMS NO CPU-BYPASS SAVING. A candidate walk deliberately keeps ' +
        'gpu_walk at 0, so `if (bypass && !gpu_walk)` re-runs the CPU clip transform over the ' +
        'whole item before any reader draws (src/gfx/sl_gfx_dl.c:16843-16853, :18958): the ' +
        'candidate pays the full CPU transform IN ADDITION to its GPU prepass. Any statement ' +
        'that a bypass saving is preserved would be false by construction. The existing vertex ' +
        'program''s direct-shader performance is also NOT the candidate''s and is not credited ' +
        'to it - a candidate walk takes the prepass INSTEAD of the program, so measurements of ' +
        'the program''s path say nothing about this one. A timing screen here is therefore ' +
        'expected to be neutral at best and is DESCRIPTIVE only: not a significance test, not ' +
        'pooled frames, not a performance claim.')
    Add-NotCovered 'CANDIDATE_OWN_FAR_COUNTS_NEVER_OBSERVED' (
        'the candidate''s OWN far0 and far1 submission counts have never been observed on a ' +
        'driver. The per-depth readings measured at this recipe are the vertex PROGRAM''s, not ' +
        'the prepass''s, and they are not transferred to it. The candidate group is the first ' +
        'run that could read the candidate''s own, which is why it is ordered first.')
    Add-NotCovered 'CANDIDATE_SINGLE_DEPTH_SUBMISSION' (
        'SINGLE-range submission by the prepass has no measured viewpoint. At this recipe every ' +
        'per-depth census report read 0 calls / 0 indices at the single-range clamp while both ' +
        'far columns ran, so TF_SOURCE_DEPTH_SINGLE_IS_A_CONSISTENCY_READING expects 0 here and ' +
        'is explicitly NOT a positive claim.')
}

$script:ordered = @()
foreach ($g in $SCHEDULE) {
    foreach ($n in @($g.arms)) {
        if ($byName.ContainsKey($n)) {
            if (Want $n) { $script:ordered += $byName[$n] }
        }
    }
}

# ---- the dry run -------------------------------------------------------
if ($DryRun) {
    if ($Candidate) {
        Write-Host ''
        Write-Host '--- THE CLIP-PREPASS CANDIDATE GROUP ---'
        Write-Host ("  runtime       {0}" -f $script:tfBin)
        Write-Host ("  exe           {0}" -f $script:tfExe)
        Write-Host ("  exe sha256    {0}" -f $script:tfExeSha)
        foreach ($k in $script:tfDllSha.Keys) {
            Write-Host ("  {0,-13} {1}" -f $k, $script:tfDllSha[$k])
        }
        Write-Host '  the correctness baseline is ORIG_CPU - the original CPU / program-0 renderer.'
        Write-Host '  DIAG_PROG is read as a DIAGNOSTIC and is never correctness authority.'
        foreach ($d in $TFCLIP_ARMS) {
            Write-Host ''
            Write-Host ("  [{0}] {1}" -f $d.name, $d.is)
            Write-Host ("        env    {0}" -f (($d.env.Keys | ForEach-Object { "$_=$($d.env[$_])" }) -join ' '))
            foreach ($k in $d.absent.Keys) {
                Write-Host ("        absent {0}: {1}" -f $k, $d.absent[$k])
            }
            Write-Host ("        {0}" -f $d.authority)
        }
        Write-Host ''
        Write-Host '  WITNESSES THAT INVERT, and are therefore NOT asked of the candidate arm:'
        Write-Host '    WINDOW_FAR0_DELTA_POSITIVE and WINDOW_FAR1_DELTA_POSITIVE read the vertex'
        Write-Host '    PROGRAM''s far columns. A prepass walk leaves gpu_walk 0 and selects pool 0,'
        Write-Host '    so those columns read ZERO when the candidate works: the witnesses would FAIL'
        Write-Host '    on success. PROGRAM_COLUMNS_MUST_INVERT reads the same zeros as the witness.'
        Write-Host '  THE CANDIDATE RECOMPUTES CPU POSITIONS and therefore claims NO bypass saving;'
        Write-Host '    the existing shader''s performance is not credited to it, and a timing screen'
        Write-Host '    on it is DESCRIPTIVE only.'
        if ($Warmups) {
            Write-Host ''
            Write-Host '--- MATCHED UNSCORED WARMUPS, TWO OPPOSITE-ORDER BLOCKS (-Warmups) ---'
            Write-Host ("  blk1  {0} then {1}" -f 'ORIG_CPU', 'TF_ON')
            Write-Host ("  blk2  {0} then {1}" -f 'TF_ON', 'ORIG_CPU')
            Write-Host ("  one {0} s settle immediately before EACH warmup, {1} settles in all" -f `
                        $WARMUP_SETTLE_SECONDS, 4)
            Write-Host '  one warmup immediately before each measured arm, in that arm''s OWN condition'
            Write-Host ("  between a warmup and its arm, only: {0}" -f ($PAIR_MANDATORY_STEPS -join ', '))
            Write-Host ("  refused there: {0}" -f ($PAIR_REFUSED_STEPS -join ', '))
            Write-Host '  a warmup is a REAL native start, claimed in the same budget, recorded with'
            Write-Host '  role WARMUP and NEVER scored; warmup records are kept OUT of `arms`, which is'
            Write-Host '  the list the checker consumes, so none can reach a verdict by mistake.'
            Write-Host '  8 starts: 4 measured repeats and 4 matched warmups, plus 1 for DIAG_PROG.'
        }
    }
    if ($Discriminate) {
        Write-Host ''
        Write-Host '--- THE TWO MISSING COMBINATIONS ---'
        Write-Host ("  arms          {0}" -f (@($AXIS_MATRIX.arms) -join ', '))
        Write-Host ("  pixel         ({0},{1}) - {2}" -f `
                    $AXIS_MATRIX.pixel.x, $AXIS_MATRIX.pixel.y, $AXIS_MATRIX.pixel.convention)
        Write-Host ("                {0}" -f $AXIS_MATRIX.pixel.measured)
        Write-Host ("  comparison    {0}" -f $AXIS_MATRIX.comparison)
        Write-Host ("  frozen exe    {0}" -f $AXIS_MATRIX.exe_sha256)
        Write-Host '  saved baselines, REUSED by hash and NOT re-run'
        foreach ($b in @($AXIS_MATRIX.baselines)) {
            Write-Host ("    {0,-8} {1}" -f $b.label, $b.is)
            Write-Host ("             {0}" -f $script:baseDirOf[$b.label])
            Write-Host ("             env {0}" -f (($b.env.Keys | ForEach-Object { "$_=$($b.env[$_])" }) -join ' '))
            foreach ($c in @($b.captures)) {
                Write-Host ("             {0}  {1}" -f $c.name, $c.sha256)
            }
        }
        Write-Host '  capture moments both saved halves recorded, which every arm must reproduce'
        foreach ($m in @($AXIS_MATRIX.moments)) {
            Write-Host ("    {0}  read={1} frame={2} cloud phase {3}" -f $m.name, $m.read, $m.frame, $m.cloud_phase)
        }
        Write-Host '  constants, identical in both arms and in the combined baseline'
        foreach ($k in $AXIS_MATRIX.constants.Keys) {
            Write-Host ("    {0,-16} {1}" -f $k, $AXIS_MATRIX.constants[$k])
        }
        Write-Host '  the one subsystem each arm disables'
        foreach ($k in $AXIS_MATRIX.axes.Keys) {
            Write-Host ("    {0,-12} {1}" -f $k, (@($AXIS_MATRIX.axes[$k].vars | ForEach-Object { "$_=0" }) -join ' '))
            Write-Host ("                 {0}" -f $AXIS_MATRIX.axes[$k].disables)
        }
        Write-Host '  establishes'
        foreach ($e in @($AXIS_MATRIX.establishes)) { Write-Host ("    - {0}" -f $e) }
        Write-Host '  does NOT establish'
        foreach ($e in @($AXIS_MATRIX.establishes_not)) { Write-Host ("    - {0}" -f $e) }
        $notHeld = @($script:discArms | Where-Object { -not $_.axis_assertions_hold })
        if ($notHeld.Count -eq 0) {
            Write-Host ("  assertions     each arm's environment differs from the {0} baseline's OWN recorded" -f $AXIS_MATRIX.assert_against)
            Write-Host '                 environment by exactly one subsystem''s disable, derived below from the two'
            Write-Host '                 maps. The delta against the other half is recorded and is NOT asserted as'
            Write-Host '                 one axis, because the row differs there too.'
        } else {
            Write-Host ("  assertions     DO NOT HOLD for: {0}. The diagnostic separates nothing in that state." -f `
                        (($notHeld | ForEach-Object { $_.name }) -join ', '))
        }
    }
    Write-Host ''
    Write-Host '--- THE STAGED BATCH, SMALLEST-FIRST ---'
    foreach ($g in $SCHEDULE) {
        $present = @(@($g.arms) | Where-Object { $byName.ContainsKey($_) -and (Want $_) })
        Write-Host ''
        Write-Host ("=== {0} ===" -f $g.group)
        Write-Host ("    {0}" -f $g.why)
        if ($g.gate) { Write-Host ("    GATE: {0} - a failure here STOPS the batch and preserves." -f $g.gate) }
        if ($present.Count -eq 0) {
            $missing = @(@($g.arms) | Where-Object { -not $byName.ContainsKey($_) })
            if (@($g.arms).Count -eq 0) {
                Write-Host '    NO ARMS DECLARED: this group is built only on request and was not asked for.'
            } elseif ($missing.Count -gt 0) {
                Write-Host ("    NOT BUILT: {0}. See the NOT_COVERED list for the reason." -f ($missing -join ', '))
            } else {
                Write-Host '    not in this -Only subset'
            }
            continue
        }
        foreach ($n in $present) { Show-Arm $byName[$n] }
    }
    $total = 0
    foreach ($a in $script:ordered) { $total += $a.expected_seconds }
    Write-Host ''
    Write-Host '--- DECLARED NOT_COVERED ---'
    foreach ($nc in ($NOT_COVERED + $script:runtimeNotCovered)) {
        Write-Host ("  {0}" -f $nc.name)
        Write-Host ("    {0}" -f $nc.reason)
    }
    Write-Host ''
    Write-Host '--- ACQUISITION ---'
    if ($Acquire) {
        foreach ($a in $ACQUISITION) {
            Write-Host ("  PROBE {0} for the {1} role: {2}" -f $a.stage, $a.role, $a.justification)
        }
    } else {
        Write-Host '  OFF (-Acquire not given). The far case is measured and needs no discovery.'
        Write-Host '  If the reduction role is wanted, these are the only arms that would run:'
        foreach ($a in $ACQUISITION) {
            Write-Host ("    {0} ({1} frames, ~{2}s) - {3}" -f $a.stage, $a.frames, $a.seconds, $a.justification)
        }
    }
    foreach ($x in $ACQUISITION_EXCLUDED) {
        Write-Host ("  NOT PROBED {0}: {1}" -f $x.stage, $x.reason)
    }
    Write-Host ''
    Write-Host '--- SAVED CONTROLS ---'
    foreach ($c in $SAVED_CONTROLS) {
        Write-Host ("  {0} (stage {1}) - {2}" -f $c.name, $c.stage, $c.covers)
    }
    Write-Host ("  of another stage, and therefore not usable here: {0}" -f ($CONTROLS_OFF_STAGE -join ', '))
    Write-Host ''
    Write-Host ("Arms: {0}.  Estimated wall time: {1} s ({2} min), sequential, one GPU run at a time." -f `
                $script:ordered.Count, $total, [Math]::Round($total / 60.0, 1))
    if ($Discriminate) {
        $cmpTool = Join-Path $PSScriptRoot 'perflive-ppmdiff.py'
        Write-Host ''
        Write-Host 'The comparison the live run makes after the two arms, from the same builder it uses:'
        Write-Host ("  {0} -I -B {1} {2}" -f $python, $cmpTool, ((Get-MatrixArgs) -join ' '))
        Write-Host ("  comparator present: {0}" -f (Test-Path -LiteralPath $cmpTool))
        Write-Host '  Whole frames byte for byte against BOTH saved halves, no tolerance, the declared'
        Write-Host '  pixel read out per capture, and every reference capture bound by its sha256. Exit 3'
        Write-Host '  means a comparison could not be MADE; it never means an arm differed.'
        Write-Host '  Each arm''s own stderr is also read for its capture moments and its sky path, which'
        Write-Host '  the witness checker cannot take across runs.'
    }
    Write-Host 'Phase H is a manual checklist and is NOT optional for full qualification:'
    Write-Host '  perflive-check.py --manual            print the steps'
    Write-Host '  perflive-check.py --manual-template <attempt-dir>   write manual.json to record them in'
    Write-Host 'A manual step with no recorded outcome leaves the batch INCOMPLETE; it is never a pass.'
    Write-Host 'DRY RUN COMPLETE. No child process was started.'
    exit 0
}

# ---- the live run ------------------------------------------------------
$script:gateStop = $null
$script:counterbalance = $null
foreach ($g in $SCHEDULE) {
    $present = @(@($g.arms) | Where-Object { $byName.ContainsKey($_) -and (Want $_) })
    if ($present.Count -eq 0) { continue }
    if ($script:gateStop) { break }
    Write-Host ''
    Write-Host ("--- {0} ---" -f $g.group)
    # THE COUNTERBALANCED TIMING PASS, when it is asked for. The two decisive
    # arms run as two opposite-order blocks with one matched unscored warmup
    # before each measured arm in that arm's own condition. The diagnostic arm
    # is not in the counterbalance: it decides nothing, so spending four more
    # starts on it would buy nothing. It runs once, after the blocks.
    if ($Warmups -and $g.group -eq 'CANDIDATE' -and
        ($present -contains 'ORIG_CPU') -and ($present -contains 'TF_ON')) {
        $script:counterbalance = Invoke-CounterbalancedBlocks $byName['ORIG_CPU'] $byName['TF_ON']
        # THE RUNS THAT ACTUALLY HAPPENED ARE THE ONES RECORDED. The two
        # declared arms were never launched as themselves - each ran twice, as
        # its own two repeat arms - so the scored list is rebuilt from the
        # repeats. Leaving the declared arms in it would carry two entries with
        # no run beside four runs with no entry.
        $rest = @($script:ordered | Where-Object {
            $_.name -ne 'ORIG_CPU' -and $_.name -ne 'TF_ON' })
        $script:ordered = @($script:repeatArms) + $rest
        foreach ($n in $present) {
            if ($n -eq 'ORIG_CPU' -or $n -eq 'TF_ON') { continue }
            Invoke-Arm $byName[$n]
        }
    } else {
        foreach ($n in $present) { Invoke-Arm $byName[$n] }
    }
    if ($g.gate -eq 'pair-images-identical' -and $present.Count -eq 2) {
        $cmp = Test-PairIdentical $present[0] $present[1]
        Write-JsonNoBom (Join-Path $script:attemptDir 'gate-far-pair.json') ([ordered]@{
            schema = 'sightline-perflive-gate/1'
            group = $g.group; arms = $present; compared = $cmp.compared
            verdict = $cmp.verdict; findings = $cmp.findings
            rule = 'byte for byte, no tolerance; a difference is a finding about the row and stops the batch'
        }) 5
        Write-Host ("  gate      {0} ({1} capture pairs compared byte for byte)" -f $cmp.verdict, $cmp.compared)
        foreach ($f in $cmp.findings) { Write-Host "            $f" }
        if (-not $cmp.identical) {
            $script:gateStop = [ordered]@{ group = $g.group; verdict = $cmp.verdict
                                           findings = $cmp.findings }
            Write-Host ''
            Write-Host 'perflive: STOP AND PRESERVE. The measured far pair is not byte-identical.'
            Write-Host '  This is a FINDING about the OPTIMIZED RENDERING row at the one scene where the'
            Write-Host '  far path is measured to submit, not a condition to run the rest of the batch'
            Write-Host '  over. Every output of this attempt is preserved and nothing further is'
            Write-Host '  launched. The two halves differ only in SL_PERF_OPT.'
        }
    }
}

# ---- the comparison, after the two arms have run -----------------------
# The comparison is a separate tool so that it can be exercised offline against
# synthetic captures without a GPU: a comparator that has only ever been run on
# real captures has never been shown to detect the one-pixel signal it exists
# for. Its exit code reports whether every comparison could be MADE. An arm that
# differs from a baseline is the reading, so that is exit 0.
function Invoke-Matrix {
    $tool = Join-Path $PSScriptRoot 'perflive-ppmdiff.py'
    $log  = Join-Path $script:attemptDir 'discriminate-matrix.txt'
    $jsn  = Join-Path $script:attemptDir 'discriminate-matrix.json'
    if (-not (Test-Path -LiteralPath $tool)) {
        Write-Host ("  matrix    NOT COMPARED: no comparator at '{0}'" -f $tool)
        $rec = [ordered]@{ argv = @($tool); exit_code = $null
                           launch_failure = "no comparator at '$tool'"
                           stdout = $log; stderr = $log }
        $script:commands += $rec
        return [ordered]@{ tool = $tool; json = $null; log = $log; exit_code = $null
                           output = @(); not_compared = "no comparator at '$tool'" }
    }
    $cargs = Get-MatrixArgs
    $argv = @($python, '-I', '-B', $tool) + $cargs
    $o = & $python -I -B $tool @cargs 2>&1
    $rc = $LASTEXITCODE
    Add-Content -LiteralPath $log -Value (@("`$ " + ($argv -join ' ')) + $o + "exit $rc") -Encoding UTF8
    $script:commands += [ordered]@{ argv = $argv; exit_code = $rc; stdout = $log; stderr = $log }
    foreach ($line in $o) { Write-Host "  | $line" }
    return [ordered]@{ tool = $tool; json = $jsn; log = $log; exit_code = $rc
                       output = @($o | ForEach-Object { "$_" }); not_compared = $null }
}

# WHAT THE TWO ARMS' OWN STDERR SAYS, read before the comparison so that an arm
# whose axis was not in force, or whose captures are at other moments, is
# visible beside the bytes rather than after them. Neither reading is a gate:
# both are recorded, and a reading that does not hold is reported as such.
$script:matrixResult = $null
$script:discReadings = $null
if ($Discriminate) {
    $script:discReadings = [ordered]@{}
    Write-Host ''
    Write-Host '--- EACH ARM''S OWN MOMENTS AND SKY PATH, from its stderr ---'
    foreach ($a in $script:ordered) {
        if (-not $a.matrix) { continue }
        $r = Get-ArmReadings $a
        $script:discReadings[$a.name] = $r
        $mState = 'HOLDS'
        if (-not $r.moments.holds) { $mState = 'DOES NOT HOLD' }
        $sState = 'HOLDS'
        if (-not $r.sky.holds) { $sState = 'DOES NOT HOLD' }
        Write-Host ("  {0,-12} moments {1}; sky ({2}) {3}" -f $a.name, $mState, $r.sky.expect, $sState)
        foreach ($f in @($r.moments.findings)) { Write-Host "               moments: $f" }
        foreach ($f in @($r.sky.findings)) { Write-Host "               sky: $f" }
        Write-Host ("               skybatch states {0}; final indexed-draws {1}; carrier markers {2}" -f `
                    ((@($r.sky.report_states) | Select-Object -Unique) -join ','),
                    "$($r.sky.final_indexed_draws)", ((@($r.sky.carrier_markers)) -join ','))
    }
    Write-Host ''
    Write-Host '--- THE COMPARISON: arm -> equal to each saved half -> the declared pixel ---'
    $script:matrixResult = Invoke-Matrix
}

# ------------------------------------------------------------------ evidence
$all = @($script:ordered)
$armsJson = Join-Path $script:attemptDir 'arms.json'

# The source these arms were produced from, so a result cannot be read against
# a different tree later. A dirty worktree is recorded as dirty rather than
# refused: this runner measures a built binary, not a clean checkout.
$gitHead = $null
$gitDirty = $null
try {
    $gitHead = (& git -C $repo rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -ne 0) { $gitHead = $null }
    $st = (& git -C $repo status --porcelain 2>$null)
    if ($LASTEXITCODE -eq 0) { $gitDirty = (@($st).Count -gt 0) }
} catch { }

$allNotCovered = @($NOT_COVERED + $script:runtimeNotCovered)

$batch = [ordered]@{
    schema = 'sightline-perflive/2'
    scope = $script:scope
    generated_utc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    repo = $repo
    out = $Out
    attempt = $script:attemptDir
    resumed_from = "$Resume"
    mandatory_phases = $script:mandatoryPhases
    mandatory_cases = $script:mandatoryCases
    census_reference_arm = $script:censusRefArm
    schedule = $SCHEDULE
    gate_stop = $script:gateStop
    binaries = [ordered]@{ sightline = $exeSha; SDL2 = $sdlSha; libwinpthread = $pthSha }
    tools = [ordered]@{
        'perflive.ps1' = (Get-Sha256Lower (Join-Path $PSScriptRoot 'perflive.ps1'))
        'perflive-check.py' = (Get-Sha256Lower $checker)
        'perflive-ppmdiff.py' = (Get-Sha256Lower (Join-Path $PSScriptRoot 'perflive-ppmdiff.py'))
    }
    discriminate = $(if ($Discriminate) { $AXIS_MATRIX } else { $null })
    discriminate_matrix = $script:matrixResult
    discriminate_readings = $script:discReadings
    # THE CANDIDATE GROUP. Its three arm declarations, the isolated runtime it
    # ran from, and every pin verified before any launch.
    candidate = $(if ($Candidate) {
        [ordered]@{
            runtime = $TFCLIP_RUNTIME
            runtime_dir = $script:tfBin
            exe = $script:tfExe
            exe_sha256 = $script:tfExeSha
            dll_sha256 = $script:tfDllSha
            pins = $script:tfPins
            arms_declared = $TFCLIP_ARMS
            correctness_baseline = 'ORIG_CPU - the original CPU / program-0 renderer'
            diagnostic_arm = 'DIAG_PROG, read only; never correctness authority'
        }
    } else { $null })
    # THE WARMUPS, IN A LIST SEPARATE FROM `arms` BY CONSTRUCTION. The witness
    # checker consumes `arms`, so a warmup cannot reach a scored verdict even
    # through a coding mistake. Every warmup record is retained whatever became
    # of the run it preconditioned.
    warmups = $script:warmupRecords
    warmups_scored = $false
    counterbalance = $script:counterbalance
    source = [ordered]@{ git_head = "$gitHead"; git_dirty = $gitDirty }
    rom = $rom
    seed_hex = $script:seed
    scratch_config_sha256 = $script:cfgSha
    scratch_config_text = $SCRATCH_CONFIG
    recipe = $RECIPE
    recipe_pins = $pinChecks
    save_output_trap = $SAVE_OUTPUT_TRAP
    pinned_eeprom = $script:eepromOf
    saved_controls = $SAVED_CONTROLS
    saved_controls_off_stage = $CONTROLS_OFF_STAGE
    control = $script:boundControl
    control_refusals = $script:controlRefusals
    acquisition = $ACQUISITION
    acquisition_excluded = $ACQUISITION_EXCLUDED
    acquisition_ran = [bool]$Acquire
    asset_roots = [ordered]@{
        SL_ASSET_OVERRIDE_DIR = (Get-RootInventory $script:aoDir)
        SL_WORLDASSET_ROOT    = (Get-RootInventory $script:waRoot)
        SL_TEXPACK_ROOT       = (Get-RootInventory $script:tpRoot)
    }
    asset_roots_note = 'bound for READING only: no arm sets a variable that writes under them, and the inventory hash above makes a later change detectable'
    role_proof = $script:roleProof
    selection = $sel
    selection_sha256 = $selSha
    selection_from = $selFrom
    pose_refusals = $script:poseRefusals
    marks_dir = "$Marks"
    pad_script = [ordered]@{ path = $script:padPath; sha256 = $script:padSha
                             steps = $script:padStepCount; text = $PAD_SCRIPT_TEXT }
    not_covered = $allNotCovered
    arms = $all
}
Write-JsonNoBom $armsJson $batch 14
Write-Host ''
Write-Host "  arms.json   $armsJson"

Write-Host '  checking witnesses'
$crc = Invoke-Checker @('--scope', $script:scope, $script:attemptDir)

$checkPath = Join-Path $script:attemptDir 'check.json'
$check = $null
if (Test-Path -LiteralPath $checkPath) { $check = Get-Content -LiteralPath $checkPath -Raw | ConvertFrom-Json }

$status = 'INCOMPLETE'
if ($check) { $status = "$($check.status)" }
$fullQual = $false
if ($check) { $fullQual = [bool]$check.full_qualification }

# EVERY REAL NONZERO EXIT COUNTS. A child that exited nonzero, timed out or
# could not be launched, and a checker that exited nonzero, each hold the
# aggregate below PASS whatever the witness roll-up said.
$nonzero = @($script:commands | Where-Object { $null -eq $_.exit_code -or $_.exit_code -ne 0 })
$killed = @($script:commands | Where-Object { $_.timed_out -eq $true })
$launchFailed = @($script:commands | Where-Object { $_.launch_failure })
if ($nonzero.Count -gt 0 -and $status -eq 'PASS') { $status = 'FAIL' }
if ($nonzero.Count -gt 0) { $fullQual = $false }
# The gate is not a witness roll-up: a non-identical far pair is a FAIL of the
# batch whatever else passed, and it is never full qualification.
if ($script:gateStop) {
    $status = 'FAIL'
    $fullQual = $false
}
# A missing mandatory CASE is an absence of measurement, and a subset of the
# cases can never qualify the whole row.
$haveNames = @($all | ForEach-Object { $_.name })
$missingMandatory = @($script:mandatoryCases | Where-Object { $haveNames -notcontains $_ })
if ($missingMandatory.Count -gt 0) {
    $fullQual = $false
    if ($status -eq 'PASS') { $status = 'INCOMPLETE' }
}
if ($allNotCovered.Count -gt 0) { $fullQual = $false }

$result = [ordered]@{
    schema = 'sightline-perflive-result/2'
    status = $status
    scope = $script:scope
    full_qualification = $fullQual
    acceptance = $(if ($check) { "$($check.acceptance)" } else { 'no check.json was produced' })
    qualification = 'live GPU runs of build\win32\sightline.exe at a hash-bound ENGINE_START_PAD recipe; witnesses are the engine''s own census, report and preflight lines read across each arm''s own capture bracket, and byte comparison of its own captures. No trace re-baseline, no image tolerance, no retention policy.'
    gate_stop = $script:gateStop
    missing_mandatory_cases = $missingMandatory
    inputs = [ordered]@{ sightline_sha256 = $exeSha; sdl2_sha256 = $sdlSha
                         libwinpthread_sha256 = $pthSha; rom = $rom
                         seed_hex = $script:seed
                         scratch_config_sha256 = $script:cfgSha
                         pad_script_sha256 = $script:padSha
                         recipe_anchor_sha256 = $RECIPE.anchor_sha256
                         recipe_initial_save_sha256 = $RECIPE.initial_save_sha256
                         control_sha256 = $(if ($script:boundControl) { $script:boundControl.input_sha256 } else { $null })
                         selection_sha256 = $selSha
                         tools = $batch.tools; source = $batch.source
                         asset_roots = $batch.asset_roots }
    recipe = $RECIPE
    recipe_pins = $pinChecks
    discriminate = $(if ($Discriminate) { $AXIS_MATRIX } else { $null })
    discriminate_matrix = $script:matrixResult
    discriminate_readings = $script:discReadings
    candidate = $batch.candidate
    warmups = $script:warmupRecords
    warmups_scored = $false
    counterbalance = $script:counterbalance
    role_proof = $script:roleProof
    selection = $sel
    commands = $script:commands
    checker_exit = $crc
    nonzero_commands = $nonzero.Count
    killed_commands = $killed.Count
    launch_failures = $launchFailed.Count
    not_covered = $allNotCovered
    arms = $all
    check = $check
}
$resultPath = Join-Path $script:attemptDir 'result.json'
Write-JsonNoBom $resultPath $result 16

# ---- result.md -----------------------------------------------------------
$md = New-Object System.Collections.Generic.List[string]
$md.Add('# perflive - OPTIMIZED RENDERING live batch')
$md.Add('')
$md.Add("status: **$status**   scope: **$($script:scope)**   full qualification: **$fullQual**")
$md.Add('')
if ($check) { $md.Add("$($check.acceptance)") }
$md.Add('')
$md.Add(("sightline.exe {0}; SDL2 {1}; libwinpthread {2}" -f $exeSha, $sdlSha, $pthSha))
$md.Add(("RNG state {0}; scratch config {1}; pad script {2}; recipe anchor {3}" -f `
         $script:seed, $script:cfgSha, $script:padSha, $RECIPE.anchor_sha256))
$md.Add('')
$md.Add(('The far case is an ENGINE_START_PAD recipe (stage {0}, pad {1}), qualified for ONE role: {2}. ' -f `
         $RECIPE.stage_number, $RECIPE.pad_index, (@($RECIPE.qualified_roles) -join ', ')) +
        ('It is NOT qualified as: {0}.' -f (@($RECIPE.unqualified_roles) -join ', ')))
$md.Add('')
if ($script:gateStop) {
    $md.Add('## GATE STOP')
    $md.Add('')
    $md.Add('The measured far pair was not byte-identical, so the batch stopped and preserved.')
    foreach ($f in @($script:gateStop.findings)) { $md.Add("- $f") }
    $md.Add('')
}
if ($Discriminate) {
    $md.Add('## The two missing combinations')
    $md.Add('')
    $md.Add('Two arms at the hash-bound recipe, the same seed, the same config bytes, a fresh copy of')
    $md.Add('the same pinned save and the saved pair''s own capture moments. The OPTIMIZED RENDERING')
    $md.Add('row is held ON in both, and each arm disables exactly one subsystem by its own switch, so')
    $md.Add('for the subsystem it does not disable it matches the combined path. The saved OFF and ON')
    $md.Add('halves are reused by hash and were not re-run.')
    $md.Add('')
    $md.Add('| arm | role | disables | absent by design | env delta vs the combined half |')
    $md.Add('| --- | --- | --- | --- | --- |')
    foreach ($a in $all) {
        if (-not $a.matrix) { continue }
        $d = '(not derived)'
        $vs = @($a.axis_assertions | Where-Object { $_.from -eq $AXIS_MATRIX.assert_against })
        if ($vs.Count -gt 0) {
            $d = ((@($vs[0].behavioural_changes)) -join ', ') +
                 (" [axes " + ((@($vs[0].axes_moved)) -join '+') + ", holds " + "$($vs[0].holds)" + "]")
        }
        $md.Add(("| {0} | {1} | {2} | {3} | {4} |" -f $a.name, $a.matrix.role,
                 $a.matrix.disabled_by, ((@($a.matrix.absent_by_design.Keys)) -join ', '), $d))
    }
    $md.Add('')
    if ($script:discReadings) {
        $md.Add('| arm | capture moments vs the baselines | sky path | skybatch final indexed-draws | carrier markers |')
        $md.Add('| --- | --- | --- | --- | --- |')
        foreach ($a in $all) {
            if (-not $a.matrix) { continue }
            $r = $script:discReadings[$a.name]
            if (-not $r) { continue }
            $md.Add(("| {0} | {1} | {2} ({3}) | {4} | {5} |" -f $a.name,
                     $(if ($r.moments.holds) { 'match' } else { 'DIFFER: ' + ((@($r.moments.findings)) -join '; ') }),
                     $(if ($r.sky.holds) { 'as declared' } else { 'NOT as declared: ' + ((@($r.sky.findings)) -join '; ') }),
                     $r.sky.expect, "$($r.sky.final_indexed_draws)",
                     ((@($r.sky.carrier_markers)) -join ',')))
        }
        $md.Add('')
    }
    if ($script:matrixResult -and $script:matrixResult.output -and @($script:matrixResult.output).Count -gt 0) {
        $md.Add(('The comparison against both saved halves, whole-file byte equality with no tolerance, ' +
                 'plus pixel ({0},{1}):' -f $AXIS_MATRIX.pixel.x, $AXIS_MATRIX.pixel.y))
        $md.Add('')
        $md.Add('```')
        foreach ($line in @($script:matrixResult.output)) { $md.Add("$line") }
        $md.Add('```')
    } else {
        $md.Add('NO COMPARISON WAS MADE. See discriminate_matrix in result.json for why.')
    }
    $md.Add('')
    $md.Add('Which baseline an arm matched is an observation and is reported without being explained.')
    $md.Add('If neither arm or both arms match a half, that is interaction or nondeterminism and is')
    $md.Add('UNRESOLVED. What this does NOT establish:')
    foreach ($e in @($AXIS_MATRIX.establishes_not)) { $md.Add("- $e") }
    $md.Add('')
}
$md.Add('| arm | phase | stage | pose | frames | shots | pid | exit | verdict | failed witnesses |')
$md.Add('| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |')
foreach ($a in $all) {
    $v = 'NOT_COVERED'; $bad = ''
    if ($check) {
        $row = $check.arms | Where-Object { $_.name -eq $a.name }
        if ($row) {
            $v = "$($row.verdict)"
            $failed = @()
            foreach ($w in $row.witnesses) { if ("$($w.verdict)" -ne 'PASS') { $failed += ("{0}={1}" -f $w.name, $w.verdict) } }
            $bad = ($failed -join '; ')
        }
    }
    $ec = ''; $pd = ''
    if ($a.run) { $ec = "$($a.run.exit_code)"; $pd = "$($a.run.pid)" }
    $sh = 'none'
    if ($a.shots) { $sh = ("{0} @ {1}" -f $a.shots.gate, ((@($a.shots.expected_moments)) -join '/')) }
    $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} | {9} |" -f `
             $a.name, $a.phase, $a.stage, $a.pose.kind, $a.frames, $sh, $pd, $ec, $v, $bad))
}
$md.Add('')
$md.Add('## Roles')
$md.Add('')
foreach ($k in $script:roleProof.Keys) {
    $p = $script:roleProof[$k]
    $state = 'UNPROVED'
    if ($p.proved) { $state = 'PROVED' }
    $md.Add(("- **{0}** {1} ({2}) - {3}" -f $k, $state, "$($p.stage)", ((@($p.because)) -join '; ')))
}
$md.Add('')
$md.Add('## Declared NOT_COVERED')
$md.Add('')
foreach ($nc in $allNotCovered) {
    $md.Add(("- **{0}** - {1}" -f $nc.name, $nc.reason))
}
$md.Add('')
if ($Discriminate) {
    $md.Add('Comparison evidence: discriminate-matrix.json and discriminate-matrix.txt beside this')
    $md.Add('file, and discriminate_readings in result.json for each arm''s moments and sky path. The')
    $md.Add('saved halves they were compared against are unchanged in their own r529 directories.')
    $md.Add('')
}
$md.Add('Evidence: arms.json, check.json, checker.txt, gate-far-pair.json, and per arm')
$md.Add('<arm>/intent.json, <arm>/launch.json, <arm>/receipt.json, <arm>/stdout.txt,')
$md.Add('<arm>/stderr.txt and <arm>/shot-NNNNN.ppm.')
[IO.File]::WriteAllLines((Join-Path $script:attemptDir 'result.md'), $md, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "  result.json $resultPath"
Write-Host "  result.md   $(Join-Path $script:attemptDir 'result.md')"
if ($Discriminate -and $script:matrixResult) {
    Write-Host "  comparison  $($script:matrixResult.json)"
    Write-Host "  its log     $($script:matrixResult.log)"
    Write-Host '  This reports WHICH saved half each arm''s bytes reproduce, and so which subsystem was'
    Write-Host '  selected when they agreed. It asserts no cause, it attributes the pixel to no draw,'
    Write-Host '  and it covers no other scene, pose or moment. Neither arm matching, or both matching,'
    Write-Host '  is interaction or nondeterminism and stays UNRESOLVED.'
}
Write-Host ''
Write-Host "perflive: $status (scope $($script:scope), full qualification $fullQual)"
if ($nonzero.Count -gt 0) {
    Write-Host "  $($nonzero.Count) command(s) did not exit zero; see result.json commands[]."
}
if ($killed.Count -gt 0) { Write-Host "  $($killed.Count) command(s) were killed on the timeout." }
if ($launchFailed.Count -gt 0) { Write-Host "  $($launchFailed.Count) arm(s) failed to launch; their receipts say why." }
if ($missingMandatory.Count -gt 0) {
    Write-Host ("  {0} mandatory case(s) absent: {1}. A subset of the cases cannot qualify the row." -f `
                $missingMandatory.Count, ($missingMandatory -join ', '))
}
if ($script:scope -eq 'subset') {
    Write-Host '  SUBSET: this is a diagnostic result for the arms supplied. It is NOT full acceptance.'
}
if ($status -ne 'PASS') { exit 3 }
exit 0
