#!/usr/bin/env python3
"""Offline grammar and safety check for the Windows crash reporter's fault header.

    python crashreport-grammar.py [--source <sl_main.c>] [--report <file> ...]

Exit 0 when every expectation held, 1 otherwise. Standard library only. It
reads files; it compiles nothing, links nothing and launches nothing.

WHAT IT GUARDS, and why each part exists.

A. THE SOURCE CONTRACT (src/platform/sl_main.c). The fault header is written by
   hand, line by line, inside a vectored exception handler, so none of its
   properties is visible to a compiler and none is visible in a diff:

     - ORDER. The fault's own fields - addr, access, eip, esp, ebp and the
       region at addr - are emitted before anything that can fail, so that no
       later evidence writer can remove, reorder or hide the fault itself.
     - NO SILENT FAILURE. Every early return in the pc-evidence writer is
       preceded by a line saying what did not resolve. A failure that printed
       nothing would read as an absent field, which is how a report stops being
       evidence.
     - NO LOADER CALL ON THE FAULT PATH. GetModuleHandle and GetModuleFileName
       acquire the loader lock ("Dynamic-Link Library Best Practices",
       Microsoft Win32 docs), so a fault raised while any thread holds it would
       stall the reporter mid-report. The check walks the handler's transitive
       call graph inside this file and fails on any loader API.
     - BOUNDED READS. The module name comes from the mapped image's own PE
       export directory. Every header and name byte must be probed with the
       handler's readability predicate and bounded by the image's SizeOfImage,
       using subtraction so a bound cannot wrap.
     - REFUSAL, NOT TRUNCATION. A name that does not fit the buffer must be
       refused with its own line. A silently truncated name is a wrong answer
       wearing the format of a right one.

B. THE TEXT GRAMMAR, on fixtures GENERATED FROM THE SOURCE LITERALS extracted
   in A - not from hand-typed copies of them, and with the name bound read out
   of the buffer declaration rather than restated here. A fixture whose
   spelling or bound drifts from the product's own format string passes while
   the product is broken, and that failure mode has cost this project live
   windows before. Negative fixtures each assert their OWN reason, so a
   negative cannot pass by failing for an unrelated cause. The boundary cases -
   a name of exactly the buffer bound, and one byte over - are both covered.

C. REAL CAPTURED REPORTS passed with --report, checked in LEGACY mode: every
   field the reporter wrote before the pc-evidence lines were added must still
   be accepted, and the new lines must be absent from a pre-change capture.
   That is the preservation claim, measured against a real artifact.

WHAT IT DOES NOT COVER: it does not compile or run anything, so the C control
flow it asserts is asserted structurally, not executed. Functions called from
the handler but DEFINED IN OTHER FILES are listed, not inspected.
"""
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------- part A ---

PRESERVED = [
    'sightline native: ACCESS VIOLATION addr 0x',
    '  access: ',
    '  eip    0x',
    '  esp    0x',
    '  ebp    0x',
    '  region at addr: ',
    '  stack words inside the image (candidate return addresses):',
    '=== sightline CRASH ===',
    '  frame  0x',
    '  pc     0x',
    '  resolve with: tools/native/crashinfo.sh',
]

ADDED = [
    '  region at eip: ',
    '  eip offset from alloc base',
    '  module at eip: ',
]

ADDED_AFTER = '  region at addr: '
ADDED_BEFORE = '  stack words inside the image (candidate return addresses):'

# Loader APIs. Any of these on the handler's call path is a defect, not a
# style question: each takes the loader lock.
LOADER_CALLS = ('GetModuleHandleA', 'GetModuleHandleW', 'GetModuleHandleExA',
                'GetModuleHandleExW', 'GetModuleHandle', 'GetModuleFileNameA',
                'GetModuleFileNameW', 'GetModuleFileName', 'LoadLibraryA',
                'LoadLibraryW', 'LoadLibraryExA', 'LoadLibraryExW',
                'LoadLibrary', 'GetProcAddress', 'FreeLibrary',
                'EnumProcessModules', 'GetModuleBaseNameA',
                'GetModuleBaseNameW', 'GetModuleFileNameExA',
                'GetModuleFileNameExW', 'SymInitialize', 'SymFromAddr',
                'SymGetModuleInfo')

