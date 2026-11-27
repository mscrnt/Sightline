#!/usr/bin/env python3
"""Synthetic fake-child output trees for the OPTIMIZED RENDERING live batch.

Launches nothing, needs no GPU, no window, no driver, no ROM and no game. It
fabricates the files a real batch leaves behind - an attempt directory with
arms.json, one stderr per arm written in the engine's own format strings, and
PPM captures with real headers and real payload lengths - so that
tools/windows/perflive-check.py and the non-launching paths of
tools/windows/perflive.ps1 can be driven against known-bad evidence.

WHY THIS EXISTS. Every witness in the checker is a claim about what the engine
printed. A claim like that is only as good as the case that would break it, and
on a machine with no far-stage pose there is no live run that produces a
known-bad case on demand. These fixtures produce them on demand, offline.

EVERY NEGATIVE FIXTURE MUST BITE. For each one, `expect` names the status the
corrected checker must report and the witness that must fire, and `legacy`
re-implements the DEFECTIVE rule the fixture was built against and reports what
it would have said. A fixture whose legacy verdict is not PASS is not a
regression test, because the behaviour it is pinned against never accepted it.

    list                      the fixture names, with the defect each targets
    build <name> <dir>        write the fixture's tree into <dir>
    expect <name>             the expected status, full_qualification, and the
                              witness or message that must fire
    legacy <name> <dir>       the DEFECTIVE rule's verdict on a built fixture

The engine format strings reproduced here are quoted in
tools/windows/perflive-check.py's module docstring, each with its source line.
"""
import hashlib
import json
import os
import sys
from pathlib import Path

SEED = '00000000000f4241'
BUILD_SHA = 'a' * 64
WA_ROOT = r'C:\fixture\sightline\assets\worldassets'
TP_ROOT = r'C:\fixture\sightline\assets\texpacks'
AO_DIR = r'C:\fixture\sightline\assets'
SHOT_W, SHOT_H = 640, 480

PASS, FAIL, NONE, INCOMPLETE = 'PASS', 'FAIL', 'NOT_COVERED', 'INCOMPLETE'


# ---------------------------------------------------------------- line shapes
def l_live(frames):
    return 'sightline native: survived %d pumped frames' % frames


def l_rng(state=SEED, how='explicit SL_RNG_SEED'):
    return 'sightline native: RNG state %s (%s)' % (state, how)


def l_waset(s='xbla', root=WA_ROOT, frm='SL_WORLDASSET_ROOT'):
    return 'sl_worldasset: set=%s root=%s (%s)' % (s, root, frm)


def l_loaded(cls, path):
    return ('sl_worldasset: LOADED %s/%s  4321 verts  9000 indices (3000 tris)  '
            '3 meshes  2 materials  2 images' % (cls, path))


def l_gpu_ready():
    return ('#88 background GPU transform ready - the eligible background meshes '
            'take the vertex program')


def l_gpu_bad(why='no entry point'):
    return ('#88 background GPU transform unavailable: %s - the background draws '
            'on the fixed pipeline' % why)


def l_teleport(x=19102.1, y=60.312, z=17326.178, theta=66.019, verta=9.65, room=12):
    return ('sl_teleport: requested pos=%.3f %.3f %.3f theta=%.3f verta=%.3f room=%d'
            % (x, y, z, theta, verta, room))


def l_pad_file(path, n):
    return 'sightline input: SL_PAD_SCRIPT %s: %d step(s)' % (path, n)


def l_pad_step(i, frame):
    return ('sightline input: SL_PAD_SCRIPT step %d at frame %d: L(0.00,-1.00) '
            'R(0.60,0.00) T(0.00,0.00) buttons=0000' % (i, frame))


def l_shot(name, read, frame, w=SHOT_W, h=SHOT_H, window='frame'):
    return ('sl_shot: %s (%dx%d) at read=%d frame=%d (window=%s) cloud phase 0.250'
            % (name, w, h, read, frame, window))


def census_block(frame, single=(0, 0), far0=(0, 0), far1=(0, 0), far_not_admitted=0,
                 far_frames=0, bg_frames=120, far_verts=0, xlu=0, sky_frames=120,
                 fogless_frames=None, pano_frames=None):
    """One census report, exactly as the per-60-frame census branch prints it.

    All of these lines are reached only from the census branch
    (src/gfx/sl_gfx_dl.c:23775), which is why no no-census fixture carries them.
    """
    out = [
        'sl_dl: frame %d cmds=100 vtx=200(rej=0) tris=300 unknown=0 '
        'bounds x[-1,1] y[-1,1] z[-1,1]' % frame,
        '#88 background GPU transform: active, SUBMITTED 4 walks, 8 mesh draws, '
        '%d draw calls, %d indices (%d triangles); BY DEPTH %d calls / %d indices '
        'at the single-range clamp, %d / %d in a far frame\'s first range, '
        '%d / %d in its game range; ADMITTED 4 walks and 8 mesh draws, 0 mesh '
        'draws kept on the walk\'s own clip positions; refused: 0 late, '
        '%d far-range not admitted, 0 panorama, 0 animated, 0 no buffer objects, '
        '0 no programs'
        % (single[0] + far0[0] + far1[0], single[1] + far0[1] + far1[1],
           (single[1] + far0[1] + far1[1]) // 3,
           single[0], single[1], far0[0], far0[1], far1[0], far1[1],
           far_not_admitted),
        "#78 background beyond the far plane: %d of %d frames drawn in two depth "
        "ranges; last such frame %d vertices past the game's far plane w 15000.0, "
        "the item reaching w 43700.0" % (far_frames, bg_frames, far_verts),
        '#68 background: drawn on %d frames, 1200 triangles and 900 vertices per '
        'frame, 12 GL draws per frame, 0 frames skipped (projection)' % bg_frames,
        '#68 background translucency: %d of those GL draws per frame are blended '
        'with NO alpha reject, 3 with one' % xlu,
        '#68 skydome: drawn on %d frames, 300 triangles and 200 vertices per '
        'frame, 0 frames skipped (projection)' % sky_frames,
    ]
    if fogless_frames is not None:
        out.append("#81 background on a fogless stage: %d frames drawn in the game's "
                   'own range, the far pass never entered' % fogless_frames)
    if pano_frames is not None:
        out.append('#74 background panorama shell (D-026): 2048 vertices, faded on '
                   '%d frames' % pano_frames)
    return out


TF_OFF_PHRASE = (' (SL_BG_TF_CLIP is not 1: the candidate is off and the pass '
                 'is byte for byte the one that ships)')
# v0.4 THE ROW'S OWN TAIL CLAUSE, reproduced verbatim from the emitter. A
# production launch prints it BESIDE POSITIVE COUNTERS, which is why it is a
# third state rather than a second spelling of the off phrase: a fixture that
# could only plant the off text could not tell a grammar that reads the tail
# as an alternation from one that reads any tail as "off".
TF_PROD_PHRASE = (' (the PRODUCTION selection: the OPTIMIZED RENDERING row '
                  'admitted these walks and no selector named an arm, so the '
                  'mode in force is the auxiliary-context real prepass on arm '
                  'GPU_SOURCE)')


def l_tf95(state='ready', prepasses=0, vertices=0, reuses=0, binds=0,
           submitted=0, indices=0, single=0, far0=0, far1=0, walks=0,
           admitted=0, refused_walks=0, refused_draws=0, slot_refills=0,
           evictions=0, module_refusals=0, off=False, production=False):
    """The clip prepass's own census line (src/gfx/sl_gfx_dl.c:19993-20020).

    EVERY PLURAL SUFFIX IS DERIVED THE WAY THE EMITTER DERIVES IT, so a
    fixture that plants a counter of exactly 1 produces the singular text the
    engine would produce rather than a plural the grammar happens to accept.
    A fixture that only ever writes plurals cannot tell a grammar that reads
    both from one that silently stops reading at 1.

    The trailing clause has THREE states, matching the emitter's three. `off`
    plants the off text, which is printed only when NO walk reached a prepass,
    so it is what distinguishes an arm that never asked for the candidate from
    one that asked and did nothing - two states with identical counters.
    `production` plants the row's clause, printed when the OPTIMIZED RENDERING
    row admitted the walks with no selector naming an arm; neither plants
    anything on a developer-switch launch, which is silent there as before. The
    two are mutually exclusive in the emitter, and asking for both here is a
    fixture defect rather than a state, so it is refused outright.
    """
    if off and production:
        raise ValueError('off and production are mutually exclusive in the '
                         'emitter: one says no walk reached a prepass and the '
                         'other says the row admitted these walks')
    def pl(n, one, many):
        return one if n == 1 else many
    return ('#95 background GPU clip prepass: %s, %d prepass%s over %d '
            'vertices, %d reuse%s, %d bind%s; SUBMITTED %d nonempty '
            "run%s on a prepass pool (%d indices) by the SOURCE's depth "
            'single %d / far0 %d / far1 %d, %d walk%s; ADMITTED %d, '
            'refused %d walk%s and %d draw%s, %d slot refill%s and '
            '%d eviction%s, %d module refusal%s%s'
            % (state, prepasses, pl(prepasses, '', 'es'), vertices,
               reuses, pl(reuses, '', 's'), binds, pl(binds, '', 's'),
               submitted, pl(submitted, '', 's'), indices,
               single, far0, far1, walks, pl(walks, '', 's'), admitted,
               refused_walks, pl(refused_walks, '', 's'),
               refused_draws, pl(refused_draws, '', 's'),
               slot_refills, pl(slot_refills, '', 's'),
               evictions, pl(evictions, '', 's'),
               module_refusals, pl(module_refusals, '', 's'),
               TF_OFF_PHRASE if off
               else TF_PROD_PHRASE if production else ''))


# THE MODULE'S OWN DIAGNOSTICS, WHICH SHARE THE CENSUS PREFIX AND ARE NOT
# CENSUSES. Reproduced verbatim from the emitters, because the defect these
# exist to pin was a classification defect: a parser that counted any line
# carrying the '#95 background GPU clip prepass' prefix as a census read the
# ready banner of a HEALTHY run as a malformed report, so prefixes = reports+1
# and every TF gate failed on the first good candidate run. A fixture written
# with invented diagnostic text could not have caught that, because the text
# is exactly what decides it.
#
# Two of the four are written in the COLON form, which is why exempting the
# ready banner alone would still have misread a driver that refused the
# program as a parser fault rather than as the refusal it is.
def l_tf95_ready():
    """src/gfx/sl_gfx_bgtf.c:434-437 - printed once, when the program links."""
    return ('#95 background GPU clip prepass ready - eligible background meshes '
            'take their clip positions from a transform-feedback pool and '
            'rasterise through the original fixed-function path '
            '(SL_BG_TF_CLIP unset or 0: the existing paths)')


def l_tf95_unavailable(why='the prepass program did not compile'):
    """src/gfx/sl_gfx_bgtf.c:263-264 - tf_refuse, which also latches g_why, so
    every later census prints THIS reason in its state field."""
    return ('#95 background GPU clip prepass unavailable: %s - the background '
            'keeps the existing path' % why)


def l_tf95_nocompile(log='0:17(5): error: precise is reserved'):
    """src/gfx/sl_gfx_bgtf.c:397 - the COLON form, immediately before the
    tf_refuse at :398 prints the unavailable line."""
    return '#95 background GPU clip prepass: program did not compile: %s' % log


def l_tf95_nolink(log='varying u_post not written by vertex shader'):
    """src/gfx/sl_gfx_bgtf.c:416 - the COLON form, as above (:418)."""
    return '#95 background GPU clip prepass: program did not link: %s' % log


def l_tf95_bad_switch(value='yes'):
    """src/gfx/sl_gfx_bgtf.c:286-287 - a DIFFERENT #95 prefix.

    Printed once when SL_BG_TF_CLIP holds neither 0 nor 1. It is a #95 line
    and it is not a clip-prepass census line, so no census count may move for
    it at all - not as a report and not as an exempted diagnostic.
    """
    return ('#95 SL_BG_TF_CLIP="%s" is not 0 or 1 - the GPU clip prepass '
            'candidate stays off' % value)


def l_reduced(walks, repaired=0, unsound=0):
    return ('#88 background GPU bypass: %d walks reduced - 40000 vertices computed '
            'whole, 12000 to the near class alone; refused: 0 switch pair, '
            '0 panorama, 0 animated, 0 statistics, 0 fog, 0 far range, 0 arrays, '
            '0 program, 0 analysis; %d repaired, %d unsound mesh draws (must be 0); '
            '3 items analysed in 3 rebuilds' % (walks, repaired, unsound))


def l_refused(considered, repaired=0, unsound=0):
    return ('#88 background GPU bypass: REFUSED - %d walks considered, 0 reduced; '
            'refused: 0 switch pair, 0 panorama, 0 animated, %d statistics, 0 fog, '
            '0 far range, 0 arrays, 0 program, 0 analysis; most statistics; '
            '%d repaired, %d unsound mesh draws (must be 0); the switch is on and '
            'the CPU loop ran whole on every walk'
            % (considered, considered, repaired, unsound))


def l_fillq(workers=2, computed=900):
    return ('#88 fill queries: %d workers, 120 batches of 4096 predicted traces; '
            '3800 answers taken from a batch (%d computed by a worker, 40 waited '
            'for), 12 traced inline (not predicted), 0 computed and not asked, '
            '0 answered inline after a floating-point change (SL_FILL_WORKERS=0: '
            'inline)' % (workers, computed))


INPUT_STREAM = r'C:\fixture\repo\tools\trace\inputs\runway.input'


def l_input(path, reads):
    """src/platform/sl_ultra_shim.c:1807 - the stream that opened, and the
    file's length in 4-byte records."""
    return 'sightline native: input stream %s (%d reads)' % (path, reads)


def l_input_fail(path):
    """src/platform/sl_ultra_shim.c:1799 - and the engine exits 2 right after
    printing it (:1798-1801), so nothing later in the run is printed at all."""
    return 'sightline native: SL_INPUT open failed: %s' % path


def l_bgv(walks, split, serial, fp=0, state='running', cores=8):
    """The vertex helper's own bounded report (src/gfx/sl_gfx_dl.c:18024).

    `split` is g_bgv_split and `fp` is g_bgv_r_fp, the splits whose upper range
    was recomputed on the render thread; the two are not independent, so
    split == fp is a helper that took ranges and saved no work.
    """
    return ('#88 background vertices: %d walk%s - %d in two ranges, %d on the '
            'render thread alone (off 0, under four cores 0, small item 0, '
            'level fog 0, statistics 0, no helper 0, upper range recomputed '
            '%d); helper %s, %d allowed physical cores'
            % (walks, '' if walks == 1 else 's', split, serial, fp, state, cores))


def l_sk474(kind='prod'):
    return ('#474 skybatch: float colour carrier %s - served cloud draws upload '
            '48-byte records with a GL_FLOAT colour (validated)' % kind)


def l_sk316(draws=480):
    return ('#316 skybatch: final items 12 passes 240 indexed-draws %d indexed '
            '98304 array-draws 0 array-vertices 0 unique 12 refused 0 '
            'mesh-refused 0 declined 0 declined-vertices 0' % draws)


# ------------------------------------------------------------------- writers
def write_text(path, text):
    # write_bytes, not write_text: on Windows write_text turns LF into CRLF,
    # and a fixture compared byte for byte has to be the bytes it declares.
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(text.encode('utf-8'))


def write_json(path, obj):
    write_text(path, json.dumps(obj, indent=2) + '\n')


def write_ppm(path, w, h, fill=0x40, payload_bytes=None):
    """A capture with a real header and a measured payload length.

    payload_bytes short of w*h*3 is a TRUNCATED capture: the header still
    declares the full grid, which is exactly the case a count comparison
    accepts.
    """
    header = ('P6\n%d %d\n255\n' % (w, h)).encode('ascii')
    want = w * h * 3 if payload_bytes is None else payload_bytes
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(header + bytes([fill]) * want)


