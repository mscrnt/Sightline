"""A stage's POSSIBLE CHARACTERS, read from the game's own tables, and the
preparation of the remaster items that stand in for them (#70).

    stagechars.py inventory dam [--json]
    stagechars.py prepare --level dam   --index <paths> --archive <source>
                          --sevenzip <7z> --out <root> --stamps <dir>
                          --stamp <stamp> [--force] [--keep-scratch]
    stagechars.py prepare --items char/greatguard2,head/headlee ...

tools/windows/prepare-xbla-assets.ps1 is the command a developer runs; this is
the derivation and the orchestration behind its -Characters switch.

WHICH CHARACTERS A STAGE CAN DRAW. Every non-player character the game puts on
a stage comes through one of three doors, and each names its body and head in
data the decomp carries:

  * a GUARD RECORD, propdef type 9 in the stage's setup: body at +0x8 (0xFFFF
    is "the random body"), head at +0x16 (negative is "a random head"). The
    layout is 'object block types.txt' `09 guards` and bondtypes.h
    GuardRecord; chraction.c expand_09_characters is what reads it. Guards
    are expanded whatever the difficulty (prop.c), so difficulty never
    narrows the set;
  * an AI SPAWN command in the stage's own AI lists or the global ones: any
    command macro whose first two arguments are (body_num, head_num)
    (bondaicommands.h chr_try_spawning_at_pad / _next_to_unseen_chr, and
    their aicommands2.h spellings). The head byte is read signed
    (aicommands.def passes `(s8) val[1]`), so 0xff is a random head;
  * a CLONE of a guard whose setup flags carry GUARD_SETUP_FLAG_CHR_CLONE:
    the same body, head -1 (chrai.c chrSpawnAtChr(..., chr->bodynum, -1,
    ...)). A clone never brings a body of its own.

A RANDOM HEAD comes from a GLOBAL pool, not a per-stage one (chraction.c
bodyChooseHead): random_male_heads[(current + (rand & 3)) % count] for a male
body, random_female_heads[current_female] for a female one, with `current`
drawn once per stage load (initguards.c bodiesReset). Any entry of the pool
can therefore come up on any stage, and a stage that can choose a random head
for a body can choose EVERY head of that body's pool. A body whose model
record says hasHead carries its own head and wears none (expand_09_characters,
retrieve_header_for_body_and_head). A RANDOM BODY is list_of_bodies[current]
(get_current_random_body): any entry, each then taking its own head rule.

WHERE THE NAMES COME FROM. Model files are c_item_entries in index order
(assets/obseg/chr/chrModelFileRecords.inc.c), enum values are
src/bondconstants.h's, and the provider key is the model's own name exactly as
the runtime derives it (src/native/sl_chr_asset.c item_of): "C" + item + "Z",
the item lowered, a body under char/ and a worn head under head/. Nothing
here is a table of any stage's characters: a stage's set is whatever these
files say it is.

The player is not a stage character and is not listed.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

#: The preprocessor symbols the native build defines among those these tables
#: test (__sgi is the IDO/N64 compiler; ALL_BONDS is never defined).
NATIVE_DEFINES = frozenset()

sys.path.insert(0, HERE)
from prepare import REMASTERED                               # noqa: E402

#: Propdef record lengths in WORDS by type - 'Action Block Types.txt', the
#: authority on record length (sizepropdef reproduces it for every type).
#: 0x30 ends the list.
PROPDEF_WORDS = {
    0x01: 0x40, 0x02: 0x02, 0x03: 0x20, 0x04: 0x21, 0x05: 0x20, 0x06: 0x3b,
    0x07: 0x21, 0x08: 0x22, 0x09: 0x07, 0x0a: 0x40, 0x0b: 0x95, 0x0c: 0x20,
    0x0d: 0x36, 0x0e: 0x03, 0x0f: 0x01, 0x10: 0x01, 0x11: 0x20, 0x12: 0x03,
    0x13: 0x04, 0x14: 0x2d, 0x15: 0x22, 0x16: 0x04, 0x17: 0x04, 0x18: 0x01,
    0x19: 0x02, 0x1a: 0x02, 0x1b: 0x02, 0x1c: 0x02, 0x1d: 0x02, 0x1e: 0x04,
    0x1f: 0x01, 0x20: 0x04, 0x21: 0x05, 0x22: 0x01, 0x23: 0x04, 0x24: 0x20,
    0x25: 0x0a, 0x26: 0x04, 0x27: 0x2c, 0x28: 0x2d, 0x29: 0x01, 0x2a: 0x20,
    0x2b: 0x20, 0x2c: 0x05, 0x2d: 0x38, 0x2e: 0x07, 0x2f: 0x25,
}
PROPDEF_GUARD = 0x09
PROPDEF_END = 0x30

ITEM_MAX = 31          # SL_CHR_ITEM_MAX 32 with its NUL


class InventoryError(Exception):
    """The game's tables do not say what this module needs them to say."""


