#!/usr/bin/env python3
"""perflive-ppmdiff - EXACT capture comparison against one or more references.

WHAT THIS IS. The comparison half of the discriminating diagnostic that
tools/windows/perflive.ps1 -Discriminate prepares: each new arm's capture set
compared to EVERY declared reference set, byte for byte with NO TOLERANCE, plus
the three channel values of one declared pixel in every capture of every set.
The output is the table a reader needs: arm -> equal to each reference yes/no ->
that pixel's RGB -> every differing byte decoded.

A REFERENCE IS EITHER SAVED EVIDENCE OR ANOTHER ARM. --baseline names a
directory of captures recorded by an earlier run, which is how a saved pair is
reused instead of re-run; --base names an arm inside the attempt directory. Both
kinds are compared the same way, and a run may declare several of each.

HASH-PINNED REFERENCES. --pin binds a reference capture to its sha256 and is
checked before anything is compared. A reference that has drifted makes every
comparison against it NOT COMPARABLE rather than quietly measuring against
different bytes. Every file read, reference or arm, has its sha256 recorded.

WHY BYTE EQUALITY AND A NAMED PIXEL, AND NOT A METRIC. The signal this exists
to read is one channel of one pixel moving by one step. Any tolerance large
enough to be called one is larger than the whole signal, so there is none here:
sets are equal or they are not, and when they are not, every differing byte is
decoded to (x, y, channel) with both values. A difference is a MEASUREMENT, not
a failure - the exit code reports whether the comparison could be MADE, never
which way it came out, and this tool draws no conclusion from which reference an
arm matched.

THE PIXEL'S COORDINATES. (x, y) is column x, row y of the IMAGE, with row 0 the
top row. That is also row 0 of the file's payload: the capture writer flips GL's
bottom-left origin as it writes, one row at a time, so the first row in the file
is the top row of the image (src/gfx/sl_gfx_sdl.c:861-863). The byte offset of
the pixel's red channel is header_length + (y * width + x) * 3.

THE FORMAT IS THE CAPTURE WRITER'S OWN AND IS NOT GUESSED AT. "P6\\n<w> <h>\\n255\\n"
then exactly width*height*3 bytes (src/gfx/sl_gfx_sdl.c:860). A file whose
magic, maxval or payload length is anything else is reported UNREADABLE rather
than parsed on a second guess: a comparison made against a misparsed image is
worse than an absent one. Comments (# to end of line) are accepted between
header tokens because the format allows them, and a file carrying one is
recorded as carrying one.

WHAT "NOT COMPARABLE" IS. A set whose directory is missing, which holds no
captures, any of whose files is unreadable in the sense above, or which failed a
pin; and an arm whose capture NAMES differ from a reference's, for that
reference. Those are absences of measurement and the process exits 3. An arm
that WAS compared and differs exits 0, because that is the reading.

usage:
  perflive-ppmdiff.py --attempt <dir> --arm <arm> [--arm <arm> ...]
                      [--baseline <label>=<dir> ...] [--base <arm> ...]
                      [--pin <label>:<capture>=<sha256> ...]
                      [--pixel X,Y] [--max-diff N] [--json <path>]

  --attempt   directory holding <arm>/shot-NNNNN.ppm per arm
  --arm       an arm to compare, repeatable; table order is the order given
  --baseline  a reference set in its own directory, repeatable
  --base      a reference set that is an arm of the attempt directory, repeatable
  --pin       a reference capture's required sha256, repeatable
  --pixel     the pixel to read out, default 71,113
  --max-diff  cap on the differing pixels enumerated per capture pair, default 16
  --json      write the full record here as well as printing the table
"""

import hashlib
import json
import os
import sys
import time

SCHEMA = 'sightline-perflive-ppmdiff/2'
DEFAULT_PIXEL = (71, 113)
DEFAULT_MAX_DIFF = 16


class Unreadable(Exception):
    """A file that is not a capture this tool will parse. Carries the reason."""