def sha256_text(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


# -------------------------------------------------------------- arm assembly
PREFLIGHT = ['EXIT_ZERO', 'NOT_TIMED_OUT', 'LIVENESS', 'SEED_ECHOED',
             'PRESENTATION_XBLA_ACTIVE', 'ASSET_ROOT_BOUND', 'STAGE_ITEM_LOADED']


def arm(name, phase, stage='runway', stage_number=35, item='runway', frames=420,
        witnesses=None, pair=None, role='', shots=None, pad_steps=None,
        pose='teleport', env=None, exit_code=0, timed_out=False,
        launch_failure=None, helper=None, control=None):
    env = dict(env or {})
    e = {'SL_BOOT_LEVEL': str(stage_number), 'SL_FRAMES': str(frames),
         'SL_RNG_SEED': SEED, 'SL_WORLDASSET_ROOT': WA_ROOT,
         'SL_TEXPACK_ROOT': TP_ROOT, 'SL_ASSET_OVERRIDE_DIR': AO_DIR}
    # THE ROLE IS RECORDED IN THE ENVIRONMENT, as the runner records it. A
    # performance row assigns its two measured arms an off and an on role, and
    # that assignment is checked against each arm's OWN SL_PERF_OPT rather
    # than against its name - so an arm has to carry the variable for the
    # binding to be readable at all. An arm with no role leaves it unset,
    # which is what makes "this arm recorded no SL_PERF_OPT" a real state a
    # fixture can produce.
    if role in ('off', 'on'):
        e['SL_PERF_OPT'] = '0' if role == 'off' else '1'
    e.update(env)
    if pose == 'teleport':
        poserec = {'kind': 'teleport', 'source': r'C:\fixture\marks\far-mountain.txt',
                   'mark_sha256': 'b' * 64, 'mark_stage': str(stage_number),
                   'shape': 'legacy', 'x': '19102.100', 'y': '60.312',
                   'z': '17326.178', 'theta': '66.019', 'verta': '9.650', 'room': '12'}
    else:
        poserec = {'kind': 'start-pad', 'source': 'stage start pad (declared)',
                   'mark_sha256': None, 'mark_stage': None, 'shape': 'none'}
    a = {
        'name': name, 'phase': phase, 'stage': stage, 'stage_number': stage_number,
        'pose': poserec, 'frames': frames,
        'shots': shots, 'pad_script': pad_steps, 'helper': helper,
        'control': control, 'expected_seconds': 95,
        'cwd': r'C:\fixture\repo', 'argv': [r'C:\fixture\repo\build\win32\sightline.exe'],
        'env': e, 'seed_hex': SEED,
        'expect': {'presentation': 2, 'presentation_set': 'xbla', 'stage_item': item,
                   'worldasset_root': WA_ROOT, 'width': SHOT_W, 'height': SHOT_H},
        'scratch_config': r'C:\fixture\attempt\%s\config.ini' % name,
        'scratch_config_sha256': 'c' * 64,
        'scratch_save': r'C:\fixture\attempt\%s\eeprom.bin' % name,
        'pinned_eeprom': {'path': r'C:\fixture\repo\tools\trace\inputs\runway.eeprom',
                          'sha256': 'd' * 64},
        'witnesses': witnesses or list(PREFLIGHT),
        'pair': pair, 'role': role, 'note': 'fixture arm',
        'run': {'argv': [r'C:\fixture\repo\build\win32\sightline.exe'], 'pid': 4321,
                'exit_code': exit_code, 'timed_out': timed_out,
                'launch_failure': launch_failure,
                'start_utc': '2026-11-01T00:00:00Z', 'end_utc': '2026-11-01T00:01:30Z',
                'stdout': None, 'stderr': None},
    }
    return a


def shots3(count=3, moments=(120, 180, 240), read_first=None, read_last=None):
    """The declared capture SCHEDULE, in the runner's own field names.

    expected_moments is the moment each capture index must carry: the frame
    counter when the window is a frame window, the input record index when
    read_first / read_last declare a read window. The engine's own rule is
    "inside [FIRST,LAST] and at least EVERY since the last capture"
    (src/gfx/sl_gfx_sdl.c:816-828), so FIRST=120 LAST=240 EVERY=60 is three
    captures at 120, 180 and 240.
    """
    return {'first': 120, 'last': 240, 'every': 60, 'count': count,
            'read_first': read_first, 'read_last': read_last,
            'expected_moments': list(moments),
            'width': SHOT_W, 'height': SHOT_H, 'derivation': 'fixture'}


def record(arms, scope='full', not_covered=None, mandatory=None,
           census_reference_arm='B-depth-on', mandatory_cases=None):
    return {
        'schema': 'sightline-perflive/2', 'scope': scope,
        'generated_utc': '2026-11-01T00:00:00Z',
        'repo': r'C:\fixture\repo', 'out': r'C:\fixture\out',
        'attempt': r'C:\fixture\attempt',
        'mandatory_phases': mandatory if mandatory is not None else ['ACQ', 'B', 'C', 'D', 'E', 'F', 'G'],
        'mandatory_cases': mandatory_cases if mandatory_cases is not None else [],
        'census_reference_arm': census_reference_arm,
        'binaries': {'sightline': BUILD_SHA, 'SDL2': 'e' * 64, 'libwinpthread': 'f' * 64},
        'tools': {'perflive.ps1': '1' * 64, 'perflive-check.py': '2' * 64},
        'source': {'git_head': '3' * 40, 'git_dirty': False},
        'rom': r'C:\fixture\ge.z64', 'seed_hex': SEED,
        'scratch_config_sha256': 'c' * 64,
        'scratch_config_text': 'version=1\npresentation=2\noptimized_rendering=1\n',
        'pinned_eeprom': {}, 'asset_roots': {
            'SL_WORLDASSET_ROOT': {'path': WA_ROOT, 'files': 20, 'inventory_sha256': '4' * 64}},
        'selection': {'status': 'SELECTED',
                      'far': {'stage': 'runway', 'stageNum': 35},
                      'reduction': {'stage': 'silo', 'stageNum': 20},
                      'skydome': {'stage': 'runway', 'stageNum': 35}},
        'selection_sha256': '5' * 64, 'pose_set': {'bound': True, 'set_sha256': '6' * 64},
        'pad_script': {'path': r'C:\fixture\attempt\padscript\move.txt',
                       'sha256': '7' * 64, 'steps': 6},
        'not_covered': not_covered if not_covered is not None else [],
        'arms': arms,
    }


# ---- real synthetic raw presentation sources ------------------------------
#
# A per-arm record names a source file. Until the checker opened it, the name
# could be anything; a fixture could therefore only ever test the summary. So
# these fixtures write ACTUAL six-column presentation files and derive their
# figures with the ACTUAL tools/profiling/analyze.py frame_report, which is
# the function the checker recomputes with. A positive fixture then passes
# because its numbers really are what its bytes produce, and a negative is
# wrong in exactly the one way it plants rather than incidentally wrong for
# want of a file.
#
# The format is analyze.py presentations (:298-320): six whitespace-separated
# columns `t_ms kind pumped_frame alpha clock digest`, kind R/E/B (B skipped),
# strictly increasing timestamps, alpha at most 1, digest exactly 8 hex
# characters. frame_report additionally requires the regular frames to land
# exactly on range(lo, hi, step) and at least two selected records.

PRES_WINDOW = (100, 110, 1)      # ten regular frames, nine intervals: tiny


def _analyze():
    """tools/profiling/analyze.py, which imports `common` with no path setup."""
    import importlib.util
    here = Path(__file__).resolve().parents[1] / 'profiling'
    if str(here) not in sys.path:
        sys.path.insert(0, str(here))
    spec = importlib.util.spec_from_file_location('sl_fixture_analyze',
                                                  here / 'analyze.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_pres(path, interval_ms, win=PRES_WINDOW):
    """Write a presentation file with the given frame intervals.

    `interval_ms` is one number, for a file whose intervals are all that -
    uniform intervals make the derived figures exact and predictable, since the
    mean and the nearest-rank p99 are then both that number, so a fixture that
    wants a particular reading states it once here rather than hand-computing
    it. It may instead be a SEQUENCE of exactly one interval per gap, for the
    cases that need the mean and the p99 to differ: analyze.py frame_report
    takes the nearest-rank p99 as `ordered[ceil(.99 * n) - 1]` (:345), which
    for nine intervals is the largest, so one long gap among short ones gives a
    file whose mean is comfortable and whose p99 is not.

    Written as BYTES: write_text would turn each newline into CRLF on Windows,
    and a fixture whose bytes are pinned by sha256 must be the bytes it meant.
    """
    lo, hi, step = win
    frames = list(range(lo, hi, step))
    gaps = len(frames) - 1
    if isinstance(interval_ms, (list, tuple)):
        if len(interval_ms) != gaps:
            raise ValueError('%d frames leave %d gaps; %d intervals were given'
                             % (len(frames), gaps, len(interval_ms)))
        steps = [float(x) for x in interval_ms]
    else:
        steps = [float(interval_ms)] * gaps
    rows, t = [], 1000.0
    for i, frame in enumerate(frames):
        rows.append('%.4f R %d 1 %d %08x' % (t, frame, 500 + i, 0xA0000000 + frame))
        if i < gaps:
            t += steps[i]
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(('\n'.join(rows) + '\n').encode('utf-8'))
    return path


def pres_record(dest, index, interval_ms, win=PRES_WINDOW):
    """A real source file plus the real frame_report derived from it."""
    path = write_pres(Path(dest) / ('pres-%d.txt' % index), interval_ms, win)
    lo, hi, step = win
    return _analyze().frame_report(str(path), lo, hi, step)


def raw_run(index, order, armname, stage, mean, p99=None, build=BUILD_SHA,
            intervals=300, schema='sightline-frames/1', dest=None,
            win=PRES_WINDOW, interval_ms=None, state_source=False):
    """One RAW per-run measurement, in the shape frame_report writes.

    The keys are tools/profiling/analyze.py frame_report's own
    ('sightline-frames/1', window, intervals, mean_ms, p99_ms_nearest_rank,
    source), plus the three fields that bind the run to a batch rather than to
    a file on disk: the build it was taken on, the arm it measured and the
    stage that arm ran. Without those a mean is a figure beside the row rather
    than a measurement of it.

    mean/p99 are written through unchanged, so a caller can place a NaN, an
    infinity, a negative, a string or a JSON boolean where a time belongs.

    WITH `dest`, A REAL SOURCE FILE IS WRITTEN and the window, the interval
    count and the source pin come from the real frame_report of those bytes.
    The intervals are uniform, so the report's mean and nearest-rank p99 are
    both the interval - which is why `interval_ms` defaults to `mean` and p99
    defaults to the report's own figure instead of the old mean + 2.5. A
    caller that passes a mean its file does not produce is planting an edited
    summary against an unchanged source, and that is now a thing a fixture can
    say. Without `dest` the source is named and never written, which is the
    absent-source case.
    """
    if dest is None:
        if p99 is None:
            p99 = mean + 2.5
        return {'run': index, 'order': order, 'arm': armname, 'stage': stage,
                'schema': schema, 'build_sha256': build, 'intervals': intervals,
                'mean_ms': mean, 'p99_ms_nearest_rank': p99,
                'window': {'lo': 120, 'hi': 420, 'step': 1},
                'source': {'path': r'C:\fixture\attempt\pres-%d.txt' % index,
                           'sha256': '8' * 64}}
    if interval_ms is None:
        # A planted non-number cannot be an interval, so the file is written at
        # a plain readable rate and the planted figure is what disagrees.
        interval_ms = mean if isinstance(mean, (int, float)) \
            and not isinstance(mean, bool) and mean == mean and abs(mean) != float('inf') \
            and mean > 0 else 12.0
    report = pres_record(dest, index, interval_ms, win)
    # `state_source` embeds exactly what the file produces, for a fixture whose
    # planted defect is somewhere else entirely and whose figures must
    # therefore be beyond reproach. Without it the caller's `mean` is written
    # through unchanged, which is how a fixture plants an edited summary.
    if state_source:
        mean, p99 = report['mean_ms'], report['p99_ms_nearest_rank']
    return {'run': index, 'order': order, 'arm': armname, 'stage': stage,
            'schema': schema, 'build_sha256': build,
            'intervals': report['intervals'],
            'mean_ms': mean,
            'p99_ms_nearest_rank': report['p99_ms_nearest_rank'] if p99 is None else p99,
            'window': dict(report['window']),
            'source': dict(report['source'])}


def perf_arms(rec):
    """(off arm, on arm, the stage they ran) for the row's measured pair."""
    names = [a['name'] for a in rec['arms']]
    offs = [a['name'] for a in rec['arms'] if a.get('role') == 'off'] or names[:1]
    ons = [a['name'] for a in rec['arms'] if a.get('role') == 'on'] or names[1:2] or names[:1]
    return offs[0], ons[0], rec['arms'][0]['stage']


def perf_row(rec, kind='bound', dest=None, arms_override=None,
             metrics_override=None, seq_override=None):
    """A performance row BOUND to the batch, in the shape the rules take.

    Four runs in order A,B,B,A, as tools/profiling/analyze.py compare_abba
    requires, with per-run mean and nearest-rank p99 frame time - the figures
    frame_report computes - rather than a summary. A is the row OFF and B the
    row ON.

    kind='fabricated' keeps the same four raw runs and the same recomputable
    savings and spreads, and states a screen verdict those numbers do not
    support: the spread between the two repeats of one arm is wider than the
    smaller paired saving, so min(savings) > max(spreads) is false.
    """
    off_arm, on_arm, stage = perf_arms(rec)
    measured = list(arms_override) if arms_override is not None else [off_arm, on_arm]
    means = ([12.40, 11.10, 11.20, 12.50] if kind != 'fabricated'
             else [12.40, 12.30, 11.20, 13.60])
    raw = []
    for i, (order, mean) in enumerate(zip(('A', 'B', 'B', 'A'), means)):
        # seq_override names the ACTUAL arm of each run while the A,B,B,A
        # labels stay as they are: that is the shape of the reproduced
        # counterexample, where the labels pair correctly and the arms do not.
        if seq_override is not None:
            armname = seq_override[i]
        else:
            armname = measured[0] if order == 'A' else measured[1]
        raw.append(raw_run(i, order, armname, stage, mean, dest=dest))
    savings = [means[0] - means[1], means[3] - means[2]]
    spreads = [abs(means[0] - means[3]), abs(means[1] - means[2])]
    metrics = {'rule': 'abba-timing-screen/1',
               'paired_savings_ms': savings, 'repeat_spreads_ms': spreads,
               'timing_screen_passed': (True if kind == 'fabricated'
                                        else min(savings) > max(spreads)),
               'rule_source': 'tools/profiling/analyze.py compare_abba '
                              "('sightline-abba/1'); the absolute 16.667 ms budget is "
                              'tools/profiling/READINESS.md:45-47'}
    if metrics_override:
        metrics.update(metrics_override)
    # `off` and `on` are the human-readable summary the row has always carried.
    # They are kept beside the raw runs on purpose: a truthy summary is exactly
    # what the old rule gated on, and it must no longer be enough by itself.
    return {'row': 'OPTIMIZED RENDERING', 'stage': rec['arms'][0]['stage'],
            'off': '%.2f ms' % ((means[0] + means[3]) / 2.0),
            'on': '%.2f ms' % ((means[1] + means[2]) / 2.0),
            'build_sha256': BUILD_SHA, 'arms': measured, 'raw': raw,
            'metrics': metrics}


def reseal_abba(row):
    """Recompute the row's DOCUMENT-INTERNAL figures from its own stated means.

    The rule these negatives are built against already recomputed four things
    from the row's own means: the per-arm `off`/`on` aggregate, the paired
    savings, the repeat spreads and the screen verdict. A fixture that moves
    one figure and leaves those four stale is wrong in five ways at once and
    is caught by the OLD rule as well as the new one, which pins nothing. This
    restores the four, exactly as that rule computed them, so the single thing
    left disagreeing is what the fixture set out to plant.
    """
    means = [float(r['mean_ms']) for r in row['raw']]
    per_arm = {}
    for r, m in zip(row['raw'], means):
        per_arm.setdefault(r['arm'], []).append(m)
    for key, index in (('off', 0), ('on', 1)):
        sample = per_arm.get(row['arms'][index])
        if sample:
            row[key] = '%.2f ms' % (sum(sample) / float(len(sample)))
    savings = [means[0] - means[1], means[3] - means[2]]
    spreads = [abs(means[0] - means[3]), abs(means[1] - means[2])]
    row['metrics']['paired_savings_ms'] = savings
    row['metrics']['repeat_spreads_ms'] = spreads
    row['metrics']['timing_screen_passed'] = min(savings) > max(spreads)
    return row


def emit(dirpath, rec, stderrs, captures=None, manual='pinned',
         performance_row=True):
    """Write one fixture tree: arms.json, per-arm stderr, captures, gates."""
    d = Path(dirpath)
    d.mkdir(parents=True, exist_ok=True)
    for a in rec['arms']:
        lines = stderrs.get(a['name'])
        if lines is None:
            continue
        p = d / a['name'] / 'stderr.txt'
        write_text(p, '\n'.join(lines) + '\n')
        a['run']['stderr'] = str(p)
        a['run']['stdout'] = str(d / a['name'] / 'stdout.txt')
        write_text(d / a['name'] / 'stdout.txt', '')
    for armname, caps in (captures or {}).items():
        for cap in caps:
            write_ppm(d / armname / cap['name'], cap.get('w', SHOT_W),
                      cap.get('h', SHOT_H), cap.get('fill', 0x40),
                      cap.get('payload'))
    if manual == 'pinned':
        steps = {}
        for i in range(1, 8):
            steps['H%d' % i] = {'step': 'fixture', 'outcome': 'PASS',
                                'build_sha256': BUILD_SHA,
                                'arm': rec['arms'][0]['name'], 'frame': 240,
                                'note': ''}
        write_json(d / 'manual.json', {'schema': 'sightline-perflive-manual/1',
                                       'steps': steps})
    elif manual == 'unpinned':
        steps = {}
        for i in range(1, 8):
            steps['H%d' % i] = {'step': 'fixture', 'outcome': 'PASS',
                                'build_sha256': '9' * 64,
                                'arm': rec['arms'][0]['name'], 'frame': 240}
        write_json(d / 'manual.json', {'schema': 'sightline-perflive-manual/1',
                                       'steps': steps})
    elif manual == 'failed':
        # Every step recorded and pinned to THIS build, and one of them says
        # the row did not do what the step required. A recorded FAIL is the
        # strongest evidence a batch can hold about the row, and reading only
        # the PRESENCE of an outcome turns it into a pass.
        steps = {}
        for i in range(1, 8):
            steps['H%d' % i] = {'step': 'fixture', 'outcome': 'PASS',
                                'build_sha256': BUILD_SHA,
                                'arm': rec['arms'][0]['name'], 'frame': 240,
                                'note': ''}
        steps['H3']['outcome'] = 'FAIL'
        steps['H3']['note'] = ('the GRAPHICS view kept the old value column after the row '
                               'was set in the SETTINGS view')
        write_json(d / 'manual.json', {'schema': 'sightline-perflive-manual/1',
                                       'steps': steps})
    elif manual == 'failed-and-absent':
        # ONE RECORDED FAIL BESIDE ONE UNRECORDED STEP, which is the exact
        # combination a precedence that reads the absence first gets wrong: H3
        # was performed on this build and says the row is broken, while H6 was
        # never performed. The absence is real and must still be reported, but
        # it is not a reason to downgrade the measurement that H3 is.
        steps = {}
        for i in range(1, 8):
            steps['H%d' % i] = {'step': 'fixture', 'outcome': 'PASS',
                                'build_sha256': BUILD_SHA,
                                'arm': rec['arms'][0]['name'], 'frame': 240,
                                'note': ''}
        steps['H3']['outcome'] = 'FAIL'
        steps['H3']['note'] = ('the GRAPHICS view kept the old value column after the row '
                               'was set in the SETTINGS view')
        steps['H6'] = {'step': 'fixture', 'outcome': None, 'build_sha256': None,
                       'arm': None, 'frame': None}
        write_json(d / 'manual.json', {'schema': 'sightline-perflive-manual/1',
                                       'steps': steps})
    elif manual == 'null':
        steps = {}
        for i in range(1, 8):
            steps['H%d' % i] = {'step': 'fixture', 'outcome': None,
                                'build_sha256': None, 'arm': None, 'frame': None}
        write_json(d / 'manual.json', {'schema': 'sightline-perflive-manual/1',
                                       'steps': steps})
    # manual == 'absent': no file at all
    if performance_row == 'placeholder':
        # The shape a truthy-field rule accepts: five present keys, every one
        # of them a string nothing can be recomputed from. No build binding to
        # these arms, no arm names, no raw runs, no rule.
        write_json(d / 'performance-row.json',
                   {'row': 'OPTIMIZED RENDERING', 'off': '12.4 ms', 'on': '9.1 ms',
                    'stage': 'runway', 'build_sha256': BUILD_SHA})
    elif isinstance(performance_row, dict):
        # A row assembled by the fixture itself, for the cases that turn on one
        # figure inside it rather than on the whole shape.
        write_json(d / 'performance-row.json', performance_row)
    elif performance_row:
        # `dest` is the fixture's own retained scratch, so the row's raw runs
        # cite real presentation files written beside it rather than names.
        write_json(d / 'performance-row.json',
                   perf_row(rec, 'fabricated' if performance_row == 'fabricated' else 'bound',
                            dest=d / 'pres'))
    write_json(d / 'arms.json', rec)
    return d


# ----------------------------------------------------- the healthy base arms
def healthy_pair_stderr(role, frames=420, far_in_window=True, blended=4,
                        pre_far=(0, 0), extra=None):
    """A census arm's stderr with a real capture bracket.

    Three census reports: one before the first capture, one between, one after
    the last. The WINDOW_* witnesses read the first and the last of those; a
    cumulative reading at the end cannot tell them apart, which is the point.
    """
    on = (role == 'on')
    s0 = (200, 6000) if on else (0, 0)
    f0_pre = pre_far if on else (0, 0)
    body = []
    body += [l_rng(), l_waset(), l_loaded('background', 'runway'), l_teleport()]
    if on:
        body.append(l_gpu_ready())
    body += census_block(61, single=s0, far0=f0_pre, far1=f0_pre,
                         far_frames=30, bg_frames=60, far_verts=4096, xlu=blended,
                         far_not_admitted=0)
    body.append(l_shot('shot-00000.ppm', 240, 120))
    if on and far_in_window:
        s1, f1 = (400, 12000), (120, 3600)
    elif on:
        s1, f1 = (400, 12000), f0_pre
    else:
        s1, f1 = (0, 0), (0, 0)
    body += census_block(121, single=s1, far0=f1, far1=f1,
                         far_frames=60, bg_frames=120, far_verts=4096, xlu=blended,
                         far_not_admitted=0)
    body.append(l_shot('shot-00001.ppm', 360, 180))
    body.append(l_shot('shot-00002.ppm', 480, 240))
    if on and far_in_window:
        s2, f2 = (600, 18000), (240, 7200)
    elif on:
        s2, f2 = (600, 18000), f0_pre
    else:
        s2, f2 = (0, 0), (0, 0)
    body += census_block(301, single=s2, far0=f2, far1=f2,
                         far_frames=150, bg_frames=300, far_verts=4096, xlu=blended,
                         far_not_admitted=0)
    body += (extra or [])
    body.append(l_live(frames))
    return body


def three_caps(fill=0x40):
    return [{'name': 'shot-00000.ppm', 'fill': fill},
            {'name': 'shot-00001.ppm', 'fill': fill},
            {'name': 'shot-00002.ppm', 'fill': fill}]


B_OFF_W = ['CENSUS_REPORT_PRESENT', 'FAR_FRAMES_POSITIVE',
           'WINDOW_FAR0_DELTA_ZERO', 'WINDOW_FAR1_DELTA_ZERO',
           'CAPTURE_SET_EXACT', 'CAPTURE_SCHEDULE_EXACT', 'CAPTURE_NAMES_MATCH_PAIR']
B_ON_W = ['CENSUS_REPORT_PRESENT', 'GPU_PROGRAM_READY', 'FAR_FRAMES_POSITIVE',
          'WINDOW_SINGLE_DELTA_POSITIVE', 'WINDOW_FAR0_DELTA_POSITIVE',
          'WINDOW_FAR1_DELTA_POSITIVE', 'WINDOW_FAR_NOT_ADMITTED_DELTA_ZERO',
          'CAPTURE_SET_EXACT', 'CAPTURE_SCHEDULE_EXACT', 'CAPTURE_NAMES_MATCH_PAIR',
          'SHOT_MOMENTS_MATCH', 'FRAMES_BYTE_IDENTICAL']


# THE VERTEX-PROGRAM DIAGNOSTIC'S OWN ARM, NAMED RATHER THAN IMPLIED.
#
# Every arm that declares WINDOW_FAR0_DELTA_POSITIVE or
# WINDOW_FAR1_DELTA_POSITIVE is reading the #88 vertex PROGRAM's per-depth
# columns, so it is only a measurement on an arm where that program actually
# takes the walk. These fixtures used to get that for free: before the clip
# prepass became a production selection, an arm that named nothing ran the
# background vertex program, so "no selector" and "the program arm" were the
# same thing and the environment never had to say which was meant.
#
# THAT STOPPED BEING TRUE, and silently. The OPTIMIZED RENDERING row is ON by
# default in a player session and src/gfx/sl_gfx_dl.c asks the prepass
# admission BEFORE the vertex program's branch, so an arm that names nothing -
# or even one that names SL_BG_GPU_XFORM=1 alone - now takes the PREPASS. On
# that arm the program's far columns read zero BECAUSE THE PREPASS WORKED, so
# a *_DELTA_POSITIVE witness fails exactly when the run succeeded. That is what
# perflive-check.py's TF_CANDIDATE_WITNESS_SET_VALID guard reports, and it is
# reporting a real declaration defect in these fixtures rather than a drift to
# be waived.
#
# SO THE PROGRAM ARM NOW SAYS SO, with both halves spelled out, because each
# does a different job: SL_BG_TF_CLIP=0 takes the prepass out of the arm (the
# developer OFF, which the production selection also honours) and
# SL_BG_GPU_XFORM=1 names the vertex-program diagnostic that is then reached.
# Naming only the second would leave the prepass in force and change nothing.
PROGRAM_ARM_ENV = {'SL_BG_TF_CLIP': '0', 'SL_BG_GPU_XFORM': '1'}


def base_pair(bw_off=None, bw_on=None):
    off = arm('B-depth-off', 'B', witnesses=PREFLIGHT + (bw_off or B_OFF_W),
              pair='B-depth-on', role='off', shots=shots3())
    # The ON arm's witness set is the vertex program's own columns, so the arm
    # names the program diagnostic; see PROGRAM_ARM_ENV above.
    on = arm('B-depth-on', 'B', witnesses=PREFLIGHT + (bw_on or B_ON_W),
             pair='B-depth-off', role='on', shots=shots3(),
             env=dict(PROGRAM_ARM_ENV))
    return [off, on]


# ------------------------------------------------------------- the fixtures
FIXTURES = {}


def fixture(name, defect, status, witness, full_qual=None, legacy='PASS'):
    def deco(fn):
        FIXTURES[name] = {'defect': defect, 'status': status, 'witness': witness,
                          'full_qualification': full_qual, 'legacy_expected': legacy,
                          'build': fn}
        return fn
    return deco


@fixture('pass-full', 'positive control (full scope)', PASS, '', full_qual=True,
         legacy='PASS')
def f_pass_full(d):
    arms = base_pair()
    arms.append(arm('C-guard', 'C', witnesses=PREFLIGHT + [
        'CENSUS_REPORT_PRESENT', 'GPU_PROGRAM_READY', 'FAR_FRAMES_POSITIVE',
        'WINDOW_SINGLE_DELTA_POSITIVE', 'WINDOW_FAR0_DELTA_ZERO',
        'WINDOW_FAR1_DELTA_ZERO', 'WINDOW_FAR_NOT_ADMITTED_DELTA_POSITIVE',
        'CENSUS_SINGLE_MATCHES_REF', 'CAPTURE_SET_EXACT',
        'CAPTURE_SCHEDULE_EXACT'], shots=shots3()))
    arms.append(arm('D-blend-on', 'D', witnesses=PREFLIGHT + [
        'CENSUS_REPORT_PRESENT', 'WINDOW_BLENDED_POSITIVE', 'CAPTURE_SET_EXACT',
        'CAPTURE_SCHEDULE_EXACT'], shots=shots3()))
    arms.append(arm('E-pose2-on', 'E', witnesses=PREFLIGHT + [
        'CENSUS_REPORT_PRESENT', 'CAPTURE_SET_EXACT', 'CAPTURE_SCHEDULE_EXACT'],
        shots=shots3()))
    arms.append(arm('F-move-on', 'F', witnesses=PREFLIGHT + [
        'CENSUS_REPORT_PRESENT', 'PAD_SCRIPT_STEPS_APPLIED',
        'WINDOW_FAR0_DELTA_POSITIVE', 'CAPTURE_SET_EXACT',
        'CAPTURE_SCHEDULE_EXACT'], shots=shots3(),
        pad_steps={'path': 'move.txt', 'sha256': '7' * 64, 'steps': 6}, role='on',
        env=dict(PROGRAM_ARM_ENV)))
    arms.append(arm('F-move-off', 'F', witnesses=PREFLIGHT + [
        'CENSUS_REPORT_PRESENT', 'PAD_SCRIPT_STEPS_APPLIED', 'CAPTURE_SET_EXACT',
        'CAPTURE_SCHEDULE_EXACT'],
        shots=shots3(), pad_steps={'path': 'move.txt', 'sha256': '7' * 64, 'steps': 6},
        role='off'))
    arms.append(arm('G1-reduction', 'G', stage='silo', stage_number=20, item='silo',
                    frames=600, pose='teleport', helper={'interval_walks': 600},
                    witnesses=PREFLIGHT + [
                        'NO_CENSUS_REQUESTED', 'REDUCTION_REPORT_PRESENT',
                        'REDUCED_WALKS_POSITIVE', 'UNSOUND_DRAWS_ZERO',
                        'REPAIRS_REPORTED', 'VERTEX_HELPER_SPLIT_POSITIVE',
                        'VERTEX_HELPER_INTERVAL_COVERED']))
    arms.append(arm('ACQ-runway', 'ACQ', pose='start-pad',
                    env={'SL_DL_CENSUS': '1'},
                    witnesses=PREFLIGHT + ['CENSUS_REPORT_PRESENT', 'GPU_PROGRAM_READY']))
    rec = record(arms)
    se = {
        'B-depth-off': healthy_pair_stderr('off'),
        'B-depth-on': healthy_pair_stderr('on'),
        'C-guard': guard_stderr(),
        'D-blend-on': healthy_pair_stderr('on'),
        'E-pose2-on': healthy_pair_stderr('on'),
        'F-move-on': move_stderr('on'),
        'F-move-off': move_stderr('off'),
        'G1-reduction': reduction_stderr(
            600, [l_reduced(300, 2, 0), l_reduced(600, 4, 0)],
            # the first walk and then every 300th, counting through the 600
            # walks the arm declares: 500 of them split, 100 of those
            # recomputed on the render thread, so 400 ranges were saved
            helper=[l_bgv(1, 1, 0, 0), l_bgv(300, 250, 50, 50),
                    l_bgv(600, 500, 100, 100)]),
        'ACQ-runway': acq_stderr(),
    }
    caps = dict((n, three_caps()) for n in
                ('B-depth-off', 'B-depth-on', 'C-guard', 'D-blend-on', 'E-pose2-on',
                 'F-move-on', 'F-move-off'))
    return emit(d, rec, se, caps)


def guard_stderr():
    """The guard-negative arm: the single range submits, the far ranges do not,
    and the far-range refusal rises. Its single-range figure matches the
    reference arm's."""
    body = [l_rng(), l_waset(), l_loaded('background', 'runway'), l_teleport(),
            l_gpu_ready()]
    body += census_block(61, single=(200, 6000), far_frames=30, bg_frames=60,
                         far_verts=4096, far_not_admitted=10)
    body.append(l_shot('shot-00000.ppm', 240, 120))
    body += census_block(121, single=(400, 12000), far_frames=60, bg_frames=120,
                         far_verts=4096, far_not_admitted=20)
    body.append(l_shot('shot-00001.ppm', 360, 180))
    body.append(l_shot('shot-00002.ppm', 480, 240))
    body += census_block(301, single=(600, 18000), far_frames=150, bg_frames=300,
                         far_verts=4096, far_not_admitted=40)
    body.append(l_live(420))
    return body


def move_stderr(role, crossed=True):
    """The movement arm: four census reports across three captures, so the
    consecutive intervals can show a crossing inside one run.

    crossed=False keeps the same cumulative totals and the same window delta
    but puts every far submission in the first interval, which is what a run
    that was already past the transition before its first capture looks like.
    """
    on = (role == 'on')
    out = [l_rng(), l_pad_file(r'C:\fixture\attempt\padscript\move.txt', 6)]
    for i, fr in enumerate((0, 60, 150, 240, 300, 390)):
        out.append(l_pad_step(i, fr))
    out += [l_waset(), l_loaded('background', 'runway'), l_teleport()]
    if on:
        out.append(l_gpu_ready())
    if on and crossed:
        # the bracket's own intervals: nothing, nothing, far, far, nothing
        far = [(0, 0), (0, 0), (100, 3000), (200, 6000), (200, 6000)]
    elif on:
        # every interval submits: the viewpoint was already past the transition
        # before the first capture. Same window delta, no crossing.
        far = [(0, 0), (100, 3000), (200, 6000), (300, 9000), (400, 12000)]
    else:
        far = [(0, 0)] * 5
    if on:
        single = [(100, 3000), (200, 6000), (300, 9000), (400, 12000), (500, 15000)]
    else:
        single = [(0, 0)] * 5
    frames = (61, 121, 181, 301, 361)
    # census, census, shot, census, shot, census, shot, census
    shots = (None, 'shot-00000.ppm', 'shot-00001.ppm', 'shot-00002.ppm', None)
    reads = (None, (240, 120), (360, 180), (480, 240), None)
    for i in range(5):
        out += census_block(frames[i], single=single[i], far0=far[i], far1=far[i],
                            far_frames=30 * (i + 1), bg_frames=60 * (i + 1),
                            far_verts=4096, xlu=4)
        if shots[i]:
            out.append(l_shot(shots[i], reads[i][0], reads[i][1]))
    out.append(l_live(420))
    return out


def reduction_stderr(frames, events, helper=None):
    """A NO-CENSUS arm: the reduction's own inline report and nothing the census
    branch prints. `helper` adds the vertex helper's own bounded report lines,
    which are printed from the same no-census path."""
    return ([l_rng(), l_waset(root=WA_ROOT), l_loaded('background', 'silo'),
             l_teleport()] + events + list(helper or []) + [l_live(frames)])


def acq_stderr(**kw):
    body = [l_rng(kw.get('seed', SEED), kw.get('how', 'explicit SL_RNG_SEED')),
            l_waset(kw.get('set', 'xbla'), kw.get('root', WA_ROOT),
                    kw.get('from', 'SL_WORLDASSET_ROOT'))]
    for cls, path in kw.get('loaded', [('background', 'runway')]):
        body.append(l_loaded(cls, path))
    if kw.get('gpu', True):
        body.append(l_gpu_ready())
    body += census_block(61, single=(400, 12000), far0=(120, 3600), far1=(120, 3600),
                         far_frames=60, bg_frames=120, far_verts=4096, xlu=4,
                         fogless_frames=kw.get('fogless'), pano_frames=kw.get('pano'))
    body.append(l_live(kw.get('frames', 420)))
    return body


@fixture('pass-subset', 'positive control (subset scope must not claim full)',
         PASS, '', full_qual=False, legacy='PASS')
def f_pass_subset(d):
    arms = base_pair()
    rec = record(arms, scope='subset')
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='absent', performance_row=False)


# ================ the clip-prepass candidate group (#95) ===================
#
# A candidate arm's evidence differs from every other arm's in one structural
# way: the vertex PROGRAM's own per-depth columns on the #88 line read ZERO
# while the prepass does the work, because a prepass walk leaves gpu_walk 0 and
# selects pool 0. So the healthy candidate stderr below carries a #88 census
# whose far columns never move, beside a #95 census whose counters do - and a
# fixture built the other way round is the inversion defect.
TF_CANDIDATE_W = [
    'CENSUS_REPORT_PRESENT', 'FAR_FRAMES_POSITIVE',
    'CAPTURE_SET_EXACT', 'CAPTURE_SCHEDULE_EXACT',
    'TF_STATE_READY', 'TF_PREPASSES_POSITIVE', 'TF_VERTICES_POSITIVE',
    'TF_SUBMITTED_POSITIVE', 'TF_SOURCE_DEPTH_FAR0_POSITIVE',
    'TF_SOURCE_DEPTH_FAR1_POSITIVE', 'TF_WALKS_POSITIVE',
    'TF_ADMITTED_POSITIVE', 'TF_REFUSALS_ARE_NUMBERS_AND_A_FALLBACK_FAILS',
    'TF_REUSES_ARE_NOT_COVERAGE', 'TF_REFILL_AND_EVICTION_READ_SEPARATELY',
    'TF_SOURCE_DEPTH_SINGLE_IS_A_CONSISTENCY_READING',
    'PROGRAM_COLUMNS_MUST_INVERT']

TF_OFF_W = ['CENSUS_REPORT_PRESENT', 'FAR_FRAMES_POSITIVE',
            'CAPTURE_SET_EXACT', 'CAPTURE_SCHEDULE_EXACT', 'TF_OFF_ARMS_READ_ZERO']

TF_ON_ENV = {'SL_PERF_OPT': '1', 'SL_PERF_BG_FAR': '1', 'SL_DL_CENSUS': '1',
             'SL_BG_GPU_XFORM': '1', 'SL_BG_GPU_BYPASS': '0', 'SL_SKY_BATCH': '0',
             'SL_BG_TF_CLIP': '1'}
TF_BASELINE_ENV = {'SL_PERF_OPT': '0', 'SL_PERF_BG_FAR': '1', 'SL_DL_CENSUS': '1',
                   'SL_BG_GPU_XFORM': '0', 'SL_BG_GPU_BYPASS': '0',
                   'SL_SKY_BATCH': '0'}

# THE PRODUCTION SELECTION'S OWN ARM: THE ROW, AND NOTHING NAMED.
#
# This is the launch a player actually gets, and it is the one arm shape no
# earlier fixture had. TF_ON_ENV names SL_BG_TF_CLIP=1, which is the DEVELOPER
# selection; this map names no prepass selector at all - no SL_BG_TF_CLIP, no
# SL_BG_TF_AUX_REAL, no main-context diagnostic - and relies on the OPTIMIZED
# RENDERING row being ON by default in a player session. SL_PERF_OPT=1 is the
# row, not a selector.
#
# WHY IT HAS TO BE A FIXTURE OF ITS OWN. "The checker reads this as a candidate"
# is the single assumption every TF_* witness and the program-column guard now
# rest on, and it is reached through a DIFFERENT branch of is_candidate_arm than
# the developer switch: the switch returns True on its first test, while this map
# falls through every disqualifying key to the final return. A regression that
# made this arm read as a non-prepass arm would invert PROGRAM_COLUMNS_MUST_INVERT
# and fail TF_OFF_ARMS_READ_ZERO on a working run, and no CLIP=1 fixture can
# notice it.
TF_PRODUCTION_ENV = {'SL_PERF_OPT': '1', 'SL_PERF_BG_FAR': '1',
                     'SL_DL_CENSUS': '1', 'SL_BG_GPU_BYPASS': '0',
                     'SL_SKY_BATCH': '0'}


def tf_arm(name='TF_ON', witnesses=None, env=None, role='candidate'):
    return arm(name, 'T', pose='start-pad', role=role, shots=shots3(),
               witnesses=PREFLIGHT + (witnesses or TF_CANDIDATE_W),
               env=dict(TF_ON_ENV if env is None else env))


def tf_stderr(reports, program_far=(0, 0), frames=420, diags=None,
              bracket='closed'):
    """A candidate arm's stderr: a real capture bracket over BOTH censuses.

    `reports` is the three #95 readings - before the first capture, between,
    after the last - as kwargs for l_tf95. `program_far` is the vertex
    PROGRAM's far columns, which on a working candidate never move: the pair
    is (per-report calls, per-report indices) and the default (0, 0) is what a
    candidate that took the prepass instead of the program produces.

    `diags` are the module's OWN diagnostic lines, placed where the module
    emits them: at program-build time, ahead of every census tick. They share
    the census prefix and are not censuses.

    `bracket` places the census reports relative to the captures. The #95
    counters are CUMULATIVE over the process, so where the reports fall is
    what decides whether a captured-window reading exists at all:

      'closed'   one report before the first capture and one after the last.
                 The only placement a capture-window reading may be taken
                 from, and the layout every earlier fixture uses.
      'no-pre'   every report AFTER the last capture. The window has no
                 measured origin: work that accrued before the first capture
                 is then indistinguishable from work done inside the window,
                 so the cumulative total is all that is left and it is not the
                 reading that was asked for. A first capture moment that
                 precedes the first census tick produces exactly this.
      'no-post'  a report before the first capture and none after the last, so
                 the window's END is unmeasured.
    """
    pf = [(0, 0), program_far, tuple(2 * x for x in program_far)]
    body = [l_rng(), l_waset(), l_loaded('background', 'runway'), l_gpu_ready()]
    body += (diags or [])
    shots = [l_shot('shot-00000.ppm', 240, 120),
             l_shot('shot-00001.ppm', 360, 180),
             l_shot('shot-00002.ppm', 480, 240)]

    def block(i, frame, bg):
        out = census_block(frame, far0=pf[i], far1=pf[i], far_frames=bg // 2,
                           bg_frames=bg, far_verts=4096, xlu=4)
        out.append(l_tf95(**reports[i]))
        return out

    if bracket == 'no-pre':
        # The census frame numbers carry the claim: the first tick is past the
        # last capture moment, so no report precedes the first capture.
        body += shots
        for i, (frame, bg) in enumerate(((241, 240), (301, 300), (361, 360))):
            body += block(i, frame, bg)
    elif bracket == 'no-post':
        body += block(0, 61, 60)
        body.append(shots[0])
        body += block(1, 121, 120)
        body += shots[1:]
    elif bracket == 'closed':
        for i, (frame, bg, nshots) in enumerate(
                ((61, 60, 0), (121, 120, 1), (301, 300, 2))):
            body += block(i, frame, bg)
            if nshots == 0:
                body.append(shots[0])
            elif nshots == 1:
                body += shots[1:]
    else:
        raise ValueError('unknown bracket placement: %r' % (bracket,))
    body.append(l_live(frames))
    return body


def tf_healthy(**over):
    """The three #95 reports of a candidate that worked, cumulative.

    SINGLE stays 0 throughout: at this recipe every per-depth report read 0
    calls / 0 indices at the single-range clamp while both far columns ran, so
    0 is the expected consistency reading and not a gap in the fixture.
    """
    base = [
        dict(state='ready', prepasses=4, vertices=9000, reuses=1, binds=2,
             submitted=3, indices=3000, single=0, far0=2, far1=1, walks=2,
             admitted=2, slot_refills=1),
        dict(state='ready', prepasses=9, vertices=20000, reuses=4, binds=5,
             submitted=7, indices=7000, single=0, far0=4, far1=3, walks=5,
             admitted=5, slot_refills=2),
        dict(state='ready', prepasses=15, vertices=34000, reuses=9, binds=9,
             submitted=12, indices=12000, single=0, far0=7, far1=5, walks=9,
             admitted=9, slot_refills=3),
    ]
    for r in base:
        r.update(over)
    return base


@fixture('tf-pass-candidate',
         'positive control for the clip prepass: the candidate submitted, and '
         'the vertex program\'s own far columns stayed at zero because the '
         'prepass took the walk instead of the program',
         PASS, '', legacy='N/A')
def f_tf_pass(d):
    a = tf_arm()
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': tf_stderr(tf_healthy())},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-census-malformed',
         'the #95 line printed and its fields did not parse: a format drift '
         'read as an ABSENT line would report a broken parser as "the '
         'candidate was off"',
         FAIL, 'TF_PREPASSES_POSITIVE', legacy='N/A')
