"""Offline sample attribution and presentation timing.

Reports remain descriptive. Sample weights measure sampled CPU running time,
not GPU duration, waiting time, whole-frame latency or a performance benefit.
Only resolve invokes an explicitly selected external debug-information reader.
"""
import argparse
import bisect
import collections
import csv
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys

from common import (ProfileError, digest, hex_address, integer, load_json,
                    number, pin, same_pin, write_json)


def symbol_map(binary, nm_text, image_base, image_size, frames_text=None):
    """Import BSD-style `nm -n -S --defined-only` saved output.

    Explicit positive-size T/t intervals qualify. Unsized entries can use saved
    FDE ranges only when the range starts exactly at the symbol entry. No range
    is extended to the next symbol. Aliases sharing an interval merge;
    overlapping unequal intervals are rejected instead of guessing ownership.
    """
    base, size = hex_address(image_base), hex_address(image_size)
    if size == 0 or base + size > 2**64:
        raise ProfileError('invalid image bounds')
    binary_pin, nm_pin = pin(binary), pin(nm_text)
    groups, unsized, entries = {}, 0, {}
    for n, line in enumerate(Path(nm_text).read_text(encoding='utf-8-sig').splitlines(), 1):
        parts = line.split()
        if not parts:
            continue
        if len(parts) == 3 and parts[1] in ('T', 't'):
            address = hex_address(parts[0])
            if not parts[2].startswith('.'):
                entries.setdefault(address, set()).add(parts[2])
            unsized += 1
            continue
        if len(parts) == 3 and len(parts[1]) == 1:
            continue  # data and non-sized non-code symbols
        if len(parts) != 4 or len(parts[2]) != 1:
            raise ProfileError('unrecognized nm row %d; use BSD format, no demangling' % n)
        address, extent = hex_address(parts[0]), hex_address(parts[1])
        if parts[2] not in ('T', 't'):
            continue
        if extent == 0:
            unsized += 1
            if not parts[3].startswith('.'):
                entries.setdefault(address, set()).add(parts[3])
            continue
        rva = address - base
        if rva < 0 or rva + extent > size:
            raise ProfileError('code symbol outside explicit image bounds')
        groups.setdefault((rva, extent), set()).add(parts[3])
    frames_pin, fde_matches = None, 0
    if frames_text is not None:
        frames_pin = pin(frames_text)
        for line in Path(frames_text).read_text(encoding='utf-8-sig').splitlines():
            if ' FDE ' not in line:
                continue
            match = re.search(r'\bpc=([0-9a-fA-F]+)\.\.([0-9a-fA-F]+)\s*$', line)
            if not match:
                raise ProfileError('unrecognized FDE PC range')
            start, end = (hex_address(value) for value in match.groups())
            if end < start or start < base or end > base + size:
                raise ProfileError('FDE outside explicit image bounds')
            # Only an exact code-symbol entry match names a range. Cold ranges,
            # unnamed FDEs and inter-function padding are not nearest-matched.
            if end > start and start in entries:
                groups.setdefault((start - base, end - start), set()).update(entries[start])
                fde_matches += 1
    intervals = [{'rva': start, 'size': extent, 'names': sorted(names)}
                 for (start, extent), names in sorted(groups.items())]
    if not intervals:
        raise ProfileError('no positive-size code symbols; symbol mapping is unavailable')
    validate_intervals(intervals, size)
    same_pin(binary_pin)
    same_pin(nm_pin)
    if frames_pin:
        same_pin(frames_pin)
    result = {'schema': 'sightline-symbols/1', 'binary': binary_pin,
            'nm_source': nm_pin, 'image_base': base, 'image_size': size,
            'unsized_code_symbols': unsized, 'intervals': intervals,
            'qualification': 'explicit nm extents or FDE ranges with exact symbol-entry matches; no nearest-symbol inference; no inline stacks'}
    if frames_pin:
        result['unwind_source'] = frames_pin
        result['fde_entry_matches'] = fde_matches
    return result