# A crash handler that allocates can fail while reporting a failure, and one
# that reads the environment leaks the machine into a shared artifact.
FORBIDDEN_CALLS = ('malloc', 'calloc', 'realloc', 'strdup', 'getenv',
                   'GetEnvironmentVariable', 'fopen')

LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
CALL = re.compile(r'\b(sl_[a-z0-9_]+)\s*\(')
DEF = re.compile(r'^(?:static\s+)?[A-Za-z_][\w \t*]*?\b(sl_[a-z0-9_]+)\s*\(',
                 re.M)


def unescape(s):
    return (s.replace('\\n', '\n').replace('\\t', '\t')
             .replace('\\"', '"').replace('\\\\', '\\'))


def strip_comments(src):
    """Remove C comments, keeping string literals and line structure. The
    loader-call and bounds assertions below are about CODE; a comment that
    names GetModuleFileName in order to explain why it is not called must not
    read as a call."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            q = c
            out.append(c)
            i += 1
            while i < n:
                out.append(src[i])
                if src[i] == '\\' and i + 1 < n:
                    out.append(src[i + 1])
                    i += 2
                    continue
                if src[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append('\n' * src.count('\n', i, j))
            i = j
            continue
        if src.startswith('//', i):
            j = src.find('\n', i)
            i = n if j < 0 else j
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def body(src, name):
    """One function's body text, from its definition to the next brace in
    column 0. Every closing brace inside these functions is indented."""
    for m in DEF.finditer(src):
        if m.group(1) != name:
            continue
        brace = src.find('{', m.end())
        semi = src.find(';', m.end())
        if brace < 0 or (0 <= semi < brace):
            continue                        # a declaration, not a definition
        end = src.find('\n}\n', brace)
        return src[m.start():end if end > 0 else len(src)]
    return None


def statements(text):
    return text.split(';')


def call_literals(text, call, nth=0):
    """Concatenated string literals of the nth call to `call` in `text`."""
    at = -1
    for _ in range(nth + 1):
        at = text.find(call, at + 1)
        if at < 0:
            return None
    end = text.find(';', at)
    return ''.join(unescape(m.group(1)) for m in LIT.finditer(text[at:end]))


def all_literals(text):
    return [unescape(m.group(1)) for m in LIT.finditer(text)]


def say_literals(text):
    out = []
    for stmt in statements(text):
        k = stmt.find('sl_say(')
        if k < 0:
            continue
        parts = all_literals(stmt[k:])
        if parts:
            out.append(''.join(parts))
    return out


def reachable(src, root):
    """The handler's transitive call graph INSIDE this file, plus the callees
    defined elsewhere. Discovery is transitive on purpose: a new helper added
    to the fault path is covered without editing this list."""
    seen, external, queue = {}, set(), [root]
    while queue:
        name = queue.pop()
        if name in seen or name in external:
            continue
        b = body(src, name)
        if b is None:
            external.add(name)
            continue
        seen[name] = b
        for m in CALL.finditer(b):
            if m.group(1) != name:
                queue.append(m.group(1))
    return seen, external


def check_source(path, log):
    """Part A. Returns (ok, literals) - the literals feed part B."""
    ok = True
    src = strip_comments(path.read_text(encoding='utf-8', errors='replace'))
    veh = body(src, 'sl_win_veh')
    pce = body(src, 'sl_win_pc_evidence')
    pnt = body(src, 'sl_win_pe_nt')
    for name, text in (('sl_win_veh', veh), ('sl_win_pc_evidence', pce),
                       ('sl_win_pe_nt', pnt)):
        if text is None:
            log('FAIL  source      %s not found in %s' % (name, path))
            return False, {}

    # A1 - the handler emits the fault's own fields, then the pc evidence, then
    # the memory walkers.
    order = ['ACCESS VIOLATION addr ', '  access: READ', '  eip    ',
             '  esp    ', '  ebp    ', '  region at addr: base',
             'sl_win_pc_evidence(', 'sl_win_stack_scan(']
    pos, bad = [], []
    for needle in order:
        at = veh.find(needle)
        if at < 0:
            bad.append('not emitted: %r' % needle)
        pos.append(at)
    for a in range(1, len(pos)):
        if pos[a - 1] >= 0 and pos[a] >= 0 and pos[a] < pos[a - 1]:
            bad.append('out of order: %r before %r' % (order[a], order[a - 1]))
    if bad:
        ok = False
        log('FAIL  A1 order    %d problem(s)' % len(bad))
        for b in bad:
            log('        %s' % b)
    else:
        log('PASS  A1 order    %d emissions in the required order, pc evidence '
            'after every fault field' % len(order))

    # A2 - no silent failure. Every `return;` in the pc-evidence writer is
    # preceded by a line that names what did not resolve. The literal may be
    # written by sl_say or formatted with snprintf first, so any literal in the
    # window counts - but comments have been stripped, so a comment cannot.
    lines = pce.split('\n')
    rets = [n for n, l in enumerate(lines) if re.search(r'\breturn\s*;', l)]
    bad = []
    for n in rets:
        window = '\n'.join(lines[max(0, n - 10):n])
        if not any(('unresolved' in s) or ('VirtualQuery failed' in s)
                   for s in all_literals(window)):
            bad.append('the return at line %d of sl_win_pc_evidence has no '
                       'unresolved line before it' % (n + 1))
    if not rets:
        ok = False
        log('FAIL  A2 silence  sl_win_pc_evidence has no early return at all - '
            'the check cannot be what it claims')
    elif bad:
        ok = False
        log('FAIL  A2 silence  %d problem(s)' % len(bad))
        for b in bad:
            log('        %s' % b)
    else:
        log('PASS  A2 silence  %d early return(s), each preceded by an '
            'explicit unresolved line' % len(rets))

    # A3 - bounds on the writer itself: no allocation, no environment, and the
    # only non-literal sl_say argument is the formatted buffer.
    bad = [c for c in FORBIDDEN_CALLS if c in pce]
    for s in statements(pce):
        if 'sl_say(' not in s or LIT.search(s):
            continue
        arg = s[s.find('sl_say(') + 7:].strip().rstrip(')').strip()
        if arg != 'b':
            bad.append('prints a buffer this check does not know: %r' % arg)
    if bad:
        ok = False
        log('FAIL  A3 bounds   %d problem(s)' % len(bad))
        for b in bad:
            log('        %s' % b)
    else:
        log('PASS  A3 bounds   no %s; the only non-literal emission is the '
            'formatted buffer' % '/'.join(FORBIDDEN_CALLS[:4]))

    # A4 - the literals part B generates its fixtures from.
    lits = {}
    lits['region_fmt'] = call_literals(pce, 'snprintf(', 0)
    lits['offset_label'] = call_literals(pce, 'sl_say_hex(')
    said = say_literals(pce)
    lits['unresolved'] = [s for s in said if 'unresolved' in s]
    lits['vq_failed'] = [s for s in said if 'VirtualQuery failed' in s]
    lits['module_fmt'] = None
    lits['toolong_fmt'] = None
    # Per STATEMENT, not per literal: a format string the product splits across
    # adjacent literals is one format string, and taking only its first chunk
    # is exactly how a fixture starts testing a sentence nothing emits.
    for s in statements(pce):
        joined = ''.join(all_literals(s))
        if joined.startswith('  module at eip: %s'):
            lits['module_fmt'] = joined
        if 'longer than %d bytes' in joined:
            lits['toolong_fmt'] = joined
    m = re.search(r'static\s+char\s+name\s*\[\s*(\d+)\s*\]', pce)
    lits['bound'] = (int(m.group(1)) - 1) if m else 0
    bad = []
    if not lits['region_fmt'] or '  region at eip: ' not in lits['region_fmt']:
        bad.append('no region-at-eip format string')
    if not lits['offset_label'] or 'offset from alloc base' not in lits['offset_label']:
        bad.append('no eip-offset label')
    if len(lits['unresolved']) < 3:
        bad.append('fewer than three unresolved lines (%d)'
                   % len(lits['unresolved']))
    if not lits['vq_failed']:
        bad.append('no VirtualQuery-failed line for the pc region')
    if not lits['module_fmt']:
        bad.append('no "  module at eip: %s" format for a resolved name')
    if not lits['toolong_fmt']:
        bad.append('no refusal line for a name over the buffer bound')
    if not lits['bound']:
        bad.append('no "static char name[N]" declaration to read the bound from')
    if bad:
        ok = False
        log('FAIL  A4 literals %d problem(s)' % len(bad))
        for b in bad:
            log('        %s' % b)
    else:
        log('PASS  A4 literals region format, offset label, %d unresolved '
            'line(s), the resolved format and a %d-byte name bound extracted '
            'from the source' % (len(lits['unresolved']), lits['bound']))

    # A5 - NO LOADER CALL anywhere the handler can reach inside this file.
    graph, external = reachable(src, 'sl_win_veh')
    bad = []
    for fn in sorted(graph):
        for call in LOADER_CALLS:
            if re.search(r'\b%s\s*\(' % re.escape(call), graph[fn]):
                bad.append('%s calls the loader API %s' % (fn, call))
    if 'sl_win_pc_evidence' not in graph or 'sl_win_stack_scan' not in graph:
        bad.append('the discovered call graph does not reach the evidence '
                   'writers - discovery is broken, not the code')
    if bad:
        ok = False
        log('FAIL  A5 loader   %d problem(s)' % len(bad))
        for b in bad:
            log('        %s' % b)
    else:
        log('PASS  A5 loader   %d function(s) on the handler path in this '
            'file, none calling any of %d loader APIs; %d callee(s) defined '
            'elsewhere are listed, not inspected: %s'
            % (len(graph), len(LOADER_CALLS), len(external),
               ', '.join(sorted(external))))

    # A6 - the basename route is bounded, and refuses rather than truncates.
    bad = []
    if 'sl_win_pe_nt(' not in pce:
        bad.append('the name route does not go through the bounds-checked PE '
                   'header helper')
    if 'nt == NULL' not in pce:
        bad.append('the PE header helper result is not NULL-checked')
    probes = len(re.findall(r'\bsl_win_readable\s*\(', pce))
    if probes < 3:
        bad.append('only %d readability probe(s) in the name route; the export '
                   'directory, each name byte and the terminator probe each '
                   'need one' % probes)
    if 'sizeof name - 1' not in pce:
        bad.append('the name bound is not derived from the buffer')
    for needle in ('size - erva', 'size - nrva'):
        if needle not in pce:
            bad.append('missing wrap-proof bound %r (an additive bound can '
                       'overflow and pass)' % needle)
    loop = re.search(r'for\s*\(\s*o\s*=\s*0\s*;([^)]*)\)', pce)
    if not loop:
        bad.append('no bounded name loop of the expected shape')
    else:
        cond = loop.group(1)
        if 'o < max' not in cond or 'o < size - nrva' not in cond:
            bad.append('the name loop is not bounded by BOTH the buffer and '
                       'the image: %r' % cond.strip())
    if not re.search(r'if\s*\(\s*o\s*>=\s*max\s*\)', pce):
        bad.append('no buffer-bound branch, so an over-long name would be '
                   'truncated silently')
    if lits.get('toolong_fmt') and '%d' not in lits['toolong_fmt']:
        bad.append('the refusal line hard-codes its bound instead of printing '
                   'the buffer bound, so the two can drift')
    if "'?'" in pce:
        bad.append('a substitution character survives in the name route; a '
                   'name that is not the module\'s must be refused, not patched')
    if bad:
        ok = False
        log('FAIL  A6 route    %d problem(s)' % len(bad))
        for b in bad:
            log('        %s' % b)
    else:
        log('PASS  A6 route    PE-header helper NULL-checked, %d readability '
            'probes, loop bounded by buffer AND image with wrap-proof '
            'subtraction, over-long name refused with a printed bound'
            % probes)

    # A7 - the PE header helper probes before every read and verifies what it
    # read. It is the only code here that dereferences a foreign image.
    bad = []
    if len(re.findall(r'\bsl_win_readable\s*\(', pnt)) < 2:
        bad.append('fewer than two readability probes (DOS header, NT headers)')
    for needle in ('IMAGE_DOS_SIGNATURE', 'IMAGE_NT_SIGNATURE',
                   'IMAGE_NT_OPTIONAL_HDR32_MAGIC', 'e_lfanew'):
        if needle not in pnt:
            bad.append('does not verify %s' % needle)
    if not re.search(r'lfa\s*<\s*sizeof\s*\*dos\s*\|\|\s*lfa\s*>', pnt):
        bad.append('e_lfanew is not bounded on both sides')
    for call in LOADER_CALLS:
        if re.search(r'\b%s\s*\(' % re.escape(call), pnt):
            bad.append('calls the loader API %s' % call)
    if bad:
        ok = False
        log('FAIL  A7 pe       %d problem(s)' % len(bad))
        for b in bad:
            log('        %s' % b)
    else:
        log('PASS  A7 pe       DOS and NT headers probed before reading, both '
            'signatures and the 32-bit optional magic verified, e_lfanew '
            'bounded both ways, no loader call')
    return ok, lits


# ---------------------------------------------------------------- part B ---

def render(fmt, name=None, bound=None):
    """Fill one of the reporter's own format strings. The spelling comes from
    the product; only the values are this file's."""
    out, vals = [], ['00401000', '00400000', '00002000']
    i = 0
    while i < len(fmt):
        if fmt.startswith('%08lx', i):
            out.append(vals.pop(0) if vals else 'deadbeef')
            i += 5
        elif fmt.startswith('%s', i):
            out.append(name if name is not None else 'COMMIT')
            i += 2
        elif fmt.startswith('%lx', i):
            out.append('20')
            i += 3
        elif fmt.startswith('%d', i):
            out.append(str(bound if bound is not None else 0))
            i += 2
        else:
            out.append(fmt[i])
            i += 1
    return ''.join(out)