def f_tf_malformed(d):
    a = tf_arm()
    body = tf_stderr(tf_healthy())
    out = []
    for line in body:
        if line.startswith('#95 background GPU clip prepass'):
            # The prefix the engine prints, and then a tail the grammar does
            # not accept. This is a drift, not an absence.
            out.append('#95 background GPU clip prepass: ready, 4 prepasses '
                       'over 9000 vertices and then the format moved on')
        else:
            out.append(line)
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': out}, {'TF_ON': three_caps()},
                manual='absent', performance_row=False)


@fixture('tf-zero-coverage',
         'the candidate was asked for, the module reported ready and every '
         'counter on the #95 line stayed at zero: nothing was transformed and '
         'nothing was drawn',
         FAIL, 'TF_PREPASSES_POSITIVE', legacy='N/A')
def f_tf_zero(d):
    a = tf_arm()
    zero = [dict(state='ready')] * 3
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': tf_stderr(zero)}, {'TF_ON': three_caps()},
                manual='absent', performance_row=False)


@fixture('tf-binds-and-reuses-only',
         'binds and reuses rose while SUBMITTED stayed at zero. A BIND IS NOT '
         'A DRAW and a reuse is work AVOIDED, so a coverage sum that admitted '
         'either would read "the prepass drew nothing" as coverage',
         FAIL, 'TF_SUBMITTED_POSITIVE', legacy='N/A')
def f_tf_binds_only(d):
    a = tf_arm()
    # Pools were bound and draws were served from already-filled slots, so the
    # two excluded counters climb steeply - and submitted runs and their index
    # total never leave zero. Prepasses and vertices are positive, which is
    # what makes this a test of the SUBMITTED rule specifically: the arm looks
    # busy on every field except the one that counts drawing.
    reports = [
        dict(state='ready', prepasses=4, vertices=9000, reuses=40, binds=30,
             submitted=0, indices=0, far0=0, far1=0, walks=2, admitted=2),
        dict(state='ready', prepasses=9, vertices=20000, reuses=90, binds=70,
             submitted=0, indices=0, far0=0, far1=0, walks=5, admitted=5),
        dict(state='ready', prepasses=15, vertices=34000, reuses=150, binds=120,
             submitted=0, indices=0, far0=0, far1=0, walks=9, admitted=9),
    ]
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': tf_stderr(reports)}, {'TF_ON': three_caps()},
                manual='absent', performance_row=False)


@fixture('tf-refusal-is-not-coverage',
         'the prepass submitted AND refused: work the candidate refused ran on '
         'the CPU, so an arm claimed as GPU coverage with a nonzero refusal is '
         'not coverage of the GPU path',
         FAIL, 'TF_REFUSALS_ARE_NUMBERS_AND_A_FALLBACK_FAILS', legacy='N/A')
def f_tf_refusal(d):
    a = tf_arm()
    reports = tf_healthy()
    for i, n in enumerate((1, 3, 6)):
        reports[i]['refused_draws'] = n
        reports[i]['module_refusals'] = n
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': tf_stderr(reports)}, {'TF_ON': three_caps()},
                manual='absent', performance_row=False)


@fixture('tf-program-columns-not-inverted',
         'a candidate arm whose vertex PROGRAM far columns rose: the program '
         'took the walk instead of the prepass, which is the one reading that '
         'makes the #95 counters uninterpretable as the candidate\'s own',
         FAIL, 'PROGRAM_COLUMNS_MUST_INVERT', legacy='N/A')