def validate_intervals(intervals, image_size):
    previous_end = 0
    for row in intervals:
        start = integer(row['rva'], 'rva')
        size = integer(row['size'], 'size', 1)
        if start < previous_end or start + size > image_size:
            raise ProfileError('overlapping, unsorted or out-of-image symbols')
        if not row['names'] or any(not isinstance(x, str) or not x for x in row['names']):
            raise ProfileError('invalid symbol names')
        previous_end = start + size


def resolve_points(binary, tool, samples, pid, image_base, image_size, runner=None):
    """Resolve exact sampled addresses with GNU addr2line, including sized-less PE.

    One-byte point intervals describe only queried instruction addresses. They
    never imply a function extent or infer an answer for a neighboring address.
    The caller supplies the exact tool path; no download or PATH search occurs.
    """
    base, size = hex_address(image_base), hex_address(image_size)
    pid = integer(pid, 'pid', 1)
    if not size or base + size > 2**64:
        raise ProfileError('invalid image bounds')
    binary_pin, tool_pin, sample_pin = pin(binary), pin(tool), pin(samples)
    addresses = set()
    for row in csv_rows(samples, SAMPLE_FIELDS):
        number(row['time_s'], 'time_s')
        row_pid = integer(row['pid'], 'pid', 1)
        integer(row['tid'], 'tid', 1)
        if number(row['weight_ms'], 'weight_ms') == 0:
            raise ProfileError('sample weight must be positive')
        loaded, ip = hex_address(row['module_base']), hex_address(row['ip'])
        if row['module_sha256'] != '-':
            digest(row['module_sha256'])
        if row_pid == pid and row['module_sha256'] == binary_pin['sha256']:
            rva = ip - loaded
            if not 0 <= rva < size or loaded + size > 2**64:
                raise ProfileError('target sample IP outside its loaded image')
            addresses.add(base + rva)
    if not addresses:
        raise ProfileError('no target addresses to resolve')
    ordered = sorted(addresses)
    argv = [tool_pin['path'], '-a', '-f', '-e', binary_pin['path']]
    options = {'input': ''.join('0x%x\n' % address for address in ordered),
               'capture_output': True, 'text': True, 'errors': 'replace', 'shell': False}
    if sys.platform == 'win32':
        options['creationflags'] = subprocess.CREATE_NO_WINDOW
    result = (runner or subprocess.run)(argv, **options)
    if result.returncode:
        raise ProfileError('addr2line failed: ' + result.stderr)
    lines = result.stdout.splitlines()
    if len(lines) != 3 * len(ordered):
        raise ProfileError('addr2line output has unexpected record count')
    intervals, unknown = [], 0
    for index, address in enumerate(ordered):
        returned, name, location = lines[3 * index:3 * index + 3]
        if hex_address(returned) != address or not name or not location:
            raise ProfileError('addr2line response does not match requested address')
        if name == '??':
            unknown += 1
        else:
            intervals.append({'rva': address - base, 'size': 1, 'names': [name]})
    if not intervals:
        raise ProfileError('debug information resolved no target addresses')
    for source_pin in (binary_pin, tool_pin, sample_pin):
        same_pin(source_pin)
    validate_intervals(intervals, size)
    return {'schema': 'sightline-symbols/1', 'binary': binary_pin,
            'image_base': base, 'image_size': size, 'intervals': intervals,
            'address_source': sample_pin, 'resolver': tool_pin,
            'resolver_argv': argv, 'resolver_stdout': result.stdout,
            'resolver_stderr': result.stderr, 'unresolved_addresses': unknown,
            'qualification': 'exact debug lookup points only; neighboring addresses remain unmapped; no inclusive inline stacks'}


SAMPLE_FIELDS = ('time_s', 'pid', 'tid', 'module_sha256', 'module_base', 'ip', 'weight_ms')
EXPORT_FIELDS = ('time_s', 'pid', 'tid', 'module', 'module_base', 'ip', 'weight_ms')