def _header(blob):
    """The three header quantities and the payload's start offset.

    Tokenised the way the format defines rather than by splitting on the first
    three newlines: the writer emits exactly two, but a file carrying extra
    whitespace or a comment is still a P6 file and reading it wrongly would
    shift every pixel.
    """
    if not blob.startswith(b'P6'):
        raise Unreadable('magic is %r, not P6' % (blob[:2],))
    i = 2
    vals = []
    comments = 0
    while len(vals) < 3:
        while i < len(blob) and blob[i:i + 1].isspace():
            i += 1
        if i < len(blob) and blob[i:i + 1] == b'#':
            comments += 1
            while i < len(blob) and blob[i:i + 1] != b'\n':
                i += 1
            continue
        start = i
        while i < len(blob) and not blob[i:i + 1].isspace() and blob[i:i + 1] != b'#':
            i += 1
        if i == start:
            raise Unreadable('the header ends before width, height and maxval are all present')
        tok = blob[start:i]
        try:
            vals.append(int(tok))
        except ValueError:
            raise Unreadable('header token %r is not an integer' % (tok,))
    if i >= len(blob) or not blob[i:i + 1].isspace():
        raise Unreadable('no single whitespace byte separates the header from the payload')
    i += 1
    w, h, maxval = vals
    if maxval != 255:
        raise Unreadable('maxval is %d; the capture writer emits 255' % (maxval,))
    if w <= 0 or h <= 0:
        raise Unreadable('dimensions %dx%d' % (w, h))
    want = w * h * 3
    got = len(blob) - i
    if got != want:
        raise Unreadable('payload is %d bytes, %dx%d*3 is %d' % (got, w, h, want))
    return w, h, i, comments


def read_ppm(path):
    with open(path, 'rb') as f:
        blob = f.read()
    w, h, off, comments = _header(blob)
    return {'path': path, 'bytes': len(blob), 'width': w, 'height': h,
            'payload_offset': off, 'header_comments': comments,
            'sha256': hashlib.sha256(blob).hexdigest(), 'blob': blob}


def pixel_rgb(img, x, y):
    """The three channel values at (x, y), or None when it is outside."""
    if x < 0 or y < 0 or x >= img['width'] or y >= img['height']:
        return None
    o = img['payload_offset'] + (y * img['width'] + x) * 3
    return [img['blob'][o], img['blob'][o + 1], img['blob'][o + 2]]


def decode_diff(ref, arm, cap):
    """Every differing payload byte, decoded, with the uncapped total.

    The decode is (x, y, channel) in the IMAGE's own coordinates, so a reader
    compares it directly against the declared pixel without arithmetic. The
    enumeration is capped; the COUNT never is, so a capped list is still
    reported beside the real total.
    """
    pr, pa = ref['payload_offset'], arm['payload_offset']
    dr, da = ref['blob'], arm['blob']
    n = ref['width'] * ref['height'] * 3
    ch = ('r', 'g', 'b')
    out = []
    total = 0
    for k in range(n):
        vr, va = dr[pr + k], da[pa + k]
        if vr == va:
            continue
        total += 1
        if len(out) < cap:
            px, c = divmod(k, 3)
            y, x = divmod(px, ref['width'])
            out.append({'x': x, 'y': y, 'channel': ch[c], 'reference': vr, 'arm': va,
                        'step': va - vr})
    return total, out


def load_set(label, path, pins):
    """One capture set: its files in name order, their hashes, and the pins.

    A set is comparable only when every file parsed AND every pin it was given
    matched. A pin naming a capture the set does not hold is a drift too: the
    set is not the one that was pinned.
    """
    rec = {'label': label, 'dir': os.path.abspath(path), 'capture_names': [],
           'captures': [], 'sha256': {}, 'comparable': False, 'not_comparable': None,
           'pins_checked': 0, 'pin_findings': []}
    if not os.path.isdir(path):
        rec['not_comparable'] = 'no directory %r' % (os.path.abspath(path),)
        return rec
    names = sorted(n for n in os.listdir(path)
                   if n.startswith('shot-') and n.endswith('.ppm')
                   and os.path.isfile(os.path.join(path, n)))
    if not names:
        rec['not_comparable'] = 'no shot-NNNNN.ppm under %r' % (os.path.abspath(path),)
        return rec
    rec['capture_names'] = names
    bad = []
    for n in names:
        try:
            img = read_ppm(os.path.join(path, n))
        except (OSError, Unreadable) as exc:
            bad.append('%s: %s' % (n, exc))
            continue
        rec['captures'].append((n, img))
        rec['sha256'][n] = img['sha256']
    for n, want in sorted(pins.items()):
        rec['pins_checked'] += 1
        got = rec['sha256'].get(n)
        if got is None:
            rec['pin_findings'].append('%s is pinned but was not read from this set' % (n,))
        elif got != want.lower():
            rec['pin_findings'].append('%s hashes %s, pinned %s' % (n, got, want.lower()))
    if bad:
        rec['not_comparable'] = '; '.join(bad[:6])
    elif rec['pin_findings']:
        rec['not_comparable'] = 'PIN DRIFT: ' + '; '.join(rec['pin_findings'][:6])
    else:
        rec['comparable'] = True
    return rec