def f_tf_not_inverted(d):
    a = tf_arm()
    rec = record([a], scope='subset')
    return emit(d, rec,
                {'TF_ON': tf_stderr(tf_healthy(), program_far=(120, 3600))},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-inverted-witness-declared',
         'a candidate arm DECLARED WINDOW_FAR0_DELTA_POSITIVE, a witness that '
         'reads the vertex program\'s columns and therefore FAILS precisely '
         'when the prepass works',
         FAIL, 'TF_CANDIDATE_WITNESS_SET_VALID', legacy='N/A')
def f_tf_inverted_witness(d):
    a = tf_arm(witnesses=TF_CANDIDATE_W + ['WINDOW_FAR0_DELTA_POSITIVE'])
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': tf_stderr(tf_healthy())},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-production-row-candidate',
         'positive control for the PRODUCTION selection: an arm that names no '
         'prepass selector at all is read as a candidate, because the '
         'OPTIMIZED RENDERING row is ON by default in a player session - so '
         'its TF_* witnesses apply and the vertex program\'s far columns are '
         'required to stay at zero',
         PASS, '', legacy='N/A')
def f_tf_production_candidate(d):
    a = tf_arm(env=dict(TF_PRODUCTION_ENV))
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': tf_stderr(tf_healthy())},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-production-inverted-witness-declared',
         'the program-column guard on the PRODUCTION route: an arm that names '
         'NOTHING still may not declare a witness reading the vertex '
         'program\'s far columns. This is the half a CLIP=1 fixture cannot '
         'reach, and the one a reader is most likely to assume is exempt '
         'because no selector appears in the arm at all',
         FAIL, 'TF_CANDIDATE_WITNESS_SET_VALID', legacy='N/A')
def f_tf_production_inverted_witness(d):
    a = tf_arm(env=dict(TF_PRODUCTION_ENV),
               witnesses=TF_CANDIDATE_W + ['WINDOW_FAR1_DELTA_POSITIVE'])
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': tf_stderr(tf_healthy())},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-single-depth-nonzero',
         'the single-range source depth submitted at a recipe measured to '
         'submit nothing there. A consistency reading expected to be 0 is not '
         'improved by a nonzero: it is a finding to investigate',
         FAIL, 'TF_SOURCE_DEPTH_SINGLE_IS_A_CONSISTENCY_READING', legacy='N/A')
def f_tf_single(d):
    a = tf_arm()
    reports = tf_healthy()
    for i, n in enumerate((1, 2, 4)):
        reports[i]['single'] = n
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': tf_stderr(reports)}, {'TF_ON': three_caps()},
                manual='absent', performance_row=False)


@fixture('tf-off-arm-without-the-off-phrase',
         'an off arm read 0 prepasses and did NOT carry the off phrase: zero '
         'counters alone are also what a candidate that was asked for and did '
         'nothing produces, so the two states would be collapsed',
         FAIL, 'TF_OFF_ARMS_READ_ZERO', legacy='N/A')
def f_tf_off_phrase(d):
    a = arm('ORIG_CPU', 'T', pose='start-pad', role='off', shots=shots3(),
            witnesses=PREFLIGHT + TF_OFF_W, env=dict(TF_BASELINE_ENV))
    # Asked for, ready, and it did nothing: no off phrase, every counter zero.
    reports = [dict(state='ready', off=False)] * 3
    rec = record([a], scope='subset')
    return emit(d, rec, {'ORIG_CPU': tf_stderr(reports)},
                {'ORIG_CPU': three_caps()}, manual='absent',
                performance_row=False)


@fixture('tf-off-arm-pass',
         'positive control for the baseline arm: 0 prepasses AND the off '
         'phrase, which together say the candidate was never asked for',
         PASS, '', legacy='N/A')
def f_tf_off_pass(d):
    a = arm('ORIG_CPU', 'T', pose='start-pad', role='off', shots=shots3(),
            witnesses=PREFLIGHT + TF_OFF_W, env=dict(TF_BASELINE_ENV))
    reports = [dict(state='not asked', off=True)] * 3
    rec = record([a], scope='subset')
    return emit(d, rec, {'ORIG_CPU': tf_stderr(reports)},
                {'ORIG_CPU': three_caps()}, manual='absent',
                performance_row=False)


# ==== two classification and bracket defects in the #95 reading ============
#
# Both were found by review of the helper rather than by a run, and both fail
# in the direction that costs a live start: one refused a healthy candidate,
# the other accepted an unmeasured one.
#
#   the classification defect  Every line carrying the '#95 background GPU
#       clip prepass' prefix was counted as a census report. The module prints
#       a ready banner with that prefix the moment the program links, so a
#       SUCCESSFUL candidate always had one more prefix than parsed report and
#       read MALFORMED - every TF gate failing on the first good run. Two of
#       the four known diagnostics are written in the COLON form, so a rule
#       that exempted the banner alone would still have reported a driver that
#       refused the program as a parser fault.
#
#   the bracket-origin defect  The reading before the first capture defaulted
#       to an all-zero counter set. The counters are cumulative, so a run
#       whose every report landed AFTER the first capture still reported a
#       closed 'capture-bracket' and subtracted that invented origin: work
#       accrued before the window read as work done inside it.
#
# THE TWO GROUPS BELOW ARE KEPT SEPARATE ON PURPOSE. A fixture carrying both a
# diagnostic line and a broken bracket is refused by the old rule for the
# classification defect before the bracket rule is ever reached, so it would
# pin nothing about the bracket. The bracket fixtures therefore carry no
# diagnostic lines, and that is why - not an oversight about what a real log
# contains.

# The fixtures built against the two defects above, as opposed to the earlier
# #95 group, which predates the checker having any #95 grammar at all. The two
# groups need different legacy re-implementations, and the name list is what
# routes between them.
TF_DEFECT_FIXTURES = frozenset((
    'tf-ready-banner-pass', 'tf-plural-forms-pass', 'tf-banner-only-no-census',
    'tf-unavailable-diagnostic', 'tf-program-refused-colon-form',
    'tf-malformed-census-beside-the-banner', 'tf-missing-pre-report',
    'tf-bracket-did-not-close', 'tf-zero-delta-large-totals',
    'tf-off-arm-unrecognised-switch-value',
))

# The twelve witnesses that read the #95 line itself, without the ones that
# need the #88 per-depth census underneath them. An arm that ran without a
# statistics reader has no #88 census to read and must not declare them.
TF_95_ONLY_W = [
    'TF_STATE_READY', 'TF_PREPASSES_POSITIVE', 'TF_VERTICES_POSITIVE',
    'TF_SUBMITTED_POSITIVE', 'TF_SOURCE_DEPTH_FAR0_POSITIVE',
    'TF_SOURCE_DEPTH_FAR1_POSITIVE', 'TF_WALKS_POSITIVE',
    'TF_ADMITTED_POSITIVE', 'TF_REFUSALS_ARE_NUMBERS_AND_A_FALLBACK_FAILS',
    'TF_REUSES_ARE_NOT_COVERAGE', 'TF_REFILL_AND_EVICTION_READ_SEPARATELY',
    'TF_SOURCE_DEPTH_SINGLE_IS_A_CONSISTENCY_READING']


def tf_flat_totals(**over):
    """Three IDENTICAL cumulative #95 reports, each one large.

    Identical reports say that every counter had already reached these values
    when the first report printed and that nothing accrued after it. The
    whole-run totals are therefore big and the work provably happened outside
    any window the reports bracket - which is the one case that separates a
    reading taken over a measured bracket from a reading taken off the totals.
    """
    base = dict(state='ready', prepasses=40, vertices=90000, reuses=18,
                binds=22, submitted=30, indices=31000, single=0, far0=18,
                far1=12, walks=25, admitted=25, slot_refills=9)
    base.update(over)
    return [dict(base) for _ in range(3)]


@fixture('tf-ready-banner-pass',
         'the ACTUAL ready banner a working candidate always prints, beside '
         'healthy census reports: the banner shares the census prefix and is '
         'not a census, and counting it as one failed every TF gate on a run '
         'that had in fact succeeded',
         PASS, '', legacy='N/A')
def f_tf_banner_pass(d):
    a = tf_arm()
    rec = record([a], scope='subset')
    return emit(d, rec,
                {'TF_ON': tf_stderr(tf_healthy(), diags=[l_tf95_ready()])},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-plural-forms-pass',
         'every plural suffix in the census grammar exercised in BOTH forms '
         'inside one log - a counter of exactly 1 prints the singular - beside '
         'the actual ready banner. A grammar tested only on plurals stops '
         'reading silently the moment a counter reaches 1',
         PASS, '', legacy='N/A')
def f_tf_plural(d):
    a = tf_arm()
    # The PRE report sets every suffixed counter to exactly 1, so that line is
    # written in the singular throughout; the two later reports are plural,
    # except `evictions`, which reaches exactly 1 and prints singular beside
    # plural neighbours. The delta across the bracket is positive on every
    # counter a coverage claim is built from, so the arm passes on its numbers
    # rather than on a relaxed rule.
    reports = [
        dict(state='ready', prepasses=1, vertices=1, reuses=1, binds=1,
             submitted=1, indices=1, single=0, far0=1, far1=1, walks=1,
             admitted=1, slot_refills=1, evictions=0),
        dict(state='ready', prepasses=6, vertices=12000, reuses=3, binds=4,
             submitted=5, indices=5000, single=0, far0=3, far1=2, walks=4,
             admitted=4, slot_refills=2, evictions=1),
        dict(state='ready', prepasses=12, vertices=26000, reuses=7, binds=8,
             submitted=10, indices=11000, single=0, far0=6, far1=4, walks=8,
             admitted=8, slot_refills=3, evictions=1),
    ]
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': tf_stderr(reports, diags=[l_tf95_ready()])},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-banner-only-no-census',
         'the ready banner and no census report at all, which is what a run '
         'without a statistics reader produces: the banner is unconditional '
         'and the census line is census-branch only. This must read as NO '
         'CENSUS - an absence - and never as a malformed report',
         INCOMPLETE, 'TF_PREPASSES_POSITIVE', legacy='N/A')
def f_tf_banner_only(d):
    env = dict(TF_ON_ENV)
    del env['SL_DL_CENSUS']
    a = tf_arm(witnesses=['CAPTURE_SET_EXACT', 'CAPTURE_SCHEDULE_EXACT']
               + TF_95_ONLY_W, env=env)
    body = [l_rng(), l_waset(), l_loaded('background', 'runway'),
            l_gpu_ready(), l_tf95_ready(),
            l_shot('shot-00000.ppm', 240, 120),
            l_shot('shot-00001.ppm', 360, 180),
            l_shot('shot-00002.ppm', 480, 240),
            l_live(420)]
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': body}, {'TF_ON': three_caps()},
                manual='absent', performance_row=False)


@fixture('tf-unavailable-diagnostic',
         'the module refused the candidate and said why. The refusal must be '
         'reported as the refusal it is - a CPU fallback, never coverage - '
         'rather than as a parser fault, and it must not be read as coverage '
         'either',
         FAIL, 'TF_STATE_READY', legacy='N/A')
def f_tf_unavailable(d):
    a = tf_arm()
    why = 'transform feedback entry points are not all present'
    # tf_refuse latches g_why, so every later census prints THAT reason in its
    # state field with every counter still at zero. Both halves are the real
    # shape: the diagnostic line, and the censuses that follow it.
    reports = [dict(state=why) for _ in range(3)]
    rec = record([a], scope='subset')
    return emit(d, rec,
                {'TF_ON': tf_stderr(reports, diags=[l_tf95_unavailable(why)])},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-program-refused-colon-form',
         'the driver refused the program, whose two diagnostics are written in '
         'the COLON form and so look like censuses to a prefix test. A rule '
         'that exempted only the ready banner would still have called a '
         'refused program a parser fault',
         FAIL, 'TF_STATE_READY', legacy='N/A')
def f_tf_colon_diags(d):
    a = tf_arm()
    why = 'the prepass program did not compile'
    reports = [dict(state=why) for _ in range(3)]
    rec = record([a], scope='subset')
    # The compile log and the refusal, in the order the source prints them
    # (sl_gfx_bgtf.c:397 then :398 -> :263). The link diagnostic carries the
    # same shape and is included so both colon forms are exercised.
    diags = [l_tf95_nocompile(), l_tf95_nolink(), l_tf95_unavailable(why)]
    return emit(d, rec, {'TF_ON': tf_stderr(reports, diags=diags)},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-malformed-census-beside-the-banner',
         'a drifted census line in the SAME log as the real ready banner: the '
         'exemption must subtract the banner and no more, so the drift is '
         'still MALFORMED. Exempting by count rather than by pattern would '
         'have opened exactly this hole',
         FAIL, 'TF_PREPASSES_POSITIVE', legacy='N/A')
def f_tf_malformed_with_banner(d):
    a = tf_arm()
    body = tf_stderr(tf_healthy(), diags=[l_tf95_ready()])
    out = []
    for line in body:
        if line.startswith('#95 background GPU clip prepass:'):
            out.append('#95 background GPU clip prepass: ready, 4 prepasses '
                       'over 9000 vertices and then the format moved on')
        else:
            out.append(line)
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': out}, {'TF_ON': three_caps()},
                manual='absent', performance_row=False)


@fixture('tf-missing-pre-report',
         'every #95 report landed AFTER the first capture, with large '
         'whole-run totals and no accrual at all inside the captured window. '
         'An invented zero origin turns that pre-window accrual into a '
         'positive reading over a window nothing was measured in',
         INCOMPLETE, 'TF_PREPASSES_POSITIVE', legacy='PASS')
def f_tf_missing_pre(d):
    a = tf_arm()
    rec = record([a], scope='subset')
    return emit(d, rec,
                {'TF_ON': tf_stderr(tf_flat_totals(), bracket='no-pre')},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-bracket-did-not-close',
         'a report before the first capture and none after the last, so the '
         "window's end is unmeasured. The whole-run totals are the weaker "
         'reading and must not be substituted for the one that was asked for',
         INCOMPLETE, 'TF_PREPASSES_POSITIVE', legacy='N/A')
def f_tf_no_post(d):
    a = tf_arm()
    rec = record([a], scope='subset')
    return emit(d, rec,
                {'TF_ON': tf_stderr(tf_flat_totals(), bracket='no-post')},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-zero-delta-large-totals',
         'a bracket that closes on both sides over IDENTICAL reports: the '
         'delta is zero while the whole-run totals are large. The totals are '
         'in the same log and in the same fields, so a reading that fell back '
         'to them would report this as full coverage',
         FAIL, 'TF_PREPASSES_POSITIVE', legacy='N/A')
def f_tf_zero_delta(d):
    a = tf_arm()
    rec = record([a], scope='subset')
    return emit(d, rec, {'TF_ON': tf_stderr(tf_flat_totals())},
                {'TF_ON': three_caps()}, manual='absent', performance_row=False)


@fixture('tf-off-arm-unrecognised-switch-value',
         'an off arm whose SL_BG_TF_CLIP held neither 0 nor 1. The engine says '
         'so on a #95 line with a DIFFERENT prefix, which is neither a census '
         'nor one of the four known census-prefix diagnostics, so no count may '
         'move for it and the arm must read zero TF work exactly as before',
         PASS, '', legacy='N/A')
def f_tf_off_bad_switch(d):
    env = dict(TF_BASELINE_ENV)
    env['SL_BG_TF_CLIP'] = 'yes'
    a = arm('ORIG_CPU', 'T', pose='start-pad', role='off', shots=shots3(),
            witnesses=PREFLIGHT + TF_OFF_W, env=env)
    # An unrecognised value leaves sl_bgtf_opt_in() false, so the state field
    # reads 'not asked' and the off phrase is printed - the same reading an
    # absent switch produces, which is what 'unchanged' means here.
    reports = [dict(state='not asked', off=True) for _ in range(3)]
    rec = record([a], scope='subset')
    return emit(d, rec,
                {'ORIG_CPU': tf_stderr(reports, diags=[l_tf95_bad_switch('yes')])},
                {'ORIG_CPU': three_caps()}, manual='absent',
                performance_row=False)


# ---- defect 1: the scratch config and the asset roots --------------------
@fixture('d1-presentation-original',
         '1: scratch config wrote optimized_rendering alone, so presentation '
         'defaulted to ORIGINAL and the world assets were dark',
         FAIL, 'PRESENTATION_XBLA_ACTIVE', legacy='PASS')
def f_d1_presentation(d):
    a = arm('ACQ-runway', 'ACQ', pose='start-pad', env={'SL_DL_CENSUS': '1'},
            witnesses=PREFLIGHT + ['CENSUS_REPORT_PRESENT', 'GPU_PROGRAM_READY'])
    rec = record([a], scope='subset')
    # ORIGINAL derives the EMPTY world-asset set, so g_set prints as nothing.
    return emit(d, rec, {'ACQ-runway': acq_stderr(set='')}, manual='absent',
                performance_row=False)


@fixture('d1-root-unbound',
         '1: the installed asset roots were never bound, so the run read the '
         'player-data ladder instead',
         FAIL, 'ASSET_ROOT_BOUND', legacy='PASS')
def f_d1_root(d):
    a = arm('ACQ-runway', 'ACQ', pose='start-pad', env={'SL_DL_CENSUS': '1'},
            witnesses=PREFLIGHT + ['CENSUS_REPORT_PRESENT'])
    rec = record([a], scope='subset')
    return emit(d, rec, {'ACQ-runway': acq_stderr(
        root=r'C:\Users\me\AppData\Local\sightline\assets\worldassets',
        **{'from': 'player data'})}, manual='absent', performance_row=False)


@fixture('d1-stage-item-missing',
         '1: no preflight witness, so a run that never loaded the declared '
         "stage's asset was trusted for pose selection",
         FAIL, 'STAGE_ITEM_LOADED', legacy='PASS')
def f_d1_item(d):
    a = arm('ACQ-runway', 'ACQ', pose='start-pad', env={'SL_DL_CENSUS': '1'},
            witnesses=PREFLIGHT + ['CENSUS_REPORT_PRESENT'])
    rec = record([a], scope='subset')
    return emit(d, rec, {'ACQ-runway': acq_stderr(loaded=[('background', 'surface')])},
                manual='absent', performance_row=False)


# ---- defect 2: no fixed initial state ------------------------------------
@fixture('d2-seed-missing',
         '2: no SL_RNG_SEED, so the two halves of a pair were seeded '
         'independently',
         FAIL, 'SEED_ECHOED', legacy='PASS')
def f_d2_seed(d):
    a = arm('ACQ-runway', 'ACQ', pose='start-pad', env={'SL_DL_CENSUS': '1'},
            witnesses=PREFLIGHT + ['CENSUS_REPORT_PRESENT'])
    rec = record([a], scope='subset')
    return emit(d, rec, {'ACQ-runway': acq_stderr(
        seed='00000000deadbeef', how='host clock')}, manual='absent',
        performance_row=False)


# ---- defect 3: measured with the switch that disables it -----------------
@fixture('d3-census-disables-feature',
         '3: the worker arm set SL_DL_CENSUS=1 to obtain the report line, and '
         'the census is one of the readers that refuses the feature',
         FAIL, 'NO_CENSUS_REQUESTED', legacy='PASS')
def f_d3(d):
    a = arm('G1-reduction', 'G', stage='silo', stage_number=20, item='silo',
            frames=600, env={'SL_DL_CENSUS': '1'},
            witnesses=PREFLIGHT + ['NO_CENSUS_REQUESTED', 'REDUCTION_REPORT_PRESENT',
                                   'REDUCED_WALKS_POSITIVE', 'UNSOUND_DRAWS_ZERO'])
    rec = record([a], scope='subset')
    # A census arm carrying a REDUCED line with positive walks: the old rule
    # read `reduced > 0` and passed. The corrected rule refuses the arm, because
    # the switch that makes the report printable is the switch that disables the
    # reduction it reports.
    body = [l_rng(), l_waset(), l_loaded('background', 'silo'), l_teleport()]
    body += census_block(61, single=(400, 12000), far_frames=0, bg_frames=120,
                         fogless_frames=120)
    body.append(l_reduced(300, 0, 0))
    body.append(l_live(600))
    return emit(d, rec, {'G1-reduction': body}, manual='absent',
                performance_row=False)


# ---- defect 4: the unsound-masking order ---------------------------------
@fixture('d4-unsound-late',
         '4: an early REFUSED with unsound=0 overwrote a later REDUCED with '
         'unsound>0 while reduced>0 still held',
         FAIL, 'UNSOUND_DRAWS_ZERO', legacy='PASS')
def f_d4_late(d):
    a = arm('G1-reduction', 'G', stage='silo', stage_number=20, item='silo',
            frames=600, witnesses=PREFLIGHT + [
                'NO_CENSUS_REQUESTED', 'REDUCTION_REPORT_PRESENT',
                'REDUCED_WALKS_POSITIVE', 'UNSOUND_DRAWS_ZERO'])
    rec = record([a], scope='subset')
    # Order: REDUCED(unsound 0) ... REFUSED(unsound 0) ... REDUCED(unsound 3).
    # The last REFUSED reads 0 and the last REDUCED reads reduced>0, so the old
    # two-pass parse produced reduced=600 with unsound=0.
    events = [l_reduced(300, 1, 0), l_refused(900, 1, 0), l_reduced(600, 2, 3)]
    return emit(d, rec, {'G1-reduction': reduction_stderr(600, events)},
                manual='absent', performance_row=False)


