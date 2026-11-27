#!/usr/bin/env python3
"""Witness checker for the OPTIMIZED RENDERING live batch (tools/windows/perflive.ps1).

Launches nothing. Reads each arm's raw stderr, the engine's own captures, and
the batch record perflive.ps1 wrote, and produces one strict JSON verdict per
named witness. Standard library only.

    <attempt-dir>              verdicts for every arm in <attempt-dir>/arms.json
                               -> check.json
    --select <attempt-dir>     pick the stages for the dependent phases from
                               <attempt-dir>/phase-acq.json, by MEASUREMENT,
                               and write selected.json
    --manual                   print the phase H manual checklist
    --manual-template <dir>    write manual.json with one null outcome per step,
                               so an unrecorded step is an ABSENCE and not a pass

FOUR OUTCOMES, AND THEY ARE NOT INTERCHANGEABLE.
    PASS         the witness line was present and said the required thing.
    FAIL         the witness line was present and said something else, or the
                 line a FAIL-ON-ABSENCE witness reads was missing.
    NOT_COVERED  the line the witness reads was absent, so nothing was measured.
    INCOMPLETE   a batch-level outcome: nothing failed, but a mandatory phase,
                 a manual outcome or the performance row is missing.
A required far case that is NOT_COVERED leaves the batch INCOMPLETE. It is
never satisfied by a far-guarded subset having passed.

SUBSET DIAGNOSTIC SUCCESS IS NOT FULL QUALIFICATION. `scope` says which was
asked for. A subset run can read status PASS, and then `full_qualification` is
false and `acceptance` says so in words. Full qualification additionally
requires every mandatory CASE present, an explicit recorded outcome for every
manual step pinned to build + arm + frame, every recorded manual outcome being
PASS, and the performance row bound to this build.

A MANDATORY PHASE IS NOT A MANDATORY CASE. A phase whose arms are a PAIR is
measured by the pair: one half alone establishes nothing about the difference
between them, which is the quantity the phase exists to read. MANDATORY_PHASES_
PRESENT therefore reads phase presence only, and MANDATORY_CASES_PRESENT reads
the cases: every name in `mandatory_cases` when the record declares one, and -
derived from the record as it stands - the other half of every arm that names a
`pair` in a mandatory phase. A subset of the cases can never qualify the whole
mode, so this gate feeds `full_qualification` beside the others.

THREE MANUAL OUTCOMES, AND THEY ARE THREE. An outcome recorded FAIL is a
measured failure of the row and makes the batch FAIL. An absent, null or
unpinned outcome measured nothing and leaves the batch INCOMPLETE. Only an
explicit PASS pinned to this build, an arm of this batch and an integer frame
is a pass. An outcome pinned to ANOTHER build or an arm outside the batch is
not an absence either: it is a record about something else, and it FAILS.

A MEASURED FAILURE OUTRANKS A MISSING STEP. The three readings are ordered: a
step recorded FAIL and pinned to this batch makes the batch FAIL even when
another step was never recorded, because a measurement that the row is broken
must not be downgraded to "nothing was measured" by the absence of a DIFFERENT
step. The absence is not waived by it: MANUAL_OUTCOMES_RECORDED reports the
unrecorded step independently and MANUAL_OUTCOMES_PASSED names it in its own
detail, so the batch reads FAIL with the missing evidence still on the record.

WHY SOME WITNESSES FAIL ON ABSENCE RATHER THAN READING NOT_COVERED. A run whose
own output does not confirm the active presentation, the bound asset root, the
expected stage item or the pinned RNG state measured something other than what
the batch declared, and the batch cannot tell which. Those four are FAIL on
absence by construction (fail_on_absence below), because treating them as
"nothing measured" is what lets a dark ORIGINAL run stand in for an XBLA one.

A CUMULATIVE COUNTER IS NOT CAPTURE-WINDOW EVIDENCE. The per-depth census is
cumulative over the process, so a nonzero reading may have accrued before the
teleport or after the last capture. The WINDOW_* witnesses therefore read the
DELTA between the census report immediately preceding the first `sl_shot:` line
in the arm's own stderr and the one immediately following the last. Boundary:
that bracket is at most 60 display-list frames wider than the captures
themselves on either side (the census prints on every 60th frame), so it is a
bracket around the window and not the window exactly. It is still the only
reading that cannot be satisfied by pre-teleport accrual.

THE GRAMMARS PARSED BELOW ARE THE ENGINE'S FORMAT STRINGS, QUOTED FROM SOURCE.

src/gfx/sl_gfx_dl.c:19513 sl_sky_report, reached only from the census branch of
the per-60-frame report (src/gfx/sl_gfx_dl.c:23749-23775); the per-depth
submission census:

    "#88 background GPU transform: %s, SUBMITTED %u walk%s, %u mesh draw%s, "
    "%lu draw call%s, %lu indices (%lu triangles); BY DEPTH %lu call%s / "
    "%lu indices at the single-range clamp, %lu / %lu in a far frame's first "
    "range, %lu / %lu in its game range; ADMITTED ... "
    "refused: %u late, %u far-range not admitted, ..."

src/gfx/sl_gfx_dl.c:19533, the two-range frame count; :19572 and :19713 the
background pass's own per-frame totals and its blended share; :19603 the
fogless-stage line; :19607 the panorama shell (all inside sl_sky_report, so all
census-only):

    "#78 background beyond the far plane: %u of %u frames drawn in "
    "two depth ranges; last such frame %u vertices past the game's "
    "far plane w %.1f, the item reaching w %.1f%s\n"
    "#68 background: drawn on %u frames, %u triangles and %u "
    "vertices per frame, %u GL draws per frame, %d frames skipped (projection)\n"
    "#68 background translucency: %u of those GL draws per "
    "frame are blended with NO alpha reject ..."
    "#68 skydome: drawn on %u frames, %u triangles and %u vertices per frame ..."
    "#81 background on a fogless stage: %u frames drawn in the game's own range ..."
    "#74 background panorama shell (D-026): %u vertices, faded on %u frames ..."

src/gfx/sl_gfx_bggpu.c:214/318/332/350, the program's own fate ON THE DRIVER -
the only line in the build that says a real GL implementation compiled and
linked the vertex program:

    "#88 background GPU transform ready - the eligible background meshes ..."
    "#88 background GPU transform unavailable: %s - the ..."
    "#88 background GPU transform: vertex program did not compile: %s\n"
    "#88 background GPU transform: program did not link: %s\n"

src/gfx/sl_gfx_dl.c:19307 and :19363, the CPU loop reduction's OWN bounded
report, printed inline from the walk on the first reduced walk and then every
300th. It needs no census - and must not have one: the census is itself one of
the readers the walk refuses (`fstats`, src/gfx/sl_gfx_dl.c:18357 and :18386):

    "#88 background GPU bypass: %lu walk%s reduced - %lu vertices computed "
    "whole, %lu to the near class alone; refused: ... ; %u repaired, "
    "%u unsound mesh draw%s (must be 0); ..."
    "#88 background GPU bypass: REFUSED - %lu walk%s considered, 0 reduced; ..."

Both shapes carry the same two cumulative error counters, and BOTH shapes can
appear in one run in either order. The counters are therefore taken as the
MAXIMUM over every occurrence in document order, never from the last line seen.

src/gfx/sl_gfx_dl.c:13561, the fill-query workers (census branch, nf_report):

    "#88 fill queries: %d worker%s, %lu batches of %lu predicted traces; %lu answers "
    "taken from a batch (%lu computed by a worker, %lu waited for), %lu traced inline ..."

src/gfx/sl_gfx_dl.c:23775, the census branch's frame line (the `bounds` tail is
what distinguishes it from the no-census heartbeat line):

    "sl_dl: frame %u cmds=%u vtx=%u(rej=%u) tris=%u unknown=%u "
    "bounds x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]\n"

src/gfx/sl_gfx_skybatch.c:1375 and :710, the validated float colour carrier
marker (once per process) and the terminal snapshot:

    "#474 skybatch: float colour carrier %s - served cloud "
    "draws upload 48-byte records with a GL_FLOAT colour (%s)\n"
    "#316 skybatch: final items %lu passes %lu indexed-draws %lu indexed %lu ..."

src/gfx/sl_gfx_dl.c:18024, the background vertex helper's OWN bounded report,
printed from the end of bgv_run on the first walk and then every 300th, and
needing no census - the census sets `fstats`, which is the first term bgv_run
refuses the split on (:17960), so the line cannot live in the census report:

    "#88 background vertices: %lu walk%s - %lu in two ranges, %lu on the "
    "render thread alone (off %lu, under four cores %lu, small item %lu, "
    "level fog %lu, statistics %lu, no helper %lu, upper range recomputed "
    "%lu); helper %s, %d allowed physical cores\n"

`%lu in two ranges` is g_bgv_split and `upper range recomputed %lu` is
g_bgv_r_fp. THEY ARE NOT INDEPENDENT: the SL_BGW_SKIPPED path recomputes the
upper range on the render thread and increments both (:17986-17989), so a split
whose upper range was recomputed after a floating-point change did the work
twice and saved nothing. The helper is witnessed by split MINUS recomputed, not
by split alone.

src/gfx/sl_gfx_sdl.c:872, each capture's self-identifying moment. The file name
carries a five-digit SEQUENCE number, not a frame, so the moment is only in
this line (src/gfx/sl_gfx_sdl.c:849-856):

    "sl_shot: %s (%dx%d) at read=%u frame=%u (window=%s) cloud phase %s%.3f\n"

THE CAPTURE SCHEDULE IS PART OF THE DECLARATION, not an afterthought. The
engine windows on [FIRST,LAST] and fires at "at least EVERY since the last
capture", never on divisibility (src/gfx/sl_gfx_sdl.c:816-828); and
SL_SHOT_READ_FIRST/LAST window on the INPUT RECORD INDEX instead, which is a
different counter advancing by two or more per rendered frame (:789-802,
B-051). Names, count, dimensions and payload can all agree while every capture
was taken at the wrong moment, so each capturing arm declares

    shots = {first, last, every, read_first, read_last, expected_moments,
             width, height, count}

and CAPTURE_SCHEDULE_EXACT reads the logged line's own counter - `read` when
the arm declares a read window, `frame` otherwise - against expected_moments
INDEX BY INDEX. Two halves that captured the same wrong moments are identical
and byte-identical and still wrong, which is the case a pair comparison cannot
see. A missing field is an absent declaration: NOT_COVERED for that arm, never
a pass.

src/gfx/sl_gfx_worldasset.c:949 and :998, the active world-asset SET (which the
presentation derives, sl_presentation.h) with the root it was bound to, and
each stage item the run actually loaded:

    "sl_worldasset: set=%s root=%s (%s)\n"
    "sl_worldasset: LOADED %s/%s  %u verts  %u indices (%u tris) ..."

src/platform/sl_ultra_shim.c:3483, the RNG state in force and how it was
chosen; "explicit SL_RNG_SEED" is the only spelling that proves the batch's own
value was honoured:

    "sightline native: RNG state %016llx (%s)\n"

src/platform/sl_ultra_shim.c:1807 and :1799, the REPLAYED input stream and the
open that failed. SL_INPUT is the replay variable (SL_INPUT_RECORD is the
recorder, src/platform/sl_main.c:276-302); the read count is the file's length
in 4-byte records, and a failed open is terminal - the engine prints the line
and exits 2 (:1798-1801), so that line and a clean exit cannot both be true:

    "sightline native: input stream %s (%lu reads)\n"
    "sightline native: SL_INPUT open failed: %s\n"

src/platform/sl_ultra_shim.c:1518's pump liveness line is the established
evidence that the build executed (tools/windows/test.ps1 asserts the same one):

    "sightline native: survived %u pumped frames\n"

src/platform/sl_input.c:1561 and :1594, the synthetic pad: the step file as
parsed, and each step as it is applied. A step line per step is within-run
MOVEMENT, which a per-process teleport cannot produce (the teleport latches,
src/native/sl_teleport.c):

    "sightline input: SL_PAD_SCRIPT %s: %d step(s)\n"
    "sightline input: SL_PAD_SCRIPT step %d at frame %u: L(%.2f,%.2f) ..."

src/native/sl_teleport.c, the applied pose:

    "sl_teleport: requested pos=%.3f %.3f %.3f theta=%.3f verta=%.3f room=%d\n"

src/gfx/sl_gfx_sdl.c:860 / src/gfx/sl_gfx_dl.c:3759, the capture format:

    "P6\n<w> <h>\n255\n" then exactly w*h*3 bytes

THE PERFORMANCE ROW IS BOUND OR IT IS NOT MEASURED. A present key with a
truthy value in it ("12.4 ms") names no metric, no workload and no run: it
cannot be recomputed, so nothing about it can be checked and it is an ABSENCE
(NOT_COVERED), not a pass. The row therefore binds this build's sha256, the
actual arm names it measured, the RAW per-run measurements, and numeric metrics
carrying the NAME of the rule applied. No metric is invented here: the two
rules recognised are the ones this tree already defines and tests,

    abba-timing-screen/1   tools/profiling/analyze.py compare_abba
                           ('sightline-abba/1'): four runs in order A,B,B,A,
                           paired_savings_ms = [A0-B0, A1-B1], repeat_spreads_ms
                           = [|A0-A1|, |B0-B1|], and the screen passes when
                           min(savings) > max(spreads). A descriptive screen,
                           not a significance test.
    absolute-budget/1      tools/profiling/READINESS.md:45-47 - mean and
                           nearest-rank p99 frame time each at most 16.667 ms
                           (1080p60), a PROSPECTIVE roadmap target that nothing
                           in this tree has yet qualified.

Both are recomputed here from the row's own raw figures (mean_ms and
p99_ms_nearest_rank per run, as tools/profiling/analyze.py frame_report
computes them). A stated verdict that its own raw numbers do not support is a
fabricated gain and FAILS; an unrecognised rule name is NOT_COVERED, because a
figure whose rule is unknown cannot be read.

NO FIELD OF THE ROW IS EVIDENCE OF ITSELF. A raw record counts as a sample only
when it is a frame report of THIS build (sightline-frames/1, build_sha256),
naming an arm the batch actually ran and the stage that arm ran on, over at
least two frame intervals, with finite POSITIVE times. A JSON `true` is not a
time: isinstance(True, int) is true in Python, so booleans are excluded by type
before any figure is read off them, as tools/profiling/common.py number() does;
NaN and the Infinity tokens json.loads accepts (and common.load_json refuses)
are excluded by value. Every arm the row aggregates must carry samples of its
own, the row's per-arm `off`/`on` summary is recomputed from those samples, and
the verdict of the rule the row names must be present as a BOOLEAN and must
equal the recomputation - `budget_met` above all, which is derived from the
samples and never read off the row. Samples absent, malformed or bound to
another build, arm or stage leave the row UNQUALIFIED; a stated verdict its own
samples contradict FAILS it.
"""
import hashlib
import json
import math
import re
import sys
from pathlib import Path

PASS = 'PASS'
FAIL = 'FAIL'
NONE = 'NOT_COVERED'
INCOMPLETE = 'INCOMPLETE'

SCHEMA_IN = 'sightline-perflive/2'

# ---- the grammars -------------------------------------------------------
RE_BYDEPTH = re.compile(
    r"#88 background GPU transform: .*?"
    r"BY DEPTH (\d+) calls? / (\d+) indices at the single-range clamp, "
    r"(\d+) / (\d+) in a far frame's first range, "
    r"(\d+) / (\d+) in its game range;.*?"
    r"refused: (\d+) late, (\d+) far-range not admitted,")
RE_GPU_READY = re.compile(r"#88 background GPU transform ready - ")
RE_GPU_BAD = re.compile(
    r"#88 background GPU transform (?:unavailable:|: vertex program did not compile|"
    r": program did not link)")

# THE BACKGROUND GPU CLIP PREPASS, ITS OWN LINE. Quoted from the format string
# at src/gfx/sl_gfx_dl.c:19993-20020, printed from the same per-60-frame census
# branch as #88 and therefore cumulative over the process.
#
# WHY IT IS A SEPARATE LINE FROM #88, AND WHY THAT MATTERS TO EVERY RULE BELOW.
# A prepass walk leaves gpu_walk at 0 and selects pool 0, so every counter on
# the #88 vertex-program line reads ZERO while the prepass is doing the work.
# Reading those zeros off #88 as "no GPU work happened" is exactly backwards,
# and reading them as coverage would be worse. The prepass is witnessed from
# its own counters here, and #88's far columns reading zero is itself one of
# the witnesses (PROGRAM_COLUMNS_MUST_INVERT).
#
#   group  1 state           'ready', the refusal reason, or 'not asked'
#          2 prepasses       prepass submissions that RAN
#          3 vertices        vertices actually transformed by them
#          4 reuses          draws served by an already-filled slot: work
#                            AVOIDED, never work done
#          5 binds           pools made the vertex source: A BIND IS NOT A DRAW
#          6 raster_draws    nonempty runs on a prepass pool - the ONLY field
#          7 raster_indices  that counts actual drawing, with its index total
#        8-10 dcalls         single / far0 / far1, by the depth of the pool
#                            that was the vertex SOURCE for that run, which is
#                            not the walk's admission depth
#         11 walks           walks that submitted a prepass
#         12 admitted        g_bgtf_elig_walks, walks the predicates admitted
#      13-14 refused         g_bgtf_r_prog walks, g_bgtf_r_draw draws
#         15 slot_refills    a free or PRIOR-WALK slot taken: normal, not
#                            pressure
#         16 evictions       a CURRENT-WALK key displaced: this and only this
#                            is cache pressure
#         17 refusals        counters.refusals, walks or draws given to the CPU
#         18 tail clause     WHICH SELECTION PUT THE NUMBERS THERE, and there
#                            are now three states rather than two. Absent on a
#                            developer-switch launch (SL_BG_TF_CLIP=1), as
#                            before. "the PRODUCTION selection" on a launch the
#                            OPTIMIZED RENDERING row admitted, which reaches the
#                            SAME auxiliary GPU_SOURCE arm with no selector
#                            named. "SL_BG_TF_CLIP is not 1" only when no walk
#                            reached a prepass at all.
#
#                            THE THIRD STATE IS WHY THE GROUP IS AN ALTERNATION
#                            AND TF_OFF_PHRASE IS STILL THE EXACT OLD STRING. A
#                            production launch prints POSITIVE prepass counts,
#                            so matching "off" on the mere presence of a tail
#                            clause would read a working production run as off;
#                            and a tail clause this grammar did not accept would
#                            read a healthy line as a DRIFTED census. Both are
#                            failure modes this file has already had once.
RE_TFCLIP = re.compile(
    r"#95 background GPU clip prepass: (.+?), (\d+) prepass(?:es)? over "
    r"(\d+) vertices, (\d+) reuses?, (\d+) binds?; "
    r"SUBMITTED (\d+) nonempty runs? on a prepass pool \((\d+) indices\) "
    r"by the SOURCE's depth single (\d+) / far0 (\d+) / far1 (\d+), "
    r"(\d+) walks?; ADMITTED (\d+), refused (\d+) walks? and (\d+) draws?, "
    r"(\d+) slot refills? and (\d+) evictions?, (\d+) module refusals?"
    r"( \((?:SL_BG_TF_CLIP is not 1|the PRODUCTION selection):[^)]*\))?")