def compare(refs, arms, pixel, cap):
    px, py = pixel
    rows = []
    for a in arms:
        row = {'arm': a['label'], 'dir': a['dir'], 'shots': len(a['capture_names']),
               'capture_names': a['capture_names'], 'sha256': a['sha256'],
               'comparable_at_all': a['comparable'],
               'not_comparable': a['not_comparable'],
               'pixel': [], 'against': []}
        for n, img in a['captures']:
            row['pixel'].append({'capture': n, 'rgb': pixel_rgb(img, px, py),
                                 'width': img['width'], 'height': img['height']})
        for r in refs:
            ag = {'reference': r['label'], 'equal': None, 'comparable': False,
                  'not_comparable': None, 'differences': []}
            if not r['comparable']:
                ag['not_comparable'] = 'the reference is not comparable: %s' % (r['not_comparable'],)
            elif not a['comparable']:
                ag['not_comparable'] = 'this arm is not comparable: %s' % (a['not_comparable'],)
            elif a['capture_names'] != r['capture_names']:
                ag['not_comparable'] = ('capture names differ from the reference: %r here, %r in %s'
                                        % (a['capture_names'], r['capture_names'], r['label']))
            else:
                ag['comparable'] = True
                equal = True
                for (n, img), (rn, rimg) in zip(a['captures'], r['captures']):
                    if img['width'] != rimg['width'] or img['height'] != rimg['height']:
                        equal = False
                        ag['differences'].append({
                            'capture': n, 'dimensions_differ': True,
                            'reference': '%dx%d' % (rimg['width'], rimg['height']),
                            'arm': '%dx%d' % (img['width'], img['height']),
                            'differing_bytes': None, 'pixels': []})
                        continue
                    if img['blob'] == rimg['blob']:
                        continue
                    equal = False
                    total, decoded = decode_diff(rimg, img, cap)
                    ag['differences'].append({
                        'capture': n, 'dimensions_differ': False,
                        'header_differs': (rimg['blob'][:rimg['payload_offset']]
                                           != img['blob'][:img['payload_offset']]),
                        'differing_bytes': total, 'enumerated': len(decoded), 'cap': cap,
                        'pixels': decoded})
                ag['equal'] = equal
            row['against'].append(ag)
        rows.append(row)
    return rows


def collapse(pixels):
    """The pixel column: one RGB when every capture agrees, else each one."""
    vals = [p['rgb'] for p in pixels]
    if not vals:
        return '-'
    if all(v == vals[0] for v in vals) and vals[0] is not None:
        return '%d,%d,%d (all %d)' % (vals[0][0], vals[0][1], vals[0][2], len(vals))
    return ' | '.join('-' if v is None else '%d,%d,%d' % (v[0], v[1], v[2]) for v in vals)