# ---- reading the decomp's C ------------------------------------------------

def _read(root, rel):
    p = os.path.join(root, rel.replace('/', os.sep))
    if not os.path.isfile(p):
        raise InventoryError('%s is missing' % rel)
    with open(p, 'r', encoding='latin-1') as f:
        return f.read()


def _strip_comments(text):
    """Comments out, line structure kept (a directive stays on its line)."""
    def keep_lines(m):
        return '\n' * m.group(0).count('\n') or ' '
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"',
                  lambda m: m.group(0) if m.group(0).startswith('"') else keep_lines(m),
                  text, flags=re.S)


def _if_value(expr, defined):
    e = expr.strip()
    e = re.sub(r'defined\s*\(\s*(\w+)\s*\)|defined\s+(\w+)',
               lambda m: '1' if (m.group(1) or m.group(2)) in defined else '0', e)
    if not re.fullmatch(r'[01\s()!&|]*', e) or not e.strip():
        raise InventoryError('a conditional this reader does not evaluate: #if %s' % expr)
    e = e.replace('&&', ' and ').replace('||', ' or ')
    e = re.sub(r'!\s*', ' not ', e)
    return bool(_BoolExpr(e).value())


class _BoolExpr:
    """0/1 with and/or/not/parentheses - what #if over defined() reduces to."""

    def __init__(self, text):
        self.t = re.findall(r'and|or|not|[01()]', text)
        self.i = 0

    def value(self):
        v = self._or()
        if self.i != len(self.t):
            raise InventoryError('a conditional this reader does not evaluate')
        return v

    def _peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def _or(self):
        v = self._and()
        while self._peek() == 'or':
            self.i += 1
            r = self._and()
            v = v or r
        return v

    def _and(self):
        v = self._not()
        while self._peek() == 'and':
            self.i += 1
            r = self._not()
            v = v and r
        return v

    def _not(self):
        if self._peek() == 'not':
            self.i += 1
            return not self._not()
        tok = self._peek()
        self.i += 1
        if tok == '(':
            v = self._or()
            if self._peek() != ')':
                raise InventoryError('a conditional this reader does not evaluate')
            self.i += 1
            return v
        if tok in ('0', '1'):
            return tok == '1'
        raise InventoryError('a conditional this reader does not evaluate')


def _preprocess(text, defined=NATIVE_DEFINES):
    """Conditional compilation only - #ifdef/#ifndef/#if/#elif/#else/#endif
    over defined() - for a region whose conditionals balance. Every other
    line is kept as written."""
    out, stack, active = [], [], True
    for line in text.split('\n'):
        m = re.match(r'\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)', line)
        if not m:
            out.append(line if active else '')
            continue
        d, arg = m.group(1), m.group(2).strip()
        if d in ('ifdef', 'ifndef', 'if'):
            if d == 'if':
                cond = _if_value(arg, defined)
            else:
                cond = (arg.split()[0] in defined) == (d == 'ifdef')
            stack.append([active, cond, cond])
            active = active and cond
        elif d == 'elif':
            if not stack:
                raise InventoryError('#elif without #if')
            parent, _, taken = stack[-1]
            cond = (not taken) and _if_value(arg, defined)
            stack[-1] = [parent, cond, taken or cond]
            active = parent and cond
        elif d == 'else':
            if not stack:
                raise InventoryError('#else without #if')
            parent, _, taken = stack[-1]
            stack[-1] = [parent, not taken, True]
            active = parent and not taken
        else:
            if not stack:
                raise InventoryError('#endif without #if')
            active = stack.pop()[0]
        out.append('')
    if stack:
        raise InventoryError('an unterminated conditional')
    return '\n'.join(out)


def _split_top(text):
    """Split on commas outside parentheses and braces."""
    parts, depth, cur = [], 0, []
    for ch in text:
        if ch in '({[':
            depth += 1
        elif ch in ')}]':
            depth -= 1
        if ch == ',' and depth == 0:
            parts.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append(''.join(cur))
    return [p.strip() for p in parts]