def csv_rows(path, fields, columns=None):
    """Accept UTF-8/BOM CSV with explicit column names, never positional guesses."""
    columns = columns or {field: field for field in fields}
    if set(columns) != set(fields) or len(set(columns.values())) != len(fields):
        raise ProfileError('column map must map every field to a distinct CSV column')
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or len(set(reader.fieldnames)) != len(reader.fieldnames):
            raise ProfileError('missing or duplicate CSV headers')
        if not set(columns.values()).issubset(reader.fieldnames):
            raise ProfileError('CSV is missing required columns: ' + str(columns))
        count = 0
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ProfileError('ragged CSV row')
            count += 1
            yield {field: row[columns[field]] for field in fields}
        if not count:
            raise ProfileError('CSV has no data rows')


def normalize_samples(source, columns, modules):
    """Adapt a leaf-IP export to canonical columns, with explicit module hashes.

    Runtime module bases come from each exported row, never a preferred/link
    base. Module keys are exact exported strings; unmatched modules stay unknown.
    A module hash is an attestation linking the exported module to a pinned
    image, not evidence inferred by matching an arbitrary filename.
    """
    for name, binary in modules.items():
        if not isinstance(name, str) or not name:
            raise ProfileError('invalid exported module key')
        digest(binary['sha256'])
        same_pin(binary)
    rows, matched = [], set()
    for row in csv_rows(source, EXPORT_FIELDS, columns):
        name = row.pop('module')
        if name in modules:
            matched.add(name)
        row['module_sha256'] = modules[name]['sha256'] if name in modules else '-'
        number(row['time_s'], 'time_s')
        integer(row['pid'], 'pid', 1)
        integer(row['tid'], 'tid', 1)
        hex_address(row['module_base'])
        hex_address(row['ip'])
        if number(row['weight_ms'], 'weight_ms') == 0:
            raise ProfileError('sample weight must be positive')
        rows.append(row)
    if not matched:
        raise ProfileError('no exported modules matched the explicit pinned module map')
    for binary in modules.values():
        same_pin(binary)
    return rows


def attribute(samples, symbols, pid, start, end, tid=None, columns=None):
    pid, start, end = integer(pid, 'pid', 1), number(start, 'start'), number(end, 'end')
    if end <= start:
        raise ProfileError('empty or inverted trace-time window')
    if tid is not None:
        tid = integer(tid, 'tid', 1)
    if symbols['schema'] != 'sightline-symbols/1':
        raise ProfileError('unknown symbol schema')
    binary_hash = digest(symbols['binary']['sha256'])
    same_pin(symbols['binary'])
    size = integer(symbols['image_size'], 'image_size', 1)
    intervals = symbols['intervals']
    validate_intervals(intervals, size)
    starts = [x['rva'] for x in intervals]
    weights, counts = collections.defaultdict(list), collections.Counter()
    reasons = collections.Counter()
    selected, selected_rows = [], 0
    source_pin = pin(samples)
    for row in csv_rows(samples, SAMPLE_FIELDS, columns):
        # Validate all rows, even those filtered out: bad data never vanishes.
        timestamp = number(row['time_s'], 'time_s')
        row_pid, row_tid = integer(row['pid'], 'pid', 1), integer(row['tid'], 'tid', 1)
        weight = number(row['weight_ms'], 'weight_ms')
        if weight == 0:
            raise ProfileError('sample weight must be positive')
        base, ip = hex_address(row['module_base']), hex_address(row['ip'])
        module = row['module_sha256']
        if module != '-':
            digest(module)
        if row_pid != pid or (tid is not None and row_tid != tid) or not start <= timestamp < end:
            continue
        selected.append(weight)
        selected_rows += 1
        label = '[other/unidentified module]'
        reason = 'other_or_unidentified_module'
        if module == binary_hash:
            rva = ip - base
            label, reason = '[unmapped target code]', 'no_explicit_symbol_extent'
            if base + size > 2**64 or not 0 <= rva < size:
                raise ProfileError('target sample IP outside its loaded image')
            index = bisect.bisect_right(starts, rva) - 1
            if index >= 0 and rva < intervals[index]['rva'] + intervals[index]['size']:
                label = ' / '.join(intervals[index]['names'])
                reason = 'mapped_target_code'
        weights[label].append(weight)
        counts[label] += 1
        reasons[reason] += 1
    if not selected:
        raise ProfileError('no samples for selected process/thread/time window')
    total = math.fsum(selected)
    if not math.isfinite(total):
        raise ProfileError('sample weight sum overflow')
    same_pin(source_pin)
    same_pin(symbols['binary'])
    return {'schema': 'sightline-samples/1', 'source': source_pin,
            'pid': pid, 'tid': tid, 'trace_window_s': [start, end],
            'sample_rows': selected_rows, 'total_weight_ms': total,
            'coverage_rows': dict(reasons),
            'functions': sorted([
                {'name': name, 'rows': counts[name], 'weight_ms': math.fsum(values),
                 'percent_of_selected_cpu_weight': 100 * math.fsum(values) / total}
                for name, values in weights.items()], key=lambda x: (-x['weight_ms'], x['name'])),
            'optimization_verdict': 'NOT_EVALUATED',
            'qualification': 'leaf-IP CPU sample attribution only; not inclusive stacks, GPU time or waiting time'}