def print_table(refs, rows, pixel, out=sys.stdout):
    px, py = pixel
    head = ['arm', 'shots'] + ['= %s' % (r['label'],) for r in refs] + \
           ['(%d,%d) RGB' % (px, py), 'differing bytes']
    body = []
    for r in rows:
        cells = [r['arm'], str(r['shots'])]
        for ag in r['against']:
            if not ag['comparable']:
                cells.append('NOT COMP.')
            elif ag['equal']:
                cells.append('yes')
            else:
                cells.append('no')
        cells.append(collapse(r['pixel']))
        per = []
        for ag in r['against']:
            if not ag['comparable']:
                per.append('%s:-' % (ag['reference'],))
            elif not ag['differences']:
                per.append('%s:0' % (ag['reference'],))
            else:
                tot = ['?' if d['differing_bytes'] is None else str(d['differing_bytes'])
                       for d in ag['differences']]
                per.append('%s:%s' % (ag['reference'], '/'.join(tot)))
        cells.append('  '.join(per))
        body.append(cells)
    n = len(head)
    widths = [max([len(head[i])] + [len(b[i]) for b in body]) for i in range(n)]
    print('  '.join(head[i].ljust(widths[i]) for i in range(n)), file=out)
    print('  '.join('-' * widths[i] for i in range(n)), file=out)
    for b in body:
        print('  '.join(b[i].ljust(widths[i]) for i in range(n)), file=out)

    for r in refs:
        line = ('reference %s: %d capture(s), %d pin(s) checked, %s'
                % (r['label'], len(r['capture_names']), r['pins_checked'],
                   'BOUND' if r['comparable'] else 'NOT COMPARABLE'))
        print('', file=out)
        print(line, file=out)
        print('  %s' % (r['dir'],), file=out)
        if r['not_comparable']:
            print('  %s' % (r['not_comparable'],), file=out)
        else:
            for nm in r['capture_names']:
                print('  %s  %s' % (nm, r['sha256'].get(nm)), file=out)
    for r in rows:
        if r['not_comparable']:
            print('', file=out)
            print('%s NOT COMPARABLE: %s' % (r['arm'], r['not_comparable']), file=out)
        for ag in r['against']:
            if ag['not_comparable']:
                print('', file=out)
                print('%s vs %s NOT COMPARABLE: %s'
                      % (r['arm'], ag['reference'], ag['not_comparable']), file=out)
                continue
            for d in ag['differences']:
                print('', file=out)
                if d.get('dimensions_differ'):
                    print('%s vs %s %s: dimensions %s against %s'
                          % (r['arm'], ag['reference'], d['capture'], d['arm'],
                             d['reference']), file=out)
                    continue
                print('%s vs %s %s: %d differing byte(s)%s'
                      % (r['arm'], ag['reference'], d['capture'], d['differing_bytes'],
                         ', header differs' if d.get('header_differs') else ''), file=out)
                for p in d['pixels']:
                    print('    (%d,%d) %s %d -> %d (step %+d)'
                          % (p['x'], p['y'], p['channel'], p['reference'], p['arm'],
                             p['step']), file=out)
                if d['enumerated'] < d['differing_bytes']:
                    print('    ... %d more, enumeration capped at %d'
                          % (d['differing_bytes'] - d['enumerated'], d['cap']), file=out)


def parse_pin(spec, pins):
    """--pin LABEL:capture=sha256 into pins[LABEL][capture]."""
    if ':' not in spec or '=' not in spec:
        return 'expected LABEL:capture=sha256, got %r' % (spec,)
    label, rest = spec.split(':', 1)
    cap, sha = rest.split('=', 1)
    label, cap, sha = label.strip(), cap.strip(), sha.strip()
    if not label or not cap or len(sha) != 64:
        return 'expected LABEL:capture=<64 hex>, got %r' % (spec,)
    try:
        int(sha, 16)
    except ValueError:
        return '%r is not a sha256' % (sha,)
    pins.setdefault(label, {})[cap] = sha.lower()
    return None