_TOKEN = re.compile(r'\s*(0[xX][0-9a-fA-F]+[uUlL]*|(?:\d+\.\d*|\.\d+)(?:[eE][-+]?\d+)?[fF]?'
                    r'|\d+[uUlL]*|[A-Za-z_]\w*|<<|>>|[-+*/%&|^~()!,])')


class _Eval:
    """Integer C constant expressions: literals, names from `scope`, the
    function-like macros in `macros`, unary - + ~ !, * / % + - << >> & ^ |.
    A float literal is what C makes of it in an integer initializer - the
    value truncated. Anything else - a cast, a name nobody defined - is
    refused."""

    def __init__(self, text, scope, macros):
        self.toks, pos = [], 0
        text = text.strip()
        while pos < len(text):
            m = _TOKEN.match(text, pos)
            if not m or m.end() == pos:
                raise InventoryError('cannot read the expression %r' % text)
            self.toks.append(m.group(1))
            pos = m.end()
        self.i, self.scope, self.macros, self.text = 0, scope, macros, text

    def value(self):
        v = self._bin(0)
        if self.i != len(self.toks):
            raise InventoryError('cannot read the expression %r' % self.text)
        return v

    _LEVELS = [('|',), ('^',), ('&',), ('<<', '>>'), ('+', '-'), ('*', '/', '%')]

    def _peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else None

    def _bin(self, level):
        if level == len(self._LEVELS):
            return self._unary()
        v = self._bin(level + 1)
        while self._peek() in self._LEVELS[level]:
            op = self.toks[self.i]
            self.i += 1
            r = self._bin(level + 1)
            if op == '|':
                v |= r
            elif op == '^':
                v ^= r
            elif op == '&':
                v &= r
            elif op == '<<':
                v <<= r
            elif op == '>>':
                v >>= r
            elif op == '+':
                v += r
            elif op == '-':
                v -= r
            elif op == '*':
                v *= r
            elif r == 0:
                raise InventoryError('division by zero in %r' % self.text)
            elif op == '/':
                v = int(v / r)
            else:
                v = v - int(v / r) * r
        return v

    def _unary(self):
        tok = self._peek()
        if tok in ('-', '+', '~', '!'):
            self.i += 1
            v = self._unary()
            return {'-': -v, '+': v, '~': ~v, '!': int(not v)}[tok]
        return self._primary()

    def _primary(self):
        tok = self._peek()
        if tok is None:
            raise InventoryError('cannot read the expression %r' % self.text)
        self.i += 1
        if tok == '(':
            v = self._bin(0)
            if self._peek() != ')':
                raise InventoryError('cannot read the expression %r' % self.text)
            self.i += 1
            return v
        if tok[0].isdigit() or tok[0] == '.':
            if re.fullmatch(r'(?:\d+\.\d*|\.\d+)(?:[eE][-+]?\d+)?[fF]?', tok):
                return int(float(tok.rstrip('fF')))
            return int(tok.rstrip('uUlL'), 0)
        if tok[0].isalpha() or tok[0] == '_':
            if self._peek() == '(' and tok in self.macros:
                self.i += 1
                args, depth, start = [], 0, self.i
                while True:
                    t = self._peek()
                    if t is None:
                        raise InventoryError('cannot read the expression %r' % self.text)
                    if t == '(':
                        depth += 1
                    elif t == ')' and depth == 0:
                        break
                    elif t == ')':
                        depth -= 1
                    elif t == ',' and depth == 0:
                        args.append(' '.join(self.toks[start:self.i]))
                        start = self.i + 1
                    self.i += 1
                args.append(' '.join(self.toks[start:self.i]))
                self.i += 1
                params, body = self.macros[tok]
                if len(args) != len(params):
                    raise InventoryError('%s takes %d arguments' % (tok, len(params)))
                vals = dict(self.scope)
                for p, a in zip(params, args):
                    vals[p] = _Eval(a, self.scope, self.macros).value()
                return _Eval(body, vals, self.macros).value()
            if tok in self.scope:
                return self.scope[tok]
        raise InventoryError('%r is not a constant this reader knows (in %r)'
                             % (tok, self.text))


def _evaluate(text, scope, macros):
    return _Eval(text, scope, macros).value()