HEAD_LEGACY = """
sightline native: ACCESS VIOLATION addr 0x00000004
  access: READ
  eip    0x65139441
  esp    0x085fb1fc
  ebp    0x085fb20c
  region at addr: base 0x00000000 alloc 0x00000000 size 0x00010000 state FREE protect 0x1 type 0x0
  stack words inside the image (candidate return addresses):
    0x005fb500

=== sightline CRASH ===
  frame  0x00000001
  pc     0x65139441
  ra     0x6520261d
  resolve with: tools/native/crashinfo.sh
"""


def report(added_lines, head=HEAD_LEGACY):
    marker = '  stack words inside the image'
    at = head.find(marker)
    return head[:at] + ''.join(added_lines) + head[at:]


def validate(text, mode, bound=0, provenance=''):
    """The grammar. Returns a sorted list of reason codes; empty means it
    holds."""
    why = []
    pos = {}
    for needle in PRESERVED:
        n = text.count(needle)
        if n == 0:
            why.append('missing:%s' % needle.strip())
            continue
        if n > 1:
            why.append('duplicate:%s' % needle.strip())
        pos[needle] = text.find(needle)
    seen = [p for p in PRESERVED if p in pos]
    for a in range(1, len(seen)):
        if pos[seen[a]] < pos[seen[a - 1]]:
            why.append('out-of-order:%s' % seen[a].strip())

    present = [p for p in ADDED if p in text]
    if mode == 'legacy':
        for p in present:
            why.append('unexpected:%s' % p.strip())
        return sorted(set(why))

    for needle in ADDED:
        n = text.count(needle)
        if n == 0:
            why.append('missing:%s' % needle.strip())
        elif n > 1:
            why.append('duplicate:%s' % needle.strip())
    for needle in present:
        at = text.find(needle)
        if ADDED_AFTER in pos and at < pos[ADDED_AFTER]:
            why.append('out-of-order:%s' % needle.strip())
        if ADDED_BEFORE in pos and at > pos[ADDED_BEFORE]:
            why.append('out-of-order:%s' % needle.strip())
    for a in range(1, len(present)):
        if text.find(present[a]) < text.find(present[a - 1]):
            why.append('out-of-order:%s' % present[a].strip())

    # The module line is the only one carrying a name, so it is the only one
    # that could carry a path, a machine's directory or a truncated answer.
    at = text.find(ADDED[2])
    if at >= 0:
        end = text.find('\n', at)
        value = text[at + len(ADDED[2]):end if end > 0 else len(text)]
        if value.strip() == '':
            why.append('empty-value:module at eip')
        elif value.startswith('unresolved - '):
            if not value[len('unresolved - '):].strip():
                why.append('empty-reason:module at eip')
        else:
            if not value.endswith(provenance) or not provenance:
                why.append('no-provenance:module at eip')
                name = value
            else:
                name = value[:len(value) - len(provenance)]
            if not name:
                why.append('empty-value:module at eip')
            if any(ch in name for ch in ('\\', '/', ':')):
                why.append('path-separator:module at eip')
            if any(not (0x20 <= ord(ch) < 0x7f) for ch in name):
                why.append('non-ascii:module at eip')
            if bound and len(name) > bound:
                why.append('too-long:module at eip')
    return sorted(set(why))