# The line's own prefix, matched on its own so that a #95 line the grammar
# above does NOT accept reads as MALFORMED rather than as absent. An absent
# line and a line whose format drifted are different states, and collapsing
# them lets a format change read as "the candidate was off".
RE_TF_ANY = re.compile(r"#95 background GPU clip prepass")

# THE MODULE'S OWN DIAGNOSTICS, WHICH ARE NOT CENSUS REPORTS.
#
# These four lines share the census prefix and can never match the field
# grammar above, so counting them as censuses made a HEALTHY run read as
# malformed: a successful candidate always prints the ready banner, which gave
# prefixes = reports + 1 and failed every TF gate on the first good run.
#
# All four are listed, and the last two matter as much as the banner: the
# compile and link failures are written in the COLON form
# ("prepass: program did not compile: ..."), so a fix that only exempted the
# ready banner would still mark a driver that refused the program as a parser
# fault rather than reporting the refusal it actually is.
#
# Each pattern is anchored to the exact emitted wording rather than a loose
# prefix, because the point is to exempt KNOWN diagnostics while still failing
# on an unknown or drifted census. Sources:
#   src/gfx/sl_gfx_bgtf.c:434  ready
#   src/gfx/sl_gfx_bgtf.c:263  unavailable
#   src/gfx/sl_gfx_bgtf.c:397  program did not compile
#   src/gfx/sl_gfx_bgtf.c:416  program did not link
# ("#95 SL_BG_TF_CLIP=..." carries a different prefix and never reaches here.)
RE_TF_READY = re.compile(
    r"#95 background GPU clip prepass ready - eligible background meshes")
RE_TF_UNAVAIL = re.compile(
    r"#95 background GPU clip prepass unavailable: ")
RE_TF_NOCOMPILE = re.compile(
    r"#95 background GPU clip prepass: program did not compile: ")
RE_TF_NOLINK = re.compile(
    r"#95 background GPU clip prepass: program did not link: ")
RE_TF_DIAGS = (RE_TF_READY, RE_TF_UNAVAIL, RE_TF_NOCOMPILE, RE_TF_NOLINK)
TF_OFF_PHRASE = 'SL_BG_TF_CLIP is not 1'
# v0.4 THE ROW'S OWN TAIL CLAUSE. Printed when the OPTIMIZED RENDERING row
# admitted the walks and no selector named an arm, which is the same auxiliary
# GPU_SOURCE arm a developer reaches by name - so this clause appears beside
# POSITIVE prepass counters and is never an off reading.
TF_PRODUCTION_PHRASE = 'the PRODUCTION selection'
RE_FAR78 = re.compile(
    r"#78 background beyond the far plane: (\d+) of (\d+) frames drawn in "
    r"two depth ranges; last such frame (\d+) vertices past the game's "
    r"far plane w (-?[\d.]+), the item reaching w (-?[\d.]+)")
RE_BG68 = re.compile(
    r"#68 background: drawn on (\d+) frames, (\d+) triangles and (\d+) "
    r"vertices per frame, (\d+) GL draws per frame, (-?\d+) frames skipped")
RE_XLU = re.compile(
    r"#68 background translucency: (\d+) of those GL draws per "
    r"frame are blended with NO alpha reject")
RE_SKY68 = re.compile(r"#68 skydome: drawn on (\d+) frames, (\d+) triangles and (\d+) ")
RE_FOGLESS = re.compile(r"#81 background on a fogless stage: (\d+) frames drawn in the game's own range")
RE_PANO = re.compile(r"#74 background panorama shell \(D-026\): (\d+) vertices, faded on (\d+) frames")
RE_REDUCED = re.compile(
    r"#88 background GPU bypass: (\d+) walks? reduced - (\d+) vertices computed "
    r"whole, (\d+) to the near class alone;.*?"
    r"(\d+) repaired, (\d+) unsound mesh draws? \(must be 0\)")
RE_REFUSED = re.compile(
    r"#88 background GPU bypass: REFUSED - (\d+) walks? considered, 0 reduced;.*?"
    r"(\d+) repaired, (\d+) unsound mesh draws? \(must be 0\)")
RE_FILLQ = re.compile(
    r"#88 fill queries: (\d+) workers?, (\d+) batches of (\d+) predicted traces; "
    r"(\d+) answers taken from a batch \((\d+) computed by a worker, (\d+) waited for\)")
RE_DLCENSUS = re.compile(
    r"sl_dl: frame (\d+) cmds=(\d+) vtx=(\d+)\(rej=(\d+)\) tris=(\d+) unknown=(\d+) bounds ")
RE_F474 = re.compile(r"#474 skybatch: float colour carrier (prod|div) - served cloud ")
RE_SKB_FINAL = re.compile(
    r"#316 skybatch: final items (\d+) passes (\d+) indexed-draws (\d+) indexed (\d+) ")
RE_SHOT = re.compile(
    r"sl_shot: (.+?) \((\d+)x(\d+)\) at read=(\d+) frame=(\d+) \(window=(frame|read)\)")
RE_BGV = re.compile(
    r"#88 background vertices: (\d+) walks? - (\d+) in two ranges, "
    r"(\d+) on the render thread alone \(off (\d+), under four cores (\d+), "
    r"small item (\d+), level fog (\d+), statistics (\d+), no helper (\d+), "
    r"upper range recomputed (\d+)\); helper ([a-z]+), (-?\d+) allowed physical cores")
RE_LIVE = re.compile(r"sightline native: survived (\d+) pumped frames")
RE_INPUT = re.compile(r"sightline native: input stream (.+?) \((\d+) reads\)")
RE_INPUT_FAIL = re.compile(r"sightline native: SL_INPUT open failed: (.+)")
RE_RNG = re.compile(r"sightline native: RNG state ([0-9a-f]{16}) \(([^)]*)\)")
RE_WASET = re.compile(r"sl_worldasset: set=(\S*) root=(.+?) \((SL_WORLDASSET_ROOT|player data)\)")
RE_WALOADED = re.compile(r"sl_worldasset: LOADED (\w+)/(\S+)\s+(\d+) verts")
RE_PADFILE = re.compile(r"sightline input: SL_PAD_SCRIPT (.+?): (\d+) step\(s\)")
RE_PADSTEP = re.compile(r"sightline input: SL_PAD_SCRIPT step (\d+) at frame (\d+): ")
RE_TELE = re.compile(
    r"sl_teleport: requested pos=(-?[\d.]+) (-?[\d.]+) (-?[\d.]+) "
    r"theta=(-?[\d.]+) verta=(-?[\d.]+) room=(-?\d+)")

# Witnesses whose ABSENCE is a FAIL, not an absence of measurement. See the
# module docstring for why each is in this set.
FAIL_ON_ABSENCE = frozenset((
    'PRESENTATION_XBLA_ACTIVE', 'ASSET_ROOT_BOUND', 'STAGE_ITEM_LOADED',
    'SEED_ECHOED', 'EXIT_ZERO', 'CAPTURE_SET_EXACT', 'CAPTURE_NAMES_MATCH_PAIR',
))

MANUAL = [
    "H1. Front end: OPTIONS -> SETTINGS -> DISPLAY. Reach the OPTIMIZED RENDERING row "
    "and change it with the KEYBOARD, then with a CONTROLLER, then with the POINTER. "
    "Look at: the row's value column after each change. FAILURE: a device that cannot "
    "reach the row, cannot change it, or leaves the column showing the old value.",
    "H2. Watch: SIGHTLINE -> GRAPHICS child. Same three devices, same row. "
    "Look at: the value column. FAILURE: as H1.",
    "H3. The label clears its value column in BOTH views. Set the row ON in one view, "
    "read it in the other, set it OFF and read it back. Look at: the printed value in "
    "each view after each change. FAILURE: the two views disagree, or a column keeps a "
    "stale digit over the new one.",
    "H4. Persistence across a relaunch. Set the row OFF, quit, relaunch, open either "
    "view. Look at: the value column, and config.ini's optimized_rendering line. "
    "FAILURE: it reads 1, or the file was not written.",
    "H5. Same asset, OFF -> ON -> OFF -> ON while a level is LOADED, without leaving "
    "the level. Look at: the picture after each press, and that no pass refuses for the "
    "life of the asset. FAILURE: a press that changes nothing, a press that takes effect "
    "only after a reload, or any new artefact in the background or the sky.",
    "H6. A stage change and then a PRESENTATION cycle, with the row left ON. Look at: "
    "the background and sky of the new stage. FAILURE: any world asset, sky dome or "
    "texture of the previous stage or the previous presentation still on screen.",
    "H7. Run every step above in ONE session on the scratch config this batch wrote, "
    "not on %LOCALAPPDATA%\\sightline\\config.ini. FAILURE: the player's own config "
    "changed mtime.",
]
MANUAL_IDS = ['H%d' % (i + 1) for i in range(len(MANUAL))]


def read_text(path):
    try:
        return Path(path).read_text(encoding='utf-8', errors='replace')
    except OSError:
        return None


def last(rx, text):
    if text is None:
        return None
    m = None
    for m in rx.finditer(text):
        pass
    return m


def nums(m, *idx):
    return tuple(int(m.group(i)) for i in idx)


class Census(object):
    """One per-depth census report, with where in the stream it was printed."""

    def __init__(self, m):
        self.pos = m.start()
        (self.single_calls, self.single_idx, self.far0_calls, self.far0_idx,
         self.far1_calls, self.far1_idx, self.r_late,
         self.r_far_not_admitted) = nums(m, 1, 2, 3, 4, 5, 6, 7, 8)
        self.text = m.group(0)

    def counters(self):
        return {
            'single_calls': self.single_calls, 'single_indices': self.single_idx,
            'far0_calls': self.far0_calls, 'far0_indices': self.far0_idx,
            'far1_calls': self.far1_calls, 'far1_indices': self.far1_idx,
            'far_not_admitted': self.r_far_not_admitted,
        }


ZERO_COUNTERS = {'single_calls': 0, 'single_indices': 0, 'far0_calls': 0,
                 'far0_indices': 0, 'far1_calls': 0, 'far1_indices': 0,
                 'far_not_admitted': 0}


class TfClip(object):
    """One #95 clip-prepass census line, with where in the stream it printed."""

    def __init__(self, m):
        self.pos = m.start()
        self.text = m.group(0)
        self.state = m.group(1)
        (self.prepasses, self.vertices, self.reuses, self.binds,
         self.raster_draws, self.raster_indices,
         self.single, self.far0, self.far1,
         self.walks, self.admitted, self.refused_walks, self.refused_draws,
         self.slot_refills, self.evictions,
         self.module_refusals) = nums(m, *range(2, 18))
        # THE TAIL CLAUSE, SPLIT INTO THE TWO THINGS IT CAN SAY. Group 18 is
        # now an alternation, so its mere PRESENCE no longer means "off": a
        # production launch prints a tail clause beside positive prepass
        # counts. `off_phrase` is therefore matched against the exact off text
        # and `production` against the row's, so an off witness cannot be
        # satisfied by a working production run and vice versa.
        tail = m.group(18) or ''
        self.off_phrase = TF_OFF_PHRASE in tail
        self.production = TF_PRODUCTION_PHRASE in tail

    # COVERAGE IS A CLOSED SET, and `binds` and `reuses` are NOT IN IT. A bind
    # makes a pool the vertex source and draws nothing; a reuse is a draw
    # served from an already-filled slot, which is work AVOIDED. Either one
    # rising while `raster_draws` stays 0 means the prepass never drew, and a
    # coverage sum that added them would report that as coverage. The two
    # numbers are still reported - excluded, not hidden.
    COVERAGE_KEYS = ('prepasses', 'vertices', 'raster_draws', 'raster_indices',
                     'single', 'far0', 'far1', 'walks', 'admitted')
    EXCLUDED_FROM_COVERAGE = ('binds', 'reuses')

    def counters(self):
        return dict((k, getattr(self, k)) for k in (
            self.COVERAGE_KEYS + self.EXCLUDED_FROM_COVERAGE +
            ('refused_walks', 'refused_draws', 'slot_refills', 'evictions',
             'module_refusals')))

    def coverage(self):
        """The fields a coverage claim may be built from, and nothing else."""
        return dict((k, getattr(self, k)) for k in self.COVERAGE_KEYS)

    def excluded(self):
        return dict((k, getattr(self, k)) for k in self.EXCLUDED_FROM_COVERAGE)


TF_ZERO = dict((k, 0) for k in (
    TfClip.COVERAGE_KEYS + TfClip.EXCLUDED_FROM_COVERAGE +
    ('refused_walks', 'refused_draws', 'slot_refills', 'evictions',
     'module_refusals')))

# The candidate's own witnesses, named exactly as the validation-chain
# declaration names them. Membership here is what marks a witness as reading
# the #95 line rather than #88.
TF_WITNESSES = frozenset((
    'TF_STATE_READY', 'TF_PREPASSES_POSITIVE', 'TF_VERTICES_POSITIVE',
    'TF_SUBMITTED_POSITIVE', 'TF_SOURCE_DEPTH_FAR0_POSITIVE',
    'TF_SOURCE_DEPTH_FAR1_POSITIVE', 'TF_WALKS_POSITIVE',
    'TF_ADMITTED_POSITIVE', 'TF_REFUSALS_ARE_NUMBERS_AND_A_FALLBACK_FAILS',
    'TF_REUSES_ARE_NOT_COVERAGE', 'TF_REFILL_AND_EVICTION_READ_SEPARATELY',
    'TF_SOURCE_DEPTH_SINGLE_IS_A_CONSISTENCY_READING',
    'TF_OFF_ARMS_READ_ZERO', 'PROGRAM_COLUMNS_MUST_INVERT',
))

# WITNESSES THAT INVERT ON A WORKING CANDIDATE, and so may never be asked of a
# candidate arm. Both read the vertex PROGRAM's per-depth columns on the #88
# line. A prepass walk leaves gpu_walk 0 and selects pool 0, so on a candidate
# arm those columns read zero BECAUSE THE CANDIDATE WORKED - the witness would
# FAIL on success and pass only if the prepass had been bypassed. Declaring one
# on a candidate arm is a declaration defect, not a measurement, and
# TF_CANDIDATE_WITNESS_SET_VALID fails the arm by name rather than letting the
# inverted reading stand as a result.
PROGRAM_COLUMN_WITNESSES = frozenset((
    'WINDOW_FAR0_DELTA_POSITIVE', 'WINDOW_FAR1_DELTA_POSITIVE',
))


# THE MAIN-CONTEXT DIAGNOSTICS AND THE TWO CAPTURES, named exactly as
# src/gfx/sl_gfx_bgtf.c's arbiter names them. An arm requesting any of these is
# asking for the MAIN-CONTEXT prepass, so the production selection is not in
# force for it: with the developer switch the candidate stands down to that
# prepass, and without the switch the row's walks draw the ORIGINAL renderer.
# A switch added to the product without being added here would let a diagnostic
# arm be read as a production arm.
TF_MAIN_CONTEXT_SELECTORS = (
    'SL_BG_TF_RASTER_CPU', 'SL_BG_TF_STATE_ONLY', 'SL_BG_TF_POS_INPUT',
    'SL_BG_TF_PRIVATE_VAO', 'SL_BG_TF_ZERO_COUNT', 'SL_BG_TF_EMPTY_STATE',
    'SL_BG_TF_POST_RESET', 'SL_BG_TF_NEUTRAL_PROG', 'SL_BG_TF_AUX_EMPTY',
    'SL_BG_TF_DUMP', 'SL_BG_GLSTATE',
)


def _set_off(v):
    """True when an environment value is absent, empty or the exact text '0'."""
    s = str(v or '')
    return s == '' or s == '0'


def is_candidate_arm(arm):
    """True when this arm runs the clip prepass, by EITHER selection.

    Read from the arm's env rather than its name: the runner clears every SL_*
    from the parent and fills each child from the arm's own map alone, so the
    map is the only record of what the child actually ran under.

    TWO SELECTIONS REACH THE PREPASS, so this can no longer be one key.

      THE DEVELOPER SWITCH. SL_BG_TF_CLIP=1, as before, whatever the row says.
      THE PRODUCTION SELECTION. The OPTIMIZED RENDERING row admits the walk and
      no selector named an arm, an OFF token, a main-context diagnostic or a
      capture - src/gfx/sl_gfx_bgtf.c, sl_bgtf_walk_admit. The row is ON by
      default in a player session, so an arm that names nothing IS a prepass
      arm, which is the whole point of this round.

    WHY THAT MATTERS HERE AND NOT ONLY IN THE PRODUCT. Two witness sets hang off
    this answer. PROGRAM_COLUMN_WITNESSES read the #88 vertex program's per-depth
    columns, which a prepass walk leaves at zero BECAUSE THE PREPASS WORKED, so
    declaring one on a prepass arm is a declaration defect rather than a
    measurement. TF_OFF_ARMS_READ_ZERO asserts the prepass counters are zero on a
    non-prepass arm. Reading a production arm as a non-prepass arm would invert
    the first and fail the second on a working run.

    THE ROW IS TAKEN AS ON UNLESS THE ARM TURNS IT OFF, and only by the two keys
    that can: SL_PERF_OPT=0 is the row's own launch override and SL_BG_GPU_XFORM=0
    governs the background subsystem alone (src/platform/sl_perf.h). An arm that
    names neither runs with the row at its default, which is ON.
    """
    env = (arm or {}).get('env') or {}
    if str(env.get('SL_BG_TF_CLIP') or '') == '1':
        return True
    # An explicit developer OFF takes the production selection out of the arm.
    if str(env.get('SL_BG_TF_CLIP') or '') == '0':
        return False
    # A named arm or an OFF token is a developer selection, never production.
    if str(env.get('SL_BG_TF_AUX_REAL') or '') != '':
        return False
    if any(not _set_off(env.get(k)) for k in TF_MAIN_CONTEXT_SELECTORS):
        return False
    if str(env.get('SL_PERF_OPT') or '') == '0':
        return False
    if str(env.get('SL_BG_GPU_XFORM') or '') == '0':
        return False
    return True