def _macros(text, names=None):
    """Function-like #defines: {name: ([params], body)}."""
    out = {}
    for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+(\w+)\(([^)]*)\)((?:[^\n\\]|\\\n)*)',
                         text, re.M):
        if names is None or m.group(1) in names:
            params = [p.strip() for p in m.group(2).split(',') if p.strip()]
            out[m.group(1)] = (params, m.group(3).replace('\\\n', ' ').strip())
    return out


def _array_body(text, name, what):
    m = re.search(r'\b%s\s*\[\s*\]\s*=\s*\{' % re.escape(name), text)
    if not m:
        raise InventoryError('%s has no %s[]' % (what, name))
    depth, i = 1, m.end()
    while depth and i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
        i += 1
    if depth:
        raise InventoryError('%s[] in %s does not close' % (name, what))
    return text[m.end():i - 1]


# ---- the game's tables -----------------------------------------------------

def enum_values(root, names, defined=NATIVE_DEFINES):
    """{enumerator: value} for the named enums of src/bondconstants.h, in
    order, each able to refer to the ones before it."""
    text = _strip_comments(_read(root, 'src/bondconstants.h'))
    scope = {}
    for name in names:
        m = re.search(r'typedef\s+enum\s+%s\s*\{(.*?)\}\s*%s\s*;' % (name, name), text, re.S)
        if not m:
            raise InventoryError('src/bondconstants.h has no enum %s' % name)
        nxt = 0
        for entry in _split_top(_preprocess(m.group(1), defined)):
            if not entry:
                continue
            if '=' in entry:
                ident, expr = entry.split('=', 1)
                ident, v = ident.strip(), _evaluate(expr, scope, {})
            else:
                ident, v = entry, nxt
            if not re.fullmatch(r'[A-Za-z_]\w*', ident):
                raise InventoryError('enum %s: cannot read %r' % (name, entry))
            scope[ident] = v
            nxt = v + 1
    return scope


def word_macros(root):
    """The setup files' word-building macros, from where the decomp defines
    them."""
    got = _macros(_read(root, 'src/bondconstants.h'), ('_mkword', '_mkshort'))
    if len(got) != 2:
        raise InventoryError('src/bondconstants.h does not define _mkword and _mkshort')
    return got


def model_records(root):
    """c_item_entries in index order: [dict(index, file, male, has_head)].
    The list ends where the game's own loops stop - at the record whose
    header is 0."""
    rel = 'assets/obseg/chr/chrModelFileRecords.inc.c'
    body = _array_body(_preprocess(_strip_comments(_read(root, rel))), 'c_item_entries', rel)
    recs = []
    for line in body.split('\n'):
        line = line.strip()
        if not line:
            continue
        m = re.fullmatch(r'#\s*include\s*[<"]([^>"]+)[>"]', line)
        if m:
            text = _strip_comments(_read(root, m.group(1)))
        else:
            text = line
        for r in re.finditer(r'\{([^{}]*)\}', text):
            f = _split_top(r.group(1))
            if len(f) < 6:
                raise InventoryError('%s: a model record of %d fields' % (rel, len(f)))
            if f[0] in ('0', 'NULL'):
                return recs
            fm = re.fullmatch(r'"([^"]*)"', f[1])
            if not fm:
                raise InventoryError('%s: record %d names no file' % (rel, len(recs)))
            recs.append(dict(index=len(recs), file=fm.group(1),
                             male=bool(int(f[4], 0)), has_head=bool(int(f[5], 0))))
    raise InventoryError('%s: c_item_entries has no terminating record' % rel)


def random_pools(root, scope):
    """The three random tables of src/game/chr.c, each cut at its first
    negative entry as the game counts them (initactorpropstuff.c)."""
    text = _preprocess(_strip_comments(_read(root, 'src/game/chr.c')))
    pools = {}
    for name in ('list_of_bodies', 'random_male_heads', 'random_female_heads'):
        out = []
        for e in _split_top(_array_body(text, name, 'src/game/chr.c')):
            if not e:
                continue
            v = _evaluate(e, scope, {})
            if v < 0:
                break
            out.append(v)
        if not out:
            raise InventoryError('src/game/chr.c: %s is empty' % name)
        pools[name] = out
    return pools