@fixture('d4-unsound-early',
         '4: the reverse order - a late REFUSED with unsound=0 after an early '
         'REDUCED with unsound>0',
         FAIL, 'UNSOUND_DRAWS_ZERO', legacy='PASS')
def f_d4_early(d):
    a = arm('G1-reduction', 'G', stage='silo', stage_number=20, item='silo',
            frames=600, witnesses=PREFLIGHT + [
                'NO_CENSUS_REQUESTED', 'REDUCTION_REPORT_PRESENT',
                'REDUCED_WALKS_POSITIVE', 'UNSOUND_DRAWS_ZERO'])
    rec = record([a], scope='subset')
    events = [l_reduced(300, 1, 5), l_reduced(600, 1, 5), l_refused(900, 1, 0)]
    return emit(d, rec, {'G1-reduction': reduction_stderr(600, events)},
                manual='absent', performance_row=False)


# ---- defect 5: the capture sets ------------------------------------------
def _cap_fixture(d, caps_on, caps_off=None, shots_on=None, on_extra=None):
    arms = base_pair()
    if shots_on is not None:
        arms[1]['shots'] = shots_on
    rec = record(arms, scope='subset')
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on', extra=on_extra)}
    return emit(d, rec, se,
                {'B-depth-off': caps_off if caps_off is not None else three_caps(),
                 'B-depth-on': caps_on}, manual='absent', performance_row=False)


def _drop_shot(lines, name):
    return [l for l in lines if ('sl_shot: %s ' % name) not in l]


def _add_shot(lines, name, read, frame):
    out = list(lines)
    # beside the last capture the engine logged, so the bracket still closes
    for i in range(len(out) - 1, -1, -1):
        if out[i].startswith('sl_shot: '):
            out.insert(i + 1, l_shot(name, read, frame))
            return out
    out.append(l_shot(name, read, frame))
    return out


@fixture('d5-capture-missing',
         '5: the window fired fewer times than the arm DECLARED, and the old '
         'rule only compared the files against the log rather than against the '
         'declaration',
         FAIL, 'CAPTURE_SET_EXACT', legacy='PASS')
def f_d5_missing(d):
    # Two files and two sl_shot lines: self-consistent, and two short of the
    # three the arm declares. A count comparison between the files and the log
    # cannot see this at all.
    two = [{'name': 'shot-00000.ppm'}, {'name': 'shot-00001.ppm'}]
    arms = base_pair()
    rec = record(arms, scope='subset')
    se = {'B-depth-off': _drop_shot(healthy_pair_stderr('off'), 'shot-00002.ppm'),
          'B-depth-on': _drop_shot(healthy_pair_stderr('on'), 'shot-00002.ppm')}
    return emit(d, rec, se, {'B-depth-off': two, 'B-depth-on': two},
                manual='absent', performance_row=False)


@fixture('d5-capture-truncated',
         '5: a capture with a valid header and a short payload',
         FAIL, 'CAPTURE_SET_EXACT', legacy='PASS')
def f_d5_trunc(d):
    caps = three_caps()
    caps[1] = {'name': 'shot-00001.ppm', 'payload': 1024}
    off = three_caps()
    off[1] = {'name': 'shot-00001.ppm', 'payload': 1024}
    return _cap_fixture(d, caps, caps_off=off)


@fixture('d5-capture-extra',
         '5: the window fired a fourth time beside the three declared, and the '
         'files and the log agreed with each other about it',
         FAIL, 'CAPTURE_SET_EXACT', legacy='PASS')
def f_d5_extra(d):
    four = three_caps() + [{'name': 'shot-00003.ppm'}]
    arms = base_pair()
    rec = record(arms, scope='subset')
    se = {'B-depth-off': _add_shot(healthy_pair_stderr('off'), 'shot-00003.ppm', 600, 300),
          'B-depth-on': _add_shot(healthy_pair_stderr('on'), 'shot-00003.ppm', 600, 300)}
    return emit(d, rec, se, {'B-depth-off': four, 'B-depth-on': four},
                manual='absent', performance_row=False)


@fixture('d5-capture-wrongdims',
         "5: captures at a grid the arm did not declare",
         FAIL, 'CAPTURE_SET_EXACT', legacy='PASS')
def f_d5_dims(d):
    caps = [{'name': n['name'], 'w': 320, 'h': 240} for n in three_caps()]
    return _cap_fixture(d, caps, caps_off=caps)


@fixture('d5-capture-identical-wrong',
         '5: byte-identical capture sets that are not the declared set - the '
         'two halves captured different moments',
         FAIL, 'SHOT_MOMENTS_MATCH', legacy='PASS')
def f_d5_identical(d):
    # Same file names and identical bytes in both halves, so a sorted-pairing
    # byte comparison passes. The sl_shot lines say the two halves captured
    # different frames, which is what makes the identity meaningless.
    arms = base_pair()
    rec = record(arms, scope='subset')
    off = healthy_pair_stderr('off')
    on = healthy_pair_stderr('on')
    on = [l.replace('at read=240 frame=120', 'at read=600 frame=300')
           .replace('at read=360 frame=180', 'at read=720 frame=360')
           .replace('at read=480 frame=240', 'at read=840 frame=420') for l in on]
    return emit(d, rec, {'B-depth-off': off, 'B-depth-on': on},
                {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='absent', performance_row=False)


# ---- the capture-window coverage requirement ----------------------------
@fixture('dw-precapture-accrual',
         'coverage: a cumulative depth counter that accrued BEFORE the teleport '
         'and did not rise inside the captured window',
         FAIL, 'WINDOW_FAR0_DELTA_POSITIVE', legacy='PASS')
def f_dw(d):
    arms = base_pair()
    rec = record(arms, scope='subset')
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on', far_in_window=False,
                                            pre_far=(500, 15000))}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='absent', performance_row=False)


@fixture('dw-no-crossing',
         'coverage: a movement arm whose viewpoint was already past the '
         'transition before its first capture - same window delta, no crossing',
         FAIL, 'F_TRANSITION_CROSSED', legacy='N/A')
def f_dw_nocross(d):
    off = arm('F-move-off', 'F', witnesses=PREFLIGHT + [
        'CENSUS_REPORT_PRESENT', 'PAD_SCRIPT_STEPS_APPLIED', 'CAPTURE_SET_EXACT'],
        pair='F-move-on', role='off', shots=shots3(),
        pad_steps={'path': 'move.txt', 'sha256': '7' * 64, 'steps': 6})
    on = arm('F-move-on', 'F', witnesses=PREFLIGHT + [
        'CENSUS_REPORT_PRESENT', 'GPU_PROGRAM_READY', 'PAD_SCRIPT_STEPS_APPLIED',
        'WINDOW_FAR0_DELTA_POSITIVE', 'CAPTURE_SET_EXACT'],
        pair='F-move-off', role='on', shots=shots3(),
        pad_steps={'path': 'move.txt', 'sha256': '7' * 64, 'steps': 6},
        env=dict(PROGRAM_ARM_ENV))
    rec = record([off, on], scope='subset')
    se = {'F-move-off': move_stderr('off'),
          'F-move-on': move_stderr('on', crossed=False)}
    return emit(d, rec, se, {'F-move-off': three_caps(), 'F-move-on': three_caps()},
                manual='absent', performance_row=False)


# ---- defect 6: premature full PASS ---------------------------------------
@fixture('d6-missing-phase',
         '6: a full-scope run missing a mandatory phase reported acceptance on '
         'the arms it happened to carry',
         INCOMPLETE, 'MANDATORY_PHASES_PRESENT', full_qual=False, legacy='PASS')
def f_d6_phase(d):
    arms = base_pair()
    rec = record(arms, scope='full')
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='pinned', performance_row=True)


@fixture('d6-manual-absent',
         '6: the manual checklist was appended to the result without any step '
         'carrying an outcome',
         INCOMPLETE, 'MANUAL_OUTCOMES_RECORDED', full_qual=False, legacy='PASS')
def f_d6_manual(d):
    arms = base_pair()
    rec = record(arms, scope='full', mandatory=['B'])
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='absent', performance_row=True)


@fixture('d6-manual-unpinned',
         '6: recorded manual outcomes pinned to another build',
         FAIL, 'MANUAL_OUTCOMES_RECORDED', full_qual=False, legacy='PASS')
def f_d6_unpinned(d):
    arms = base_pair()
    rec = record(arms, scope='full', mandatory=['B'])
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='unpinned', performance_row=True)


@fixture('d6-no-performance-row',
         '6: full acceptance claimed with no performance row',
         INCOMPLETE, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d6_perf(d):
    arms = base_pair()
    rec = record(arms, scope='full', mandatory=['B'])
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='pinned', performance_row=False)


# ---- defect 7: discovery failure and the marks ---------------------------
@fixture('d7-discovery-failure',
         '7: discovery found no far-submitting stage, and the dependent phases '
         'had to be refused rather than reported as a subset',
         NONE, 'selection-failed.json', legacy='PASS')
def f_d7(d):
    """Built as a phase-acq.json for --select: every candidate is ineligible."""
    arms = []
    se = {}
    for nm, num, item in (('runway', 35, 'runway'), ('dam', 33, 'dam')):
        a = arm('ACQ-%s' % nm, 'ACQ', stage=nm, stage_number=num, item=item,
                pose='start-pad', env={'SL_DL_CENSUS': '1'},
                witnesses=PREFLIGHT + ['CENSUS_REPORT_PRESENT'])
        arms.append(a)
        # No far submission anywhere: far0/far1 are zero on every candidate.
        body = [l_rng(), l_waset(), l_loaded('background', item), l_gpu_ready()]
        body += census_block(61, single=(400, 12000), far_frames=0, bg_frames=120)
        body.append(l_live(420))
        se['ACQ-%s' % nm] = body
    dd = Path(d)
    dd.mkdir(parents=True, exist_ok=True)
    for a in arms:
        p = dd / a['name'] / 'stderr.txt'
        write_text(p, '\n'.join(se[a['name']]) + '\n')
        a['run']['stderr'] = str(p)
    write_json(dd / 'phase-acq.json', {'schema': 'sightline-perflive-acq/1',
                                       'arms': arms})
    return dd


@fixture('d8-counters-only-selection',
         '8 and the selector: a candidate whose counters read far-positive but '
         'whose run did not exit zero, stay live or confirm its assets',
         NONE, 'selection-failed.json', legacy='PASS')
def f_d8_sel(d):
    a = arm('ACQ-runway', 'ACQ', pose='start-pad', env={'SL_DL_CENSUS': '1'},
            witnesses=PREFLIGHT + ['CENSUS_REPORT_PRESENT'], exit_code=1)
    dd = Path(d)
    dd.mkdir(parents=True, exist_ok=True)
    # Far counters are strongly positive; the run crashed, drew a short pump and
    # loaded nothing. Counters alone used to be the whole selector.
    body = [l_rng(how='host clock'), l_waset(''), l_gpu_ready()]
    body += census_block(61, single=(400, 12000), far0=(900, 27000), far1=(900, 27000),
                         far_frames=90, bg_frames=120, far_verts=9000)
    body.append(l_live(37))
    p = dd / a['name'] / 'stderr.txt'
    write_text(p, '\n'.join(body) + '\n')
    a['run']['stderr'] = str(p)
    write_json(dd / 'phase-acq.json', {'schema': 'sightline-perflive-acq/1',
                                       'arms': [a]})
    return dd


@fixture('d7-mark-wrong-stage',
         '7: a mark of another stage silently fell back or was used as if it '
         "were the declared pose",
         FAIL, 'pose refusal: stage mismatch', legacy='PASS')
def f_d7_mark(d):
    """A marks directory for the runner's own non-launching pose binding.

    far-mountain.txt here is the real shape of the only marks on this machine:
    a LEGACY lvl33 (Dam) record with no [teleport] section. Dam is the
    documented far-NEGATIVE control, so binding it as a far pose is exactly the
    substitution the old fall back performed in silence.
    """
    dd = Path(d)
    dd.mkdir(parents=True, exist_ok=True)
    legacy_mark = (
        'sightline bug mark 1\n'
        '=====================\n'
        '\n'
        '[mark]\n'
        'mark-number         1\n'
        'run-dir             C:\\fixture/.sightline/runs/20261015-194304-lvl33\n'
        'pumped-frame        4375   (sl_frames_completed)\n'
        '\n'
        '[camera]\n'
        'camera-mode         0\n'
        'eye-world-pos       19102.100 60.312 17326.178   (viewtoworldmtxf m[3])\n'
        'look-theta-deg      66.019\n'
        'look-verta-deg      9.650  (verta360 9.650)\n'
        '\n'
        '[rooms]\n'
        'player-room         12\n')
    # Every name a pose could be asked for by, including the two acquisition
    # probes (acq-silo, acq-bunker1): a mark is honoured in the discovery phase
    # as well as the dependent ones, so a wrong-stage mark must be refused
    # there too rather than quietly binding a Dam pose to a Silo probe.
    for nm in ('far-mountain.txt', 'far-building.txt', 'far-blended.txt',
               'far-occluder.txt', 'reduction.txt', 'acq-runway.txt',
               'acq-silo.txt', 'acq-bunker1.txt'):
        write_text(dd / nm, legacy_mark)
    write_json(dd / 'manifest.json',
               {'note': 'every mark here is of stage 33 (Dam), the documented '
                        'far-NEGATIVE control; a far arm must refuse all of them',
                'mark_stage': 33})
    return dd


@fixture('d8-stale-attempt',
         '8: -Out was reused, so an earlier run\'s captures, selection and check '
         'could be consumed by a later one',
         FAIL, 'attempt directory is not empty', legacy='PASS')
def f_d8_stale(d):
    """A non-empty attempt directory, with the stale artefacts that used to be
    read by name: a selection, a check and a capture from an earlier run."""
    dd = Path(d)
    dd.mkdir(parents=True, exist_ok=True)
    write_json(dd / 'selected.json',
               {'schema': 'sightline-perflive-select/2', 'status': 'SELECTED',
                'far': {'stage': 'runway', 'stageNum': 35, 'because': 'STALE'},
                'reduction': {'stage': 'silo', 'stageNum': 20, 'because': 'STALE'},
                'skydome': {'stage': 'runway', 'stageNum': 35, 'because': 'STALE'}})
    write_json(dd / 'check.json', {'status': 'PASS', 'note': 'STALE'})
    write_ppm(dd / 'B-depth-on' / 'shot-00000.ppm', SHOT_W, SHOT_H, 0x11)
    return dd


# ---- defect 9: per-arm durable receipts ---------------------------------
@fixture('d9-launch-failure',
         '9: an arm that never produced an exit code - a launch exception or an '
         'early termination - was indistinguishable from one that passed',
         FAIL, 'EXIT_ZERO', legacy='PASS')
def f_d9(d):
    arms = base_pair()
    arms[1]['run']['exit_code'] = None
    arms[1]['run']['launch_failure'] = ('System.ComponentModel.Win32Exception: '
                                        'The system cannot find the file specified')
    rec = record(arms, scope='subset')
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='absent', performance_row=False)


@fixture('d9-timed-out',
         '9: an arm killed on the timeout still carried an exit code and was '
         'not held apart from a clean exit',
         FAIL, 'NOT_TIMED_OUT', legacy='PASS')
def f_d9_to(d):
    arms = base_pair()
    arms[1]['run']['timed_out'] = True
    arms[1]['run']['exit_code'] = 0
    rec = record(arms, scope='subset')
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='absent', performance_row=False)


# ---- defect 10: an explicit manual FAIL ----------------------------------
@fixture('d10-manual-explicit-fail',
         '10: a manual step recorded FAIL on this build, and the gate read the '
         'PRESENCE of an outcome rather than what it said, so the one '
         'measurement a person took rolled up as a pass',
         FAIL, 'MANUAL_OUTCOMES_PASSED', full_qual=False, legacy='PASS')
def f_d10_manual_fail(d):
    arms = base_pair()
    rec = record(arms, scope='full', mandatory=['B'])
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='failed', performance_row=True)


# ---- defect 11: a case is not a phase ------------------------------------
@fixture('d11-missing-pair-half',
         '11: one half of a mandatory PAIR was run and the phase read as '
         'present, so a subset of the cases qualified the whole mode',
         INCOMPLETE, 'MANDATORY_CASES_PRESENT', full_qual=False, legacy='PASS')
def f_d11_pair_half(d):
    # The movement phase's ON half alone, declaring the OFF half it was meant
    # to be compared against. Phase F is present; the case is not. None of this
    # arm's own witnesses reads the peer, so nothing else fires: the gate is the
    # only thing between this and a full-qualification claim.
    arms = base_pair()
    on = arm('F-move-on', 'F', witnesses=PREFLIGHT + [
        'CENSUS_REPORT_PRESENT', 'PAD_SCRIPT_STEPS_APPLIED',
        'WINDOW_FAR0_DELTA_POSITIVE', 'CAPTURE_SET_EXACT', 'CAPTURE_SCHEDULE_EXACT'],
        shots=shots3(), pad_steps={'path': 'move.txt', 'sha256': '7' * 64, 'steps': 6},
        role='on', pair='F-move-off', env=dict(PROGRAM_ARM_ENV))
    arms.append(on)
    rec = record(arms, scope='full', mandatory=['B', 'F'])
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on'),
          'F-move-on': move_stderr('on')}
    caps = {'B-depth-off': three_caps(), 'B-depth-on': three_caps(),
            'F-move-on': three_caps()}
    return emit(d, rec, se, caps, manual='pinned', performance_row=True)


# ---- defect 12: the capture SCHEDULE -------------------------------------
def _shift_moments(lines, pairs):
    out = []
    for l in lines:
        for a, b in pairs:
            l = l.replace(a, b)
        out.append(l)
    return out


@fixture('d12-schedule-wrong-identically',
         '12: both halves captured the same WRONG moments - identical names, '
         'identical bytes, identical sl_shot lines, and not the schedule the '
         'arms declared',
         FAIL, 'CAPTURE_SCHEDULE_EXACT', legacy='PASS')
def f_d12_schedule(d):
    # Three captures at frames 100, 160, 220 against a declared 120, 180, 240:
    # the first is outside [FIRST,LAST] and none of them is at its declared
    # moment. Every pair comparison in the checker passes, because the two
    # halves agree with each other about the wrong moments.
    shift = ((('at read=240 frame=120'), 'at read=200 frame=100'),
             (('at read=360 frame=180'), 'at read=320 frame=160'),
             (('at read=480 frame=240'), 'at read=440 frame=220'))
    arms = base_pair()
    rec = record(arms, scope='subset')
    se = {'B-depth-off': _shift_moments(healthy_pair_stderr('off'), shift),
          'B-depth-on': _shift_moments(healthy_pair_stderr('on'), shift)}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='absent', performance_row=False)


@fixture('d12b-schedule-read-units',
         '12: the arm declared a READ window and the run windowed on FRAMES '
         '(B-051: the two counters are not proportional), so the captures are '
         'moments of a different schedule and both logs read healthy',
         FAIL, 'CAPTURE_SCHEDULE_EXACT', legacy='PASS')
def f_d12b_units(d):
    # expected_moments in RECORD units, matching the read indices the run
    # logged - and the run says window=frame, so the engine was given
    # SL_SHOT_FIRST/LAST and never the read window the record declares. The
    # numbers agree; the unit does not, which is the whole defect.
    sched = shots3(moments=(240, 360, 480), read_first=240, read_last=480)
    arms = base_pair()
    arms[0]['shots'] = sched
    arms[1]['shots'] = sched
    rec = record(arms, scope='subset')
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='absent', performance_row=False)


# ---- defect 13: the performance row -------------------------------------
@fixture('d13-performance-placeholder',
         '13: a performance row of five present-but-truthy strings - no build '
         'binding, no arm names, no raw runs and no rule - read as the row '
         'being present',
         INCOMPLETE, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d13_placeholder(d):
    arms = base_pair()
    rec = record(arms, scope='full', mandatory=['B'])
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='pinned', performance_row='placeholder')


@fixture('d13b-performance-fabricated-gain',
         '13: a fully bound row whose stated screen verdict its own raw means '
         'do not support - the repeat spread is wider than the smaller paired '
         'saving, so the rule it names does not pass on its own numbers',
         FAIL, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d13b_fabricated(d):
    arms = base_pair()
    rec = record(arms, scope='full', mandatory=['B'])
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return emit(d, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()},
                manual='pinned', performance_row='fabricated')


# ---- defect 14: the selector's unproved roles ---------------------------
@fixture('d14-selection-reduction-unproved',
         '14: the reduction role was filled with the FAR candidate because no '
         'candidate carried fogless frames - the measured case: reduction = '
         'runway while that run\'s own fogless figure was null and its own '
         'reduction report read 0 reduced, refused for statistics',
         NONE, 'selected.json: reduction NOT_COVERED', legacy='PASS')