def check_text(lits, log):
    """Part B. Fixtures generated from the part A literals and bound."""
    bound = lits['bound']
    region = render(lits['region_fmt'])
    offset = lits['offset_label'] + '0x00039441\n'
    unres = {}
    for s in lits['unresolved']:
        for key, pre in (('region', '  region at eip:'),
                         ('offset', '  eip offset from alloc base'),
                         ('module', '  module at eip:')):
            if s.startswith(pre):
                unres.setdefault(key, s)
    vq = lits['vq_failed'][0]
    prov = lits['module_fmt'].split('%s', 1)[1].rstrip('\n')

    def mod(name):
        return render(lits['module_fmt'], name=name)

    resolved = mod('nvoglv32.dll')
    at_bound = mod('m' * bound)
    over_bound = mod('m' * (bound + 1))
    toolong = render(lits['toolong_fmt'], bound=bound)

    cases = []
    # Positives: every shape the handler can actually produce.
    cases.append(('resolved', 'current', report([region, offset, resolved]), []))
    cases.append(('name-at-bound', 'current',
                  report([region, offset, at_bound]), []))
    cases.append(('name-over-bound-refused', 'current',
                  report([region, offset, toolong]), []))
    cases.append(('module-unresolved', 'current',
                  report([region, offset, unres['module']]), []))
    cases.append(('region-unresolved', 'current',
                  report([vq, unres['offset'], unres['module']]), []))
    cases.append(('legacy-head', 'legacy', HEAD_LEGACY, []))

    # Negatives, each asserting its OWN reason.
    cases.append(('neg-name-over-bound', 'current',
                  report([region, offset, over_bound]),
                  ['too-long:module at eip']))
    cases.append(('neg-no-provenance', 'current',
                  report([region, offset, '  module at eip: nvoglv32.dll\n']),
                  ['no-provenance:module at eip']))
    cases.append(('neg-no-region-at-addr', 'current',
                  report([region, offset, resolved]).replace(
                      '  region at addr: ', '  region at ADDR: '),
                  ['missing:region at addr:']))
    cases.append(('neg-module-before-eip', 'current',
                  report([region, offset, resolved]).replace(
                      '  eip    0x65139441\n', '')
                  .replace('  access: READ\n',
                           '  access: READ\n' + resolved
                           + '  eip    0x65139441\n'),
                  ['duplicate:module at eip:', 'out-of-order:module at eip:']))
    cases.append(('neg-full-path', 'current',
                  report([region, offset, mod('X:\\build\\win32\\sightline.exe')]),
                  ['path-separator:module at eip']))
    cases.append(('neg-empty-name', 'current',
                  report([region, offset, '  module at eip: \n']),
                  ['empty-value:module at eip']))
    cases.append(('neg-empty-reason', 'current',
                  report([region, offset, '  module at eip: unresolved - \n']),
                  ['empty-reason:module at eip']))
    cases.append(('neg-non-ascii', 'current',
                  report([region, offset, mod('nvogl\x7fv32.dll')]),
                  ['non-ascii:module at eip']))
    cases.append(('neg-region-dropped', 'current', report([offset, resolved]),
                  ['missing:region at eip:']))
    cases.append(('neg-duplicate-eip', 'current',
                  report([region, offset, resolved]).replace(
                      '  eip    0x65139441\n',
                      '  eip    0x65139441\n  eip    0x65139441\n'),
                  ['duplicate:eip    0x']))
    cases.append(('neg-legacy-carries-new', 'legacy',
                  report([region, offset, resolved]),
                  ['unexpected:eip offset from alloc base',
                   'unexpected:module at eip:',
                   'unexpected:region at eip:']))

    passed = failed = 0
    for name, mode, text, expect in cases:
        why = validate(text, mode, bound, prov)
        if why == sorted(set(expect)):
            if expect:
                log('PASS  %-24s rejected for its own reason(s): %s'
                    % (name, ', '.join(why)))
            else:
                log('PASS  %-24s accepted in %s mode' % (name, mode))
            passed += 1
        else:
            log('FAIL  %-24s expected %s, got %s'
                % (name, sorted(set(expect)), why))
            failed += 1
    return passed, failed