def level_names(root):
    """{name: LEVELID} - the trace harness's table, which the launcher and
    tools/native/levelstage.py already resolve names through."""
    text = _read(root, 'tools/trace/sltrace/levelboot.py')
    m = re.search(r'LEVEL_IDS\s*=\s*\{(.*?)\}', text, re.S)
    if not m:
        raise InventoryError('tools/trace/sltrace/levelboot.py has no LEVEL_IDS')
    return {k: int(v) for k, v in re.findall(r'"([a-z0-9_]+)"\s*:\s*(\d+)', m.group(1))}


def setup_of(root, level, country='u'):
    """(LEVELID, setup name, setup source path relative to root). The setup
    name is the game's own (chraidata.c setup_text_pointers, indexed by
    LEVELID); the source is the country's own file when there is one, the
    shared file otherwise - the order assets/obseg/Makefile.setup builds
    them in."""
    names = level_names(root)
    if level not in names:
        raise InventoryError('unknown level %r. Known: %s'
                             % (level, ', '.join(sorted(names))))
    lid = names[level]
    text = _strip_comments(_read(root, 'src/game/chraidata.c'))
    ptrs = [e for e in _split_top(_array_body(text, 'setup_text_pointers',
                                              'src/game/chraidata.c')) if e]
    if lid >= len(ptrs) or not re.fullmatch(r'"\w+"', ptrs[lid]):
        raise InventoryError('level %r (LEVELID %d) has no setup' % (level, lid))
    name = ptrs[lid].strip('"')
    for rel in ('assets/obseg/setup/%s/%s.c' % (country, name),
                'assets/obseg/setup/%s.c' % name):
        if os.path.isfile(os.path.join(root, rel.replace('/', os.sep))):
            return lid, name, rel
    raise InventoryError('level %r: no source for setup %s' % (level, name))


def propdef_records(text, setup_name, macros, rel):
    """The setup's propdef list as [(type, [words])], walked record by record
    to its end marker - a list that does not walk exactly is refused."""
    m = re.search(r'\bstagesetup\s+%s\s*=\s*\{([^}]*)\}' % re.escape(setup_name), text)
    if not m:
        raise InventoryError('%s: no stagesetup %s' % (rel, setup_name))
    members = [e.strip().lstrip('&').strip() for e in _split_top(m.group(1))]
    if len(members) < 4 or not re.fullmatch(r'\w+', members[3]):
        raise InventoryError('%s: stagesetup %s names no propdef list' % (rel, setup_name))
    words = [_evaluate(e, {}, macros) & 0xffffffff
             for e in _split_top(_array_body(text, members[3], rel)) if e]
    recs, at = [], 0
    while True:
        if at >= len(words):
            raise InventoryError('%s: the propdef list ends without its end marker' % rel)
        kind = words[at] & 0xff
        if kind == PROPDEF_END:
            if at + 1 != len(words):
                raise InventoryError('%s: words after the propdef end marker' % rel)
            return recs
        n = PROPDEF_WORDS.get(kind)
        if n is None:
            raise InventoryError('%s: propdef type %#x at word %d has no known length'
                                 % (rel, kind, at))
        if at + n > len(words):
            raise InventoryError('%s: a type %#x record runs past the list' % (rel, kind))
        recs.append((kind, words[at:at + n]))
        at += n


def spawn_macros(root):
    """AI command macros that create a character: their first two arguments
    are the body and the head, in either spelling of the command set."""
    out = {}
    for rel in ('src/bondaicommands.h', 'src/aicommands2.h'):
        for name, (params, _body) in _macros(_read(root, rel)).items():
            if [p.lower() for p in params[:2]] == ['body_num', 'head_num']:
                out[name] = params
    if not out:
        raise InventoryError('no AI command creates a character - the command '
                             'headers are not the ones this reader expects')
    return out


def ai_spawns(text, macros_spawn, scope, rel):
    """[(macro, body, head)] for every spawn command written in `text`."""
    got = []
    for m in re.finditer(r'\b(%s)\s*\(' % '|'.join(sorted(map(re.escape, macros_spawn))), text):
        depth, i = 1, m.end()
        while depth and i < len(text):
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
            i += 1
        args = _split_top(text[m.end():i - 1])
        if len(args) != len(macros_spawn[m.group(1)]):
            raise InventoryError('%s: %s with %d arguments' % (rel, m.group(1), len(args)))
        body = _evaluate(args[0], scope, {}) & 0xff
        head = _evaluate(args[1], scope, {}) & 0xff
        got.append((m.group(1), body, head - 256 if head >= 0x80 else head))
    return got