def f_d14_select(d):
    """Built as a phase-acq.json for --select: far is proved, reduction is not."""
    dd = Path(d)
    dd.mkdir(parents=True, exist_ok=True)
    arms = []
    for nm, num, item, far in (('runway', 35, 'runway', True), ('dam', 33, 'dam', False)):
        a = arm('ACQ-%s' % nm, 'ACQ', stage=nm, stage_number=num, item=item,
                pose='start-pad', env={'SL_DL_CENSUS': '1'},
                witnesses=PREFLIGHT + ['CENSUS_REPORT_PRESENT'])
        arms.append(a)
        body = [l_rng(), l_waset(), l_loaded('background', item), l_gpu_ready()]
        # NO #81 line anywhere: the stage's fogless coverage was never measured,
        # so the figure is null rather than zero. The census arm's own reduction
        # report reads REFUSED for statistics, which is the census refusing the
        # feature and is evidence for nothing about the stage.
        body += census_block(61, single=(400, 12000),
                             far0=(120, 3600) if far else (0, 0),
                             far1=(120, 3600) if far else (0, 0),
                             far_frames=60 if far else 0, bg_frames=120,
                             far_verts=4096 if far else 0)
        body.append(l_refused(120, 0, 0))
        body.append(l_live(420))
        p = dd / a['name'] / 'stderr.txt'
        write_text(p, '\n'.join(body) + '\n')
        a['run']['stderr'] = str(p)
    write_json(dd / 'phase-acq.json', {'schema': 'sightline-perflive-acq/1',
                                       'arms': arms})
    return dd


# ---- defect 15 and 16: the vertex helper's own report -------------------
def helper_arm(witnesses, interval=600):
    return arm('G4-vertexhelper', 'G', stage='silo', stage_number=20, item='silo',
               frames=600, helper={'interval_walks': interval},
               witnesses=PREFLIGHT + witnesses)


HELPER_W = ['NO_CENSUS_REQUESTED', 'VERTEX_HELPER_SPLIT_POSITIVE',
            'VERTEX_HELPER_INTERVAL_COVERED']


@fixture('d15-helper-split-equals-fp',
         '15: every split the helper took had its upper range RECOMPUTED on '
         'the render thread, so the work was done twice and the walk count in '
         'two ranges alone read as a positive',
         FAIL, 'VERTEX_HELPER_SPLIT_POSITIVE', legacy='PASS')
def f_d15_helper(d):
    a = helper_arm(HELPER_W)
    rec = record([a], scope='subset')
    # 300 splits, 300 upper ranges recomputed: the difference is 0.
    se = {a['name']: reduction_stderr(600, [],
                                      helper=[l_bgv(1, 1, 0, 1),
                                              l_bgv(300, 150, 150, 150),
                                              l_bgv(600, 300, 300, 300)])}
    return emit(d, rec, se, manual='absent', performance_row=False)


@fixture('d16-helper-milestone-short',
         '16: the last helper report is a periodic MILESTONE at 300 walks and '
         'the arm tests 600, so the walks after the checkpoint are unreported '
         'and the line was read as the run\'s final word',
         INCOMPLETE, 'VERTEX_HELPER_INTERVAL_COVERED', legacy='PASS')
def f_d16_helper_short(d):
    a = helper_arm(HELPER_W)
    rec = record([a], scope='subset')
    se = {a['name']: reduction_stderr(600, [],
                                      helper=[l_bgv(1, 1, 0, 0), l_bgv(300, 250, 50, 0)])}
    return emit(d, rec, se, manual='absent', performance_row=False)


# ---- defect 17: the replayed input stream --------------------------------
# The control-phase arms replay a pinned saved stream instead of binding a pose
# mark, so the stream IS the pose: which stream opened, and how much of it the
# run read, is the only thing that says where the arm was posed.
def replay_arm(reads=7949, sha=None):
    return arm('E-pose2-on', 'E', pose='start-pad',
               env={'SL_INPUT': INPUT_STREAM},
               control={'name': 'runway', 'input': INPUT_STREAM,
                        'input_sha256': sha, 'reads': reads},
               witnesses=PREFLIGHT + ['INPUT_STREAM_REPLAYED'])


@fixture('d17-input-stream-reads-mismatch',
         '17: the arm replayed a stream of a different length from the one it '
         'declared - a prefix, or another file at the same path - and the '
         'witness name was unknown to the checker, so it read NOT_COVERED',
         FAIL, 'INPUT_STREAM_REPLAYED', legacy='PASS')
def f_d17_reads(d):
    a = replay_arm(reads=7949)
    rec = record([a], scope='subset')
    body = [l_rng(), l_waset(), l_loaded('background', 'runway'),
            l_input(INPUT_STREAM, 4096), l_live(420)]
    return emit(d, rec, {a['name']: body}, manual='absent', performance_row=False)


@fixture('d17b-input-stream-open-failed',
         '17: the replay stream did not open, so the arm replayed nothing and '
         'the engine exited 2 where it printed that - and the unknown witness '
         'name read NOT_COVERED rather than reading the failure',
         FAIL, 'INPUT_STREAM_REPLAYED', legacy='PASS')
def f_d17b_failed(d):
    a = replay_arm(reads=7949)
    a['run']['exit_code'] = 2
    rec = record([a], scope='subset')
    # Nothing after the open is printed: the engine exits at that line, so the
    # run confirms no presentation, no asset root and no stage item either.
    body = [l_rng(), l_input_fail(INPUT_STREAM)]
    return emit(d, rec, {a['name']: body}, manual='absent', performance_row=False)


# ---- defect 18: a recorded FAIL outranked by a missing step ---------------
# The manual gate's three readings are ORDERED. Reading the absence first lets
# an unrecorded step downgrade a recorded one: the batch reports INCOMPLETE
# because some OTHER step was not performed, while a step that was performed
# says the row is broken. A measurement that something is broken is the
# strongest evidence a batch holds, and nothing about a different step's
# absence makes it weaker.
def pair_batch(**kw):
    """The healthy B pair with its captures, as every gate fixture starts."""
    arms = base_pair()
    rec = record(arms, scope='full', mandatory=['B'])
    se = {'B-depth-off': healthy_pair_stderr('off'),
          'B-depth-on': healthy_pair_stderr('on')}
    return arms, rec, se, {'B-depth-off': three_caps(), 'B-depth-on': three_caps()}


@fixture('d18-manual-fail-outranks-absence',
         '18: one step recorded FAIL on this build AND one step never recorded '
         '- the absence was read first, so the measured failure was downgraded '
         'to "nothing was measured" and the batch read INCOMPLETE rather than '
         'FAIL',
         FAIL, 'MANUAL_OUTCOMES_PASSED', full_qual=False, legacy='PASS')
def f_d18_fail_outranks_absence(d):
    arms, rec, se, caps = pair_batch()
    return emit(d, rec, se, caps, manual='failed-and-absent', performance_row=True)


# ---- defect 19-22: the row's own numbers, recomputed ---------------------
# A performance row qualified on fields it reported about itself. These four
# pin the four ways that fails: a figure that is not a time, a sample bound to
# something other than this batch, an aggregate over an arm that holds no
# sample, and a budget verdict the row's own samples contradict.
def budget_row(rec, raw, off=None, on=None, budget_met=True, rule='absolute-budget/1',
               arms_override=None):
    """A row under the absolute-budget rule, around a caller's raw records.

    Every field is present and bound - row, stage, build, the measured arm
    pair, the summary strings and a metrics block naming the rule - so the only
    thing a verdict can turn on is the raw records and the figures recomputed
    from them.
    """
    off_arm, on_arm, stage = perf_arms(rec)
    return {'row': 'OPTIMIZED RENDERING', 'stage': stage,
            'off': '12.40 ms' if off is None else off,
            'on': '11.15 ms' if on is None else on,
            'build_sha256': BUILD_SHA,
            'arms': [off_arm, on_arm] if arms_override is None else arms_override,
            'raw': raw,
            'metrics': {'rule': rule, 'budget_met': budget_met,
                        'rule_source': 'tools/profiling/READINESS.md:45-47 - mean and '
                                       'nearest-rank p99 each at most 16.667 ms at 1080p60'}}


