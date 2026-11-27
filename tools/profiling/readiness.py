"""Offline performance readiness: plans, producer checks and regression gates.

No command launches a process, records a trace, loads a graphics API, changes
an environment variable, or updates a baseline. Live evidence must be supplied
separately. Run --help for the small JSON interfaces.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze
from common import ProfileError, digest, integer, load_json, number, pin, same_pin, write_json

HERE = Path(__file__).resolve().parent
SUITE = HERE / 'readiness-suite.json'
KNOWN_HARNESS = '7afa3dde642a6af1b8b88adb4b4abd016ade6cd040021deeb53d0eb6c59a24b9'
METRIC_FIELDS = ['frame', 'scope', 'cpu_submit_ms', 'gpu_elapsed_ms', 'gpu_status']


def require(ok, message):
    if not ok:
        raise ProfileError(message)


def object_keys(value, required, optional=()):
    require(isinstance(value, dict), 'expected an object')
    require(set(required) <= set(value), 'missing fields: ' + ', '.join(sorted(set(required)-set(value))))
    require(set(value) <= set(required) | set(optional), 'unknown fields: ' + ', '.join(sorted(set(value)-set(required)-set(optional))))


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     allow_nan=False).encode('utf-8')).hexdigest()


def suite(path=SUITE):
    value = load_json(path)
    require(value.get('schema') == 'sightline-readiness-suite/1', 'unknown suite schema')
    window = value['window']
    integer(window['lo'], 'window lo', 0);integer(window['hi'], 'window hi', 1);integer(window['step'], 'window step', 1)
    require(window['hi'] > window['lo'], 'empty frame window')
    for name, profile in value['profiles'].items():
        require(re.fullmatch(r'[a-z0-9-]+', name), 'invalid profile id')
        for key in ('width', 'height', 'target_fps'):
            integer(profile[key], key, 1)
        for key in ('mean_ms', 'p99_ms', 'mean_regression_pct', 'p99_regression_pct',
                    'mean_noise_floor_ms', 'p99_noise_floor_ms'):
            number(profile['budget'][key], key)
        require(profile['budget']['mean_ms'] > 0 and profile['budget']['p99_ms'] > 0,
                'absolute frame budgets must be positive')
    require(value['scenes'] and len({x['id'] for x in value['scenes']}) == len(value['scenes']),
            'scene ids must be unique and nonempty')
    for scene in value['scenes']:
        require(re.fullmatch(r'[a-z0-9-]+', scene['id']), 'invalid scene id')
        require(scene['timing_role'] in ('budget', 'diagnostic'), 'unknown timing role')
        require(isinstance(scene['events_required'], bool), 'events_required must be boolean')
        require(scene['required_metrics'] and len(set(scene['required_metrics'])) == len(scene['required_metrics']),
                'metric scopes must be unique and nonempty')
        require(set(scene['required_metrics']) <= {'host_frames','cpu_submit','gpu_elapsed'}, 'unknown required metric')
        sample = scene['captures']
        integer(sample['first'], 'first capture', 0);integer(sample['last'], 'last capture', 0);integer(sample['every'], 'capture interval', 1)
        require(sample['last'] >= sample['first'] and sample['phases'], 'empty capture schedule')
        for phase in sample['phases']:
            integer(phase, 'capture phase', 0)
            require(phase < sample['every'] and sample['first']+phase <= sample['last'], 'invalid capture phase')
    return value


def child(root, name):
    """Only ordinary evidence files within a declared root, never symlinks."""
    require(isinstance(name, str) and name and not Path(name).is_absolute(), 'expected relative evidence path')
    p = Path(root) / name
    require('..' not in Path(name).parts, 'parent traversal in evidence path')
    require(not any(x.is_symlink() for x in [p, *p.parents] if x != Path(root).parent),
            'symlink evidence is not supported')
    require(p.resolve().is_relative_to(Path(root).resolve()), 'evidence path escapes root')
    return p


PUMP_ANCHORS = ('int held = sl_gfx_active() && sl_pres_hold();',
                '/* limit 0 means run until')
# Spelled as the pump spells them, spacing included, so a rewritten statement
# is an unknown layout rather than a silently satisfied pattern.
PUMP_ORDER = ('sl_gfx_end_frame();', 'sl_pres_note(back, 0, 1.0, h);',
              'sl_gfx_begin_frame();', 'sl_frame++;')


def pump_order(shim):
    """Read the native frame-pump statement order out of the source text.

    The renderer timing producer stamps each frame with the pumped-frame
    counter as it stands when that frame ENDS, which is the value the regular
    present is recorded at. That identity holds only while the pump ends the
    previous frame, notes the regular present, opens the next frame, and
    advances the counter after both - exactly the order checked here, inside
    the block delimited by the two anchors. If the increment moves ahead of
    either frame boundary, every emitted row is attributed to the wrong frame,
    and nothing downstream can detect it: the CSV grid check compares which
    frame numbers appear, not which work each row measured.

    No clock ratio, frame offset or line number is assumed; the counter
    identity itself stays the producer's.

    Boundary: this is text adjacency, not program semantics. It cannot prove
    which statements execute, how often, under what condition, or that a
    shipped binary was built from this text. An unknown or reordered layout
    refuses so the adapter is reviewed instead of re-interpreted.
    """
    begin, end = PUMP_ANCHORS
    require(shim.count(begin) == 1 and shim.count(end) == 1,
            'native frame-pump block not found exactly once; review the adapter')
    start, stop = shim.index(begin), shim.index(end)
    require(start < stop, 'native frame-pump anchors are out of order; review the adapter')
    block = shim[start:stop]
    positions = []
    for token in PUMP_ORDER:
        require(block.count(token) == 1,
                'native frame-pump statement not present exactly once in the block: '
                + token + '; review the adapter')
        positions.append(block.index(token))
    require(positions == sorted(positions),
            'native frame-pump order changed; the producer frame identity is void '
            'until the adapter is reviewed. Required order: ' + ', '.join(PUMP_ORDER))
    return list(PUMP_ORDER)


def source_contract(root):
    """Audit the current native event-producer prerequisites without executing C.

    This is a source check, not a binary-to-source proof or runtime observation.
    Unknown source layouts refuse instead of guessing that an event writer runs.
    """
    root = Path(root)
    files = {name: pin(root / name) for name in (
        'src/platform/sl_main.c', 'src/platform/sl_ultra_shim.c')}
    main = (root / 'src/platform/sl_main.c').read_text(encoding='utf-8')
    shim = (root / 'src/platform/sl_ultra_shim.c').read_text(encoding='utf-8')
    needed = ["if (off != NULL && *off == '0') return;",
              'getenv("SL_RUN")', 'getenv("SL_RUN_DIR")',
              'if (g_run_dir[0] == 0) return;',
              'snprintf(path, sizeof path, "%s/events", g_run_dir);',
              'far-fog %.1f -> %.1f', 'env-rgb']
    require(all(x in main for x in needed), 'unrecognized event-producer source; review the adapter')
    require('sl_watch_frame((unsigned) sl_vi_advances);' in shim,
            'unrecognized event-record counter; no assumed frame conversion')
    # The event-record counter and the pumped-frame counter are different
    # series, and the metric CSV joins on the second one. Checking the counter
    # fixture without the pump order would pin the name of a counter whose
    # meaning the surrounding statement order decides.
    order = pump_order(shim)
    return {'schema': 'sightline-event-source-contract/1', 'inputs': files,
            'enable': {'SL_RUN': '1'}, 'private_root': 'SL_RUN_DIR',
            'event_index': 'sl_vi_advances / producer read=',
            'frame_identity_order': order,
            'qualification': 'source prerequisites and frame-pump statement order checked as text; '
                             'neither C semantics, native operation nor build provenance certified'}


def environment(base, extra):
    """The final override map; keys/values are literal data, never shell code."""
    require(isinstance(base, dict) and isinstance(extra, dict), 'environment maps required')
    for data in (base, extra):
        require(all(isinstance(k, str) and re.fullmatch(r'SL_[A-Z0-9_]+', k)
                    and isinstance(v, str) and '\x00' not in v and '\n' not in v
                    for k, v in data.items()), 'invalid literal SL_ environment entry')
    return dict(base, **extra)


def required_metrics(plan):
    result = set(plan['scene']['required_metrics'])
    if plan['measurement_mode'] == 'profile':
        result.update(('cpu_submit', 'gpu_elapsed'))
    return result


def make_plan(suite_path, scene_id, profile_id, bindings):
    catalog = suite(suite_path)
    matches = [x for x in catalog['scenes'] if x['id'] == scene_id]
    require(len(matches) == 1 and profile_id in catalog['profiles'], 'unknown scene or profile')
    object_keys(bindings, ['binary', 'source_root', 'machine_id', 'input', 'pose',
                          'harness', 'output_root', 'base_environment', 'overrides',
                          'counter_mapping_reviewed', 'metric_capabilities', 'measurement_mode', 'workload'])
    require(bindings['measurement_mode'] in ('clean', 'profile'), 'unknown measurement mode')
    object_keys(bindings['workload'], ['stage', 'pose_id', 'seed'])
    require(all(isinstance(v, str) and v.strip() for v in bindings['workload'].values()), 'workload identity strings required')
    require(isinstance(bindings['machine_id'], str) and bool(bindings['machine_id'].strip()), 'machine identity required')
    require(bindings['counter_mapping_reviewed'] is True, 'producer counter mapping must be reviewed')
    require(isinstance(bindings['metric_capabilities'], list), 'metric capability list required')
    for capability in bindings['metric_capabilities']:
        require(capability in ('host_frames', 'cpu_submit', 'gpu_elapsed'), 'unknown metric capability')
    scene = matches[0]
    output = Path(bindings['output_root']).resolve()
    require(not output.exists() and output.parent.is_dir(), 'output must be a new path under an existing parent')
    require(not output.is_relative_to(Path(bindings['source_root']).resolve()),
            'live output must be outside the source tree')
    harness = pin(bindings['harness'])
    effective = environment(bindings['base_environment'], bindings['overrides'])
    if bindings['measurement_mode'] == 'profile':
        require(effective.get('SL_RENDER_PROFILE') == str(output/'metrics.csv'), 'profile output must be private output_root/metrics.csv')
    else:
        require('SL_RENDER_PROFILE' not in effective, 'clean budget runs must have profiling disabled')
    if scene['events_required']:
        # An unrecognized harness is not assumed to honor overrides. This
        # compatibility adapter is bound to the reviewed byte-exact runner.
        require(harness['sha256'] == KNOWN_HARNESS, 'event-dependent harness needs a reviewed adapter')
        text = Path(harness['path']).read_text(encoding='utf-8')
        require("SL_RUN = '0'" in text and "foreach ($kv in ($ExtraKv -split ';'))" in text,
                'known harness override order changed')
        require(bindings['base_environment'].get('SL_RUN') == '0', 'harness default SL_RUN must be recorded honestly')
        require(bindings['overrides'].get('SL_RUN') == '1' and effective['SL_RUN'] == '1',
                'event producer disabled: explicit SL_RUN=1 override required')
        event_root = Path(effective.get('SL_RUN_DIR', '')).resolve()
        require('SL_RUN_DIR' in bindings['overrides'] and event_root == output/'native-events',
                'event logging needs an isolated new output_root/native-events root')
    source = source_contract(bindings['source_root'])
    return {'schema': 'sightline-readiness-plan/1', 'suite': pin(suite_path),
            'scene': scene, 'profile_id': profile_id, 'profile': catalog['profiles'][profile_id],
            'window': catalog['window'], 'binary': pin(bindings['binary']),
            'input': pin(bindings['input']), 'pose': pin(bindings['pose']),
            'harness': harness, 'source_contract': source, 'machine_id': bindings['machine_id'],
            'measurement_mode': bindings['measurement_mode'], 'workload': bindings['workload'],
            'output_root': str(output), 'base_environment':bindings['base_environment'],'effective_environment': effective,
            'environment_overrides': bindings['overrides'],
            'metric_capabilities': sorted(set(bindings['metric_capabilities'])),
            'counter_mapping_reviewed': True, 'launch_authorized': False,
            'native_qualification': 'NOT_RUN',
            'qualification': 'inert plan; source and configuration checks do not launch or certify a game'}


def check_plan(plan):
    require(plan.get('schema') == 'sightline-readiness-plan/1', 'unknown plan schema')
    require(plan['measurement_mode'] in ('clean', 'profile'), 'unknown measurement mode')
    object_keys(plan['workload'], ['stage', 'pose_id', 'seed'])
    require(all(isinstance(v, str) and v.strip() for v in plan['workload'].values()), 'workload identity strings required')
    for key in ('suite', 'binary', 'input', 'pose', 'harness'):
        same_pin(plan[key])
    for value in plan['source_contract']['inputs'].values():
        same_pin(value)
    source=plan['source_contract'];require(set(source['inputs'])=={'src/platform/sl_main.c','src/platform/sl_ultra_shim.c'},'event source pins incomplete')
    source_root=Path(source['inputs']['src/platform/sl_main.c']['path']).parents[2]
    require(source==source_contract(source_root),'source contract differs from its producer')
    require(environment(plan['base_environment'],plan['environment_overrides'])==plan['effective_environment'],'effective environment differs from override order')
    catalog = suite(plan['suite']['path'])
    require(plan['scene'] in catalog['scenes'] and plan['profile'] == catalog['profiles'][plan['profile_id']]
            and plan['window'] == catalog['window'], 'plan differs from its pinned suite')
    require(plan['launch_authorized'] is False and plan['counter_mapping_reviewed'] is True,
            'a plan cannot authorize execution')
    if plan['measurement_mode'] == 'profile':
        require(plan['effective_environment'].get('SL_RENDER_PROFILE') == str(Path(plan['output_root'])/'metrics.csv'), 'profile output redirected')
    else:
        require('SL_RENDER_PROFILE' not in plan['effective_environment'], 'clean budget run has profiling enabled')
    if plan['scene']['events_required']:
        env = plan['effective_environment']
        require(plan['harness']['sha256']==KNOWN_HARNESS and plan['base_environment'].get('SL_RUN')=='0' and plan['environment_overrides'].get('SL_RUN')=='1','event harness/override adapter changed')
        require(env.get('SL_RUN') == '1' and env.get('SL_RUN_DIR') == str(Path(plan['output_root'])/'native-events'),
                'gas logging disabled or redirected to a shared run root')


def preflight(plan):
    check_plan(plan)
    available = set(plan['metric_capabilities'])
    required = required_metrics(plan)
    missing = sorted(required - available)
    return {'schema': 'sightline-readiness-preflight/1', 'plan_sha256': identity(plan),
            'status': 'PREPARED_NOT_LIVE_QUALIFIED' if not missing else 'BLOCKED',
            'missing_metric_capabilities': missing, 'launch_authorized': False,
            'qualification': 'no process, graphics capability, privilege, event writer or frame budget measured'}


def parse_events(path):
    """Strict finite numeric event timelines; malformed witnesses never vanish."""
    env, fog = [], []
    numeric = r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?'
    rgb_re = re.compile(r'^\s*(\d+)\s+env-rgb\s+(\d+),\s*(\d+),\s*(\d+)\s*$')
    fog_re = re.compile(r'^\s*(\d+)\s+far-fog\s+('+numeric+r')\s*->\s*('+numeric+r')(?:\s+<-- GAS)?\s*$')
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        if 'env-rgb' in line:
            m = rgb_re.fullmatch(line)
            require(m is not None, 'malformed env-rgb event')
            record, *rgb = map(int, m.groups())
            require(all(0 <= x <= 255 for x in rgb), 'RGB event outside byte range')
            require(not env or record > env[-1][0], 'RGB records must strictly increase')
            env.append((record, rgb))
        elif 'far-fog' in line:
            m = fog_re.fullmatch(line)
            require(m is not None, 'malformed far-fog event')
            record = int(m[1]);prev = number(m[2], 'previous fog', -math.inf);now = number(m[3], 'current fog', -math.inf)
            require(not fog or record > fog[-1][0], 'fog records must strictly increase')
            fog.append((record, prev, now))
        # Other native event families do not describe the fog ladder.
    require(env and fog, 'missing RGB/far-fog event timelines')
    return {'env': env, 'fog': fog}


def gas_bands(timeline, captures, recipe):
    witnessed = set();previous=-1
    for capture in captures:
        record = integer(capture['read'], 'capture read', 0)
        rgb = [x[1] for x in timeline['env'] if x[0] <= record]
        fog = [x[2] for x in timeline['fog'] if x[0] <= record]
        if not rgb or not fog:
            continue
        if rgb[-1] == recipe['initial_rgb'] and fog[-1] == recipe['initial_far']:
            band='before'
        elif rgb[-1] == recipe['full_rgb'] and fog[-1] == recipe['full_far']:
            band='full'
        elif recipe['full_far'] < fog[-1] < recipe['initial_far']:
            band='buildup'
        else:continue
        order={'before':0,'buildup':1,'full':2}[band]
        require(order>=previous,'gas sample bands move backwards');previous=order;witnessed.add(band)
    require(witnessed == {'before', 'buildup', 'full'}, 'raw samples do not cover before, buildup and full gas')
    return sorted(witnessed)


def ppm(path, size):
    data = Path(path).read_bytes()
    parts = data.split(b'\n', 3)
    require(len(parts) == 4 and parts[0] == b'P6' and parts[2] == b'255', 'invalid strict P6 header')
    require(parts[1] == ('%d %d' % tuple(size)).encode(), 'capture resolution differs from plan')
    require(len(parts[3]) == size[0]*size[1]*3, 'truncated or padded P6 capture')
    return hashlib.sha256(parts[3]).hexdigest()


def feature_metrics(path, window):
    """Import normalized measured spans, never infer GPU time from host time.

    cpu_submit_ms is wall time of CPU submission, not sampled CPU execution.
    GPU values must be completed elapsed queries, not pending values or zeros
    substituted for absent data. Nested scopes are reported, never summed.
    """
    groups = {};seen = set()
    with Path(path).open(encoding='utf-8-sig', newline='') as f:
        reader = csv.DictReader(f)
        require(reader.fieldnames == METRIC_FIELDS, 'unknown feature metric columns')
        for row in reader:
            require(None not in row and all(v is not None for v in row.values()), 'malformed metric CSV row')
            frame = integer(row['frame'], 'metric frame', 0);scope = row['scope']
            require(bool(re.fullmatch(r'[a-z0-9_-]+', scope)), 'invalid scope name')
            require((frame, scope) not in seen, 'duplicate frame/scope metric')
            seen.add((frame, scope))
            cpu = number(row['cpu_submit_ms'], 'CPU submit time')
            require(row['gpu_status'] in ('ready', 'pending', 'unsupported'), 'unknown GPU timing status')
            gpu = number(row['gpu_elapsed_ms'], 'GPU elapsed time') if row['gpu_status'] == 'ready' else None
            require(gpu is not None or row['gpu_elapsed_ms'] == '', 'unavailable GPU time must be blank')
            if window['lo'] <= frame < window['hi'] and (frame-window['lo']) % window['step'] == 0:
                groups.setdefault(scope, []).append((frame, cpu, gpu))
    require(groups, 'no feature metrics in selected window')
    for rows in groups.values():
        rows.sort(key=lambda row: row[0])
    return {scope: {'samples': len(rows), 'frames': [r[0] for r in rows],
                    'cpu_submit_mean_ms': statistics.fmean(r[1] for r in rows),
                    'gpu_ready_samples': sum(r[2] is not None for r in rows),
                    'gpu_elapsed_mean_ms': statistics.fmean(r[2] for r in rows) if all(r[2] is not None for r in rows) else None}
            for scope, rows in sorted(groups.items())}


def foreground(path, work_area, grace):
    header='iso_time elapsed_s fg_hwnd fg_pid fg_is_game visible iconic unobscured rect_l rect_t rect_r rect_b game_alive'.split()
    require(isinstance(work_area,list) and len(work_area)==4 and
            all(type(x) is int for x in work_area) and work_area[2]>work_area[0] and work_area[3]>work_area[1], 'invalid primary working area')
    lines=Path(path).read_text(encoding='utf-8').splitlines()
    require(bool(lines) and lines[0].split()==header, 'unknown foreground log header')
    last=-1;covered=0
    for line in lines[1:]:
        if not line.strip():continue
        f=line.split();require(len(f)==len(header),'malformed foreground row')
        row=dict(zip(header,f));time=number(row['elapsed_s'],'foreground time')
        require(time>last,'foreground observation times must increase');last=time
        for k in ['fg_is_game','visible','iconic','unobscured','game_alive']:
            require(row[k] in ('True','False'),'invalid foreground boolean')
        rect=[int(row[k]) for k in ('rect_l','rect_t','rect_r','rect_b')]
        if time>=grace and row['game_alive']=='True':
            require(row['fg_is_game']==row['visible']==row['unobscured']=='True' and row['iconic']=='False', 'game lost qualified foreground')
            require(work_area[0]<=rect[0]<rect[2]<=work_area[2] and work_area[1]<=rect[1]<rect[3]<=work_area[3], 'game outside primary working area')
            covered+=1
    require(covered>0,'no qualified foreground observations after grace')
    return {'covered_polls':covered,'qualification':'sampled native launcher observations, not proof between polls'}


def validate_run(plan, root):
    check_plan(plan);root = Path(root).resolve()
    require(preflight(plan)['status']!='BLOCKED','required metric producer is unavailable')
    require(root == Path(plan['output_root']), 'evidence root is not the planned output root')
    input_files=[p for p in root.rglob('*') if p.is_file()]
    require(all(not p.is_symlink() and p.resolve().is_relative_to(root) for p in input_files), 'symlink or escaped input')
    pins={p.relative_to(root).as_posix():pin(p) for p in input_files}
    receipt = load_json(child(root, 'receipt.json'))
    object_keys(receipt, ['schema', 'plan_sha256', 'binary_sha256', 'machine_id',
                         'resolution', 'normal_exit', 'foreground_qualified',
                         'effective_environment', 'captures', 'events_path', 'metrics_path',
                         'evidence_kind', 'primary_work_area', 'workload'])
    require(receipt['schema'] == 'sightline-performance-receipt/1', 'unknown receipt schema')
    require(receipt['evidence_kind'] in ('synthetic', 'live'), 'receipt evidence kind required')
    require(receipt['plan_sha256'] == identity(plan), 'receipt belongs to another plan')
    require(receipt['workload'] == plan['workload'], 'observed stage/pose/seed differs from plan')
    require(receipt['binary_sha256'] == plan['binary']['sha256'] and receipt['machine_id'] == plan['machine_id'], 'binary or machine identity mismatch')
    size = [plan['profile']['width'], plan['profile']['height']]
    require(receipt['resolution'] == size, 'receipt physical resolution differs')
    require(receipt['normal_exit'] is True and receipt['foreground_qualified'] is True, 'normal exit and foreground qualification required')
    launch=child(root,'launch.txt').read_text(encoding='utf-8').splitlines()
    require('exit_code 0' in launch and 'timed_out False' in launch and
            len([x for x in launch if x.startswith('exit_code ')])==1 and
            len([x for x in launch if x.startswith('timed_out ')])==1, 'missing or contradictory normal-exit producer record')
    fg=foreground(child(root,'foreground-log.txt'),receipt['primary_work_area'],6)
    require(receipt['effective_environment'] == plan['effective_environment'], 'observed environment contradicts preflight')
    window = plan['window'];pres = analyze.frame_report(child(root, 'pres.txt'), window['lo'], window['hi'], window['step'])
    require(all(x[0] == 'R' for x in pres['identities']), 'regular-only benchmark required')
    captures = receipt['captures'];require(isinstance(captures, list) and captures, 'raw capture identities required')
    ids=[];images={}
    for row in captures:
        object_keys(row, ['path', 'frame', 'read'])
        frame = integer(row['frame'], 'capture frame', 0);record = integer(row['read'], 'capture read', 0)
        require(not ids or frame > ids[-1][0] and record > ids[-1][1], 'capture identities must increase')
        require(row['path'] not in images, 'duplicate capture path')
        images[row['path']] = ppm(child(root, row['path']), size);ids.append((frame, record))
    schedule = plan['scene']['captures']
    allowed = [list(range(schedule['first']+phase, schedule['last']+1, schedule['every'])) for phase in schedule['phases']]
    require([frame for frame, _ in ids] in allowed, 'captures differ from predeclared sampling schedule')
    if plan['scene']['timing_role'] == 'budget':
        require(all(frame < window['lo'] for frame, _ in ids), 'capture overlaps benchmark window')
    timeline = None
    if plan['scene']['events_required']:
        path=child(root, receipt['events_path']);require(path.resolve().is_relative_to(root/'native-events'), 'gas events outside private root')
        timeline=parse_events(path);gas_bands(timeline, captures, plan['scene']['gas'])
    if plan['measurement_mode'] == 'clean':
        require(receipt['metrics_path'] is None, 'clean budget run includes instrumented metrics')
    else:
        require(receipt['metrics_path'] == 'metrics.csv', 'profile metric output missing or redirected')
    metrics = feature_metrics(child(root, receipt['metrics_path']), window) if receipt['metrics_path'] is not None else {}
    required = required_metrics(plan)
    require('host_frames' in required, 'host frame metric required')
    if 'cpu_submit' in required or 'gpu_elapsed' in required:
        require('frame' in metrics, 'whole-frame CPU/GPU span missing')
        require(metrics['frame']['frames'] == list(range(window['lo'], window['hi'], window['step'])), 'frame span coverage differs from present grid')
    if 'gpu_elapsed' in required:
        require(metrics['frame']['gpu_elapsed_mean_ms'] is not None, 'GPU timing unavailable; never substitute host duration')
    require(set(pins)=={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}, 'input file set changed during analysis')
    for value in pins.values():same_pin(value)
    return {'schema': 'sightline-qualified-performance-run/1', 'plan': plan,
            'evidence_root': str(root), 'receipt_kind': receipt['evidence_kind'],
            'frames': pres, 'capture_identities': ids, 'image_digests': images,
            'events': timeline, 'feature_metrics': metrics, 'foreground':fg,'inputs': pins,
            'qualification': 'receipt exit/foreground/environment are supplied launcher observations; native fidelity requires separate live qualification'}


def compare_runs(plans, roots):
    require(len(plans) == len(roots) == 4, 'four ordered ABBA inputs required')
    require(len({str(Path(p).resolve()) for p in roots}) == 4, 'reused run root')
    runs=[validate_run(p, root) for p,root in zip(plans,roots)]
    require(len({x['receipt_kind'] for x in runs}) == 1, 'mixed synthetic and live evidence')
    # Only binary identity and the private output/event root may differ.
    contexts=[]
    for p in plans:
        context={k:v for k,v in p.items() if k not in ('binary','output_root','base_environment','environment_overrides','effective_environment')}
        env=dict(p['effective_environment']);env.pop('SL_RUN_DIR',None);env.pop('SL_RENDER_PROFILE',None);context['environment']=env;contexts.append(context)
    require(all(c == contexts[0] for c in contexts), 'unmatched scene/profile/input/source/machine controls')
    require(plans[0]['binary']==plans[3]['binary'] and plans[1]['binary']==plans[2]['binary'], 'repeated-arm binary identity differs')
    require(all(x['frames']['identities'] == runs[0]['frames']['identities'] and
                x['capture_identities'] == runs[0]['capture_identities'] for x in runs), 'work or capture identities differ')
    require(all(x['image_digests'] == runs[0]['image_digests'] for x in runs), 'raw sampled image mismatch: visual review or a separately versioned reference required')
    require(all(x['events'] == runs[0]['events'] for x in runs), 'gas timelines differ')
    profile=plans[0]['profile'];budget=profile['budget'];timed=plans[0]['scene']['timing_role']=='budget' and plans[0]['measurement_mode']=='clean'
    means=[x['frames']['mean_ms'] for x in runs];tails=[x['frames']['p99_ms_nearest_rank'] for x in runs]
    mean_bar=max(means[0],means[3])+max(abs(means[0]-means[3]),budget['mean_noise_floor_ms'],min(means[0],means[3])*budget['mean_regression_pct']/100)
    tail_bar=max(tails[0],tails[3])+max(abs(tails[0]-tails[3]),budget['p99_noise_floor_ms'],min(tails[0],tails[3])*budget['p99_regression_pct']/100)
    candidate_within_budget=all(means[i] <= budget['mean_ms'] and tails[i] <= budget['p99_ms'] for i in (1,2))
    no_regression=all(means[i] <= mean_bar and tails[i] <= tail_bar for i in (1,2))
    verdict='DIAGNOSTIC_ONLY' if not timed else 'REGRESSION' if not no_regression else 'OVER_BUDGET' if not candidate_within_budget else 'WITHIN_CONFIGURED_BUDGET'
    return {'schema':'sightline-performance-comparison/1','verdict':verdict,
            'evidence_kind':runs[0]['receipt_kind'],'scene_id':plans[0]['scene']['id'],
            'measurement_mode':plans[0]['measurement_mode'],
            'profile_id':plans[0]['profile_id'],'means_ms':means,'p99_ms':tails,
            'mean_regression_bar_ms':mean_bar,'p99_regression_bar_ms':tail_bar,
            'candidate_within_absolute_budget':candidate_within_budget,
            'no_material_regression':no_regression,'runs':runs,
            'release_authorized':False,
            'qualification':'configured budget screen only; synthetic evidence cannot qualify a renderer, and changing graphics requires separately reviewed visual references'}


def suite_gate(suite_path, comparison_paths, profile_id):
    policy=suite(suite_path);require(profile_id in policy['profiles'],'unknown profile')
    reports={};arm_context=None;used_roots=set()
    for path in comparison_paths:
        stored=load_json(path);require(stored.get('schema')=='sightline-performance-comparison/1','unknown comparison schema')
        actual=compare_runs([x['plan'] for x in stored['runs']], [x['evidence_root'] for x in stored['runs']])
        require(identity(actual)==identity(stored),'comparison summary differs from its raw evidence')
        scene=actual['scene_id']+':'+actual['measurement_mode'];require(scene not in reports,'duplicate scene comparison')
        require(actual['profile_id']==profile_id and all(x['plan']['suite']==pin(suite_path) for x in actual['runs']), 'comparison uses another profile or policy revision')
        context=[(x['plan']['binary'], x['plan']['machine_id'], x['receipt_kind']) for x in actual['runs']]
        require(arm_context is None or arm_context==context, 'suite mixes arm binaries, machines or evidence kinds')
        arm_context=context
        roots={x['evidence_root'] for x in actual['runs']}
        require(not used_roots.intersection(roots), 'suite reuses a run across workloads')
        used_roots.update(roots)
        reports[scene]=actual
    expected={x['id']+':clean' for x in policy['scenes']}
    expected.update(x['id']+':profile' for x in policy['scenes'] if x.get('requires_profile'))
    missing=sorted(expected-set(reports))
    require(not (set(reports)-expected),'unknown scene comparison')
    bad=sorted(k for k,v in reports.items() if v['verdict'] not in ('WITHIN_CONFIGURED_BUDGET','DIAGNOSTIC_ONLY'))
    synthetic=any(v['evidence_kind']!='live' for v in reports.values())
    status='INCOMPLETE' if missing else 'REGRESSION_OR_OVER_BUDGET' if bad else 'SYNTHETIC_ONLY' if synthetic else 'WITHIN_CONFIGURED_BUDGET'
    return {'schema':'sightline-performance-suite-gate/1','status':status,
            'missing_scenes':missing,'failing_scenes':bad,'profile_id':profile_id,
            'suite':pin(suite_path),'comparison_inputs':[pin(p) for p in comparison_paths],
            'automatic_baseline_update':False,'release_authorized':False,
            'qualification':'complete supplied live suite can meet configured budgets; shipping qualification, intended visual changes and machine coverage remain separate reviews'}


def main(argv=None):
    ap=argparse.ArgumentParser(description=__doc__);sub=ap.add_subparsers(dest='command',required=True)
    c=sub.add_parser('catalog');c.add_argument('--suite',type=Path,default=SUITE)
    p=sub.add_parser('plan');p.add_argument('--suite',type=Path,default=SUITE);p.add_argument('--scene',required=True);p.add_argument('--profile',default='1080p60');p.add_argument('--bindings',type=Path,required=True)
    f=sub.add_parser('preflight');f.add_argument('--plan',type=Path,required=True)
    v=sub.add_parser('validate');v.add_argument('--plan',type=Path,required=True);v.add_argument('--root',type=Path,required=True)
    g=sub.add_parser('compare');g.add_argument('--plans',type=Path,nargs=4,required=True);g.add_argument('--roots',type=Path,nargs=4,required=True)
    q=sub.add_parser('gate');q.add_argument('--suite',type=Path,default=SUITE);q.add_argument('--profile',default='1080p60');q.add_argument('--comparisons',type=Path,nargs='+',required=True)
    for parser in (c,p,f,v,g,q):parser.add_argument('--out',type=Path,required=True)
    args=ap.parse_args(argv)
    try:
        require(not args.out.exists(), 'output exists; evidence is create-only')
        if args.command=='catalog':result=suite(args.suite)
        elif args.command=='plan':result=make_plan(args.suite,args.scene,args.profile,load_json(args.bindings))
        elif args.command=='preflight':result=preflight(load_json(args.plan))
        elif args.command=='validate':result=validate_run(load_json(args.plan),args.root)
        elif args.command=='compare':result=compare_runs([load_json(p) for p in args.plans],args.roots)
        else:result=suite_gate(args.suite,args.comparisons,args.profile)
        write_json(args.out,result);print(json.dumps({'written':str(args.out),'schema':result['schema']}))
        return 1 if result.get('status') in ('BLOCKED','INCOMPLETE','REGRESSION_OR_OVER_BUDGET','SYNTHETIC_ONLY') or result.get('verdict') in ('REGRESSION','OVER_BUDGET') else 0
    except (OSError,ValueError,KeyError,TypeError,OverflowError) as error:
        print(str(error),file=sys.stderr);return 2


if __name__=='__main__':
    sys.exit(main())