def item_of(filename):
    """The runtime's rule (src/native/sl_chr_asset.c item_of): "C" + item +
    "Z", the item lowered, letters, digits and '_' only. None otherwise."""
    if not filename or filename[0] != 'C' or filename[-1] != 'Z' or len(filename) < 3:
        return None
    item = filename[1:-1].lower()
    if len(item) > ITEM_MAX or not re.fullmatch(r'[a-z0-9_]+', item):
        return None
    return item


def provider_key(cls, filename):
    item = item_of(filename)
    return None if item is None else '%s/%s' % (cls, item)


# ---- the inventory ---------------------------------------------------------

def inventory(level, root=ROOT, country='u'):
    """Every body and every separately worn head the stage can draw, each
    with the model file, the provider key and why it is there. Ordered by
    model index."""
    lid, setup, rel = setup_of(root, level, country)
    scope = enum_values(root, ('BODIES', 'HEADS', 'GUARD_SETUP_FLAG'))
    clone_flag = scope.get('GUARD_SETUP_FLAG_CHR_CLONE')
    if clone_flag is None:
        raise InventoryError('src/bondconstants.h has no GUARD_SETUP_FLAG_CHR_CLONE')
    models = model_records(root)
    pools = random_pools(root, scope)
    words = word_macros(root)
    spawners = spawn_macros(root)
    text = _preprocess(_strip_comments(_read(root, rel)))

    # {model index: {reason: count}} - a reason is one door into the stage;
    # a random head's reason is its pool, with the doors that draw from it.
    bodies, heads, via = {}, {}, {}

    def model(ix, what):
        if not (0 <= ix < len(models)):
            raise InventoryError('%s names model %d; c_item_entries has %d'
                                 % (what, ix, len(models)))
        return models[ix]

    def note(table, ix, why):
        reasons = table.setdefault(ix, {})
        reasons[why] = reasons.get(why, 0) + 1

    def place(body, head, why):
        """One way a character is made: a body (or the random body) and a
        head (or a random one)."""
        if body == 0xffff:
            cands, why_b = pools['list_of_bodies'], '%s (list_of_bodies)' % why
        else:
            cands, why_b = [body], why
        for b in cands:
            rec = model(b, why)
            note(bodies, b, why_b)
            if rec['has_head']:
                continue
            if head >= 0:
                model(head, why)
                note(heads, head, '%s, explicit head' % why)
                continue
            pool = 'random_male_heads' if rec['male'] else 'random_female_heads'
            for h in pools[pool]:
                model(h, pool)
                note(heads, h, pool)
                doors = via.setdefault((h, pool), [])
                if why not in doors:
                    doors.append(why)

    n_guards = 0
    for kind, w in propdef_records(text, setup, words, rel):
        if kind != PROPDEF_GUARD:
            continue
        n_guards += 1
        chrnum = w[1] >> 16
        body = w[2] >> 16
        flags, head = w[5] >> 16, w[5] & 0xffff
        head = head - 0x10000 if head >= 0x8000 else head
        place(body, head, 'setup guard')
        if flags & clone_flag:
            rec = model(body, 'setup guard %d' % chrnum) if body != 0xffff else None
            if rec is None or not rec['has_head']:
                place(body, -1, 'clone')

    for macro, body, head in ai_spawns(text, spawners, scope, rel):
        place(body, head, 'AI %s' % macro)
    glb = 'src/game/chraidata.c'
    for macro, body, head in ai_spawns(_preprocess(_strip_comments(_read(root, glb))),
                                       spawners, scope, glb):
        place(body, head, 'global AI %s' % macro)

    def rows(table, cls):
        out = []
        for ix in sorted(table):
            f = models[ix]['file']
            key = provider_key(cls, f)
            if key is None:
                raise InventoryError('model %d file %r has no provider key' % (ix, f))
            reasons = []
            for why, n in table[ix].items():
                if (ix, why) in via:
                    reasons.append('%s via %s' % (why, ', '.join(via[(ix, why)])))
                else:
                    reasons.append(why if n == 1 else '%s x%d' % (why, n))
            out.append(dict(index=ix, file=f, key=key, reasons=reasons))
        return out

    return dict(level=level, levelid=lid, setup=setup, setup_source=rel,
                guards=n_guards, bodies=rows(bodies, 'char'), heads=rows(heads, 'head'))


# ---- preparing the inventory's items ---------------------------------------