@fixture('d19-performance-raw-malformed',
         '19: the row carried five raw figures that are not times - a NaN, an '
         'infinity, a negative, a string and a JSON `true` where a frame time '
         'belongs - and qualified on the keys being present; isinstance(True, '
         'int) is true in Python, so the boolean read as 1 ms',
         INCOMPLETE, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d19_raw_malformed(d):
    arms, rec, se, caps = pair_batch()
    off_arm, on_arm, stage = perf_arms(rec)
    nan, inf = float('nan'), float('inf')
    raw = [
        # Each record is bound correctly and carries exactly one figure that is
        # not a duration, so what refuses it is the number and nothing else.
        raw_run(0, 'A', off_arm, stage, nan, p99=14.90),
        raw_run(1, 'B', on_arm, stage, 11.10, p99=inf),
        raw_run(2, 'B', on_arm, stage, -11.20, p99=13.70),
        raw_run(3, 'A', off_arm, stage, '12.50', p99=15.00),
        raw_run(4, 'A', off_arm, stage, 12.50, p99=True),
    ]
    row = budget_row(rec, raw, off='12.50 ms', on='11.10 ms')
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


@fixture('d20-performance-raw-unbound',
         '20: the raw records were not bound to this batch - one pinned to '
         'another build, one naming an arm the batch never ran, one declaring a '
         'stage its arm did not run on - and the row qualified anyway, because '
         'a mean beside a row was taken for a measurement of it',
         INCOMPLETE, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d20_raw_unbound(d):
    arms, rec, se, caps = pair_batch()
    off_arm, on_arm, stage = perf_arms(rec)
    raw = [
        raw_run(0, 'A', off_arm, stage, 12.40),
        raw_run(1, 'B', on_arm, stage, 11.10, build='9' * 64),
        raw_run(2, 'B', 'X-depth-on', stage, 11.20),
        raw_run(3, 'A', off_arm, 'silo', 12.50),
    ]
    row = budget_row(rec, raw, off='12.40 ms')
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


@fixture('d21-performance-arm-unsampled',
         '21: every raw record was well formed and bound, and all of them '
         'measured the OFF arm - so the row aggregated an ON arm it held no '
         'sample of, and a mean over one arm stood in for the difference '
         'between two',
         INCOMPLETE, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d21_arm_unsampled(d):
    arms, rec, se, caps = pair_batch()
    off_arm, _on_arm, stage = perf_arms(rec)
    pres = Path(d) / 'pres'
    raw = [raw_run(0, 'A', off_arm, stage, 12.40, dest=pres, state_source=True),
           raw_run(1, 'A', off_arm, stage, 12.50, dest=pres, state_source=True)]
    row = budget_row(rec, raw, off='12.45 ms')
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


@fixture('d22-performance-false-budget',
         '22: a fully bound row stated budget_met=true while its own raw '
         "nearest-rank p99 is 24.90 ms - the row's self-reported verdict was "
         'read instead of being recomputed from the samples beside it',
         FAIL, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d22_false_budget(d):
    arms, rec, se, caps = pair_batch()
    off_arm, on_arm, stage = perf_arms(rec)
    pres = Path(d) / 'pres'
    # Both means sit under the 16.667 ms budget and one p99 does not, which is
    # the case READINESS.md:45-47 reads on BOTH figures rather than on the mean
    # alone. The two figures come from REAL files with one long gap among eight
    # short ones: eight intervals of 12.0875 ms and one of 14.90 give a mean of
    # exactly 12.40 and a nearest-rank p99 of 14.90, and eight of 9.375 with
    # one of 24.90 give a mean of exactly 11.10 and a p99 of 24.90. So the
    # over-budget reading is the file's own, not a figure typed beside it, and
    # the only thing this row plants is the verdict it states about them.
    raw = [raw_run(0, 'A', off_arm, stage, None, dest=pres, state_source=True,
                   interval_ms=[12.0875] * 8 + [14.90]),
           raw_run(1, 'B', on_arm, stage, None, dest=pres, state_source=True,
                   interval_ms=[9.375] * 8 + [24.90])]
    row = budget_row(rec, raw, off='12.40 ms', on='11.10 ms', budget_met=True)
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


# ---- the evidence-binding negatives ---------------------------------------
# Each of these starts from a row that is CORRECT in every other respect -
# real sources, real figures derived from them, the right pairing - and plants
# exactly one defect. A negative that is wrong in two ways cannot show which
# one the rule caught.

@fixture('d23-performance-abba-arm-sequence',
         '23: the reproduced counterexample - four runs whose ACTUAL arms were '
         'OFF,OFF,ON,ON, relabelled A,B,B,A, yielding savings [2,2] and '
         'spreads [0,0] and PASSING, while both arms had in truth averaged '
         '9 ms. The labels decided which measurements counted as A and which '
         'as B, and the saving is computed from the run positions',
         FAIL, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d23_abba_sequence(d):
    arms, rec, se, caps = pair_batch()
    off_arm, on_arm, stage = perf_arms(rec)
    pres = Path(d) / 'pres'
    # The reviewer's figures exactly: means 10,8,8,10 over the arms
    # OFF,OFF,ON,ON, so each arm's own aggregate really is 9 ms.
    seq = [off_arm, off_arm, on_arm, on_arm]
    means = [10.0, 8.0, 8.0, 10.0]
    raw = [raw_run(i, order, seq[i], stage, means[i], dest=pres)
           for i, order in enumerate(('A', 'B', 'B', 'A'))]
    row = {'row': 'OPTIMIZED RENDERING', 'stage': stage,
           'off': '9.00 ms', 'on': '9.00 ms',
           'build_sha256': BUILD_SHA, 'arms': [off_arm, on_arm], 'raw': raw,
           'metrics': {'rule': 'abba-timing-screen/1',
                       'paired_savings_ms': [2.0, 2.0],
                       'repeat_spreads_ms': [0.0, 0.0],
                       'timing_screen_passed': True,
                       'rule_source': 'tools/profiling/analyze.py compare_abba'}}
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


@fixture('d24-performance-duplicate-measured-arms',
         '24: a two-arm rule whose row named one arm as both of its measured '
         'arms, so it reported no difference between two arms at all',
         FAIL, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d24_duplicate_arms(d):
    arms, rec, se, caps = pair_batch()
    off_arm, _on, stage = perf_arms(rec)
    pres = Path(d) / 'pres'
    # All four runs really are that one arm, and `off` and `on` are resealed
    # to its own samples, so the row's aggregate is honest about what it
    # measured. What it has no quantity for is the difference between arms.
    row = reseal_abba(perf_row(rec, dest=pres, arms_override=[off_arm, off_arm]))
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


@fixture('d25-performance-source-absent',
         '25: every raw run named a presentation source that was never '
         'written, so the row\'s figures rested on their own summaries - the '
         'reviewed case where performance-row.json was the only file opened',
         FAIL, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d25_source_absent(d):
    arms, rec, se, caps = pair_batch()
    # dest=None names the sources and writes none of them.
    row = perf_row(rec)
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


@fixture('d26-performance-source-tampered',
         '26: the sources exist and recompute, but the row pins each one to a '
         'sha256 that is not the hash of those bytes',
         FAIL, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d26_source_tampered(d):
    arms, rec, se, caps = pair_batch()
    pres = Path(d) / 'pres'
    row = perf_row(rec, dest=pres)
    for r in row['raw']:
        r['source']['sha256'] = '8' * 64
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


@fixture('d27-performance-summary-edited',
         '27: a row that is perfectly consistent with ITSELF - aggregate, '
         'savings, spreads and screen all recompute from its stated means - '
         'and states one mean the unchanged file it cites does not produce',
         FAIL, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d27_summary_edited(d):
    arms, rec, se, caps = pair_batch()
    pres = Path(d) / 'pres'
    row = perf_row(rec, dest=pres)
    # One stated mean moved a whole millisecond - far past the printed-digit
    # tolerance - and the four figures derived from it resealed, so nothing
    # inside the document disagrees with anything else inside the document.
    # The file beside it is untouched and its sha256 still belongs to those
    # bytes, so only opening it shows the summary was edited. The run's p99 is
    # deliberately left at the file's own figure: no in-document rule relates
    # a mean to a p99, so moving both would plant the same defect twice.
    row['raw'][0]['mean_ms'] = 13.40
    reseal_abba(row)
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


@fixture('d28-performance-nan-derived-metrics',
         '28: a fully source-backed row whose stated savings are NaN. Every '
         'comparison with a NaN is false, so abs(stated - recomputed) > tol '
         'read as agreement and the figure passed unexamined',
         FAIL, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d28_nan_metrics(d):
    arms, rec, se, caps = pair_batch()
    pres = Path(d) / 'pres'
    nan = float('nan')
    row = perf_row(rec, dest=pres,
                   metrics_override={'paired_savings_ms': [nan, nan]})
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


# ---- the threshold negatives ----------------------------------------------
# Half a printed digit is enough to invert a verdict at a threshold. These
# three plant figures that are WITHIN the tolerance the embedded summaries are
# compared with, so the source comparison passes and nothing in the document
# disagrees with anything else in it - and the verdict is still wrong, because
# it was computed from the summary instead of from the file.

@fixture('d29-performance-threshold-rounded-budget',
         '29: a row whose sources really take 16.670 ms, summarised as '
         '16.666 ms. The 0.004 ms difference is inside the printed-digit '
         'tolerance, so the summary agrees with its source - and 16.666 then '
         'meets the 16.667 ms budget that 16.670 exceeds',
         FAIL, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d29_threshold_budget(d):
    arms, rec, se, caps = pair_batch()
    off_arm, on_arm, stage = perf_arms(rec)
    pres = Path(d) / 'pres'
    # The files run at 16.670 ms flat, so their mean AND their nearest-rank p99
    # are both over budget. The embedded summaries say 16.666.
    raw = [raw_run(0, 'A', off_arm, stage, 16.666, p99=16.666, dest=pres,
                   interval_ms=16.670),
           raw_run(1, 'B', on_arm, stage, 16.666, p99=16.666, dest=pres,
                   interval_ms=16.670)]
    # Printed to two decimals, 16.67 is within half a digit of 16.666 AND of
    # 16.670, so the human-readable summary is honest under either reading and
    # is not what this fixture plants.
    row = budget_row(rec, raw, off='16.67 ms', on='16.67 ms', budget_met=True)
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


@fixture('d30-performance-threshold-rounded-screen',
         '30: a marginal ABBA screen inverted by rounding. The sources give '
         'means 10.000, 9.996, 9.990, 10.000 - savings [0.004, 0.010] against '
         'spreads [0.000, 0.006], so the screen FAILS - while the summaries '
         'beside them, each within half a printed digit, give savings '
         '[0.012, 0.012] against spreads [0.000, 0.000] and pass',
         FAIL, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d30_threshold_screen(d):
    arms, rec, se, caps = pair_batch()
    off_arm, on_arm, stage = perf_arms(rec)
    pres = Path(d) / 'pres'
    true_ms = [10.000, 9.996, 9.990, 10.000]
    said_ms = [10.004, 9.992, 9.992, 10.004]
    seq = [off_arm, on_arm, on_arm, off_arm]
    raw = [raw_run(i, order, seq[i], stage, said_ms[i], p99=said_ms[i], dest=pres,
                   interval_ms=true_ms[i])
           for i, order in enumerate(('A', 'B', 'B', 'A'))]
    # Every derived figure here is computed from `said_ms`, so the document is
    # consistent with itself and the rule that reads it recomputes exactly
    # these numbers. Only the files disagree.
    savings = [said_ms[0] - said_ms[1], said_ms[3] - said_ms[2]]
    spreads = [abs(said_ms[0] - said_ms[3]), abs(said_ms[1] - said_ms[2])]
    row = {'row': 'OPTIMIZED RENDERING', 'stage': stage,
           # Within half a digit of the summaries AND of the sources' own
           # per-arm means (10.000 and 9.993), so the printed aggregate is not
           # the planted defect either.
           'off': '10.00 ms', 'on': '9.99 ms',
           'build_sha256': BUILD_SHA, 'arms': [off_arm, on_arm], 'raw': raw,
           'metrics': {'rule': 'abba-timing-screen/1',
                       'paired_savings_ms': savings,
                       'repeat_spreads_ms': spreads,
                       'timing_screen_passed': min(savings) > max(spreads),
                       'rule_source': 'tools/profiling/analyze.py compare_abba'}}
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


@fixture('d31-performance-source-unpinned',
         '31: a row correct in every other respect whose raw sources carry no '
         'sha256 at all. The old check was `if key in src`, so a missing key '
         'opted the source out of the comparison it then appeared to pass; '
         'nothing identifies the bytes the figures were read from, which is an '
         'absence of provenance rather than a contradiction of it',
         INCOMPLETE, 'PERFORMANCE_ROW_PRESENT', full_qual=False, legacy='PASS')
def f_d31_source_unpinned(d):
    arms, rec, se, caps = pair_batch()
    pres = Path(d) / 'pres'
    row = perf_row(rec, dest=pres)
    for r in row['raw']:
        del r['source']['sha256']
    return emit(d, rec, se, caps, manual='pinned', performance_row=row)


# ------------------------------------------------- the DEFECTIVE rules, kept
# Each reproduces the rule the fixture beside it was built against, so that
# "this fixture catches a regression" is a measurement rather than a claim. A
# fixture whose legacy verdict is not PASS is not pinning anything.
def legacy_verdict(name, d):
    d = Path(d)

    def stderr_of(armname):
        p = d / armname / 'stderr.txt'
        if not p.is_file():
            return ''
        return p.read_text(encoding='utf-8', errors='replace')

    if name in ('d1-presentation-original', 'd1-root-unbound', 'd1-stage-item-missing',
                'd2-seed-missing'):
        # The old checker had NO preflight witness at all: no presentation, no
        # bound root, no stage item, no seed. The run was trusted for pose
        # selection on its counters. Nothing could fail.
        return PASS, ('the old witness set contained no PRESENTATION_XBLA_ACTIVE, '
                      'ASSET_ROOT_BOUND, STAGE_ITEM_LOADED or SEED_ECHOED, so this '
                      'evidence had nothing to fail against')

    if name == 'd3-census-disables-feature':
        import re
        t = stderr_of('G1-reduction')
        m = None
        for m in re.finditer(r'#88 background GPU bypass: (\d+) walks? reduced', t):
            pass
        ok = bool(m) and int(m.group(1)) > 0
        return (PASS if ok else FAIL), ('the old rule read REDUCED_WALKS_POSITIVE off '
                                        'the line alone and never asked whether a '
                                        'statistics reader was armed')

    if name in ('d4-unsound-late', 'd4-unsound-early'):
        import re
        t = stderr_of('G1-reduction')
        reduced = repaired = unsound = None
        m = None
        for m in re.finditer(r'#88 background GPU bypass: (\d+) walks? reduced - '
                             r'(\d+) vertices computed whole, (\d+) to the near class '
                             r'alone;.*?(\d+) repaired, (\d+) unsound mesh draws? '
                             r'\(must be 0\)', t):
            pass
        if m:
            reduced, repaired, unsound = int(m.group(1)), int(m.group(4)), int(m.group(5))
        m2 = None
        for m2 in re.finditer(r'#88 background GPU bypass: REFUSED - (\d+) walks? '
                              r'considered, 0 reduced;.*?(\d+) repaired, (\d+) unsound '
                              r'mesh draws? \(must be 0\)', t):
            pass
        if m2:
            # THE DEFECT: repaired and unsound are overwritten from the last
            # REFUSED line whatever the order, while `reduced` keeps the REDUCED
            # line's value.
            repaired, unsound = int(m2.group(2)), int(m2.group(3))
            if reduced is None:
                reduced = 0
        ok = reduced is not None and reduced > 0 and unsound == 0
        return (PASS if ok else FAIL), ('the old parse took the last REDUCED line for '
                                        'reduced and then overwrote repaired/unsound '
                                        'from the last REFUSED line regardless of order')

    if name.startswith('d5-') or name == 'dw-precapture-accrual':
        import re
        if name == 'dw-precapture-accrual':
            t = stderr_of('B-depth-on')
            m = None
            for m in re.finditer(r"BY DEPTH (\d+) calls? / (\d+) indices at the "
                                 r"single-range clamp, (\d+) / (\d+) in a far frame's "
                                 r"first range, (\d+) / (\d+) in its game range", t):
                pass
            ok = bool(m) and int(m.group(3)) > 0 and int(m.group(4)) > 0
            return (PASS if ok else FAIL), ('the old rule read the LAST cumulative '
                                            'BY DEPTH line, which cannot distinguish '
                                            'accrual inside the captured window from '
                                            'accrual before the teleport')
        mine = sorted((d / 'B-depth-on').glob('shot-*.ppm'))
        theirs = sorted((d / 'B-depth-off').glob('shot-*.ppm'))
        nshot = len(re.findall(r'sl_shot: ', stderr_of('B-depth-on')))
        # The old SHOTS_WRITTEN: any nonzero count equal to the log's count.
        ok = len(mine) > 0 and len(mine) == nshot
        # The old FRAMES_BYTE_IDENTICAL: equal counts, sorted pairing, bytes equal.
        if ok and mine and theirs and len(mine) == len(theirs):
            for a, b in zip(mine, theirs):
                if a.read_bytes() != b.read_bytes():
                    ok = False
                    break
        elif not (mine and theirs and len(mine) == len(theirs)):
            ok = False
        return (PASS if ok else FAIL), ('the old capture rules accepted any nonzero '
                                        'equal count and a sorted pairing whose bytes '
                                        'matched - no declared count, no declared grid, '
                                        'no payload length, no name or moment identity')

    if name in ('d6-missing-phase', 'd6-manual-absent', 'd6-manual-unpinned',
                'd6-no-performance-row'):
        # The old do_check evaluated only the arms it was handed and appended
        # the checklist without gating on it. With every supplied arm passing,
        # the batch read PASS and nothing distinguished it from acceptance.
        return PASS, ('the old do_check evaluated only the arms supplied and appended '
                      'the manual checklist to the result without gating on it, and had '
                      'no concept of a mandatory phase or a performance row')

    if name in ('d7-discovery-failure', 'd8-counters-only-selection'):
        import re
        acq = json.loads((d / 'phase-acq.json').read_text(encoding='utf-8-sig'))
        arms_ = acq['arms'] if isinstance(acq, dict) else acq
        best = -1
        for a in arms_:
            t = stderr_of(a['name'])
            m = None
            for m in re.finditer(r"BY DEPTH (\d+) calls? / (\d+) indices at the "
                                 r"single-range clamp, (\d+) / (\d+) in a far frame's "
                                 r"first range, (\d+) / (\d+) in its game range", t):
                pass
            m78 = None
            for m78 in re.finditer(r'#78 background beyond the far plane: (\d+) of', t):
                pass
            if not m or not m78:
                continue
            ff, f0, f1 = int(m78.group(1)), int(m.group(3)), int(m.group(5))
            if ff and f0 and f1:
                best = max(best, f0 + f1)
        if name == 'd8-counters-only-selection':
            return (PASS if best > 0 else FAIL), (
                'the old selector scored on far counters alone and never asked whether '
                'the candidate exited zero, stayed live or confirmed its assets')
        # d7: the old runner reported the dependent phases NOT_COVERED and left
        # the operator a remedy that phase A could not act on, because phase A
        # ignored -Marks entirely.
        return PASS, ('the old phase A ignored -Marks and probed start pads only, so the '
                      'printed remedy ("supply -Marks") could not change the outcome; and '
                      'the dependent phases were described as a NOT_COVERED subset rather '
                      'than refused')

    if name == 'd7-mark-wrong-stage':
        import re
        t = (d / 'far-mountain.txt').read_text(encoding='utf-8')
        pos = re.search(r'(?m)^eye-world-pos\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)', t)
        th = re.search(r'(?m)^look-theta-deg\s+(-?[\d.]+)', t)
        vt = re.search(r'(?m)^look-verta-deg\s+(-?[\d.]+)', t)
        ok = bool(pos and th and vt)
        return (PASS if ok else FAIL), ('the old Read-Mark accepted any mark carrying a '
                                        'position and two angles, asked nothing about '
                                        'which stage it was taken on, and fell back to '
                                        'the start pad in silence when it could not')

    if name == 'd8-stale-attempt':
        stale = sorted(p.name for p in d.iterdir())
        ok = 'selected.json' in stale
        return (PASS if ok else FAIL), ('the old runner reused -Out, read selected.json '
                                        'by name whatever produced it, and ignored the '
                                        "selector's exit code")

    if name in ('d9-launch-failure', 'd9-timed-out'):
        rec = json.loads((d / 'arms.json').read_text(encoding='utf-8-sig'))
        a = [x for x in rec['arms'] if x['name'] == 'B-depth-on'][0]
        code = a['run'].get('exit_code')
        # The old EXIT_ZERO read NOT_COVERED on a null exit, which rolled the
        # arm up to NOT_COVERED rather than FAIL; and there was no timeout
        # witness at all, so a killed arm was indistinguishable from a clean one.
        if name == 'd9-timed-out':
            return PASS, ('the old witness set had no NOT_TIMED_OUT, so a killed arm '
                          'with exit 0 read exactly like a clean exit')
        return (PASS if code is None else FAIL), (
            'the old EXIT_ZERO treated a null exit as NOT_COVERED rather than FAIL, so a '
            'lost launch was an absence of measurement rather than a failure')

    if name == 'dw-no-crossing':
        # The old movement phase was a SEQUENCE OF SEPARATE PROCESSES, each
        # teleported to one pose, so it had no notion of an interval inside one
        # run and this evidence has no old counterpart. That form is recorded in
        # the batch's not_covered list as limited evidence, not as coverage.
        return 'N/A', ('the old movement phase was a sequence of separate processes with a '
                       'latched teleport, so no old rule reads within-run intervals; this '
                       'fixture pins the new coverage requirement rather than a regression')

    if name == 'd10-manual-explicit-fail':
        doc = json.loads((d / 'manual.json').read_text(encoding='utf-8-sig'))
        rec = json.loads((d / 'arms.json').read_text(encoding='utf-8-sig'))
        names = set(a['name'] for a in rec['arms'])
        want = (rec.get('binaries') or {}).get('sightline')
        ok = True
        for sid in ['H%d' % i for i in range(1, 8)]:
            e = (doc.get('steps') or {}).get(sid)
            # THE DEFECT: an outcome counts as satisfied as soon as it is one of
            # the two words and is pinned. What it SAYS is never read.
            if not isinstance(e, dict) or e.get('outcome') not in ('PASS', 'FAIL'):
                ok = False
            elif e.get('build_sha256') != want or e.get('arm') not in names:
                ok = False
            elif not isinstance(e.get('frame'), int):
                ok = False
        return (PASS if ok else FAIL), ('the old gate asked whether every step carried an '
                                        'outcome pinned to this build, arm and frame, and '
                                        'never asked whether the outcome read PASS')

    if name == 'd11-missing-pair-half':
        rec = json.loads((d / 'arms.json').read_text(encoding='utf-8-sig'))
        want = rec.get('mandatory_phases') or []
        have = set(a['phase'] for a in rec['arms'])
        ok = bool(want) and not [p for p in want if p not in have]
        return (PASS if ok else FAIL), ('the old gate compared the set of PHASES present '
                                        'against the mandatory list, so one arm of a pair '
                                        'made its whole phase present')

    if name in ('d12-schedule-wrong-identically', 'd12b-schedule-read-units'):
        import re
        rec = json.loads((d / 'arms.json').read_text(encoding='utf-8-sig'))
        ok = True
        logs = {}
        for a in rec['arms']:
            t = stderr_of(a['name'])
            shots = re.findall(r'sl_shot: (\S+) \((\d+)x(\d+)\) at read=(\d+) frame=(\d+)', t)
            files = sorted((d / a['name']).glob('shot-*.ppm'))
            want = (a.get('shots') or {})
            # The old capture rules in full: declared count, declared grid,
            # measured payload, name identity against the log - and nothing
            # whatever about WHEN each capture was taken.
            if len(files) != want.get('count') or len(shots) != want.get('count'):
                ok = False
            for p in files:
                raw = p.read_bytes()
                head = raw.split(b'\n', 3)
                w, h = [int(x) for x in head[1].split()]
                if (w, h) != (want.get('width'), want.get('height')) or len(head[3]) != w * h * 3:
                    ok = False
            if sorted(s[0] for s in shots) != sorted(p.name for p in files):
                ok = False
            logs[a['name']] = [(s[0], s[3], s[4]) for s in shots]
        if len(set(tuple(v) for v in logs.values())) != 1:
            ok = False
        return (PASS if ok else FAIL), ('the old capture rules read the declared count, the '
                                        'declared grid, every payload length, the names '
                                        'against the log and the two halves against each '
                                        'other - and never which MOMENT a capture was taken '
                                        'at, nor which counter the window was declared in')

    if name == 'd18-manual-fail-outranks-absence':
        doc = json.loads((d / 'manual.json').read_text(encoding='utf-8-sig'))
        rec = json.loads((d / 'arms.json').read_text(encoding='utf-8-sig'))
        names = set(a['name'] for a in rec['arms'])
        want = (rec.get('binaries') or {}).get('sightline')
        absent, mispinned, failed = [], [], []
        for sid in ['H%d' % i for i in range(1, 8)]:
            e = (doc.get('steps') or {}).get(sid)
            if not isinstance(e, dict) or e.get('outcome') in (None, '', 'PENDING'):
                absent.append(sid)
            elif (not e.get('build_sha256')) or (not e.get('arm')) or (
                    not isinstance(e.get('frame'), int)):
                absent.append(sid)
            elif e.get('outcome') not in ('PASS', 'FAIL'):
                mispinned.append(sid)
            elif e.get('build_sha256') != want or e.get('arm') not in names:
                mispinned.append(sid)
            elif e.get('outcome') == 'FAIL':
                failed.append(sid)
        # THE DEFECT: the absence was read BEFORE the recorded outcomes, so one
        # unrecorded step returned NOT_COVERED and the recorded FAIL was never
        # reached. The witness did not fail, and the batch rolled up INCOMPLETE.
        if absent or mispinned:
            return PASS, ('the old precedence returned on the first absent or unpinned step, '
                          'so MANUAL_OUTCOMES_PASSED read NOT_COVERED and never reached the '
                          '%d step(s) recorded FAIL (%r); the batch reported INCOMPLETE and '
                          'the measured failure was not in it' % (len(failed), failed))
        return (PASS if not failed else FAIL), ('the old precedence reached the recorded '
                                                'outcomes, because nothing was absent')

    if name in ('d19-performance-raw-malformed', 'd20-performance-raw-unbound',
                'd21-performance-arm-unsampled', 'd22-performance-false-budget'):
        doc = json.loads((d / 'performance-row.json').read_text(encoding='utf-8-sig'))
        need = ('row', 'off', 'on', 'stage', 'build_sha256')
        ok = not [k for k in need if not doc.get(k)]
        # Measured beside the verdict: which of this row's figures the
        # INTERMEDIATE numeric rule also admitted. That rule tested
        # isinstance(x, (int, float)) and not isinstance(x, bool) and asked
        # nothing about finiteness, sign, schema, build or stage, so a NaN, an
        # infinity and a negative duration all read as frame times.
        admitted = []
        for i, r in enumerate(doc.get('raw') or []):
            if not isinstance(r, dict):
                continue
            for k in ('mean_ms', 'p99_ms_nearest_rank'):
                v = r.get(k)
                if isinstance(v, (int, float)) and not isinstance(v, bool) and (
                        v != v or v == float('inf') or v == float('-inf') or v <= 0):
                    admitted.append('raw[%d].%s=%r' % (i, k, v))
        return (PASS if ok else FAIL), (
            'the old gate asked only whether five keys were present and truthy; it bound no '
            'build, named no arm, read no raw measurement and recomputed no stated verdict. '
            'The intermediate numeric rule that followed it admitted %r as frame times, and '
            'bound no raw record to a build, an arm or a stage' % (admitted,))

    if name in ('d23-performance-abba-arm-sequence',
                'd24-performance-duplicate-measured-arms',
                'd25-performance-source-absent',
                'd26-performance-source-tampered',
                'd27-performance-summary-edited',
                'd28-performance-nan-derived-metrics'):
        # These six were built against a LATER rule than d13 and d19-d22 were,
        # so this is that rule and not the five-key gate: it bound the build,
        # named the arms, read every raw measurement and recomputed the stated
        # aggregate, the savings, the spreads, the screen verdict and the
        # absolute budget from the row's own samples. Everything it checked,
        # it checked against another figure in the same document. The four
        # things it did not do are what these fixtures measure: it never asked
        # whether the two named arms were two DIFFERENT arms carrying the
        # SL_PERF_OPT of the roles the row assigned them, never required the
        # runs' ACTUAL arm sequence to be the paired pattern its savings
        # assume, never opened a cited source, and compared derived figures
        # with abs(stated - computed) > tol, which is false for a NaN.
        import re
        doc = json.loads((d / 'performance-row.json').read_text(encoding='utf-8-sig'))
        batch = json.loads((d / 'arms.json').read_text(encoding='utf-8-sig'))
        arm_stage = dict((a['name'], a['stage']) for a in batch['arms'])
        want_build = (batch.get('binaries') or {}).get('sightline')
        tol, budget = 0.005 + 1e-9, 16.667

        def numeric(v):
            return isinstance(v, (int, float)) and not isinstance(v, bool)

        bad, notes = [], []
        for k in ('row', 'stage', 'build_sha256'):
            if not doc.get(k):
                bad.append('%s is absent' % k)
        if doc.get('build_sha256') != want_build:
            bad.append('the row is pinned to another build')
        measured = doc.get('arms')
        if not isinstance(measured, list) or len(measured) != 2:
            bad.append('the row does not name two measured arms')
            measured = []
        else:
            for a in measured:
                if a not in arm_stage:
                    bad.append('arm %r is not in this batch' % (a,))
            if measured[0] == measured[1]:
                notes.append('it named arm %r as BOTH measured arms and did not '
                             'notice' % (measured[0],))
        runs = doc.get('raw')
        if not isinstance(runs, list) or len(runs) != 4:
            bad.append('the row does not carry four raw runs')
            runs = []
        means = []
        for i, r in enumerate(runs):
            if not isinstance(r, dict):
                bad.append('raw[%d] is not a record' % i)
                continue
            for k in ('mean_ms', 'p99_ms_nearest_rank'):
                if not numeric(r.get(k)):
                    bad.append('raw[%d].%s is no number' % (i, k))
            if r.get('build_sha256') != want_build:
                bad.append('raw[%d] was taken on another build' % i)
            if r.get('arm') not in arm_stage:
                bad.append('raw[%d] names an arm outside the batch' % i)
            elif r.get('stage') != arm_stage[r['arm']]:
                bad.append('raw[%d] names a stage its arm did not run' % i)
            for k in ('mean_ms', 'p99_ms_nearest_rank'):
                if numeric(r.get(k)) and float(r[k]) > budget:
                    bad.append('raw[%d].%s exceeds the %.3f ms budget' % (i, k, budget))
            means.append(float(r['mean_ms']) if numeric(r.get('mean_ms')) else None)
            notes.append('raw[%d] cited source %r, which it never opened'
                         % (i, (r.get('source') or {}).get('path')))
        metrics = doc.get('metrics') if isinstance(doc.get('metrics'), dict) else None
        if not metrics or metrics.get('rule') != 'abba-timing-screen/1':
            bad.append('the row names no rule this tree defines')
        if len(means) == 4 and all(m is not None for m in means) and metrics:
            per_arm = {}
            for r, m in zip(runs, means):
                per_arm.setdefault(r['arm'], []).append(m)
            for key, index in (('off', 0), ('on', 1)):
                if len(measured) <= index:
                    continue
                sample = per_arm.get(measured[index])
                if not sample:
                    bad.append('the row aggregates arm %r and carries no measurement '
                               'of it' % (measured[index],))
                    continue
                m = re.match(r'^\s*(-?\d+(?:\.\d+)?)\s*ms\s*$', str(doc.get(key)))
                if not m:
                    bad.append('%s is no millisecond figure' % key)
                elif abs(float(m.group(1)) - sum(sample) / float(len(sample))) > tol:
                    bad.append('%s is not the mean of its arm\'s own samples' % key)
            # THE LABELS, AND ONLY THE LABELS. The arms the runs actually
            # measured were never compared against this pattern.
            if [r.get('order') for r in runs] != ['A', 'B', 'B', 'A']:
                bad.append('the four runs are not labelled A,B,B,A')
            else:
                notes.append('it took the A,B,B,A labels as the pairing and never '
                             'looked at the runs\' actual arms %r'
                             % ([r.get('arm') for r in runs],))
                savings = [means[0] - means[1], means[3] - means[2]]
                spreads = [abs(means[0] - means[3]), abs(means[1] - means[2])]
                for key, got in (('paired_savings_ms', savings),
                                 ('repeat_spreads_ms', spreads)):
                    stated = metrics.get(key)
                    if stated is None:
                        continue
                    if not isinstance(stated, list) or len(stated) != 2 \
                            or not all(numeric(x) for x in stated):
                        bad.append('%s is not two numbers' % key)
                        continue
                    for j, (x, y) in enumerate(zip(stated, got)):
                        # The defect, reproduced: a NaN makes this false, so
                        # the figure was admitted without being compared.
                        if abs(float(x) - y) > tol:
                            bad.append('%s[%d] is not what the means give' % (key, j))
                        elif float(x) != float(x):
                            notes.append('%s[%d] was a NaN and abs(stated - computed) '
                                         '> tol read it as agreement' % (key, j))
                stated_screen = metrics.get('timing_screen_passed')
                if isinstance(stated_screen, bool) \
                        and stated_screen != (min(savings) > max(spreads)):
                    bad.append('the stated screen verdict is not what its own figures '
                               'support')
        return (PASS if not bad else FAIL), (
            'the rule these were built against recomputed the aggregate, the savings, '
            'the spreads, the screen and the budget from the row\'s own means, and every '
            'one of those checks held here. What it did not do: %s'
            % ('; '.join(notes) if notes else 'nothing it was asked'))

    if name in ('d29-performance-threshold-rounded-budget',
                'd30-performance-threshold-rounded-screen',
                'd31-performance-source-unpinned'):
        # These three were built against the r532 rule, which is a generation
        # later again: it required two distinct measured arms bound to the
        # roles their recorded SL_PERF_OPT gives them, required the runs' own
        # arm sequence to be the paired pattern, OPENED every cited source and
        # recomputed it, and required derived figures to be finite. What it did
        # with those recomputed reports is the defect: it discarded them. The
        # embedded summaries were compared to their sources with the
        # printed-digit tolerance and then used, as they stood, to compute the
        # aggregate, the budget, the savings, the spreads and the screen - so
        # anything inside half a printed digit decided the verdict. It also
        # compared a source pin only `if key in src`, which a record with no
        # sha256 at all satisfies by having opted out.
        import re
        doc = json.loads((d / 'performance-row.json').read_text(encoding='utf-8-sig'))
        batch = json.loads((d / 'arms.json').read_text(encoding='utf-8-sig'))
        arm_stage = dict((a['name'], a['stage']) for a in batch['arms'])
        arm_env = dict((a['name'], (a.get('env') or {})) for a in batch['arms'])
        want_build = (batch.get('binaries') or {}).get('sightline')
        tol, budget = 0.005 + 1e-9, 16.667
        vkeys = {'abba-timing-screen/1': 'timing_screen_passed',
                 'absolute-budget/1': 'budget_met'}

        def numeric(v):
            return isinstance(v, (int, float)) and not isinstance(v, bool)

        bad, notes = [], []
        for k in ('row', 'stage', 'build_sha256'):
            if not doc.get(k):
                bad.append('%s is absent' % k)
        if doc.get('build_sha256') != want_build:
            bad.append('the row is pinned to another build')
        measured = doc.get('arms')
        if not isinstance(measured, list) or len(measured) != 2 \
                or measured[0] == measured[1]:
            bad.append('the row does not name two distinct measured arms')
            measured = []
        else:
            for role, index, want in (('off', 0, '0'), ('on', 1, '1')):
                if measured[index] not in arm_stage:
                    bad.append('arm %r is not in this batch' % (measured[index],))
                elif str(arm_env.get(measured[index], {}).get('SL_PERF_OPT')) != want:
                    bad.append('arm %r did not run as the %s role' % (measured[index], role))
        runs = doc.get('raw')
        if not isinstance(runs, list) or len(runs) < 2:
            bad.append('the row carries no raw runs to read')
            runs = []
        metrics = doc.get('metrics') if isinstance(doc.get('metrics'), dict) else None
        rule = (metrics or {}).get('rule')
        if rule not in vkeys:
            bad.append('the row names no rule this tree defines')
        means = []
        for i, r in enumerate(runs):
            for k in ('mean_ms', 'p99_ms_nearest_rank'):
                v = r.get(k)
                if not (numeric(v) and v == v and abs(v) != float('inf') and v > 0):
                    bad.append('raw[%d].%s is no frame time' % (i, k))
            if r.get('schema') != 'sightline-frames/1':
                bad.append('raw[%d] is not a frame report' % i)
            if r.get('build_sha256') != want_build:
                bad.append('raw[%d] was taken on another build' % i)
            if r.get('arm') not in arm_stage:
                bad.append('raw[%d] names an arm outside the batch' % i)
            elif r.get('stage') != arm_stage[r['arm']]:
                bad.append('raw[%d] names a stage its arm did not run' % i)
            means.append(float(r['mean_ms']) if numeric(r.get('mean_ms')) else None)
        # THE SOURCES, OPENED - AND THE REPORTS THEN DISCARDED.
        reports = []
        if runs and not bad:
            analyze = _analyze()
            for i, r in enumerate(runs):
                src, win = r.get('source') or {}, r.get('window') or {}
                try:
                    rep = analyze.frame_report(src.get('path'), win.get('lo'),
                                               win.get('hi'), win.get('step'))
                except Exception as exc:                   # noqa: BLE001
                    bad.append('raw[%d] source could not be recomputed: %s' % (i, exc))
                    continue
                for key in ('sha256', 'bytes'):
                    # The defect, reproduced: a key that is simply absent skips
                    # the comparison rather than failing it.
                    if key in src:
                        if src[key] != (rep.get('source') or {}).get(key):
                            bad.append('raw[%d] pins a source %s the file does not have'
                                       % (i, key))
                    else:
                        notes.append('raw[%d] carried no source %s, so `if key in src` '
                                     'skipped the comparison and the bytes its figures '
                                     'were read from were never identified' % (i, key))
                if rep.get('window') != {'lo': win.get('lo'), 'hi': win.get('hi'),
                                         'step': win.get('step')}:
                    bad.append('raw[%d] declares another window' % i)
                for key in ('intervals', 'mean_ms', 'p99_ms_nearest_rank'):
                    if key not in r:
                        continue
                    stated, got = r[key], rep.get(key)
                    if key == 'intervals':
                        if int(stated) != int(got):
                            bad.append('raw[%d] states another interval count' % i)
                    elif abs(float(stated) - float(got)) > tol:
                        bad.append('raw[%d] states %s=%r against a recomputed %.4f'
                                   % (i, key, stated, float(got)))
                    elif float(stated) != float(got):
                        notes.append('raw[%d] states %s=%r where its source gives %.4f, and '
                                     'the %.3f ms tolerance admitted it - then the EMBEDDED '
                                     '%r was what every verdict below was computed from'
                                     % (i, key, stated, float(got), tol, stated))
                reports.append(rep)
            if len(reports) == len(runs) and reports:
                first = reports[0]
                for i, rep in enumerate(reports[1:], 1):
                    if rep.get('window') != first.get('window') \
                            or rep.get('identities') != first.get('identities'):
                        bad.append('raw[%d] is not the same workload as raw[0]' % i)
                notes.append('it recomputed all %d sources and then discarded the reports'
                             % (len(reports),))
        if runs and all(m is not None for m in means) and metrics and not bad:
            per_arm = {}
            for r, m in zip(runs, means):
                per_arm.setdefault(r['arm'], []).append(m)
            for key, index in (('off', 0), ('on', 1)):
                if len(measured) <= index or doc.get(key) is None:
                    continue
                sample = per_arm.get(measured[index])
                if not sample:
                    bad.append('the row aggregates arm %r and holds no sample of it'
                               % (measured[index],))
                    continue
                m = re.match(r'^\s*(-?\d+(?:\.\d+)?)\s*ms\s*$', str(doc.get(key)))
                if not m:
                    bad.append('%s is no millisecond figure' % key)
                elif abs(float(m.group(1)) - sum(sample) / float(len(sample))) > tol:
                    bad.append('%s is not the mean of its arm\'s own samples' % key)
            # THE BUDGET, FROM THE EMBEDDED FIGURES. This is the inversion: a
            # summary inside the tolerance carries the threshold with it.
            over = [i for i, r in enumerate(runs)
                    if float(r['mean_ms']) > budget
                    or float(r['p99_ms_nearest_rank']) > budget]
            computed = not over
            if rule == 'abba-timing-screen/1':
                order = [r.get('order') for r in runs]
                want_seq = ([measured[0], measured[1], measured[1], measured[0]]
                            if len(measured) == 2 else None)
                if order != ['A', 'B', 'B', 'A']:
                    bad.append('the four runs are not labelled A,B,B,A')
                elif want_seq is not None and [r.get('arm') for r in runs] != want_seq:
                    bad.append('the runs\' actual arms are not the paired pattern')
                else:
                    savings = [means[0] - means[1], means[3] - means[2]]
                    spreads = [abs(means[0] - means[3]), abs(means[1] - means[2])]
                    computed = min(savings) > max(spreads)
                    for key, got in (('paired_savings_ms', savings),
                                     ('repeat_spreads_ms', spreads)):
                        stated = metrics.get(key)
                        if stated is None:
                            continue
                        if not isinstance(stated, list) or len(stated) != 2 \
                                or not all(numeric(x) and x == x
                                           and abs(x) != float('inf') for x in stated) \
                                or any(abs(float(x) - y) > 1e-9
                                       for x, y in zip(stated, got)):
                            bad.append('%s is not what the embedded means give' % key)
            said = metrics.get(vkeys.get(rule))
            if not isinstance(said, bool):
                bad.append('the row states no boolean verdict for the rule it names')
            elif said != computed:
                bad.append('the stated verdict is not what the embedded figures support')
        return (PASS if not bad else FAIL), (
            'the r532 rule bound the arms and their roles, required the paired arm '
            'sequence, opened every cited source and required finite derived figures, and '
            'every one of those checks held here. What it did with what it read: %s'
            % ('; '.join(notes) if notes else 'nothing it was asked'))

    if name in ('d13-performance-placeholder', 'd13b-performance-fabricated-gain'):
        doc = json.loads((d / 'performance-row.json').read_text(encoding='utf-8-sig'))
        need = ('row', 'off', 'on', 'stage', 'build_sha256')
        ok = not [k for k in need if not doc.get(k)]
        return (PASS if ok else FAIL), ('the old gate asked only whether five keys were '
                                        'present and truthy; it bound no build, named no '
                                        'arm, read no raw measurement and recomputed no '
                                        'stated verdict')

    if name == 'd14-selection-reduction-unproved':
        import re
        acq = json.loads((d / 'phase-acq.json').read_text(encoding='utf-8-sig'))
        arms_ = acq['arms'] if isinstance(acq, dict) else acq
        cands = []
        for a in arms_:
            t = stderr_of(a['name'])
            m = None
            for m in re.finditer(r"BY DEPTH (\d+) calls? / (\d+) indices at the "
                                 r"single-range clamp, (\d+) / (\d+) in a far frame's "
                                 r"first range, (\d+) / (\d+) in its game range", t):
                pass
            m78 = None
            for m78 in re.finditer(r'#78 background beyond the far plane: (\d+) of', t):
                pass
            m68 = None
            for m68 in re.finditer(r'#68 background: drawn on (\d+) frames', t):
                pass
            fog = re.search(r'#81 background on a fogless stage: (\d+) frames', t)
            pano = re.search(r'#74 background panorama shell', t)
            cands.append({'stage': a['stage'],
                          'far': (int(m78.group(1)) if m78 else 0) and
                                 (int(m.group(3)) + int(m.group(5)) if m else 0),
                          'bg': int(m68.group(1)) if m68 else 0,
                          'fogless': int(fog.group(1)) if fog else None,
                          'pano': bool(pano)})
        far = max(cands, key=lambda r: r['far'])
        red = [r for r in cands if (r['fogless'] or 0) > 0 and r['bg'] > 0 and not r['pano']]
        if not red:
            red = [r for r in cands if r['bg'] > 0 and not r['pano']]
        # THE DEFECT: with no fogless evidence anywhere the role still gets a
        # stage - the background-frame fallback, and failing that the far
        # candidate itself.
        reduction = max(red, key=lambda r: r['bg'])['stage'] if red else far['stage']
        return (PASS if reduction else FAIL), (
            'the old selector filled the reduction role from background frames alone when no '
            'candidate carried fogless frames, and from the far candidate when that failed '
            'too: it selected reduction=%s from a run whose own fogless figure is %r'
            % (reduction, [r['fogless'] for r in cands]))

    if name in ('d15-helper-split-equals-fp', 'd16-helper-milestone-short'):
        import re
        t = stderr_of('G4-vertexhelper')
        m = None
        for m in re.finditer(r'#88 background vertices: (\d+) walks? - (\d+) in two ranges', t):
            pass
        ok = bool(m) and int(m.group(2)) > 0
        if name == 'd15-helper-split-equals-fp':
            return (PASS if ok else FAIL), ('the old reading took "%lu in two ranges" alone as '
                                            'the helper having taken ranges, and never '
                                            'subtracted the splits whose upper range was '
                                            'recomputed on the render thread')
        return (PASS if ok else FAIL), ('the old reading took the last milestone line as the '
                                        "run's final word and asked nothing about which "
                                        'interval of walks it had counted through')

    if name in ('d17-input-stream-reads-mismatch', 'd17b-input-stream-open-failed'):
        # The old witness set had no INPUT_STREAM_REPLAYED: the name an arm
        # declared was unknown to the checker and read NOT_COVERED, so no
        # reading of the stream could fail. This reimplements THAT rule - the
        # arm's other witnesses are different rules and this fixture pins none
        # of them.
        import re
        rec = json.loads((d / 'arms.json').read_text(encoding='utf-8-sig'))
        reads = re.findall(r'sightline native: input stream (\S+) \((\d+) reads\)',
                           stderr_of(rec['arms'][0]['name']))
        return PASS, ('the old witness set contained no INPUT_STREAM_REPLAYED, so an arm '
                      'declaring it got "unknown witness name" and NOT_COVERED; the stream '
                      'lines in this evidence (%r) were read by no rule at all' % (reads,))

    if name in ('pass-full', 'pass-subset'):
        return PASS, 'positive control; the old behaviour also accepted it'

    # ---- the two #95 helper defects -------------------------------------
    #
    # These fixtures DO have an old counterpart, unlike the #95 group above:
    # the grammar and the witnesses existed, and the two rules re-implemented
    # here are what the checker did with them before this round.
    if name.startswith('tf-') and name in TF_DEFECT_FIXTURES:
        import re

        def tf95_lines(armname):
            return re.findall(r'#95 background GPU clip prepass[^\n]*',
                              stderr_of(armname))

        def old_classification(armname):
            """The OLD malformed test: every line carrying the census prefix
            counted as a census report, the module's own diagnostics included.

            The `parsed` pattern stops at the first field group rather than
            re-stating the whole grammar; every line these fixtures write is
            produced by l_tf95, so a line matching this far matches the full
            grammar too, and a drifted line does not reach it.
            """
            t = stderr_of(armname)
            prefixes = len(re.findall(r'#95 background GPU clip prepass', t))
            parsed = len(re.findall(
                r'#95 background GPU clip prepass: [^,]+, \d+ prepass(?:es)? '
                r'over \d+ vertices, \d+ reuses?, \d+ binds?; SUBMITTED \d+ '
                r'nonempty run', t))
            return prefixes, parsed

        def old_bracket_reading(armname):
            """The OLD capture-window reading: `pre` defaulted to all zeros.

            Returns (status, prepasses, vertices, submitted) where status says
            which span the old rule reported. A log with no report before the
            first capture still reported 'capture-bracket' and subtracted the
            invented origin, so the delta IS the later cumulative total.
            """
            t = stderr_of(armname)
            shots = [m.start() for m in re.finditer(r'sl_shot: ', t)]
            reps = [(m.start(), (int(m.group(1)), int(m.group(2)), int(m.group(3))))
                    for m in re.finditer(
                        r'#95 background GPU clip prepass: [^,]+, (\d+) prepass(?:es)? '
                        r'over (\d+) vertices, \d+ reuses?, \d+ binds?; SUBMITTED '
                        r'(\d+) nonempty run', t)]
            if not reps:
                return 'absent', None, None, None
            if not shots:
                return 'whole-run-cumulative', reps[-1][1][0], reps[-1][1][1], reps[-1][1][2]
            pre = (0, 0, 0)                      # THE DEFECT, re-implemented
            for pos, vals in reps:
                if pos < shots[0]:
                    pre = vals
            post = None
            for pos, vals in reps:
                if pos > shots[-1]:
                    post = vals
                    break
            if post is None:
                return 'bracket-did-not-close', None, None, None
            return ('capture-bracket', post[0] - pre[0], post[1] - pre[1],
                    post[2] - pre[2])

        armname = 'ORIG_CPU' if name.startswith('tf-off-') else 'TF_ON'
        prefixes, parsed = old_classification(armname)
        span, prep, verts, subm = old_bracket_reading(armname)

        if name == 'tf-missing-pre-report':
            # The bracket defect on its own: no diagnostic line in this log,
            # so the old classification is satisfied (%d prefixes, %d parsed)
            # and the old reading is reached. It then subtracts a zero origin
            # from the later cumulative report and reads every positive
            # counter as work done inside a window nothing was measured in.
            ok = (prefixes == parsed and span == 'capture-bracket'
                  and prep > 0 and verts > 0 and subm > 0)
            return ((PASS if ok else FAIL),
                    ('the old reading defaulted the pre-capture report to an all-zero '
                     'origin, so a log whose every report follows the first capture '
                     "still reported span '%s'; it read prepasses %r, vertices %r and "
                     'SUBMITTED runs %r as the captured window, when the window holds '
                     'no report at all and those are the whole-run totals '
                     '(%d census prefix(es), %d parsed)'
                     % (span, prep, verts, subm, prefixes, parsed)))

        if name in ('tf-bracket-did-not-close', 'tf-zero-delta-large-totals'):
            # Rules that were already right. Reported N/A rather than PASS
            # because the old behaviour REFUSED this evidence too: there is no
            # defective acceptance to pin, and what these fixtures hold is the
            # absence of a regression - specifically, that no later change
            # substitutes the whole-run totals for a reading that was asked
            # for over a bracket.
            return 'N/A', ('the old rule reached the same refusal: it reported span %r, '
                           'reading %r prepasses across the bracket while the log\'s own '
                           'cumulative reports are large. This fixture pins the ABSENCE '
                           'of a totals fallback rather than an old acceptance '
                           '(%d census prefix(es), %d parsed)'
                           % (span, prep, prefixes, parsed))

        # The classification group. Where the old prefix count exceeded the
        # parsed reports the old rule REFUSED the evidence - the healthy logs
        # included - so none of those is a regression test against a defective
        # acceptance. What they pin is that the corrected classification
        # reaches the right reading instead of a parser fault.
        if prefixes > parsed:
            return 'N/A', ('the old classification counted %d line(s) carrying the census '
                           'prefix against %d parseable census report(s) and so marked this '
                           'log MALFORMED, failing every #95 witness on a parser fault. It '
                           'did not accept this evidence, it refused it, so there is no old '
                           'acceptance for a regression test to pin; the reading under test '
                           'is that the known diagnostics are classified as diagnostics and '
                           'the %d #95 line(s) here are read for what they are'
                           % (prefixes, parsed, len(tf95_lines(armname))))
        # AND WHERE IT DID NOT. A #95 line whose prefix is not the census
        # prefix never reached the classification at all, so the old count and
        # the new count agree and the old reading was reached. This fixture's
        # claim is that the correction left that case alone: the same span and
        # the same zero reading, unchanged.
        return 'N/A', ('the old classification counted %d line(s) carrying the census '
                       'prefix against %d parseable census report(s) - equal, so it did '
                       'NOT mark this log malformed and reached the same reading: span '
                       '%r, %r prepasses. The %d #95 line(s) here include one whose '
                       'prefix is not the census prefix, which neither count moves for. '
                       'This fixture pins that the correction left this case unchanged '
                       'rather than an old acceptance'
                       % (prefixes, parsed, span, prep, len(tf95_lines(armname))))

    if name.startswith('tf-'):
        # NO OLD COUNTERPART, and the reason is structural rather than an
        # oversight: before this round the checker had no #95 grammar at all,
        # so every TF_* witness name read "unknown witness name" and came back
        # NOT_COVERED. There was no rule to accept or reject this evidence, so
        # there is nothing for a legacy re-implementation to be. These
        # fixtures pin NEW coverage requirements and are not regression tests
        # against a defective older rule.
        import re
        t = ''
        for sub in sorted(p.name for p in d.iterdir() if p.is_dir()):
            t += stderr_of(sub)
        lines = re.findall(r'#95 background GPU clip prepass[^\n]*', t)
        return 'N/A', ('the old checker had no #95 grammar and no TF_* witness, so every '
                       'one of these names read "unknown witness name" and NOT_COVERED; '
                       'the %d clip-prepass line(s) in this evidence were read by no rule '
                       'at all. This fixture pins a new coverage requirement and has no '
                       'old counterpart to bite' % len(lines))

    return FAIL, 'no legacy rule is recorded for this fixture'


# ------------------------------------------------------------------- driver
def main(argv):
    if not argv:
        print(__doc__.splitlines()[0], file=sys.stderr)
        return 2
    cmd = argv[0]
    if cmd == 'list':
        for n in sorted(FIXTURES):
            f = FIXTURES[n]
            fq = f['full_qualification']
            print('%-28s %-12s %-34s %s'
                  % (n, f['status'],
                     (f['witness'] or '-') + ('' if fq is None else ' [fq=%s]' % fq),
                     f['defect']))
        return 0
    if cmd == 'expect' and len(argv) == 2:
        f = FIXTURES.get(argv[1])
        if f is None:
            print('no such fixture: %s' % argv[1], file=sys.stderr)
            return 2
        print(json.dumps({'name': argv[1], 'status': f['status'],
                          'witness': f['witness'],
                          'full_qualification': f['full_qualification'],
                          'legacy_expected': f['legacy_expected'],
                          'defect': f['defect']}, indent=2))
        return 0
    if cmd == 'build' and len(argv) == 3:
        f = FIXTURES.get(argv[1])
        if f is None:
            print('no such fixture: %s' % argv[1], file=sys.stderr)
            return 2
        d = Path(argv[2])
        if d.exists() and any(d.iterdir()):
            print('refusing to build into a non-empty directory: %s' % d, file=sys.stderr)
            return 2
        out = f['build'](d)
        print('built %s in %s' % (argv[1], out))
        return 0
    if cmd == 'legacy' and len(argv) == 3:
        f = FIXTURES.get(argv[1])
        if f is None:
            print('no such fixture: %s' % argv[1], file=sys.stderr)
            return 2
        v, why = legacy_verdict(argv[1], argv[2])
        print(json.dumps({'name': argv[1], 'legacy_verdict': v, 'rule': why}, indent=2))
        return 0
    print('usage: perflive-fixture.py list', file=sys.stderr)
    print('       perflive-fixture.py expect <name>', file=sys.stderr)
    print('       perflive-fixture.py build <name> <dir>', file=sys.stderr)
    print('       perflive-fixture.py legacy <name> <dir>', file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