class Facts(object):
    """Everything one arm's stderr says, parsed once, in document order."""

    def __init__(self, text):
        self.text = text
        self.present = text is not None
        t = text or ''

        # THE PER-DEPTH CENSUS, every occurrence. The last one is the
        # cumulative total; the bracket around the captures is what the
        # WINDOW_* witnesses read.
        self.census = [Census(m) for m in RE_BYDEPTH.finditer(t)]
        self.bydepth = self.census[-1].text if self.census else None
        if self.census:
            c = self.census[-1]
            self.single_calls, self.single_idx = c.single_calls, c.single_idx
            self.far0_calls, self.far0_idx = c.far0_calls, c.far0_idx
            self.far1_calls, self.far1_idx = c.far1_calls, c.far1_idx
            self.r_far_not_admitted = c.r_far_not_admitted
        self.gpu_ready = RE_GPU_READY.search(t) is not None
        self.gpu_bad = RE_GPU_BAD.search(t)

        # THE CLIP PREPASS CENSUS, every occurrence, in document order. A line
        # whose prefix is present but whose fields do not parse is recorded as
        # MALFORMED and never as absent: a format drift that read as absence
        # would make "the candidate was off" the reading of a broken parser.
        self.tf = [TfClip(m) for m in RE_TFCLIP.finditer(t)]
        self.tf_prefixes = len(RE_TF_ANY.findall(t))
        # The module's own diagnostics share the prefix and are not censuses,
        # so they are subtracted before the malformed test. Only the remainder
        # - a #95 line that is neither a known diagnostic nor a parseable
        # census - is a format fault.
        self.tf_diag_lines = sum(len(r.findall(t)) for r in RE_TF_DIAGS)
        self.tf_ready = RE_TF_READY.search(t) is not None
        self.tf_unavailable = RE_TF_UNAVAIL.search(t) is not None
        self.tf_build_failed = (RE_TF_NOCOMPILE.search(t) is not None
                                or RE_TF_NOLINK.search(t) is not None)
        self.tf_malformed = (self.tf_prefixes - self.tf_diag_lines) > len(self.tf)
        self.tf_last = self.tf[-1] if self.tf else None

        m = last(RE_FAR78, t)
        self.far_frames = None
        self.far_verts = None
        if m:
            self.far_frames, self.bg_frames_78, self.far_verts = nums(m, 1, 2, 3)
        m = last(RE_BG68, t)
        self.bg = None
        if m:
            self.bg_frames, self.bg_tris_pf, self.bg_verts_pf, self.bg_draws_pf = nums(m, 1, 2, 3, 4)
            self.bg = m.group(0)
        self.xlu = [(m.start(), int(m.group(1))) for m in RE_XLU.finditer(t)]
        self.xlu_pf = self.xlu[-1][1] if self.xlu else None
        m = last(RE_SKY68, t)
        self.sky_frames = nums(m, 1)[0] if m else None
        m = last(RE_FOGLESS, t)
        self.fogless_frames = nums(m, 1)[0] if m else None
        m = last(RE_PANO, t)
        self.pano_frames = nums(m, 2)[0] if m else None

        # THE REDUCTION, CHRONOLOGICALLY. Both line shapes carry the same two
        # cumulative error counters and either can be printed last, so the
        # reading is the MAXIMUM over the whole arm and not the final line.
        # DEFECT HISTORY: taking the last REFUSED line's counters let an early
        # REFUSED with unsound=0 overwrite a later REDUCED with unsound>0 while
        # `reduced > 0` still held from the REDUCED line - a pass built from two
        # different moments of the same run.
        self.red_events = []
        for m in RE_REDUCED.finditer(t):
            self.red_events.append({'at': m.start(), 'kind': 'reduced',
                                    'reduced': int(m.group(1)),
                                    'repaired': int(m.group(4)),
                                    'unsound': int(m.group(5))})
        for m in RE_REFUSED.finditer(t):
            self.red_events.append({'at': m.start(), 'kind': 'refused',
                                    'reduced': 0,
                                    'repaired': int(m.group(2)),
                                    'unsound': int(m.group(3))})
        self.red_events.sort(key=lambda e: e['at'])
        if self.red_events:
            self.reduced_max = max(e['reduced'] for e in self.red_events)
            self.repaired_max = max(e['repaired'] for e in self.red_events)
            self.unsound_max = max(e['unsound'] for e in self.red_events)
        else:
            self.reduced_max = self.repaired_max = self.unsound_max = None

        m = last(RE_FILLQ, t)
        self.fillq = None
        if m:
            (self.qw_workers, self.qw_batches, self.qw_jobs, self.qw_served,
             self.qw_worker, self.qw_waited) = nums(m, 1, 2, 3, 4, 5, 6)
            self.fillq = m.group(0)
        m = last(RE_DLCENSUS, t)
        self.dl_tris = nums(m, 5)[0] if m else None
        self.f474 = RE_F474.findall(t)
        m = last(RE_SKB_FINAL, t)
        self.skb_draws = nums(m, 3)[0] if m else None

        # Captures, in the order the engine logged them, with the window kind
        # and the dimensions it read off the window.
        self.shots = [{'at': m.start(), 'path': m.group(1),
                       'name': m.group(1).replace('\\', '/').rsplit('/', 1)[-1],
                       'w': int(m.group(2)), 'h': int(m.group(3)),
                       'read': int(m.group(4)), 'frame': int(m.group(5)),
                       'window': m.group(6)}
                      for m in RE_SHOT.finditer(t)]

        # THE VERTEX HELPER'S BOUNDED REPORT, every occurrence. The line is a
        # periodic MILESTONE (first walk, then every 300th), so the last one
        # read is a checkpoint and not the end of the run: what it establishes
        # it establishes over the walks it counted, which is why the interval a
        # witness claims has to be compared against this walk count.
        self.bgv = [{'at': m.start(), 'walks': int(m.group(1)),
                     'split': int(m.group(2)), 'serial': int(m.group(3)),
                     'r_off': int(m.group(4)), 'r_cores': int(m.group(5)),
                     'r_small': int(m.group(6)), 'r_fog': int(m.group(7)),
                     'r_stats': int(m.group(8)), 'r_start': int(m.group(9)),
                     'r_fp': int(m.group(10)), 'state': m.group(11),
                     'cores': int(m.group(12))}
                    for m in RE_BGV.finditer(t)]

        m = last(RE_LIVE, t)
        self.survived = nums(m, 1)[0] if m else None
        m = last(RE_INPUT, t)
        self.input_path = m.group(1) if m else None
        self.input_reads = int(m.group(2)) if m else None
        m = last(RE_INPUT_FAIL, t)
        self.input_fail = m.group(1) if m else None
        m = last(RE_RNG, t)
        self.rng_state = m.group(1) if m else None
        self.rng_how = m.group(2) if m else None
        m = last(RE_WASET, t)
        self.wa_set = m.group(1) if m else None
        self.wa_root = m.group(2) if m else None
        self.wa_root_from = m.group(3) if m else None
        self.wa_loaded = [(m.group(1), m.group(2)) for m in RE_WALOADED.finditer(t)]
        m = last(RE_PADFILE, t)
        self.pad_file = m.group(1) if m else None
        self.pad_declared_steps = int(m.group(2)) if m else None
        self.pad_steps = [(int(m.group(1)), int(m.group(2))) for m in RE_PADSTEP.finditer(t)]
        self.teleported = RE_TELE.search(t) is not None

    # ---- the capture window -------------------------------------------
    def window(self):
        """The census bracket around this arm's own captures.

        Returns (pre, post, delta) where pre/post are counter dicts and delta
        is post - pre, or (None, None, None) when the bracket does not close.
        A cumulative counter read once at the end cannot distinguish work done
        inside the captured window from work done before the teleport, which is
        the whole reason this exists.
        """
        if not self.shots or not self.census:
            return None, None, None
        first, lastp = self.shots[0]['at'], self.shots[-1]['at']
        pre = ZERO_COUNTERS
        for c in self.census:
            if c.pos < first:
                pre = c.counters()
        post = None
        for c in self.census:
            if c.pos > lastp:
                post = c.counters()
                break
        if post is None:
            return pre, None, None
        return pre, post, dict((k, post[k] - pre[k]) for k in post)

    def tf_reading(self):
        """What this arm's #95 counters may be read as, and over what span.

        Returns (reading, span, pre, post) where `reading` is a counter dict
        or None.

        The #95 counters are CUMULATIVE over the process, exactly like #88's,
        so the same rule applies: a capturing arm is read over the bracket
        between the census report before its first capture and the one after
        its last, which is the only reading a pre-teleport accrual cannot
        satisfy. An arm that declares no captures has no bracket to read, and
        is read over the whole run with the span saying so. A capturing arm
        whose bracket does not close returns None rather than silently
        falling back to the totals, because the totals are the weaker reading
        and substituting one for the other without saying so is how a witness
        comes to claim more than it measured.
        """
        if not self.tf:
            return None, 'absent', None, None
        if not self.shots:
            return self.tf[-1].counters(), 'whole-run-cumulative', None, None
        first, lastp = self.shots[0]['at'], self.shots[-1]['at']
        # BOTH BOUNDARIES MUST BE REAL REPORTS. `pre` starts as None, not as a
        # zero reading, and that is the whole correctness argument here.
        #
        # Defaulting it to zero meant a run whose every census landed AFTER the
        # first capture still returned 'capture-bracket' and subtracted zero -
        # so a prepass that accrued all its work BEFORE the bracket and did
        # nothing inside it satisfied every positive counter from its later
        # cumulative total. The counters are cumulative, so the origin of the
        # subtraction is the measurement; inventing that origin invents the
        # delta.
        #
        # A missing boundary is therefore NO READING rather than a zero origin.
        # It is reported under its own name so the caller can say which
        # boundary was absent instead of reporting a weaker number.
        pre = None
        for c in self.tf:
            if c.pos < first:
                pre = c.counters()
        if pre is None:
            return None, 'no-report-before-first-capture', None, None
        post = None
        for c in self.tf:
            if c.pos > lastp:
                post = c.counters()
                break
        if post is None:
            return None, 'bracket-did-not-close', pre, None
        return (dict((k, post[k] - pre[k]) for k in post),
                'capture-bracket', pre, post)

    def window_intervals(self):
        """Consecutive census deltas across the capture bracket.

        The movement arm's claim is that the viewpoint CROSSED the near/far
        transition inside one run, so what proves it is an interval in which
        the far ranges submitted nothing beside an interval in which they did -
        not a total over the whole bracket, which a run that was already past
        the transition reads identically.
        """
        if not self.shots or len(self.census) < 2:
            return []
        first, lastp = self.shots[0]['at'], self.shots[-1]['at']
        chain = []
        for c in self.census:
            if c.pos < first:
                chain = [c]          # keep only the last one before the window
            else:
                chain.append(c)
                if c.pos > lastp:
                    break
        out = []
        for a, b in zip(chain, chain[1:]):
            ca, cb = a.counters(), b.counters()
            out.append(dict((k, cb[k] - ca[k]) for k in cb))
        return out

    def xlu_in_window(self):
        """The blended share reported INSIDE the capture bracket.

        The line is a per-frame average, not a cumulative counter, so a delta
        of it means nothing; what ties it to the window is the position of the
        report that printed it.
        """
        if not self.shots or not self.xlu:
            return None
        first, lastp = self.shots[0]['at'], self.shots[-1]['at']
        inside = [v for at, v in self.xlu if first < at]
        if not inside:
            return None
        after = [v for at, v in self.xlu if at > lastp]
        if not after:
            return None
        return inside[0]