def presentations(path):
    """Parse the engine's six-column pres.txt; B rows are budget records."""
    records, previous = [], -1.0
    for n, line in enumerate(Path(path).read_text(encoding='utf-8-sig').splitlines(), 1):
        fields = line.split()
        if not fields or fields[0].startswith('#'):
            continue
        if len(fields) != 6 or fields[1] not in ('R', 'E', 'B'):
            raise ProfileError('invalid presentation row %d' % n)
        timestamp = number(fields[0], 't_ms')
        frame = integer(fields[2], 'pumped_frame')
        if fields[1] == 'B':
            integer(fields[3], 'extras')
            number(fields[4], 'us-to-VI', -math.inf)
            number(fields[5], 'us-budget', -math.inf)
            continue
        alpha = number(fields[3], 'alpha')
        clock = integer(fields[4], 'clock')
        if alpha > 1 or not re.fullmatch('[0-9a-fA-F]{8}', fields[5]):
            raise ProfileError('invalid alpha or digest at row %d' % n)
        if timestamp <= previous:
            raise ProfileError('presentation timestamps must strictly increase')
        if records and frame < records[-1]['frame']:
            raise ProfileError('pumped frame moved backwards')
        records.append({'t_ms': timestamp, 'kind': fields[1], 'frame': frame,
                        'clock': clock, 'digest': fields[5].lower(), 'alpha': alpha})
        previous = timestamp
    if not records:
        raise ProfileError('no presentation records')
    return records


def frame_report(path, lo, hi, frame_step):
    lo, hi = integer(lo, 'lo'), integer(hi, 'hi')
    frame_step = integer(frame_step, 'frame_step', 1)
    if hi <= lo:
        raise ProfileError('empty or inverted frame window')
    source_pin = pin(path)
    records = presentations(path)
    selected = [x for x in records if lo <= x['frame'] < hi]
    # Explicit grid anchored at lo, never a shifted best-looking time window.
    expected = list(range(lo, hi, frame_step))
    regular = [x['frame'] for x in selected if x['kind'] == 'R']
    if regular != expected or len(selected) < 2:
        raise ProfileError('missing/duplicate/out-of-order regular frames on declared grid')
    intervals = [b['t_ms'] - a['t_ms'] for a, b in zip(selected, selected[1:])]
    ordered = sorted(intervals)
    p99 = ordered[math.ceil(.99 * len(ordered)) - 1]
    tail = ordered[-max(1, math.ceil(.01 * len(ordered))):]
    mean = statistics.fmean(intervals)
    same_pin(source_pin)
    return {'schema': 'sightline-frames/1', 'source': source_pin,
            'window': {'lo': lo, 'hi': hi, 'step': frame_step},
            'presentations': len(selected), 'intervals': len(intervals),
            'mean_ms': mean, 'fps_from_mean': 1000 / mean,
            'p99_ms_nearest_rank': p99, 'one_percent_low_fps': 1000 / statistics.fmean(tail),
            'identities': [[x['kind'], x['frame'], x['clock'], x['alpha']] for x in selected],
            'digests': [x['digest'] for x in selected],
            'hashes_informative': all(x['digest'] != '00000000' for x in selected),
            'optimization_verdict': 'NOT_EVALUATED',
            'qualification': 'all R/E presentation intervals within a declared frame window; no cross-clock alignment'}