BUILD, CURRENT, ABSENT = 'BUILD', 'CURRENT', 'SOURCE ABSENT'
PREPARED, REFUSED = 'PREPARED', 'CONVERSION REFUSED'


def remaster_files(paths, cls, item):
    """The source files of the REMASTERED item `cls/item` in an archive
    index - whatever prefix the user's copy puts above the generation."""
    want = '%s/%s/%s' % (REMASTERED, cls, item)
    got = []
    for p in paths:
        q = p.replace('\\', '/')
        d, _, leaf = q.rpartition('/')
        if (d == want or d.endswith('/' + want)) and leaf.lower().endswith(('.bin', '.rba')):
            got.append(p)
    return sorted(set(got))


def plan(keys, paths, out, stamps, stamp_now, force=False):
    """One row per key: BUILD (with its source files), CURRENT (the bundle
    was built from this very source by these very tools) or SOURCE ABSENT."""
    rows = []
    for key in keys:
        cls, item = key.split('/')
        files = remaster_files(paths, cls, item)
        bundle = os.path.join(out, 'xbla', cls, item + '.slwa')
        stamp = os.path.join(stamps, '%s-%s.stamp' % (cls, item))
        if not files:
            state = ABSENT
        elif (not force and os.path.isfile(bundle) and os.path.isfile(stamp)
              and open(stamp).read().strip() == stamp_now):
            state = CURRENT
        else:
            state = BUILD
        rows.append(dict(key=key, cls=cls, item=item, state=state, files=files,
                         bundle=bundle, stamp=stamp, reason=''))
    return rows


def sevenzip_extract(sevenzip, archive, dest, listfile):
    return subprocess.call([sevenzip, 'x', archive, '-o' + dest, '@' + listfile,
                            '-y', '-bso0', '-bsp0'])


def execute(rows, stamp_now, scratch, extract, convert, keep_scratch=False):
    """Extract every BUILD row's files in ONE pass (the source may be one
    solid block), convert each, stamp what converted. A row that is SOURCE
    ABSENT or REFUSED leaves no bundle behind: an older derivation must not
    answer for a source that no longer yields it."""
    build = [r for r in rows if r['state'] == BUILD]
    if build:
        os.makedirs(scratch, exist_ok=True)
        listfile = os.path.join(scratch, 'include.txt')
        with open(listfile, 'w', encoding='ascii', newline='\n') as f:
            for r in build:
                for p in r['files']:
                    f.write(p + '\n')
        rc = extract(scratch, listfile)
        for r in build:
            if rc != 0:
                r['state'], r['reason'] = REFUSED, 'the source could not be extracted (7-Zip exit %d)' % rc
                continue
            try:
                r['reason'] = convert(r['cls'], r['item'], scratch) or ''
                r['state'] = PREPARED
                with open(r['stamp'], 'w', encoding='ascii', newline='\n') as f:
                    f.write(stamp_now + '\n')
            except Exception as exc:                            # noqa: BLE001
                r['state'], r['reason'] = REFUSED, str(exc) or exc.__class__.__name__
        if not keep_scratch:
            import shutil
            shutil.rmtree(scratch, ignore_errors=True)
    for r in rows:
        if r['state'] in (ABSENT, REFUSED):
            removed = []
            for p in (r['bundle'], r['stamp']):
                if os.path.isfile(p):
                    os.remove(p)
                    removed.append(os.path.basename(p))
            if removed:
                r['reason'] = (r['reason'] + '; ' if r['reason'] else '') + \
                    'removed the stale %s' % ' and '.join(removed)
    return rows


def _convert_with_prepare(out):
    def convert(cls, item, tree):
        import prepare                                        # noqa: E402
        return prepare.convert_character(cls, item, tree, out)
    return convert