def main(argv):
    attempt = jsonp = None
    arm_names = []
    baselines = []
    base_arms = []
    pins = {}
    pixel = DEFAULT_PIXEL
    cap = DEFAULT_MAX_DIFF
    i = 0
    while i < len(argv):
        a = argv[i]
        nxt = argv[i + 1] if i + 1 < len(argv) else None
        if nxt is None:
            print('perflive-ppmdiff: %r takes a value' % (a,), file=sys.stderr)
            return 2
        if a == '--attempt':
            attempt = nxt
        elif a == '--arm':
            arm_names.append(nxt)
        elif a == '--base':
            base_arms.append(nxt)
        elif a == '--baseline':
            if '=' not in nxt:
                print('perflive-ppmdiff: --baseline takes LABEL=DIR', file=sys.stderr)
                return 2
            lab, d = nxt.split('=', 1)
            if not lab.strip() or not d.strip():
                print('perflive-ppmdiff: --baseline takes LABEL=DIR', file=sys.stderr)
                return 2
            baselines.append((lab.strip(), d.strip()))
        elif a == '--pin':
            why = parse_pin(nxt, pins)
            if why:
                print('perflive-ppmdiff: --pin %s' % (why,), file=sys.stderr)
                return 2
        elif a == '--json':
            jsonp = nxt
        elif a == '--pixel':
            parts = nxt.split(',')
            if len(parts) != 2:
                print('perflive-ppmdiff: --pixel takes X,Y', file=sys.stderr)
                return 2
            try:
                pixel = (int(parts[0]), int(parts[1]))
            except ValueError:
                print('perflive-ppmdiff: --pixel takes two integers', file=sys.stderr)
                return 2
        elif a == '--max-diff':
            try:
                cap = int(nxt)
            except ValueError:
                print('perflive-ppmdiff: --max-diff takes an integer', file=sys.stderr)
                return 2
            if cap < 1:
                print('perflive-ppmdiff: --max-diff must be at least 1', file=sys.stderr)
                return 2
        else:
            print('perflive-ppmdiff: unexpected argument %r' % (a,), file=sys.stderr)
            print(__doc__.strip().splitlines()[0], file=sys.stderr)
            return 2
        i += 2
    if not attempt or not arm_names or not (baselines or base_arms):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    if not os.path.isdir(attempt):
        print('perflive-ppmdiff: no directory %r' % (attempt,), file=sys.stderr)
        return 2
    labels = [b[0] for b in baselines] + base_arms
    if len(set(labels)) != len(labels):
        print('perflive-ppmdiff: two references share a label %r' % (labels,), file=sys.stderr)
        return 2
    for n in arm_names:
        if n in labels:
            print('perflive-ppmdiff: %r is both an arm and a reference; a set is never '
                  'compared to itself' % (n,), file=sys.stderr)
            return 2
    unknown = [k for k in pins if k not in labels]
    if unknown:
        print('perflive-ppmdiff: --pin names no declared reference: %r' % (unknown,),
              file=sys.stderr)
        return 2

    refs = [load_set(lab, d, pins.get(lab, {})) for lab, d in baselines]
    refs += [load_set(n, os.path.join(attempt, n), pins.get(n, {})) for n in base_arms]
    arms = [load_set(n, os.path.join(attempt, n), {}) for n in arm_names]
    rows = compare(refs, arms, pixel, cap)
    print_table(refs, rows, pixel)

    # The record drops the pixel payloads it was built from and keeps the
    # readings: a table is read, not re-derived, and an embedded image would
    # make the record unreadable.
    doc = {'schema': SCHEMA,
           'written_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
           'attempt': os.path.abspath(attempt),
           'pixel': {'x': pixel[0], 'y': pixel[1],
                     'convention': 'column x, row y of the image, row 0 the TOP row, which '
                                   'is also the first payload row: the capture writer flips '
                                   "GL's bottom-left origin as it writes "
                                   '(src/gfx/sl_gfx_sdl.c:861-863)'},
           'max_diff': cap,
           'rule': 'whole-file byte equality against every declared reference, no tolerance; '
                   'a difference is a measurement and not a failure, the exit code reports '
                   'only whether every comparison could be made, and which reference an arm '
                   'matched is reported without being explained',
           'references': [{k: v for k, v in r.items() if k != 'captures'} for r in refs],
           'rows': rows}
    if jsonp:
        d = os.path.dirname(os.path.abspath(jsonp))
        if d and not os.path.isdir(d):
            os.makedirs(d)
        with open(jsonp, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(doc, f, indent=2)
            f.write('\n')
        print('', file=sys.stdout)
        print('record: %s' % (os.path.abspath(jsonp),), file=sys.stdout)

    bad = ['reference ' + r['label'] for r in refs if not r['comparable']]
    for row in rows:
        for ag in row['against']:
            if not ag['comparable']:
                bad.append('%s vs %s' % (row['arm'], ag['reference']))
    if bad:
        print('', file=sys.stdout)
        print('NOT COMPARABLE: %s - the table is incomplete and says nothing about those'
              % (', '.join(bad),), file=sys.stdout)
        return 3
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