def compare_abba(reports, contexts):
    """Descriptive same-work ABBA screen, separate from any correctness gate."""
    if len(reports) != 4 or len(contexts) != 4:
        raise ProfileError('ABBA requires exactly four reports and contexts')
    required = {'workload', 'config_sha256', 'input_sha256', 'binary_sha256', 'arm',
                'foreground_passed', 'normal_exit', 'correctness_passed'}
    for context in contexts:
        if set(context) != required or not isinstance(context['workload'], str) or not context['workload']:
            raise ProfileError('invalid comparison context')
        for name in ('config_sha256', 'input_sha256', 'binary_sha256'):
            digest(context[name])
        for name in ('foreground_passed', 'normal_exit', 'correctness_passed'):
            if context[name] is not True:
                raise ProfileError('comparison requires independently reviewed ' + name)
    if [x['arm'] for x in contexts] != ['A', 'B', 'B', 'A']:
        raise ProfileError('required order is A,B,B,A')
    for name in ('workload', 'config_sha256', 'input_sha256'):
        if len({x[name] for x in contexts}) != 1:
            raise ProfileError('workload context differs: ' + name)
    if contexts[0]['binary_sha256'] != contexts[3]['binary_sha256'] or contexts[1]['binary_sha256'] != contexts[2]['binary_sha256']:
        raise ProfileError('binary differs within repeated arm')
    for report in reports:
        if report.get('schema') != 'sightline-frames/1':
            raise ProfileError('unknown frame schema')
        same_pin(report['source'])
        # Recompute raw inputs: edited summary figures are never trusted.
        w = report['window']
        if report != frame_report(report['source']['path'], w['lo'], w['hi'], w['step']):
            raise ProfileError('frame report does not match source')
        if report['window'] != reports[0]['window'] or report['identities'] != reports[0]['identities']:
            raise ProfileError('selected workload identities differ')
    means = [x['mean_ms'] for x in reports]
    savings = [means[0] - means[1], means[3] - means[2]]
    spreads = [abs(means[0] - means[3]), abs(means[1] - means[2])]
    return {'schema': 'sightline-abba/1', 'order': ['A', 'B', 'B', 'A'],
            'means_ms': means, 'paired_savings_ms': savings, 'repeat_spreads_ms': spreads,
            'timing_screen_passed': min(savings) > max(spreads),
            'informative_log_digests_equal': all(x['hashes_informative'] for x in reports) and all(
                x['digests'] == reports[0]['digests'] for x in reports),
            'optimization_verdict': 'NOT_EVALUATED',
            'qualification': 'descriptive screen; contexts are supplied attestations, not proof of image/game-state parity or a noise model'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    symbols = commands.add_parser('symbols')
    symbols.add_argument('--binary', type=Path, required=True)
    symbols.add_argument('--nm-text', type=Path, required=True)
    symbols.add_argument('--image-base', required=True)
    symbols.add_argument('--image-size', required=True)
    symbols.add_argument('--frames-text', type=Path)
    samples = commands.add_parser('samples')
    samples.add_argument('--csv', type=Path, required=True)
    samples.add_argument('--symbols', type=Path, required=True)
    samples.add_argument('--columns', type=Path)
    samples.add_argument('--pid', type=int, required=True)
    samples.add_argument('--tid', type=int)
    samples.add_argument('--start', type=float, required=True)
    samples.add_argument('--end', type=float, required=True)
    normalize = commands.add_parser('normalize')
    normalize.add_argument('--csv', type=Path, required=True)
    normalize.add_argument('--columns', type=Path, required=True)
    normalize.add_argument('--modules', type=Path, required=True)
    resolve = commands.add_parser('resolve')
    resolve.add_argument('--binary', type=Path, required=True)
    resolve.add_argument('--addr2line', type=Path, required=True)
    resolve.add_argument('--csv', type=Path, required=True)
    resolve.add_argument('--pid', type=int, required=True)
    resolve.add_argument('--image-base', required=True)
    resolve.add_argument('--image-size', required=True)
    frames = commands.add_parser('frames')
    frames.add_argument('--pres', type=Path, required=True)
    frames.add_argument('--lo', type=int, required=True)
    frames.add_argument('--hi', type=int, required=True)
    frames.add_argument('--frame-step', type=int, required=True)
    compare = commands.add_parser('compare')
    compare.add_argument('--reports', type=Path, nargs=4, required=True)
    compare.add_argument('--contexts', type=Path, required=True)
    for command in (symbols, samples, normalize, resolve, frames, compare):
        command.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.out.exists():
            raise ProfileError('output already exists')
        if args.action == 'symbols':
            result = symbol_map(args.binary, args.nm_text, args.image_base, args.image_size,
                                args.frames_text)
        elif args.action == 'resolve':
            result = resolve_points(args.binary, args.addr2line, args.csv, args.pid,
                                    args.image_base, args.image_size)
        elif args.action == 'samples':
            symbol_pin = pin(args.symbols)
            columns_pin = pin(args.columns) if args.columns else None
            result = attribute(args.csv, load_json(args.symbols), args.pid, args.start, args.end,
                               args.tid, load_json(args.columns) if args.columns else None)
            same_pin(symbol_pin)
            result['symbol_map'] = symbol_pin
            if columns_pin:
                same_pin(columns_pin)
                result['column_map'] = columns_pin
        elif args.action == 'normalize':
            pins = [pin(p) for p in (args.csv, args.columns, args.modules)]
            rows = normalize_samples(args.csv, load_json(args.columns), load_json(args.modules))
            for source_pin in pins:
                same_pin(source_pin)
            # A provenance sidecar must also be new; preserve partial failures.
            sidecar = args.out.with_name(args.out.name + '.provenance.json')
            if sidecar.exists():
                raise ProfileError('provenance output already exists')
            with args.out.open('x', encoding='utf-8', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=SAMPLE_FIELDS)
                writer.writeheader()
                writer.writerows(rows)
            write_json(sidecar, {'schema': 'sightline-normalized-samples/1',
                                'inputs': pins, 'output': pin(args.out),
                                'module_identity': 'explicit pinned module map supplied with export'})
            print(json.dumps({'written': str(args.out.resolve()), 'rows': len(rows)}))
            return 0
        elif args.action == 'frames':
            result = frame_report(args.pres, args.lo, args.hi, args.frame_step)
        else:
            report_pins = [pin(p) for p in args.reports]
            context_pin = pin(args.contexts)
            result = compare_abba([load_json(p) for p in args.reports], load_json(args.contexts))
            for source_pin in [*report_pins, context_pin]:
                same_pin(source_pin)
            result['reports'] = report_pins
            result['contexts'] = context_pin
        write_json(args.out, result)
        print(json.dumps({'written': str(args.out.resolve()), 'schema': result['schema']}))
        return 0
    except (OSError, ValueError, KeyError, TypeError, OverflowError) as exc:
        print(str(exc), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