# ---------------------------------------------------------------- part C ---

def check_reports(paths, log):
    passed = failed = 0
    for p in paths:
        p = Path(p)
        if not p.exists():
            log('FAIL  report      no such file: %s' % p)
            failed += 1
            continue
        text = p.read_text(encoding='utf-8', errors='replace')
        if 'ACCESS VIOLATION' not in text:
            log('SKIP  report      %s holds no access-violation report' % p.name)
            continue
        why = validate(text, 'legacy')
        if why:
            log('FAIL  report      %s: %s' % (p.name, ', '.join(why)))
            failed += 1
        else:
            log('PASS  report      %s satisfies the preserved grammar and '
                'carries no pc-evidence line (pre-change capture)' % p.name)
            passed += 1
    return passed, failed


def main(argv):
    source = Path(__file__).resolve().parents[2] / 'src/platform/sl_main.c'
    reports = []
    i = 1
    while i < len(argv):
        if argv[i] == '--source' and i + 1 < len(argv):
            source = Path(argv[i + 1])
            i += 2
        elif argv[i] == '--report' and i + 1 < len(argv):
            reports.append(argv[i + 1])
            i += 2
        else:
            print('crashreport-grammar.py: unknown argument %r' % argv[i])
            return 2
    out = []

    def log(s):
        out.append(s)
        print(s)

    log('source: %s' % source)
    if not source.exists():
        log('FAIL  source      no such file')
        return 1
    ok, lits = check_source(source, log)
    sp = sum(1 for l in out if l.startswith('PASS'))
    sf = sum(1 for l in out if l.startswith('FAIL'))
    if not ok:
        log('\n%d passed, %d failed (source contract broken - text grammar not '
            'generated)' % (sp, sf))
        return 1
    bp, bf = check_text(lits, log)
    cp, cf = check_reports(reports, log)
    log('\n%d passed, %d failed  (source %d/%d, text %d/%d, reports %d/%d)'
        % (sp + bp + cp, sf + bf + cf, sp, sp + sf, bp, bp + bf, cp, cp + cf))
    return 1 if (sf + bf + cf) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