def _report(rows, inv, stream=None):
    stream = stream or sys.stdout
    why = {}
    if inv is not None:
        for r in inv['bodies'] + inv['heads']:
            why[r['key']] = '%s  [%s]' % (r['file'], '; '.join(r['reasons']))
    for r in rows:
        size = ''
        if r['state'] in (PREPARED, CURRENT) and os.path.isfile(r['bundle']):
            size = '%d KB' % (os.path.getsize(r['bundle']) // 1024)
        stream.write('  %-20s %-19s %9s  %s\n' % (r['key'], r['state'], size,
                                                  r['reason'] or why.get(r['key'], '')))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    a_inv = sub.add_parser('inventory', help='list a stage\'s possible characters')
    a_inv.add_argument('level')
    a_inv.add_argument('--root', default=ROOT, help=argparse.SUPPRESS)
    a_inv.add_argument('--json', action='store_true')
    a_prep = sub.add_parser('prepare', help='prepare the remaster items for them')
    g = a_prep.add_mutually_exclusive_group(required=True)
    g.add_argument('--level')
    g.add_argument('--items', help='char/<item>,head/<item>,... (diagnostic)')
    for opt in ('--index', '--archive', '--sevenzip', '--out', '--stamps', '--stamp'):
        a_prep.add_argument(opt, required=True)
    a_prep.add_argument('--scratch')
    a_prep.add_argument('--report')
    a_prep.add_argument('--root', default=ROOT, help=argparse.SUPPRESS)
    a_prep.add_argument('--force', action='store_true')
    a_prep.add_argument('--keep-scratch', action='store_true')
    a = ap.parse_args(argv)

    if a.cmd == 'inventory':
        try:
            inv = inventory(a.level, a.root)
        except InventoryError as exc:
            print('stagechars: %s' % exc)
            return 1
        if a.json:
            print(json.dumps(inv, indent=1))
        else:
            print('%s: LEVELID %d, setup %s (%s), %d guard records'
                  % (inv['level'], inv['levelid'], inv['setup'], inv['setup_source'],
                     inv['guards']))
            for part in ('bodies', 'heads'):
                print('%s (%d):' % (part, len(inv[part])))
                for r in inv[part]:
                    print('  %3d %-22s %-18s %s' % (r['index'], r['file'], r['key'],
                                                   '; '.join(r['reasons'])))
        return 0

    inv = None
    if a.level:
        try:
            inv = inventory(a.level, a.root)
        except InventoryError as exc:
            print('prepare-xbla-assets: %s' % exc)
            return 1
        keys = [r['key'] for r in inv['bodies'] + inv['heads']]
    else:
        keys = []
        for k in a.items.split(','):
            k = k.strip()
            if not k:
                continue
            if not re.fullmatch(r'(char|head)/[a-z0-9_]{1,%d}' % ITEM_MAX, k):
                print("prepare-xbla-assets: '%s' is not char/<item> or head/<item>." % k)
                return 1
            if k not in keys:
                keys.append(k)
    with open(a.index, 'r', encoding='utf-8-sig') as f:
        paths = [ln.rstrip('\r\n') for ln in f if ln.strip()]
    stamp_now = a.stamp.strip()
    rows = plan(keys, paths, a.out, a.stamps, stamp_now, a.force)
    if a.items:
        absent = [r['key'] for r in rows if r['state'] == ABSENT]
        if absent:
            print('prepare-xbla-assets: that source holds no remastered %s.' % ', '.join(absent))
            print('  It is refused rather than guessed at. Check that it is the')
            print('  source this pipeline was made against.')
            return 1
    scratch = a.scratch or os.path.join(a.stamps, 'characters')
    print('')
    if inv is not None:
        print('characters: %s (setup %s) - %d bodies, %d heads the stage can draw'
              % (inv['level'], inv['setup'], len(inv['bodies']), len(inv['heads'])))
    n_build = sum(1 for r in rows if r['state'] == BUILD)
    if n_build:
        print('  extracting %d item(s) in one pass -> %s' % (n_build, scratch))
    execute(rows, stamp_now, scratch,
            lambda dest, lst: sevenzip_extract(a.sevenzip, a.archive, dest, lst),
            _convert_with_prepare(a.out), a.keep_scratch)
    print('')
    _report(rows, inv)
    counts = {}
    for r in rows:
        counts[r['state']] = counts.get(r['state'], 0) + 1
    print('')
    print('  ' + ', '.join('%d %s' % (counts[s], s.lower())
                           for s in (PREPARED, CURRENT, ABSENT, REFUSED) if s in counts))
    if counts.get(ABSENT):
        print('  A source-absent part draws as the game\'s own under XBLA - the fallback')
        print('  the provider gives any model with no prepared item.')
    if a.report:
        with open(a.report, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(dict(inventory=inv, rows=[{k: r[k] for k in
                                                 ('key', 'state', 'reason', 'files')}
                                                for r in rows]), f, indent=1)
    return 1 if counts.get(REFUSED) else 0


if __name__ == '__main__':
    sys.exit(main())