def verdict(name, ok, covered, detail, excerpt=''):
    if not covered:
        if name in FAIL_ON_ABSENCE:
            return {'name': name, 'verdict': FAIL, 'detail':
                    detail + ' - ABSENT, and absence of this line is a FAIL: '
                             'the run did not confirm what the batch declared',
                    'excerpt': excerpt}
        return {'name': name, 'verdict': NONE, 'detail': detail, 'excerpt': excerpt}
    return {'name': name, 'verdict': PASS if ok else FAIL, 'detail': detail, 'excerpt': excerpt}


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        for block in iter(lambda: fh.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def shot_files(out, arm_name):
    d = Path(out) / arm_name
    if not d.is_dir():
        return []
    return sorted((p for p in d.glob('shot-*.ppm') if p.is_file()), key=lambda p: p.name)


def ppm_facts(path):
    """Header and payload completeness of one capture, read as bytes.

    src/gfx/sl_gfx_sdl.c:860 writes "P6\\n<w> <h>\\n255\\n" and then exactly
    w*h*3 bytes. A truncated file has a valid header and a short payload, which
    is why the payload length is measured rather than assumed from the header.
    """
    try:
        raw = Path(path).read_bytes()
    except OSError as exc:
        return {'ok': False, 'why': 'unreadable: %s' % (exc,)}
    if not raw.startswith(b'P6'):
        return {'ok': False, 'why': 'not a P6 capture (first bytes %r)' % (raw[:8],)}
    parts = raw.split(b'\n', 3)
    if len(parts) < 4:
        return {'ok': False, 'why': 'header has fewer than three newline-terminated fields'}
    try:
        dims = parts[1].split()
        w, h = int(dims[0]), int(dims[1])
    except (ValueError, IndexError):
        return {'ok': False, 'why': 'dimension field %r is not two integers' % (parts[1],)}
    if parts[2].strip() != b'255':
        return {'ok': False, 'why': 'maxval field %r is not 255' % (parts[2],)}
    want = w * h * 3
    got = len(parts[3])
    return {'ok': got == want, 'w': w, 'h': h, 'payload': got, 'want': want,
            'why': '' if got == want
                   else 'payload %d bytes, %dx%d declares %d' % (got, w, h, want)}


def capture_set_exact(out, arm, facts):
    """The DECLARED capture set, not merely a nonzero equal count.

    Every clause here is a case that a count comparison accepts: a missing
    file, an extra file, a truncated payload, a capture at the wrong size, and
    a set that is byte-identical to its pair but is not the declared set.
    """
    shots = arm.get('shots') or {}
    want_n = shots.get('count')
    want_w = shots.get('width')
    want_h = shots.get('height')
    files = shot_files(out, arm['name'])
    bad = []
    if want_n is None or want_w is None or want_h is None:
        return None, 'the arm declares no capture count and grid', files
    if len(files) != want_n:
        bad.append('%d capture files, %d declared' % (len(files), want_n))
    if len(facts.shots) != want_n:
        bad.append('%d sl_shot lines, %d declared' % (len(facts.shots), want_n))
    for p in files:
        f = ppm_facts(p)
        if not f['ok']:
            bad.append('%s: %s' % (p.name, f['why']))
        elif f['w'] != want_w or f['h'] != want_h:
            bad.append('%s: %dx%d, %dx%d declared' % (p.name, f['w'], f['h'], want_w, want_h))
    for s in facts.shots:
        if s['w'] != want_w or s['h'] != want_h:
            bad.append('sl_shot %s logged %dx%d, %dx%d declared'
                       % (s['name'], s['w'], s['h'], want_w, want_h))
    logged = sorted(s['name'] for s in facts.shots)
    onDisk = sorted(p.name for p in files)
    if logged and onDisk and logged != onDisk:
        bad.append('logged %r but found %r' % (logged, onDisk))
    return bad, '%d files, %d logged, declared %d at %dx%d' % (
        len(files), len(facts.shots), want_n, want_w, want_h), files


SHOT_SCHEDULE_FIELDS = ('first', 'last', 'every', 'read_first', 'read_last',
                        'expected_moments', 'width', 'height', 'count')


def capture_schedule(arm, facts):
    """The DECLARED capture schedule, moment by moment.

    Returns (bad, detail) with bad a list of findings, or (None, why) when the
    arm declares no schedule - an absent declaration is NOT_COVERED for the
    arm, because nothing was declared for the captures to disagree with.

    Every clause is a case that names, count, dimensions and payload accept.
    The grid comes from the engine's own rule (src/gfx/sl_gfx_sdl.c:816-828):
    the moment must lie inside [FIRST,LAST] and must be at least EVERY past
    the previous capture - "at least EVERY since the last capture", never
    divisibility, which over a read window can step over every multiple of
    the stride and capture nothing. Which COUNTER the window is expressed in
    is part of the declaration too: SL_SHOT_READ_FIRST/LAST switch the engine
    to the input record index (:789-802), a counter that is not proportional
    to the frame counter, so a schedule declared in one unit and executed in
    the other lands somewhere else entirely and both logs still look healthy.
    """
    shots = arm.get('shots') or {}
    absent = [k for k in SHOT_SCHEDULE_FIELDS if k not in shots]
    if absent:
        return None, 'the arm declares no capture schedule: %r absent from `shots`' % (absent,)
    first, lastf, every = shots['first'], shots['last'], shots['every']
    rfirst, rlast = shots['read_first'], shots['read_last']
    moments, count = shots['expected_moments'], shots['count']
    required = {'first': first, 'last': lastf, 'every': every,
                'expected_moments': moments, 'count': count}
    null = sorted(k for k, v in required.items() if v is None)
    if null:
        return None, 'the arm declares a capture schedule with %r unset' % (null,)
    bad = []
    by_read = rfirst is not None and rlast is not None
    if (rfirst is None) != (rlast is None):
        bad.append('half a read window is declared (read_first=%r read_last=%r): the engine '
                   'switches to the record index on either one and leaves the other at its '
                   'frame default, so the window it ran is not the window declared'
                   % (rfirst, rlast))
    lo, hi = (rfirst, rlast) if by_read else (first, lastf)
    unit = 'read' if by_read else 'frame'
    # The arm may also NAME the counter it windowed on. When it does, that name
    # and the window it declared must agree: the engine switches to the record
    # index on SL_SHOT_READ_FIRST/LAST alone, so a `gate` that disagrees with
    # the bounds says the schedule was built in one unit and armed in another.
    gate = shots.get('gate')
    if gate is not None and gate != unit:
        bad.append('the arm names gate=%r and declares a %s window' % (gate, unit))
    if not isinstance(moments, list) or not moments:
        bad.append('expected_moments %r is not a non-empty list of moments' % (moments,))
        return bad, 'the declared schedule carries no moments'
    if count != len(moments):
        bad.append('count %r but %d expected moments declared' % (count, len(moments)))
    if not isinstance(every, int) or every < 1:
        bad.append('every=%r: the engine clamps a non-positive stride to 60, so this is not '
                   'the schedule that ran' % (every,))
    if lo is not None and hi is not None:
        out_of = [m for m in moments if m < lo or m > hi]
        if out_of:
            bad.append('declared moments %r lie outside the declared %s window [%r,%r]'
                       % (out_of, unit, lo, hi))
    for a, b in zip(moments, moments[1:]):
        if b <= a:
            bad.append('declared moments do not advance: %r then %r' % (a, b))
        elif isinstance(every, int) and every >= 1 and b - a < every:
            bad.append('declared moments %r and %r are %d apart, less than every=%d'
                       % (a, b, b - a, every))
    got = [(s['read'] if by_read else s['frame']) for s in facts.shots]
    kinds = sorted(set(s['window'] for s in facts.shots))
    if len(facts.shots) != len(moments):
        bad.append('%d sl_shot lines, %d moments declared' % (len(facts.shots), len(moments)))
    if facts.shots and kinds != [unit]:
        bad.append('the arm declares a %s window and the run logged window=%r' % (unit, kinds))
    for i, (want, s) in enumerate(zip(moments, facts.shots)):
        have = s['read'] if by_read else s['frame']
        if have != want:
            bad.append('capture %d (%s) is at %s=%d, declared %d'
                       % (i, s['name'], unit, have, want))
    return bad, ('%d captures declared at %s %r inside [%r,%r] every %r; the run logged %r'
                 % (count, unit, moments, lo, hi, every, got[:8]))


def helper_net(facts):
    """The helper's split MINUS the splits whose upper range was recomputed.

    Both counters are cumulative and the recomputing path increments both
    (src/gfx/sl_gfx_dl.c:17986-17989), so the difference is monotone and the
    LAST line carries the largest interval. A split whose upper range was
    recomputed on the render thread after a floating-point change did the work
    twice: it is not evidence that the helper took a range.
    """
    if not facts.bgv:
        return None, None
    e = facts.bgv[-1]
    return e, e['split'] - e['r_fp']


def check_arm(out, arm, facts, peers, census_ref, record):
    """One arm's named witnesses. `peers` maps arm name -> Facts."""
    name = arm['name']
    run = arm.get('run') or {}
    code = run.get('exit_code')
    expect = arm.get('expect') or {}
    pre, post, delta = facts.window()
    tf_read, tf_span, tf_pre, tf_post = facts.tf_reading()
    tf_detail = ('over the %s' % tf_span) if tf_read is not None else tf_span
    res = []

    def tf_absent(wname):
        """The one place a #95 witness reports why it could not read.

        MALFORMED IS A FAIL, NOT AN ABSENCE. A line that printed and did not
        parse is evidence the grammar drifted from the emitter, and reporting
        it as "not measured" would let the drift pass as a candidate that was
        simply off.
        """
        if facts.tf_malformed:
            return verdict(wname, False, True,
                           'the #95 clip-prepass line was printed %d time(s) and %d of '
                           'them did not parse against the emitter grammar at '
                           'src/gfx/sl_gfx_dl.c:19993 - a MALFORMED line, not an absent '
                           'one' % (facts.tf_prefixes, facts.tf_prefixes - len(facts.tf)),
                           'parsed=%d prefixes=%d' % (len(facts.tf), facts.tf_prefixes))
        if not facts.tf:
            return verdict(wname, False, False,
                           'this arm printed no #95 clip-prepass line at all (it is a '
                           'census-branch line, so SL_DL_CENSUS=1 is its precondition)')
        if tf_span == 'no-report-before-first-capture':
            return verdict(wname, False, False,
                           'no #95 report PRECEDES this arm\'s first capture, so the '
                           'bracket has no measured origin; a zero origin is NOT '
                           'invented for it, because the counters are cumulative and '
                           'work accrued before the bracket would otherwise read as '
                           'work done inside it')
        if tf_span == 'bracket-did-not-close':
            return verdict(wname, False, False,
                           'no #95 report follows this arm\'s last capture, so the '
                           'bracket around the captured window does not close; the '
                           'whole-run totals are NOT substituted for it')
        # EVERY NO-READING STATUS MUST BE NAMED ABOVE. Falling through with
        # tf_read None would reach tf_count's subscript and raise, turning a
        # measurement gap into a crash; refusing by name keeps it a verdict.
        if tf_read is None:
            return verdict(wname, False, False,
                           'the #95 reading is unavailable and its span reported '
                           '"%s", which this checker does not recognise as a '
                           'measurable bracket' % tf_span)
        return None

    def tf_count(wname, key, sense, note=''):
        """One cumulative #95 counter, read over the span tf_reading chose."""
        missing = tf_absent(wname)
        if missing is not None:
            return missing
        v = tf_read[key]
        ok = (v > 0) if sense == 'positive' else (v == 0)
        return verdict(wname, ok, True,
                       '%s read %d %s (must be %s)%s'
                       % (key, v, tf_detail, sense, (' - ' + note) if note else ''),
                       'pre=%r post=%r last=%r'
                       % (tf_pre, tf_post, facts.tf_last.counters() if facts.tf_last else None))

    for w in arm.get('witnesses') or []:
        if w == 'EXIT_ZERO':
            res.append(verdict(w, code == 0, code is not None,
                               'the real OS exit of the child', 'exit_code=%r' % (code,)))
        elif w == 'NOT_TIMED_OUT':
            to = run.get('timed_out')
            res.append(verdict(w, to is False, to is not None,
                               'the child exited on its own rather than being killed',
                               'timed_out=%r' % (to,)))
        elif w == 'LIVENESS':
            want = int(arm['frames'])
            res.append(verdict(w, facts.survived == want, facts.survived is not None,
                               'the pump says it survived %d frames' % want,
                               'survived=%r' % (facts.survived,)))
        elif w == 'SEED_ECHOED':
            want = (arm.get('seed_hex') or '').lower()
            ok = (facts.rng_state == want and facts.rng_how == 'explicit SL_RNG_SEED')
            res.append(verdict(w, ok, facts.rng_state is not None,
                               'the batch pinned RNG state %s and the run must say it '
                               'took it explicitly' % want,
                               'state=%r how=%r' % (facts.rng_state, facts.rng_how)))
        elif w == 'PRESENTATION_XBLA_ACTIVE':
            want = expect.get('presentation_set', 'xbla')
            ok = facts.wa_set == want
            res.append(verdict(w, ok, facts.wa_set is not None,
                               "the world-asset set the presentation derived must be '%s'; "
                               "ORIGINAL derives the empty set, which is the dark picture "
                               "a scratch config without presentation=2 produces" % want,
                               'set=%r root=%r (%r)' % (facts.wa_set, facts.wa_root, facts.wa_root_from)))
        elif w == 'ASSET_ROOT_BOUND':
            want = (expect.get('worldasset_root') or '').replace('\\', '/').rstrip('/').lower()
            got = (facts.wa_root or '').replace('\\', '/').rstrip('/').lower()
            ok = bool(want) and got == want and facts.wa_root_from == 'SL_WORLDASSET_ROOT'
            res.append(verdict(w, ok, facts.wa_root is not None,
                               'the run must have read the root this batch bound, and must '
                               'say it came from SL_WORLDASSET_ROOT rather than the player '
                               'data ladder',
                               'bound=%r read=%r from=%r' % (want, got, facts.wa_root_from)))
        elif w == 'STAGE_ITEM_LOADED':
            want = expect.get('stage_item')
            got = ['%s/%s' % (c, p) for c, p in facts.wa_loaded]
            ok = bool(want) and any(p == want for _c, p in facts.wa_loaded)
            res.append(verdict(w, ok, len(facts.wa_loaded) > 0,
                               "the declared stage item '%s' must appear in the run's own "
                               'LOADED lines; a run that loaded nothing, or loaded another '
                               "stage's item, measured something the batch did not declare"
                               % (want,),
                               'loaded=%r' % (got[:8],)))
        elif w == 'CENSUS_REPORT_PRESENT':
            res.append(verdict(w, len(facts.census) > 0, len(facts.census) > 0,
                               'the per-depth submission census was printed at all',
                               '%d census reports' % len(facts.census)))
        elif w == 'NO_CENSUS_REQUESTED':
            # The reduction and the vertex helper are both refused while a
            # statistics reader is active (src/gfx/sl_gfx_dl.c:18357, :18386,
            # :17960: fstats = bg_fogtrace_on() || dl_census_on()). An arm that
            # measures either must therefore prove it ran WITHOUT the census.
            env = arm.get('env') or {}
            asked = env.get('SL_DL_CENSUS') not in (None, '', '0')
            ok = (not asked) and len(facts.census) == 0
            res.append(verdict(w, ok, True,
                               'no statistics reader may be armed on an arm that measures '
                               'a feature the readers switch off',
                               'SL_DL_CENSUS=%r, %d census reports in the log'
                               % (env.get('SL_DL_CENSUS'), len(facts.census))))
        elif w == 'GPU_PROGRAM_READY':
            if facts.gpu_bad is not None:
                res.append(verdict(w, False, True,
                                   'the driver refused the vertex program',
                                   facts.gpu_bad.group(0)))
            else:
                res.append(verdict(w, facts.gpu_ready, facts.gpu_ready,
                                   'a real GL implementation compiled and linked the '
                                   'background vertex program'))
        elif w in ('WINDOW_SINGLE_DELTA_POSITIVE', 'WINDOW_FAR0_DELTA_POSITIVE',
                   'WINDOW_FAR1_DELTA_POSITIVE', 'WINDOW_FAR0_DELTA_ZERO',
                   'WINDOW_FAR1_DELTA_ZERO', 'WINDOW_FAR_NOT_ADMITTED_DELTA_ZERO',
                   'WINDOW_FAR_NOT_ADMITTED_DELTA_POSITIVE'):
            if delta is None:
                why = 'this arm logged no capture' if not facts.shots else (
                      'no census report follows the last capture, so the bracket around '
                      'the captured window does not close')
                if not facts.census:
                    why = 'this arm printed no per-depth census'
                res.append(verdict(w, False, False, why))
                continue
            key = {'WINDOW_SINGLE_DELTA_POSITIVE': ('single_calls', 'single_indices', 'positive'),
                   'WINDOW_FAR0_DELTA_POSITIVE': ('far0_calls', 'far0_indices', 'positive'),
                   'WINDOW_FAR1_DELTA_POSITIVE': ('far1_calls', 'far1_indices', 'positive'),
                   'WINDOW_FAR0_DELTA_ZERO': ('far0_calls', 'far0_indices', 'zero'),
                   'WINDOW_FAR1_DELTA_ZERO': ('far1_calls', 'far1_indices', 'zero'),
                   'WINDOW_FAR_NOT_ADMITTED_DELTA_ZERO': ('far_not_admitted', None, 'zero'),
                   'WINDOW_FAR_NOT_ADMITTED_DELTA_POSITIVE': ('far_not_admitted', None, 'positive')}[w]
            ka, kb, sense = key
            va = delta[ka]
            vb = delta[kb] if kb else None
            if sense == 'positive':
                ok = va > 0 and (vb is None or vb > 0)
            else:
                ok = va == 0 and (vb is None or vb == 0)
            res.append(verdict(w, ok, True,
                               'across the bracket around this arm\'s own captures, %s rose '
                               'by %d%s (must be %s)'
                               % (ka, va, '' if vb is None else ' and %s by %d' % (kb, vb), sense),
                               'pre=%r post=%r' % (pre, post)))
        elif w == 'WINDOW_BLENDED_POSITIVE':
            v = facts.xlu_in_window()
            res.append(verdict(w, v is not None and v > 0, v is not None,
                               'the blended share of background GL draws per frame, reported '
                               'inside the capture bracket, = %r' % (v,),
                               'the line is a per-frame average, so what ties it to the '
                               'window is the position of the report, not a delta'))
        elif w == 'CLAMP_INPUT_PRESENT':
            # The single-range clamp (u_dmode == 0) is gated on u_fogless == 0
            # and c.z > c.w (src/gfx/sl_gfx_bggpu.c BGG_VS). Its CPU twin at
            # src/gfx/sl_gfx_dl.c:17849 counts the vertices that satisfy the
            # same test into nfar, reported as #78's "vertices past the game's
            # far plane". This witness establishes the clamp's INPUT CONDITION
            # on this stage and pose. It does NOT establish that the driver
            # executed the branch - see not_covered, CLAMP_BRANCH_EXECUTED.
            covered = facts.far_verts is not None and facts.fogless_frames is not None
            ok = covered and facts.far_verts > 0 and facts.fogless_frames == 0
            res.append(verdict(w, ok, covered,
                               '%r vertices past the far plane on the last two-range frame, '
                               'on a stage reporting %r fogless frames (the clamp cannot fire '
                               'on a fogless walk)' % (facts.far_verts, facts.fogless_frames),
                               'INPUT CONDITION ONLY - not driver execution of the branch'))
        elif w == 'CENSUS_SINGLE_MATCHES_REF':
            ref = census_ref
            if facts.bydepth is None or ref is None or ref.bydepth is None:
                res.append(verdict(w, False, False, 'one of the two arms printed no BY DEPTH line'))
            else:
                ok = facts.single_calls == ref.single_calls
                res.append(verdict(w, ok, True,
                                   'single-range calls %d here vs %d in the far-admitted arm'
                                   % (facts.single_calls, ref.single_calls)))
        elif w == 'FAR_FRAMES_POSITIVE':
            res.append(verdict(w, (facts.far_frames or 0) > 0, facts.far_frames is not None,
                               'frames drawn in two depth ranges = %r' % (facts.far_frames,)))
        elif w == 'FAR_FRAMES_ZERO':
            res.append(verdict(w, facts.far_frames == 0, facts.far_frames is not None,
                               'frames drawn in two depth ranges = %r (must be 0)' % (facts.far_frames,)))
        elif w == 'CAPTURE_SET_EXACT':
            bad, detail, _files = capture_set_exact(out, arm, facts)
            if bad is None:
                res.append(verdict(w, False, False, detail))
            else:
                res.append(verdict(w, not bad, True, detail, '; '.join(bad[:6])))
        elif w == 'CAPTURE_SCHEDULE_EXACT':
            if not facts.present:
                res.append(verdict(w, False, False, 'this arm produced no stderr to read '
                                                    'capture moments from'))
                continue
            bad, detail = capture_schedule(arm, facts)
            if bad is None:
                res.append(verdict(w, False, False, detail))
            else:
                res.append(verdict(w, not bad, True, detail, '; '.join(bad[:6])))
        elif w == 'VERTEX_HELPER_SPLIT_POSITIVE':
            e, net = helper_net(facts)
            res.append(verdict(w, net is not None and net > 0, e is not None,
                               'the helper took %r ranges of which %r had their upper range '
                               'recomputed, so it saved the work on %r walks (must be > 0)'
                               % (None if e is None else e['split'],
                                  None if e is None else e['r_fp'], net),
                               '' if e is None else
                               'walks=%d split=%d serial=%d recomputed=%d helper=%s cores=%d'
                               % (e['walks'], e['split'], e['serial'], e['r_fp'],
                                  e['state'], e['cores'])))
        elif w == 'VERTEX_HELPER_INTERVAL_COVERED':
            # A periodic milestone establishes nothing about the walks that
            # followed it. The line prints on the first walk and then every
            # 300th (src/gfx/sl_gfx_dl.c:18022), so the arm declares the
            # interval it means to test and the last line must have counted
            # through it. An undeclared interval is an absence, not a pass.
            want = (arm.get('helper') or {}).get('interval_walks')
            e = facts.bgv[-1] if facts.bgv else None
            if want is None:
                res.append(verdict(w, False, False,
                                   'the arm declares no helper interval (helper.interval_walks), '
                                   'so there is nothing the last milestone can be measured '
                                   'against'))
            elif e is None:
                res.append(verdict(w, False, False, 'this arm printed no helper report line'))
            else:
                # A milestone short of the interval did not measure the walks
                # that followed it, so this is an ABSENCE over the tail and not
                # a measured failure: NOT_COVERED, never a pass.
                res.append(verdict(w, e['walks'] >= want, e['walks'] >= want,
                                   'the last helper report counted %d walks and the arm declares '
                                   'an interval of %d; a milestone short of the interval leaves '
                                   'the walks after it unreported' % (e['walks'], want),
                                   '%d report line(s), walk counts %r'
                                   % (len(facts.bgv), [x['walks'] for x in facts.bgv][:8])))
        elif w == 'CAPTURE_NAMES_MATCH_PAIR':
            other = arm.get('pair')
            of = peers.get(other)
            mine = [p.name for p in shot_files(out, name)]
            theirs = [p.name for p in shot_files(out, other)] if other else []
            myLog = [s['name'] for s in facts.shots]
            theirLog = [s['name'] for s in of.shots] if of else []
            covered = bool(other) and bool(mine) and bool(theirs)
            ok = covered and mine == theirs and myLog == theirLog
            res.append(verdict(w, ok, covered,
                               'the two halves must have written the SAME capture names: '
                               '%r here, %r in %s' % (mine, theirs, other),
                               'logged %r vs %r' % (myLog, theirLog)))
        elif w == 'FRAMES_BYTE_IDENTICAL':
            other = arm.get('pair')
            mine = shot_files(out, name)
            theirs = shot_files(out, other) if other else []
            if not mine or not theirs:
                res.append(verdict(w, False, False,
                                   'one half of the pair wrote no captures (%d here, %d in %s)'
                                   % (len(mine), len(theirs), other)))
                continue
            if len(mine) != len(theirs):
                res.append(verdict(w, False, True,
                                   'capture counts differ: %d here, %d in %s'
                                   % (len(mine), len(theirs), other)))
                continue
            diffs = []
            for a, b in zip(mine, theirs):
                if a.name != b.name:
                    diffs.append('name %s != %s' % (a.name, b.name))
                    continue
                ha, hb = sha256(a), sha256(b)
                if ha != hb:
                    diffs.append('%s %s != %s %s' % (a.name, ha[:16], b.name, hb[:16]))
            res.append(verdict(w, not diffs, True,
                               '%d capture pairs compared byte for byte, no tolerance' % len(mine),
                               '; '.join(diffs[:4])))
        elif w == 'SHOT_MOMENTS_MATCH':
            other = arm.get('pair')
            of = peers.get(other)
            if of is None or not facts.shots or not of.shots:
                res.append(verdict(w, False, False, 'one half of the pair logged no sl_shot line'))
                continue
            a = [(s['name'], s['read'], s['frame'], s['window']) for s in facts.shots]
            b = [(s['name'], s['read'], s['frame'], s['window']) for s in of.shots]
            res.append(verdict(w, a == b, True,
                               'capture identity (name, read, frame, window): %r here vs %r in %s'
                               % (a, b, other)))
        elif w == 'REDUCTION_REPORT_PRESENT':
            res.append(verdict(w, len(facts.red_events) > 0, len(facts.red_events) > 0,
                               "the reduction's own inline report was printed at all",
                               '%d report lines' % len(facts.red_events)))
        elif w == 'REDUCED_WALKS_POSITIVE':
            covered = facts.reduced_max is not None
            res.append(verdict(w, covered and facts.reduced_max > 0, covered,
                               'the greatest reduced-walk count reported anywhere in this arm '
                               '= %r' % (facts.reduced_max,),
                               'a REFUSED line with 0 reduced is a FAIL, not an absence'))
        elif w == 'UNSOUND_DRAWS_ZERO':
            covered = facts.unsound_max is not None
            res.append(verdict(w, covered and facts.unsound_max == 0, covered,
                               'the GREATEST unsound mesh draw count reported anywhere in this '
                               'arm = %r (must be 0); taken over every report line in document '
                               'order, so a later nonzero cannot be hidden by an earlier zero'
                               % (facts.unsound_max,),
                               'events=%r' % ([(e['kind'], e['unsound']) for e in facts.red_events][:8],)))
        elif w == 'REPAIRS_REPORTED':
            covered = facts.repaired_max is not None
            res.append(verdict(w, covered, covered,
                               'the greatest repair count reported anywhere in this arm = %r '
                               '(recorded, not gated)' % (facts.repaired_max,)))
        elif w == 'SKY_FLOAT_CARRIER_PROD_ONCE':
            n = len([x for x in facts.f474 if x == 'prod'])
            res.append(verdict(w, n == 1 and len(facts.f474) == 1, len(facts.f474) > 0,
                               '#474 carrier markers = %r (exactly one, and it must be prod)'
                               % (facts.f474,)))
        elif w == 'SKYBATCH_INDEXED_DRAWS_POSITIVE':
            res.append(verdict(w, (facts.skb_draws or 0) > 0, facts.skb_draws is not None,
                               'skybatch final indexed-draws = %r' % (facts.skb_draws,)))
        elif w == 'FILL_WORKERS_DID_WORK':
            covered = facts.fillq is not None
            ok = covered and facts.qw_workers > 0 and facts.qw_worker > 0
            res.append(verdict(w, ok, covered,
                               '%r workers, %r answers computed by a worker'
                               % (getattr(facts, 'qw_workers', None), getattr(facts, 'qw_worker', None)),
                               facts.fillq or ''))
        elif w == 'PAD_SCRIPT_STEPS_APPLIED':
            want = (arm.get('pad_script') or {}).get('steps')
            ok = (facts.pad_declared_steps == want
                  and want is not None
                  and len(facts.pad_steps) >= 2
                  and len(set(f for _s, f in facts.pad_steps)) >= 2)
            res.append(verdict(w, ok, facts.pad_declared_steps is not None,
                               'the step file must have parsed to the %r steps the batch pinned, '
                               'and at least two of them must have been APPLIED at distinct '
                               'frames - which is within-run movement, not a latched teleport'
                               % (want,),
                               'parsed=%r applied=%r' % (facts.pad_declared_steps, facts.pad_steps[:8])))
        elif w == 'INPUT_STREAM_REPLAYED':
            # The replay arm's pose comes from a pinned saved input stream
            # rather than a mark, so the stream IS the pose: an arm that
            # replayed another stream, or a prefix of the declared one, was
            # posed somewhere the batch did not declare. The stream is named by
            # the arm's own `control` record - the pinned stream, its hash and
            # its length in reads - or, failing that, by the variable the engine
            # reads (env SL_INPUT).
            decl = arm.get('control') or {}
            env = arm.get('env') or {}
            want_path = decl.get('input') or env.get('SL_INPUT')
            want_reads = decl.get('reads')
            want_sha = decl.get('input_sha256')

            def _norm(p):
                return (p or '').replace('\\', '/').rstrip('/').lower()

            if facts.input_fail is not None:
                res.append(verdict(w, False, True,
                                   'the replay stream did not open, so the arm replayed '
                                   'nothing and the engine exited at that point',
                                   'SL_INPUT open failed: %s' % (facts.input_fail,)))
            elif facts.input_reads is None:
                res.append(verdict(w, False, False,
                                   'neither the opened-stream line nor an open failure was '
                                   'printed, so nothing says this arm replayed a stream'))
            elif not want_path:
                res.append(verdict(w, False, False,
                                   'the arm declares no replay stream (neither control.input '
                                   'nor env SL_INPUT), so the stream the run opened has '
                                   'nothing to be measured against'))
            else:
                bad = []
                if _norm(facts.input_path) != _norm(want_path):
                    bad.append('the run opened %r, the arm declares %r'
                               % (facts.input_path, want_path))
                if facts.input_reads <= 0:
                    bad.append('the stream holds 0 reads, so it drives no input')
                if want_reads is not None and facts.input_reads != want_reads:
                    bad.append('the run read %d records, the arm declares %r'
                               % (facts.input_reads, want_reads))
                note = ''
                if want_sha:
                    try:
                        got = sha256(facts.input_path)
                    except OSError as exc:
                        # Recorded as unverified rather than passed: the file is
                        # not readable from here, and an unreadable clause is
                        # not a satisfied one.
                        note = ('the declared sha256 %s was NOT verified: the stream is not '
                                'readable from here (%s)' % (want_sha, exc))
                    else:
                        if got != want_sha:
                            bad.append('the stream on disk hashes %s, the arm declares %s'
                                       % (got, want_sha))
                res.append(verdict(w, not bad, True,
                                   'the arm replayed %r with %r reads (declared %r reads)%s'
                                   % (facts.input_path, facts.input_reads, want_reads,
                                      '; ' + note if note else ''),
                                   '; '.join(bad[:4])))
        # ---- the clip prepass's OWN witnesses, from the #95 line -----------
        elif w == 'TF_STATE_READY':
            missing = tf_absent(w)
            if missing is not None:
                res.append(missing)
            else:
                st = facts.tf_last.state
                res.append(verdict(w, st == 'ready', True,
                                   "the module's state field must read 'ready'; a refusal "
                                   'reason there is the module declining, which is a CPU '
                                   'fallback and never GPU coverage',
                                   'state=%r' % (st,)))
        elif w == 'TF_PREPASSES_POSITIVE':
            res.append(tf_count(w, 'prepasses', 'positive',
                                'prepass submissions that RAN'))
        elif w == 'TF_VERTICES_POSITIVE':
            res.append(tf_count(w, 'vertices', 'positive',
                                'vertices actually transformed by those prepasses'))
        elif w == 'TF_SUBMITTED_POSITIVE':
            # THE ONLY FIELD THAT COUNTS ACTUAL DRAWING. Both halves are
            # required: a run count with no indices drew nothing.
            missing = tf_absent(w)
            if missing is not None:
                res.append(missing)
            else:
                rd, ri = tf_read['raster_draws'], tf_read['raster_indices']
                res.append(verdict(w, rd > 0 and ri > 0, True,
                                   'nonempty runs on a prepass pool read %d over %d indices '
                                   '%s (both must be positive). This is the ONLY field that '
                                   'counts actual drawing from a prepass pool: binds (%d) and '
                                   'reuses (%d) are excluded from every coverage sum'
                                   % (rd, ri, tf_detail, tf_read['binds'], tf_read['reuses']),
                                   'pre=%r post=%r' % (tf_pre, tf_post)))
        elif w == 'TF_SOURCE_DEPTH_FAR0_POSITIVE':
            res.append(tf_count(w, 'far0', 'positive',
                                "the depth of the pool that was the vertex SOURCE for the "
                                "run, not the walk's admission depth"))
        elif w == 'TF_SOURCE_DEPTH_FAR1_POSITIVE':
            res.append(tf_count(w, 'far1', 'positive',
                                'the source pool depth, as above'))
        elif w == 'TF_WALKS_POSITIVE':
            res.append(tf_count(w, 'walks', 'positive',
                                'walks that submitted a prepass'))
        elif w == 'TF_ADMITTED_POSITIVE':
            res.append(tf_count(w, 'admitted', 'positive',
                                'g_bgtf_elig_walks, walks the predicates admitted'))
        elif w == 'TF_REFUSALS_ARE_NUMBERS_AND_A_FALLBACK_FAILS':
            # A CPU FALLBACK IS NEVER GPU COVERAGE. The three refusal counters
            # must all be reported, and any of them nonzero fails an arm that
            # is claimed as coverage - the work that was refused was done on
            # the CPU, so the prepass did not do it.
            missing = tf_absent(w)
            if missing is not None:
                res.append(missing)
            else:
                rw, rd2 = tf_read['refused_walks'], tf_read['refused_draws']
                mr = tf_read['module_refusals']
                res.append(verdict(w, rw == 0 and rd2 == 0 and mr == 0, True,
                                   'refused walks %d, refused draws %d, module refusals %d '
                                   '%s - all three are reported, and any of them nonzero in '
                                   'an arm claimed as coverage FAILS that arm, because the '
                                   'refused work ran on the CPU'
                                   % (rw, rd2, mr, tf_detail),
                                   'pre=%r post=%r' % (tf_pre, tf_post)))
        elif w == 'TF_REUSES_ARE_NOT_COVERAGE':
            # A reporting rule, asserted on the reading rather than on the
            # numbers: both fields must be present and readable, and neither
            # appears in TfClip.COVERAGE_KEYS, so no coverage sum in this
            # checker can include them. A reuse or a bind rising is not a
            # failure - it is simply not coverage.
            missing = tf_absent(w)
            if missing is not None:
                res.append(missing)
            else:
                excluded = sorted(TfClip.EXCLUDED_FROM_COVERAGE)
                leaked = [k for k in excluded if k in TfClip.COVERAGE_KEYS]
                res.append(verdict(w, not leaked, True,
                                   'binds %d and reuses %d are reported %s and are excluded '
                                   'from every coverage sum: a bind makes a pool the vertex '
                                   'source and draws nothing, and a reuse is a draw served '
                                   'from an already-filled slot, which is work AVOIDED. The '
                                   'coverage fields are %s'
                                   % (tf_read['binds'], tf_read['reuses'], tf_detail,
                                      ', '.join(TfClip.COVERAGE_KEYS)),
                                   'excluded=%r leaked_into_coverage=%r' % (excluded, leaked)))
        elif w == 'TF_REFILL_AND_EVICTION_READ_SEPARATELY':
            # TWO NUMBERS, ONE OF WHICH MEANS PRESSURE. A refill takes a free
            # or PRIOR-WALK slot and is the normal course; only a displaced
            # CURRENT-WALK key is cache pressure. Summing them would report
            # ordinary operation as pressure.
            missing = tf_absent(w)
            if missing is not None:
                res.append(missing)
            else:
                rf, ev = tf_read['slot_refills'], tf_read['evictions']
                res.append(verdict(w, True, True,
                                   'slot refills %d (a free or PRIOR-WALK slot taken: normal, '
                                   'NOT pressure) and evictions %d (a CURRENT-WALK key '
                                   'displaced: this and only this is cache pressure, and it '
                                   'is %s here) %s'
                                   % (rf, ev, 'PRESENT' if ev > 0 else 'absent', tf_detail),
                                   'cache_pressure=%r' % (ev > 0,)))
        elif w == 'TF_SOURCE_DEPTH_SINGLE_IS_A_CONSISTENCY_READING':
            # NOT A POSITIVE CLAIM. At this recipe every per-depth census
            # report read 0 calls / 0 indices at the single-range clamp while
            # both far columns ran, so 0 here AGREES with the recipe and is
            # the expected reading. A nonzero is a finding to investigate, not
            # a pass - which is why this witness fails on one rather than
            # reporting it as better coverage.
            missing = tf_absent(w)
            if missing is not None:
                res.append(missing)
            else:
                v = tf_read['single']
                res.append(verdict(w, v == 0, True,
                                   'the single-range source depth read %d %s. This is a '
                                   'CONSISTENCY READING EXPECTED TO BE 0 at this recipe and '
                                   'is explicitly NOT a positive coverage claim: SINGLE '
                                   'submission has no measured viewpoint. A nonzero here is a '
                                   'finding to investigate, not a pass' % (v, tf_detail),
                                   'single=%d' % v))
        elif w == 'TF_OFF_ARMS_READ_ZERO':
            # The off arms' side of the same line: zero prepasses AND the
            # emitter's own off phrase, which is printed exactly when
            # sl_bgtf_opt_in() is false. Zero counters alone would also be
            # produced by a candidate that was asked for and did nothing.
            missing = tf_absent(w)
            if missing is not None:
                res.append(missing)
            else:
                prep = tf_read['prepasses']
                phrase = facts.tf_last.off_phrase
                res.append(verdict(w, prep == 0 and phrase, True,
                                   'an off arm must read 0 prepasses %s AND carry the '
                                   "'%s' phrase; zero counters alone would also be produced "
                                   'by a candidate that was asked for and did nothing'
                                   % (tf_detail, TF_OFF_PHRASE),
                                   'prepasses=%d off_phrase=%r state=%r'
                                   % (prep, phrase, facts.tf_last.state)))
        elif w == 'PROGRAM_COLUMNS_MUST_INVERT':
            # THE INVERTED WITNESS. On a candidate arm the vertex PROGRAM's
            # own per-depth columns must read ZERO across the bracket, because
            # a prepass walk leaves gpu_walk 0 and selects pool 0. Positive
            # program columns on a candidate arm mean the program took the
            # walk instead of the prepass, so this reading is what says the
            # prepass - and not the program - did the work.
            if delta is None:
                why = ('this arm printed no per-depth #88 census' if not facts.census
                       else 'the #88 census bracket around this arm\'s captures does not close')
                res.append(verdict(w, False, False, why))
            else:
                f0, f1 = delta['far0_calls'], delta['far1_calls']
                res.append(verdict(w, f0 == 0 and f1 == 0, True,
                                   "the vertex PROGRAM's far columns read %d and %d across "
                                   'the bracket and must BOTH be zero on a candidate arm: a '
                                   'prepass walk leaves gpu_walk 0 and selects pool 0, so '
                                   'positive program columns here mean the program took the '
                                   'walk instead of the prepass. This inversion is itself the '
                                   'witness, which is why WINDOW_FAR0/FAR1_DELTA_POSITIVE '
                                   'cannot be asked of a candidate arm' % (f0, f1),
                                   'pre=%r post=%r' % (pre, post)))
        elif w == 'TELEPORT_APPLIED':
            res.append(verdict(w, facts.teleported, facts.teleported,
                               'the declared pose was applied by the engine'))
        else:
            res.append(verdict(w, False, False, 'unknown witness name'))

    if arm['pose']['kind'] == 'teleport' and 'TELEPORT_APPLIED' not in (arm.get('witnesses') or []):
        res.append(verdict('TELEPORT_APPLIED', facts.teleported, facts.teleported,
                           'the declared pose was applied by the engine'))

    # THE CANDIDATE ARM'S WITNESS SET IS ITSELF CHECKED. A witness that reads
    # the vertex program's own far columns inverts on a working candidate, so
    # declaring one on a candidate arm would FAIL the arm precisely when the
    # prepass worked. This fires from the arm's OWN environment map, not its
    # name, and it is added unconditionally for candidate arms so that a
    # declaration defect is reported rather than inferred from a confusing
    # result. Arms that do not ask for the candidate are untouched by it.
    if is_candidate_arm(arm):
        declared = [x for x in (arm.get('witnesses') or [])
                    if x in PROGRAM_COLUMN_WITNESSES]
        res.append(verdict('TF_CANDIDATE_WITNESS_SET_VALID', not declared, True,
                           'this arm runs the clip prepass, so it may not declare a witness '
                           "that reads the vertex PROGRAM's far columns: those invert to zero "
                           'when the prepass works, so such a witness FAILS on success. Use '
                           'PROGRAM_COLUMNS_MUST_INVERT instead. EITHER SELECTION reaches the '
                           'prepass and SL_BG_TF_CLIP=1 is only one of them: an arm that names '
                           'NOTHING is the production selection, because the OPTIMIZED '
                           'RENDERING row is ON by default in a player session (see '
                           'is_candidate_arm). To declare a program-column witness, the arm '
                           'must take the prepass away and name the developer vertex-program '
                           'diagnostic instead - SL_BG_TF_CLIP=0 with SL_BG_GPU_XFORM=1',
                           'forbidden witnesses declared: %r; this arm was read as a '
                           'candidate from its own env map' % (declared,)))
    return res


def roll_up(witnesses):
    if any(w['verdict'] == FAIL for w in witnesses):
        return FAIL
    if any(w['verdict'] == NONE for w in witnesses):
        return NONE
    if not witnesses:
        return NONE
    return PASS


def transition_crossed(arms, peers):
    """The movement arm must CROSS the transition inside one run.

    Read from the consecutive census intervals across the arm's own capture
    bracket, not from a cumulative total and not from a sequence of separate
    processes. A run that was already past the transition before its first
    capture reads the same cumulative figure as one that crossed it; what
    distinguishes them is an interval that submitted no far geometry beside an
    interval that did.
    """
    on = [a for a in arms if a.get('role') == 'on' and a['phase'] == 'F']
    if not on:
        # The caller decides whether the movement phase had to be here at all:
        # in a subset this witness is simply not emitted, and in a full run the
        # absent phase is caught by MANDATORY_PHASES_PRESENT. Returning
        # NOT_COVERED here would make every subset INCOMPLETE on a phase it was
        # never asked to run.
        return verdict('F_TRANSITION_CROSSED', False, False,
                       'no movement-phase ON arm is in this record')
    best, detail, measured = False, [], 0
    for a in on:
        f = peers.get(a['name'])
        if f is None:
            continue
        iv = f.window_intervals()
        if len(iv) < 2:
            detail.append('%s: %d census interval(s) across its captures, so no crossing '
                          'can be read' % (a['name'], len(iv)))
            continue
        measured += 1
        pos = len([d for d in iv if d['far0_calls'] + d['far1_calls'] > 0])
        zero = len(iv) - pos
        detail.append('%s: %d of %d intervals submitted far geometry, %d submitted none'
                      % (a['name'], pos, len(iv), zero))
        if pos > 0 and zero > 0:
            best = True
    if measured == 0:
        return verdict('F_TRANSITION_CROSSED', False, False,
                       'no movement-phase ON arm produced two census reports across its '
                       'captures, so no within-run crossing was measured', '; '.join(detail))
    return verdict('F_TRANSITION_CROSSED', best, True,
                   'at least one movement arm must hold both an interval with no far '
                   'submission and an interval with one', '; '.join(detail))


def phases_present(record, arms):
    """Full qualification needs every mandatory phase, not the ones supplied."""
    want = record.get('mandatory_phases') or []
    have = sorted(set(a['phase'] for a in arms))
    missing = [p for p in want if p not in have]
    # An absent phase is an ABSENCE of measurement, which makes the batch
    # INCOMPLETE. It is not a FAIL - nothing about it was measured and found
    # wrong - and it is certainly not a pass on the arms that happened to run.
    return verdict('MANDATORY_PHASES_PRESENT', not missing, bool(want) and not missing,
                   'mandatory phases %r; this run carries %r; missing %r'
                   % (want, have, missing))


def cases_present(record, arms):
    """Full qualification needs every mandatory CASE, not one arm per phase.

    A phase is present as soon as one of its arms is; the CASES are the arms
    themselves. Two sources, both read from the record as it stands:

      * `mandatory_cases`, when the record declares the case list;
      * the other half of every arm that names a `pair` in a mandatory phase.
        A paired phase measures the DIFFERENCE between its halves - capture
        identity, the depth columns, the blended share - and one half alone
        cannot carry any of those readings whatever its own witnesses say.

    An absent case measured nothing, so this is NOT_COVERED (INCOMPLETE for
    the batch) rather than a FAIL; and it is never a pass on the half supplied.
    """
    want_phases = record.get('mandatory_phases') or []
    declared = record.get('mandatory_cases') or []
    names = set(a['name'] for a in arms)
    have_phases = set(a['phase'] for a in arms)
    missing_phases = [p for p in want_phases if p not in have_phases]
    missing_cases = [c for c in declared if c not in names]
    half_pairs = sorted('%s declares pair %r, absent here' % (a['name'], a.get('pair'))
                        for a in arms
                        if a['phase'] in want_phases and a.get('pair')
                        and a['pair'] not in names)
    # An absent case, like an absent phase, is an ABSENCE of measurement: the
    # batch is INCOMPLETE on it. It is not a FAIL - nothing about the missing
    # half was measured and found wrong - and it is never a pass on the half
    # that did run. With a whole phase missing nothing can be said about that
    # phase's cases either, so that is uncovered too.
    covered = (bool(want_phases) and not missing_phases
               and not missing_cases and not half_pairs)
    ok = covered
    return verdict('MANDATORY_CASES_PRESENT', ok, covered,
                   'declared cases %r; %d arm(s) present; missing cases %r; unpaired halves %r'
                   % (declared, len(arms), missing_cases, half_pairs),
                   'missing phases %r' % (missing_phases,))


def manual_read(out, record, arms):
    """Read manual.json once and sort every step into one of three bins.

    Returns (parse_error, absent, mispinned, failed) where

      absent     the step recorded nothing this gate can read: no entry, a
                 null / empty / PENDING outcome, or an outcome with no pinning
                 at all (null build, null arm, no integer frame). Nothing was
                 measured, so the batch is INCOMPLETE.
      mispinned  the step carries an outcome and a pinning that names ANOTHER
                 build or an arm outside this batch, or an outcome that is
                 neither PASS nor FAIL. That is a record about something else,
                 which is wrong rather than missing: FAIL.
      failed     the step is pinned to this batch and its outcome is FAIL. The
                 row failed where a person looked at it: FAIL.
    """
    p = Path(out) / 'manual.json'
    if not p.is_file():
        return 'no manual.json beside the arms; run --manual-template to write one, ' \
               'perform the steps and record each outcome', None, None, None
    try:
        doc = json.loads(p.read_text(encoding='utf-8-sig'))
    except ValueError as exc:
        return 'manual.json does not parse: %s' % (exc,), None, None, None
    want_build = (record.get('binaries') or {}).get('sightline')
    names = set(a['name'] for a in arms)
    steps = doc.get('steps') or {}
    absent, mispinned, failed = [], [], []
    for sid in MANUAL_IDS:
        e = steps.get(sid)
        if not isinstance(e, dict) or e.get('outcome') in (None, '', 'PENDING'):
            absent.append(sid)
            continue
        unpinned = []
        if not e.get('build_sha256'):
            unpinned.append('no build')
        if not e.get('arm'):
            unpinned.append('no arm')
        if not isinstance(e.get('frame'), int):
            unpinned.append('no integer frame')
        if unpinned:
            absent.append('%s (%s)' % (sid, ', '.join(unpinned)))
            continue
        if e.get('outcome') not in ('PASS', 'FAIL'):
            mispinned.append('%s outcome=%r is neither PASS nor FAIL' % (sid, e.get('outcome')))
            continue
        if want_build and e.get('build_sha256') != want_build:
            mispinned.append('%s is pinned to build %r, not this one' % (sid, e.get('build_sha256')))
            continue
        if e.get('arm') not in names:
            mispinned.append('%s names arm %r, which is not in this batch' % (sid, e.get('arm')))
            continue
        if e.get('outcome') == 'FAIL':
            failed.append('%s recorded FAIL on arm %r at frame %r: %s'
                          % (sid, e.get('arm'), e.get('frame'), e.get('note') or ''))
    return None, absent, mispinned, failed


def manual_outcomes(out, record, arms):
    """Every manual step must carry an EXPLICIT outcome pinned to this build.

    The checklist used to be appended to the result unconditionally, so a batch
    could read PASS with no step having been performed. An absent, null or
    unpinned outcome is an ABSENCE here, which makes the batch INCOMPLETE; an
    outcome pinned to another build or arm is a FAIL. What the recorded
    outcomes SAY is MANUAL_OUTCOMES_PASSED's reading, not this one's.
    """
    err, absent, mispinned, _failed = manual_read(out, record, arms)
    if err is not None:
        covered = 'does not parse' in err
        return verdict('MANUAL_OUTCOMES_RECORDED', False, covered, err)
    if absent:
        return verdict('MANUAL_OUTCOMES_RECORDED', False, False,
                       'no recorded, pinned outcome for %r' % (absent,),
                       '; '.join(mispinned[:4]))
    return verdict('MANUAL_OUTCOMES_RECORDED', not mispinned, True,
                   'every manual step carries an explicit outcome pinned to build, arm and frame',
                   '; '.join(mispinned[:6]))


def manual_passed(out, record, arms):
    """An outcome recorded FAIL makes the batch FAIL, ahead of any absence.

    A recorded FAIL is the strongest evidence the batch can hold about the row:
    the step was performed on this build and the row did not do what the step
    required. Rolling that up as "recorded, therefore satisfied" - which is
    what reading only the presence of an outcome does - turns the one
    measurement taken into a pass.

    PRECEDENCE, AND IT IS ORDERED. A recorded FAIL is a measurement; an absent
    or unpinned step is the absence of one. Reading the absence first downgrades
    the measurement: the batch reports INCOMPLETE because a DIFFERENT step was
    not recorded, while a step that was recorded says the row is broken. The
    measured outcome therefore outranks the absence, in this order:

      1. any step recorded FAIL and pinned to this batch -> FAIL
      2. otherwise any step absent, unpinned or mispinned -> not qualified
      3. otherwise every step PASS and pinned                -> PASS

    The absence is never waived by the FAIL either. MANUAL_OUTCOMES_RECORDED
    reads the absence on its own and still reports it, and the unrecorded steps
    are named in this witness's own detail, so the batch reads FAIL with the
    missing evidence still on the record.
    """
    err, absent, mispinned, failed = manual_read(out, record, arms)
    if err is not None:
        return verdict('MANUAL_OUTCOMES_PASSED', False, 'does not parse' in err,
                       'no readable manual outcomes: %s' % (err,))
    if failed:
        missing = ('; %d step(s) are additionally absent or unpinned (%r) and are reported '
                   'by MANUAL_OUTCOMES_RECORDED, which this FAIL does not waive'
                   % (len(absent) + len(mispinned), absent + mispinned)
                   if (absent or mispinned) else '')
        return verdict('MANUAL_OUTCOMES_PASSED', False, True,
                       'a recorded FAIL outranks an unrecorded step: %d step(s) read FAIL on '
                       'this build%s' % (len(failed), missing),
                       '; '.join(failed[:6]))
    if absent or mispinned:
        return verdict('MANUAL_OUTCOMES_PASSED', False, False,
                       'no step read FAIL, and not every step is recorded and pinned, so what '
                       'the recorded ones say cannot qualify the row (%d absent, %d mispinned)'
                       % (len(absent), len(mispinned)))
    return verdict('MANUAL_OUTCOMES_PASSED', True, True,
                   'every manual step is recorded, pinned and reads PASS; 0 read FAIL')


PERF_BUDGET_MS = 16.667          # tools/profiling/READINESS.md:45-47, 1080p60
PERF_RULES = ('abba-timing-screen/1', 'absolute-budget/1')


PERF_RAW_SCHEMA = 'sightline-frames/1'   # tools/profiling/analyze.py frame_report
# The verdict field each rule states and that is recomputed here. A rule whose
# verdict the row does not state established nothing: there is no claim to
# recompute against, so the row is not qualified rather than passed by default.
PERF_VERDICT_KEY = {'abba-timing-screen/1': 'timing_screen_passed',
                    'absolute-budget/1': 'budget_met'}
# `off` and `on` are printed to two decimals, so the aggregate recomputed from
# the samples may differ from the printed figure by at most half the last digit.
#
# PERF_PRINT_TOLERANCE_MS IS FOR HUMAN-PRINTED SUMMARIES ONLY, NEVER FOR A
# DECISION INPUT. Allowing it on a figure a verdict is computed from makes it
# wrong by up to half a printed digit, which is enough to invert it at a
# threshold. MEASURED: a run whose source really takes 16.670 ms, stated as
# 16.666 ms, differs by 0.004 ms, so the embedded summary passed the source
# comparison - and 16.666 then met the 16.667 ms budget that 16.670 exceeds. A
# marginal ABBA screen inverts the same way, because half a digit on each of
# four means moves a saving or a spread by up to 0.010 ms. Every aggregate,
# saving, spread, budget and screen below is therefore computed from the
# RECOMPUTED source reports; the embedded records supply identity only - arm,
# order, build, stage, schema and context.
PERF_PRINT_TOLERANCE_MS = 0.005 + 1e-9
# A provenance pin identifies the bytes a figure was read from, so it has to be
# a digest. A key that is absent, or present and not a sha256, pins nothing: it
# leaves the source unidentified, which is an absence of evidence rather than a
# contradiction of it.
PERF_PIN_RE = re.compile(r'^[0-9a-f]{64}$')


def _num(v):
    """A JSON number and nothing else.

    isinstance(True, int) is true in Python, so a JSON `true` passes a naive
    int/float test and is then read as 1. bool is therefore excluded BY TYPE
    before any value is read off it, which is the same guard
    tools/profiling/common.py number() and integer() apply.
    """
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _time_ms(v):
    """One frame time in milliseconds, or the reason the figure is not one.

    tools/profiling/common.py number() refuses booleans, non-numeric data,
    non-finite values and anything below its minimum. A frame interval is an
    elapsed duration, so the minimum applied here is strictly above zero: a
    mean of 0 ms describes no time passing and a negative one describes time
    that did not pass. json.loads accepts the non-standard NaN, Infinity and
    -Infinity tokens that common.load_json refuses outright, so a document read
    here can carry them and they are rejected by value as well as by type.
    """
    if isinstance(v, bool):
        return None, 'is the JSON boolean %r, not a time' % (v,)
    if not isinstance(v, (int, float)):
        return None, 'is %s, not a number' % (type(v).__name__,)
    f = float(v)
    if f != f:
        return None, 'is NaN'
    if f == float('inf') or f == float('-inf'):
        return None, 'is %r, which is not finite' % (f,)
    if f <= 0.0:
        return None, 'is %r, which is not a positive duration' % (f,)
    return f, None


def perf_raw(doc, arm_stage, want_build):
    """The row's RAW per-run measurements, qualified or refused.

    A figure is a measurement only when the record carrying it can be traced to
    a run of THIS batch: the frame-report schema this tree produces, the build
    these arms ran, an arm name the batch actually ran, the stage that arm ran
    on, at least two frame intervals - fewer is no sample a mean exists over,
    which is why analyze.py frame_report refuses them - and finite positive
    times. Those are tools/profiling/analyze.py compare_abba's own
    preconditions (schema, per-arm binding, figures recomputed instead of read
    off a summary) applied to the records the row embeds rather than to four
    loose report files.

    Returns (runs, absent, wrong). `absent` lists reasons no readable sample is
    there, which leaves the row UNQUALIFIED; `wrong` lists contradictions among
    figures that ARE readable, which FAIL it. Every record is read rather than
    the first bad one returned, so a row carrying five different bad figures
    names five.
    """
    raw = doc.get('raw')
    if not isinstance(raw, list) or not raw:
        return None, ['the row carries no raw measurements (`raw` is %s)'
                      % (type(raw).__name__,)], []
    runs, absent, wrong = [], [], []
    for i, r in enumerate(raw):
        if not isinstance(r, dict):
            absent.append('raw[%d] is not a measurement record' % i)
            continue
        bad = []
        if r.get('schema') != PERF_RAW_SCHEMA:
            bad.append('schema is %r, not the %r this tree produces'
                       % (r.get('schema'), PERF_RAW_SCHEMA))
        for key in ('mean_ms', 'p99_ms_nearest_rank'):
            why = _time_ms(r.get(key))[1]
            if why:
                bad.append('%s %s' % (key, why))
        iv = r.get('intervals')
        if not (_num(iv) and float(iv).is_integer() and iv >= 2):
            bad.append('reports %r frame intervals, so no mean exists over them' % (iv,))
        if want_build and r.get('build_sha256') != want_build:
            bad.append('is pinned to build %r, not the %r these arms ran'
                       % (r.get('build_sha256'), want_build))
        if r.get('arm') not in arm_stage:
            bad.append('names arm %r, which this batch did not run' % (r.get('arm'),))
        elif r.get('stage') != arm_stage[r['arm']]:
            bad.append('declares stage %r while arm %r ran on %r'
                       % (r.get('stage'), r.get('arm'), arm_stage[r['arm']]))
        if bad:
            absent.append('raw[%d] %s' % (i, '; '.join(bad)))
            continue
        runs.append(r)
    if not runs:
        absent.append('no raw record in the row is a readable per-arm measurement of this '
                      'build, so the row has nothing to be recomputed from')
        return None, absent, wrong
    return runs, absent, wrong


# ---- the raw presentation sources, opened and recomputed -------------------
#
# A per-arm record is a SUMMARY. Validating its schema, build, arm, stage and
# numbers says only that the summary is well formed, and a well-formed summary
# of nothing is still nothing: four records whose every source.path named a
# file that does not exist, with an invented sha256, were measured to PASS,
# because performance-row.json was the only file ever opened. Embedded
# frame-report metadata is not proof of timing.
#
# So each source is OPENED, PINNED and RECOMPUTED here, through the module this
# tree already uses for it rather than a second implementation:
#
#   tools/profiling/analyze.py frame_report(path, lo, hi, step) pins the file
#   through common.pin, which resolves it with strict=True - so a missing
#   source RAISES rather than passing - parses the six-column presentation
#   format, requires the regular frames to land exactly on the declared grid,
#   and derives the mean and the nearest-rank p99 from the intervals. It ends
#   by calling same_pin, so a file edited while it was being read is caught.
#
# analyze.py does `from common import ...` with no sys.path setup of its own,
# so its directory has to be on the path before it will import at all.

PROFILING_DIR = Path(__file__).resolve().parents[1] / 'profiling'


def _load_analyze():
    """The profiling module, or the reason it is unavailable.

    An unavailable module is NOT COVERED rather than failed: nothing was
    measured and nothing was contradicted. A module that loads and then
    refuses a source is a different answer entirely, and the caller keeps the
    two apart.
    """
    path = PROFILING_DIR / 'analyze.py'
    if not path.is_file():
        return None, 'tools/profiling/analyze.py is not present'
    try:
        import importlib.util
        if str(PROFILING_DIR) not in sys.path:
            sys.path.insert(0, str(PROFILING_DIR))
        spec = importlib.util.spec_from_file_location('sl_perflive_analyze', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    except Exception as exc:                      # noqa: BLE001 - reported, not raised
        return None, 'tools/profiling/analyze.py could not be loaded: %s' % (exc,)
    for name in ('frame_report', 'ProfileError'):
        if not hasattr(module, name):
            return None, 'tools/profiling/analyze.py exposes no %s' % (name,)
    return module, None


# The summary fields a per-arm record shares with a recomputed frame report.
# `identities` and `digests` are recomputed too but are not expected in the
# record, so they are compared across the four runs rather than against it.
PERF_RECOMPUTED_FIELDS = ('intervals', 'mean_ms', 'p99_ms_nearest_rank')


def perf_sources(runs, analyze):
    """Recompute every run's source. Returns (reports, bad, unpinned).

    `reports` is one recomputed frame report per run, in run order, or None if
    any could not be produced. THE REPORTS ARE THE DECISION INPUTS: the caller
    computes every aggregate and verdict from them and never from the embedded
    summaries compared here. Every disagreement is collected rather than
    returning at the first, so a row with four bad sources says so four times
    instead of once.

    `bad` lists contradictions - a file that does not produce what the row says
    it does - which FAIL the row. `unpinned` lists sources whose provenance pin
    is absent or is not a digest; those identify no bytes, so they leave the row
    UNQUALIFIED instead.
    """
    reports, bad, unpinned = [], [], []
    for i, r in enumerate(runs):
        src = r.get('source')
        win = r.get('window')
        if not isinstance(src, dict) or not src.get('path'):
            bad.append('raw[%d] names no source file, so its figures cannot be recomputed' % i)
            continue
        if not isinstance(win, dict) or not all(_num(win.get(k)) for k in ('lo', 'hi', 'step')):
            bad.append('raw[%d] declares no numeric lo/hi/step window, so there is no grid to '
                       'recompute its figures on' % i)
            continue
        try:
            report = analyze.frame_report(src['path'], win['lo'], win['hi'], win['step'])
        except Exception as exc:                  # noqa: BLE001 - the reason is the evidence
            bad.append('raw[%d] source %r could not be recomputed: %s'
                       % (i, src.get('path'), exc))
            continue
        # THE PIN IS COMPARED FIELD BY FIELD, so a sha256 that does not belong
        # to the bytes on disk is named as such rather than merely differing.
        #
        # A PIN IS A DIGEST OR IT IS NOT A PIN. `if key in src` skipped the
        # comparison whenever the key was simply missing, so a row could cite a
        # file, have it recompute, and never identify the bytes it was read
        # from - the comparison it passed was one it had opted out of. An
        # absent or malformed hash is therefore unqualified, and only a
        # well-formed digest is compared and able to contradict.
        got = report.get('source') or {}
        claimed = src.get('sha256')
        if not isinstance(claimed, str) or not PERF_PIN_RE.match(claimed):
            unpinned.append('raw[%d] cites source %r with no well-formed sha256 pin (%r), so '
                            'the bytes its figures were read from are not identified'
                            % (i, src.get('path'), claimed))
        elif claimed != got.get('sha256'):
            bad.append('raw[%d] pins source sha256 %r; the file on disk has %r'
                       % (i, claimed, got.get('sha256')))
        if 'bytes' in src and src['bytes'] != got.get('bytes'):
            bad.append('raw[%d] pins source bytes %r; the file on disk has %r'
                       % (i, src['bytes'], got.get('bytes')))
        if report.get('window') != {'lo': win['lo'], 'hi': win['hi'], 'step': win['step']}:
            bad.append('raw[%d] declares window %r; the recomputation used %r'
                       % (i, win, report.get('window')))
        for key in PERF_RECOMPUTED_FIELDS:
            if key not in r:
                continue
            stated, actual = r[key], report.get(key)
            if not _num(stated) or not _num(actual) or not math.isfinite(float(stated)) \
                    or not math.isfinite(float(actual)):
                bad.append('raw[%d] states %s=%r against a recomputed %r; one of them is no '
                           'finite figure, so they cannot be compared'
                           % (i, key, stated, actual))
            elif key == 'intervals':
                if int(stated) != int(actual):
                    bad.append('raw[%d] states %d %s; its own source yields %d'
                               % (i, int(stated), key, int(actual)))
            elif abs(float(stated) - float(actual)) > PERF_PRINT_TOLERANCE_MS:
                # A PRINTED SUMMARY, CLASSIFIED - NOT A VERDICT INPUT. This is
                # the embedded figure beside the row, read to the two decimals
                # a human reads, so half a printed digit of drift is tolerated
                # here and reported beyond that. It decides nothing: `report`
                # is what the caller computes the aggregate, budget and screen
                # from, so a summary that drifts within that slack can no
                # longer carry a threshold with it.
                bad.append('raw[%d] states %s=%r ms; recomputed from its own source that is '
                           '%.4f ms' % (i, key, stated, float(actual)))
        reports.append(report)
    if len(reports) != len(runs):
        return None, bad, unpinned
    # ONE WORKLOAD, OR IT IS NOT A PAIRED COMPARISON. analyze.py compare_abba
    # requires the selected window and identities to agree across the four
    # reports; four runs over different frames are four different experiments
    # and their difference is not a saving.
    first = reports[0]
    for i, report in enumerate(reports[1:], 1):
        if report.get('window') != first.get('window'):
            bad.append('raw[%d] was recomputed over window %r, not raw[0]\'s %r'
                       % (i, report.get('window'), first.get('window')))
        if report.get('identities') != first.get('identities'):
            bad.append('raw[%d] selected different workload identities from raw[0], so the two '
                       'are not the same workload measured twice' % (i,))
    return reports, bad, unpinned


def performance_row(out, record, arms):
    """The performance row, BOUND to this build, these arms and its own samples.

    What makes a row readable is that it can be recomputed. A key with a
    truthy string in it cannot be: it names no run, no workload and no rule,
    so it is an absence of measurement and not a pass. The rules recognised
    here are the two this tree already defines (see the module docstring).

    NO FIELD OF THE ROW IS EVIDENCE OF ITSELF, AND NEITHER IS A SUMMARY
    EMBEDDED BESIDE IT. Every aggregate, saving, spread, budget reading and
    screen verdict is computed from the RECOMPUTED frame reports of the pinned
    source files; the embedded raw records supply identity only - arm, order,
    build, stage, schema and interval count. That split is load-bearing: the
    embedded summaries are compared to their sources within
    PERF_PRINT_TOLERANCE_MS, so a row whose source really takes 16.670 ms may
    state 16.666 ms and pass that comparison, and if a verdict then read the
    summary the row would meet a 16.667 ms budget it exceeds.

    The per-arm aggregate the row summarises, the rule it names and the
    absolute budget are each recomputed from those sources, and its stated
    verdict is read only to be compared against that recomputation.
    `budget_met` in particular is never taken at its word: the 16.667 ms
    reading is derived from the sources, with no tolerance, so a row that
    asserts the budget was met while the files it cites exceed it is a
    fabricated gain and FAILS. A sample set that is absent, malformed
    or bound to another build, arm or stage leaves the row UNQUALIFIED
    (NOT_COVERED), which is not a pass either; and a rule whose verdict the row
    does not state established nothing, so it is unqualified rather than
    assumed.
    """
    p = Path(out) / 'performance-row.json'
    if not p.is_file():
        return verdict('PERFORMANCE_ROW_PRESENT', False, False,
                       'no performance-row.json beside the arms')
    try:
        doc = json.loads(p.read_text(encoding='utf-8-sig'))
    except ValueError as exc:
        return verdict('PERFORMANCE_ROW_PRESENT', False, True,
                       'performance-row.json does not parse: %s' % (exc,))
    want_build = (record.get('binaries') or {}).get('sightline')
    arm_stage = dict((a['name'], a['stage']) for a in arms)
    absent, wrong = [], []
    for k in ('row', 'stage', 'build_sha256'):
        if not doc.get(k):
            absent.append('%s is absent' % k)
    # EXACTLY TWO DISTINCT ARMS, BOUND TO THE ROLES THE BATCH ACTUALLY RAN.
    # A two-arm rule reports the difference between two arms, so a row naming
    # one arm twice, or three arms, or an arm this batch never ran, has no
    # two-arm quantity to report. The order is load-bearing: measured[0] is
    # the OFF role the row's `off` summarises and measured[1] the ON role, and
    # that is checked against each arm's OWN recorded environment rather than
    # taken from the name.
    arm_env = dict((a['name'], (a.get('env') or {})) for a in arms)
    measured = doc.get('arms')
    if not isinstance(measured, list) or len(measured) != 2:
        absent.append('the row must name exactly two measured arms for a two-arm rule; it '
                      'names %r' % (measured,))
        measured = []
    elif measured[0] == measured[1]:
        wrong.append('the row names arm %r as both of its two measured arms, so it reports no '
                     'difference between two arms' % (measured[0],))
        measured = []
    else:
        off_batch = [a for a in measured if a not in arm_stage]
        if off_batch:
            wrong.append('the row names arm(s) %r, which are not in this batch' % (off_batch,))
            measured = []
        else:
            for role, index, want in (('off', 0, '0'), ('on', 1, '1')):
                got = arm_env.get(measured[index], {}).get('SL_PERF_OPT')
                if got is None:
                    absent.append('arm %r recorded no SL_PERF_OPT, so it cannot be read as the '
                                  '%s role the row assigns it' % (measured[index], role))
                elif str(got) != want:
                    wrong.append('the row assigns arm %r the %s role, but that arm ran with '
                                 'SL_PERF_OPT=%r' % (measured[index], role, got))
    runs, raw_absent, raw_wrong = perf_raw(doc, arm_stage, want_build)
    absent += raw_absent
    wrong += raw_wrong
    metrics = doc.get('metrics')
    rule = (metrics or {}).get('rule') if isinstance(metrics, dict) else None
    if not isinstance(metrics, dict) or not rule:
        absent.append('the row carries no metrics naming the rule that was applied')
    elif rule not in PERF_RULES:
        absent.append('the metrics name rule %r, which is not one this tree defines (%r), so '
                      'the figure cannot be read' % (rule, list(PERF_RULES)))
    # None means "not recomputed", which is never the same as a measured pass:
    # with no reports there are no decision inputs, so no aggregate, budget or
    # screen is computed at all and the row is unqualified or contradicted on
    # the source reasons already collected.
    budget_met = None
    reports = None
    actual = None
    over = []
    if runs is not None:
        if want_build and doc.get('build_sha256') != want_build:
            wrong.append('the row is pinned to build %r, not the %r these arms ran'
                         % (doc.get('build_sha256'), want_build))
        # THE SOURCES, OPENED AND RECOMPUTED - FIRST, because everything below
        # is decided from them. Until this point every figure has been checked
        # against another figure in the same document; this is where the
        # document is checked against the files it cites, and where the numbers
        # the verdicts are computed from come from.
        analyze, why = _load_analyze()
        if analyze is None:
            # The context is unavailable, so nothing was measured and nothing
            # was contradicted: unqualified, which is not a pass. There are
            # also no decision inputs, and the embedded summaries are NOT a
            # fallback for them - falling back to the figures the row reports
            # about itself is the whole defect being removed here.
            absent.append('the raw sources could not be recomputed, so the row\'s figures rest '
                          'on their own summaries: %s' % (why,))
        else:
            reports, src_bad, src_unpinned = perf_sources(runs, analyze)
            # A source the row cites and cannot produce is a readable
            # contradiction of a claim the row makes, not a gap in it. A source
            # it cites without a digest identifies no bytes, so that is a gap.
            wrong += src_bad
            absent += src_unpinned
        # EVERY ARM THE ROW AGGREGATES MUST CARRY SAMPLES OF ITS OWN. A mean
        # taken over one arm says nothing about the other, which is the whole
        # quantity a two-arm row exists to report, so an arm with no raw record
        # is an unmeasured arm rather than a measured zero. WHICH arm a run
        # measured is identity, not a figure, so it is read from the record -
        # that is what the embedded metadata is still for.
        runs_of = {}
        for i, r in enumerate(runs):
            runs_of.setdefault(r['arm'], []).append(i)
        for a in measured:
            if a not in runs_of:
                absent.append('the row aggregates arm %r and carries no raw measurement of it'
                              % (a,))
        # THE ACTUAL FIGURES: one (mean, nearest-rank p99) per run, taken from
        # the recomputation of that run's own source and from nowhere else.
        if reports is not None:
            actual = [(float(rep['mean_ms']), float(rep['p99_ms_nearest_rank']))
                      for rep in reports]
            per_arm = dict((a, [actual[i][0] for i in idx]) for a, idx in runs_of.items())
            # THE STATED AGGREGATE, RECOMPUTED. `off` and `on` are the row's own
            # PRINTED summary of the per-arm mean - two decimals for a human to
            # read - so this is the one comparison PERF_PRINT_TOLERANCE_MS
            # applies to. It is still compared against the sources' own means
            # and not against the embedded ones.
            for key, index in (('off', 0), ('on', 1)):
                stated = doc.get(key)
                if stated is None or len(measured) <= index:
                    continue
                sample = per_arm.get(measured[index])
                if not sample:
                    continue
                want_mean = sum(sample) / float(len(sample))
                m = re.match(r'^\s*(-?\d+(?:\.\d+)?)\s*ms\s*$', str(stated))
                if not m:
                    wrong.append('the row states %s=%r, which is no millisecond figure its own '
                                 'samples can be compared against' % (key, stated))
                elif abs(float(m.group(1)) - want_mean) > PERF_PRINT_TOLERANCE_MS:
                    wrong.append('the row states %s=%r; the mean of its own %d raw run(s) of '
                                 'arm %r, recomputed from their sources, is %.4f ms'
                                 % (key, stated, len(sample), measured[index], want_mean))
            # THE ABSOLUTE BUDGET, FROM THE SOURCES, NEVER READ OFF THE ROW AND
            # NEVER OFF AN EMBEDDED SUMMARY. tools/profiling/READINESS.md:45-47:
            # mean and nearest-rank p99 each at most 16.667 ms. No tolerance is
            # applied: a threshold read with half a printed digit of slack is a
            # different threshold, and an ACTUAL over-budget must never become
            # a PASS.
            over = [(runs[i]['arm'], actual[i][0], actual[i][1]) for i in range(len(runs))
                    if actual[i][0] > PERF_BUDGET_MS or actual[i][1] > PERF_BUDGET_MS]
            budget_met = not over
    if runs is not None and rule in PERF_RULES:
        computed = None
        if rule == 'abba-timing-screen/1':
            # tools/profiling/analyze.py compare_abba, recomputed here from the
            # row's own means rather than read off its summary.
            # THE LABELS ARE NOT THE EVIDENCE. `order` is free text in the
            # document, and the savings are computed POSITIONALLY - run 0
            # against run 1, run 3 against run 2 - so whoever writes the
            # labels decides which measurements count as A and which as B.
            # MEASURED: four runs whose actual arms were OFF,OFF,ON,ON, with
            # means 10,8,8,10 and every other field valid, relabelled A,B,B,A,
            # yielded savings [2,2] and spreads [0,0] and PASSED - while both
            # arms had in truth averaged 9 ms. The arms carry the roles, so
            # the ACTUAL arm sequence is required to be the paired pattern as
            # well as the labels. Both, not either.
            order = [r.get('order') for r in runs]
            seq = [r.get('arm') for r in runs]
            want_seq = None
            if len(measured) == 2:
                want_seq = [measured[0], measured[1], measured[1], measured[0]]
            if order != ['A', 'B', 'B', 'A']:
                wrong.append('abba-timing-screen/1 requires four runs in order A,B,B,A; the '
                             'row carries %r' % (order,))
            elif want_seq is None:
                pass            # the two measured arms are already reported above
            elif seq != want_seq:
                wrong.append('abba-timing-screen/1 pairs the first run with the last and the '
                             'second with the third, so its four runs must be arms %r; they '
                             'are actually %r. The A,B,B,A labels agree, which is exactly why '
                             'they are not evidence: they are free text, and the saving is '
                             'computed from the run positions' % (want_seq, seq))
            elif actual is None:
                pass            # no sources recomputed, so there is nothing to screen
            else:
                # THE SOURCES' OWN MEANS, NOT THE EMBEDDED ONES. MEASURED: four
                # runs whose true means are 10.000, 9.996, 9.990, 10.000 give
                # savings [0.004, 0.010] and spreads [0.000, 0.006], so the
                # screen FAILS - while the same runs summarised to within half
                # a printed digit as 10.004, 9.992, 9.992, 10.004 give savings
                # [0.012, 0.012] and spreads [0.000, 0.000] and PASS. Half a
                # digit on each of four means moves a saving or a spread by up
                # to 0.010 ms, which is more than a marginal screen's margin.
                mean = [actual[i][0] for i in range(4)]
                savings = [mean[0] - mean[1], mean[3] - mean[2]]
                spreads = [abs(mean[0] - mean[3]), abs(mean[1] - mean[2])]
                computed = min(savings) > max(spreads)
                for key, got in (('paired_savings_ms', savings), ('repeat_spreads_ms', spreads)):
                    stated = metrics.get(key)
                    if stated is None:
                        continue
                    # FINITE, OR THERE IS NO COMPARISON. Every comparison with
                    # a NaN is false, so `abs(stated - computed) > tol` lets a
                    # NaN through as agreement. The figure is therefore
                    # required to be finite BEFORE it is compared.
                    #
                    # A SAVING MAY LEGITIMATELY BE NEGATIVE - the measured arm
                    # can be the slower one - so no positive or duration rule
                    # is imposed here. The screen below decides what the signs
                    # mean; this only decides whether the numbers are numbers.
                    if (not isinstance(stated, list) or len(stated) != 2
                            or not all(_num(x) and math.isfinite(float(x)) for x in stated)
                            or not all(math.isfinite(y) for y in got)
                            or any(abs(float(x) - y) > 1e-9 for x, y in zip(stated, got))):
                        wrong.append('the row states %s=%r; recomputed from the means its own '
                                     'raw sources produce that is %r' % (key, stated, got))
        else:
            computed = budget_met
        # THE ROW MUST STATE THE VERDICT OF THE RULE IT NAMES, AS A BOOLEAN. A
        # row that states none recorded no verdict, and a truthy string is not
        # one: either way there is no claim to recompute against, which is an
        # absence of measurement. A stated verdict is then compared against the
        # recomputation and never accepted as it stands.
        vkey = PERF_VERDICT_KEY[rule]
        said = metrics.get(vkey)
        if not isinstance(said, bool):
            absent.append('the row names rule %r and states no boolean %s (%s=%r), so its '
                          'verdict is unrecorded and nothing can be recomputed against it'
                          % (rule, vkey, vkey, said))
        elif computed is not None and said != computed:
            wrong.append('the row states %s=%r under rule %r; recomputed from its own %d raw '
                         'run(s) that is %r%s'
                         % (vkey, said, rule, len(runs), computed,
                            ' - the run(s) %r exceed the %.3f ms budget'
                            % (over, PERF_BUDGET_MS) if vkey == 'budget_met' and over else ''))
        # A row framed by one rule may still assert the absolute budget. The
        # 16.667 ms reading applies to the figures, not to the rule that framed
        # them, so it is recomputed wherever it is claimed.
        if vkey != 'budget_met' and metrics.get('budget_met') is not None \
                and budget_met is not None:
            also = metrics.get('budget_met')
            if not isinstance(also, bool) or also != budget_met:
                wrong.append('the row states budget_met=%r; recomputed from the sources of its '
                             'own %d raw run(s) that is %r (over budget: %r)'
                             % (also, len(runs), budget_met, over))
    if absent:
        return verdict('PERFORMANCE_ROW_PRESENT', False, False,
                       'the performance row measured nothing readable: %s'
                       % ('; '.join(absent[:6]),), '; '.join(wrong[:4]))
    # THE DETAIL MUST NOT READ AS A PASS WHEN THE VERDICT IS A FAILURE. The
    # summary sentence used to be written once and reused for both outcomes,
    # so a contradicted row was reported as "every stated figure recomputed
    # here from those runs" with the contradictions relegated to the excerpt.
    # A reader who takes the detail at its word is then misled by it, which is
    # the same defect as a provenance line that disagrees with the provenance.
    if wrong:
        return verdict('PERFORMANCE_ROW_PRESENT', False, True,
                       'the row binds build %r, arms %r, %d raw run(s) and rule %r, and %d of '
                       'its claims did NOT survive recomputation against its own runs and '
                       'sources' % (doc.get('build_sha256'), measured, len(runs or []), rule,
                                    len(wrong)),
                       '; '.join(wrong[:6]))
    return verdict('PERFORMANCE_ROW_PRESENT', True, True,
                   'the row binds build %r, arms %r, %d raw run(s) and rule %r, every stated '
                   'figure recomputed here from those runs\' own pinned raw sources rather '
                   'than from the summaries beside them; those sources %s the %.3f ms budget'
                   % (doc.get('build_sha256'), measured, len(runs or []), rule,
                      'meet' if budget_met else 'do NOT meet', PERF_BUDGET_MS))


def do_check(out, scope_override=None):
    out = Path(out)
    record = json.loads((out / 'arms.json').read_text(encoding='utf-8-sig'))
    if record.get('schema') != SCHEMA_IN:
        print('perflive-check: arms.json schema %r, expected %r'
              % (record.get('schema'), SCHEMA_IN), file=sys.stderr)
        return 2
    arms = record['arms']
    scope = scope_override or record.get('scope') or 'full'

    peers = {}
    for a in arms:
        run = a.get('run') or {}
        peers[a['name']] = Facts(read_text(run.get('stderr')) if run.get('stderr') else None)
    # The far-admitted reference arm the guard arm compares its single-range
    # column against, named by the record rather than hard-coded.
    census_ref = peers.get(record.get('census_reference_arm'))

    rows = []
    for a in arms:
        ws = check_arm(out, a, peers[a['name']], peers, census_ref, record)
        rows.append({'name': a['name'], 'phase': a['phase'], 'stage': a['stage'],
                     'pose': a['pose']['kind'], 'pose_source': a['pose'].get('source'),
                     'verdict': roll_up(ws), 'witnesses': ws})

    # The movement witness is emitted only when the movement phase is in this
    # record. A subset that was never asked to run it must not be held
    # INCOMPLETE by it; a full run that omitted it is caught by
    # MANDATORY_PHASES_PRESENT instead.
    batch = []
    if any(a['phase'] == 'F' for a in arms):
        batch.append(transition_crossed(arms, peers))
    gate = []
    if scope == 'full':
        gate = [phases_present(record, arms), cases_present(record, arms),
                manual_outcomes(out, record, arms), manual_passed(out, record, arms),
                performance_row(out, record, arms)]

    all_v = [r['verdict'] for r in rows] + [b['verdict'] for b in batch] + [g['verdict'] for g in gate]
    status = PASS
    if FAIL in all_v:
        status = FAIL
    elif NONE in all_v:
        status = INCOMPLETE

    declared_nc = record.get('not_covered') or []
    full_qual = (status == PASS and scope == 'full' and not declared_nc)
    if scope == 'full':
        acceptance = ('FULL QUALIFICATION' if full_qual else
                      'NOT full qualification: status %s, %d declared NOT_COVERED case(s)'
                      % (status, len(declared_nc)))
    else:
        acceptance = ('SUBSET DIAGNOSTIC SUCCESS - the arms supplied passed. This is NOT '
                      'full acceptance of the row: the phases not run were not evaluated.'
                      if status == PASS else
                      'SUBSET DIAGNOSTIC %s - and not full acceptance either way.' % status)

    result = {
        'schema': 'sightline-perflive-check/2',
        'status': status,
        'scope': scope,
        'full_qualification': full_qual,
        'acceptance': acceptance,
        'qualification': "parsed from each arm's own stderr and its own captures; "
                         'NOT_COVERED means the witness line was absent, which is not a pass; '
                         'the WINDOW_* readings are deltas across the census bracket around '
                         "each arm's own captures, not terminal cumulative counters",
        'inputs': [{'path': str(out / 'arms.json'), 'sha256': sha256(out / 'arms.json')}],
        'bound': {'binaries': record.get('binaries'), 'tools': record.get('tools'),
                  'source': record.get('source'), 'selection': record.get('selection'),
                  'pose_set': record.get('pose_set'), 'asset_roots': record.get('asset_roots'),
                  'seed_hex': record.get('seed_hex'),
                  'scratch_config_sha256': record.get('scratch_config_sha256'),
                  'pinned_eeprom': record.get('pinned_eeprom'),
                  'pad_script': record.get('pad_script')},
        'arms': rows,
        'batch_witnesses': batch,
        'qualification_gates': gate,
        'declared_not_covered': declared_nc,
        'manual_checklist': MANUAL,
    }
    (out / 'check.json').write_text(json.dumps(result, indent=2) + '\n',
                                    encoding='utf-8', newline='\n')
    for r in rows:
        print('%-20s %-12s %s' % (r['name'], r['verdict'],
                                  '; '.join('%s=%s' % (w['name'], w['verdict'])
                                            for w in r['witnesses'] if w['verdict'] != PASS)))
    for b in batch + gate:
        print('%-20s %-12s %s' % (b['name'], b['verdict'], b['detail']))
    for nc in declared_nc:
        print('%-20s %-12s %s' % (nc.get('name', '?'), NONE, nc.get('reason', '')))
    print('scope %s  full_qualification %s' % (scope, full_qual))
    print('status %s' % status)
    return 0 if status == PASS else 1


def do_select(out):
    """Pick the stages for the dependent phases by measurement, never by name.

    A counter alone is not a selection. A candidate is eligible only if its own
    run ALSO exited zero, was not killed on the timeout, reported the pump's
    liveness for the frames it was given, confirmed the presentation-derived
    asset set the batch declared, bound the asset ROOT the batch bound rather
    than the player-data ladder, loaded the stage item it declared, echoed the
    pinned RNG state EXACTLY and took it explicitly, and saw no shader or
    program error. A dark ORIGINAL run with a nonzero counter from some other
    path is not a far stage. These are the acquisition witnesses the arms
    themselves are held to (FAIL_ON_ABSENCE above); a selection made on
    anything weaker chooses the stage every later phase is measured on.

    AND AN UNPROVED ROLE IS REFUSED, NEVER SUBSTITUTED. The reduction and the
    skydome each need evidence of their OWN, and the far candidate is not it.
    MEASURED, 2026-11: a discovery run selected `reduction = runway` while that
    run's own fogless figure was null - the #81 line was never printed, so the
    stage's fogless coverage was not measured at all - and its own reduction
    report read 0 reduced, refused for statistics. The role was filled by the
    far candidate because no candidate carried fogless frames, and the
    dependent phase then measured a stage nothing had qualified. A role with no
    evidence is NOT_COVERED and its phase must not run.
    """
    out = Path(out)
    record = json.loads((out / 'phase-acq.json').read_text(encoding='utf-8-sig'))
    arms = record['arms'] if isinstance(record, dict) else record
    seen = []
    for a in arms:
        run = a.get('run') or {}
        f = Facts(read_text(run.get('stderr')) if run.get('stderr') else None)
        expect = a.get('expect') or {}
        want_item = expect.get('stage_item')
        want_set = expect.get('presentation_set', 'xbla')
        reasons = []
        if run.get('exit_code') != 0:
            reasons.append('exit=%r' % (run.get('exit_code'),))
        if run.get('timed_out'):
            reasons.append('timed out')
        if f.survived != int(a['frames']):
            reasons.append('liveness survived=%r of %r' % (f.survived, a['frames']))
        if f.wa_set != want_set:
            reasons.append('world-asset set=%r, declared %r' % (f.wa_set, want_set))
        want_root = (expect.get('worldasset_root') or '').replace('\\', '/').rstrip('/').lower()
        got_root = (f.wa_root or '').replace('\\', '/').rstrip('/').lower()
        if not want_root or got_root != want_root or f.wa_root_from != 'SL_WORLDASSET_ROOT':
            reasons.append('asset root read=%r from=%r, bound %r'
                           % (got_root, f.wa_root_from, want_root))
        if want_item and not any(p == want_item for _c, p in f.wa_loaded):
            reasons.append('stage item %r not in LOADED %r' % (want_item, [p for _c, p in f.wa_loaded][:6]))
        want_seed = (a.get('seed_hex') or '').lower()
        if f.rng_how != 'explicit SL_RNG_SEED' or not want_seed or f.rng_state != want_seed:
            reasons.append('RNG state=%r how=%r, pinned %r' % (f.rng_state, f.rng_how, want_seed))
        if f.gpu_bad is not None:
            reasons.append('the driver refused the vertex program: %s' % (f.gpu_bad.group(0),))
        seen.append({
            'stage': a['stage'], 'stageNum': a['stage_number'], 'arm': a['name'],
            'eligible': not reasons, 'ineligible_because': reasons,
            'far_frames': f.far_frames, 'far0_calls': getattr(f, 'far0_calls', None),
            'far1_calls': getattr(f, 'far1_calls', None), 'sky_frames': f.sky_frames,
            'fogless_frames': f.fogless_frames, 'pano_frames': f.pano_frames,
            'far_verts': f.far_verts,
            'bg_frames': getattr(f, 'bg_frames', None), 'carriers': f.f474,
            'gpu_program_ready': f.gpu_ready,
            'reduced': f.reduced_max, 'unsound': f.unsound_max,
            'reduction_reports': len(f.red_events),
            'pose_kind': a['pose']['kind'], 'pose_source': a['pose'].get('source'),
            'pose_mark_sha256': a['pose'].get('mark_sha256'),
        })

    def score_far(r):
        if not r['eligible'] or not r['gpu_program_ready']:
            return -1
        if not r['far_frames'] or not r['far0_calls'] or not r['far1_calls']:
            return -1
        return r['far0_calls'] + r['far1_calls']

    ok = [r for r in seen if score_far(r) > 0]
    if not ok:
        print('perflive-check: no eligible candidate submitted far geometry through the GPU '
              'transform.', file=sys.stderr)
        print('  Nothing to select. The dependent phases MUST NOT run: there is no stage and '
              'no pose for them.', file=sys.stderr)
        for r in seen:
            print('  %-10s eligible=%s %s' % (r['stage'], r['eligible'],
                                              '; '.join(r['ineligible_because'])), file=sys.stderr)
        (out / 'selection-failed.json').write_text(
            json.dumps({'schema': 'sightline-perflive-select/2', 'status': 'NOT_COVERED',
                        'reason': 'no eligible candidate submitted far geometry',
                        'candidates': seen}, indent=2) + '\n', encoding='utf-8', newline='\n')
        return 1
    far = max(ok, key=score_far)

    # The reduction needs a FOGLESS walk with no panorama: the predicate admits
    # only fogless walks (src/gfx/sl_gfx_dl.c:18388) and refuses a panorama
    # walk outright (:18385). A candidate whose fogless figure is NULL was not
    # measured on that term at all, and an unsound draw disqualifies the stage
    # outright. There is no fallback: a role with no candidate is NOT_COVERED.
    elig = [r for r in seen if r['eligible']]
    red = [r for r in elig
           if r['fogless_frames'] is not None and r['fogless_frames'] > 0
           and (r['bg_frames'] or 0) > 0 and not (r['pano_frames'] or 0)
           and (r['unsound'] is None or r['unsound'] == 0)]
    reduction = max(red, key=lambda r: r['bg_frames'] or 0) if red else None

    # The carrier marker needs a stage that actually composed a skydome. A null
    # or zero skydome figure is not a skydome stage, and the far candidate is
    # not a substitute for one; the prod carrier is a preference among proved
    # candidates, since the carrier is chosen by the dependent arm's own switch.
    sky = [r for r in elig if r['sky_frames'] is not None and r['sky_frames'] > 0]
    prod = [r for r in sky if 'prod' in (r['carriers'] or [])]
    pool = prod or sky
    skydome = max(pool, key=lambda r: r['sky_frames'] or 0) if pool else None

    def role(r, because):
        if r is None:
            return {'stage': None, 'stageNum': None, 'status': 'NOT_COVERED',
                    'because': because}
        return {'stage': r['stage'], 'stageNum': r['stageNum'], 'status': 'SELECTED',
                'because': because}

    unselected = []
    if reduction is None:
        unselected.append('reduction')
    if skydome is None:
        unselected.append('skydome')
    sel = {
        'schema': 'sightline-perflive-select/2',
        'status': 'SELECTED' if not unselected else 'INCOMPLETE',
        'far': dict(role(far, 'far0+far1 draw calls = %d on %d two-range frames, on an arm that '
                              'exited zero, stayed live, confirmed the declared asset set and '
                              'root, loaded the declared stage item and echoed the pinned RNG '
                              'state' % (score_far(far), far['far_frames'])),
                    pose_kind=far['pose_kind'], pose_source=far['pose_source'],
                    pose_mark_sha256=far['pose_mark_sha256']),
        'reduction': role(reduction,
                          'fogless frames = %r, panorama frames = %r, unsound draws = %r'
                          % (reduction['fogless_frames'], reduction['pano_frames'],
                             reduction['unsound']) if reduction is not None else
                          'no eligible candidate measured a positive fogless frame count with a '
                          'background pass and no panorama shell, so no stage is qualified for '
                          'the reduction. The far candidate is NOT assigned to this role: its '
                          'own fogless figure is %r. The dependent phase must not run.'
                          % ([(r['stage'], r['fogless_frames']) for r in elig],)),
        'skydome': role(skydome,
                        'skydome frames = %r, carriers = %r'
                        % (skydome['sky_frames'], skydome['carriers']) if skydome is not None else
                        'no eligible candidate drew a skydome frame, so no stage is qualified '
                        'for the carrier marker and the far candidate is not assigned to it. '
                        'Measured skydome frames per candidate: %r. The dependent phase must '
                        'not run.' % ([(r['stage'], r['sky_frames']) for r in elig],)),
        'unselected_roles': unselected,
        'candidates': seen,
    }
    (out / 'selected.json').write_text(json.dumps(sel, indent=2) + '\n',
                                       encoding='utf-8', newline='\n')
    for k in ('far', 'reduction', 'skydome'):
        print('%-10s %-10s %-12s %s' % (k, sel[k]['stage'], sel[k]['status'], sel[k]['because']))
    if unselected:
        print('perflive-check: role(s) %r are NOT_COVERED: no candidate proved them and the far '
              'candidate is not substituted. Their phases must not run.' % (unselected,),
              file=sys.stderr)
        return 1
    return 0


def do_manual_template(out):
    out = Path(out)
    record = {}
    p = out / 'arms.json'
    if p.is_file():
        record = json.loads(p.read_text(encoding='utf-8-sig'))
    steps = {}
    for i, sid in enumerate(MANUAL_IDS):
        steps[sid] = {'step': MANUAL[i], 'outcome': None, 'build_sha256': None,
                      'arm': None, 'frame': None, 'note': ''}
    doc = {'schema': 'sightline-perflive-manual/1',
           'how': 'Perform each step and replace its null outcome with PASS or FAIL. '
                  'build_sha256 must be this batch\'s sightline.exe hash, arm must name an '
                  'arm of this batch, frame must be the integer frame the outcome was read '
                  'at. A null or unpinned outcome leaves the batch INCOMPLETE; it is never '
                  'treated as a pass.',
           'build_sha256_expected': (record.get('binaries') or {}).get('sightline'),
           'steps': steps}
    (out / 'manual.json').write_text(json.dumps(doc, indent=2) + '\n',
                                     encoding='utf-8', newline='\n')
    print('wrote %s with %d unrecorded steps' % (out / 'manual.json', len(MANUAL_IDS)))
    return 0


def main(argv):
    scope = None
    rest = []
    i = 0
    while i < len(argv):
        if argv[i] == '--scope' and i + 1 < len(argv):
            scope = argv[i + 1]
            i += 2
            continue
        rest.append(argv[i])
        i += 1
    if scope is not None and scope not in ('subset', 'full'):
        print('perflive-check: --scope must be subset or full', file=sys.stderr)
        return 2
    if len(rest) == 1 and rest[0] == '--manual':
        for line in MANUAL:
            print(line)
            print('')
        return 0
    if len(rest) == 2 and rest[0] == '--select':
        return do_select(rest[1])
    if len(rest) == 2 and rest[0] == '--manual-template':
        return do_manual_template(rest[1])
    if len(rest) == 1:
        return do_check(rest[0], scope)
    print(__doc__.splitlines()[0], file=sys.stderr)
    print('usage: perflive-check.py [--scope subset|full] <attempt-dir>', file=sys.stderr)
    print('       perflive-check.py --select <attempt-dir>', file=sys.stderr)
    print('       perflive-check.py --manual-template <attempt-dir>', file=sys.stderr)
    print('       perflive-check.py --manual', file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
