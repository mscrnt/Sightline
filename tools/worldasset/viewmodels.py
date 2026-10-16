"""The player's FIRST-PERSON view models on a stage, read from the game's own
tables, and the preparation of the remaster items that stand in for them
(#70).

    viewmodels.py inventory dam [--json]
    viewmodels.py prepare --level dam [--player] --index <paths>
                          --archive <source> --sevenzip <7z> --out <root>
                          --stamps <dir> --stamp <stamp> [--force]
    viewmodels.py prepare --items gun/wppksil ...                (diagnostic)

tools/windows/prepare-xbla-assets.ps1 is the command a developer runs; this is
the derivation behind its -ViewModels and -Player switches (--player adds the
external body and head). It is a separate category from
stagechars.py's: a view model is the player's, not a stage character's, and
it is a different kind of model (a weapon file with the hand that holds it).

WHAT THE PLAYER HOLDS. A stage's setup carries an intro list
(bondview_r.c reads it): INTROTYPE_ITEM records give the player an item in
the right hand (and the left, for a pair), each flagged with the demo
playback it belongs to - 0 is ordinary play, and only those are read here.
Every life also begins with ITEM_FIST (bondview.c bondviewPlayerBeginLife).
The game's ALL GUNS cheat puts every weapon below ITEM_BOMBCASE in reach
(bondinv.c), and the watch puts the watch hand in the left hand
(first_person_set). Each item's first-person model is the game's gun record
of the same index (gitem_structs, assets/obseg/gun/gunModelFileRecord.inc.c):
its file is G<name>Z (the watch hand's C<name>Z), and the runtime's provider
key is gun/<name>, the name lowered (src/native/sl_vm_asset.c), with no table
between them. Whether the game draws it at all, and whether it pairs, are the
weapon's compiled stats (weapon_flags).

WHICH SOURCE ITEM. The remaster names its weapon items in its own words
(the game's GwppksilZ is its gun/ppksilenced) and the pairing is not a table
here either: the remaster's EXECUTABLE carries the game's own gun record
table, record for record, with the file-name field pointing at its item
name. `locate_gun_names` finds that table by the decomp's own record values -
each record's watch position, rotation and rotation origin, twenty-eight
bytes no other structure repeats in order - requires every match on one
stride in the game's own order, and reads the name through the file-name
field. An executable where that table is not found exactly once is refused.

WHAT THE ITEM CARRIES, and how the runtime reads it, is prepare.py's
convert_viewmodel and src/native/sl_vm_asset.c; the GAME SWITCH vocabulary
both use is `gun_switch_names`. The WATCH HAND is its own conversion
(prepare.convert_watchhand): its item is a character-class item carrying one
sleeve per costume, which watchhand.py pairs with the game's cuffs from the
game's own model - read from the user's ROM (--rom) - and the source's
original generation of the same item.

This module contains no source bytes, no identifier of any particular source
and no offset into any particular executable.
"""
import argparse
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import stagechars                                           # noqa: E402

ROOT = stagechars.ROOT
CLASS = 'gun'
REMASTERED = stagechars.REMASTERED


class ViewModelError(Exception):
    """The game's tables or the source say something this reader refuses."""


def _read(root, rel):
    return stagechars._read(root, rel)


def gun_switch_names(root=ROOT):
    """The game's own vocabulary for a weapon model's Switches[] table: the
    36 names the decomp keeps beside the gun model headers
    (assets/obseg/gun/gunModelFileHeaders.inc.c, the one table that lists
    every index the game's code reads - CARTRIDGE 0, SW_FLASH 1, ... FINGER
    6, SLIDE 7, SW_HANDOFF1..6 8..13, ... SW_HANDOFF7 35). Every gun model
    header declares that many switches."""
    rel = 'assets/obseg/gun/gunModelFileHeaders.inc.c'
    text = _read(root, rel)
    m = re.search(r'weapon_gun_revolver_SwitchNames\s*\[\s*\]\s*=\s*\{(.*?)\}', text, re.S)
    if not m:
        raise ViewModelError('%s: no weapon switch-name table' % rel)
    names = re.findall(r'"([A-Za-z0-9_]+)"', m.group(1))
    if len(names) != 36 or len(set(names)) != len(names):
        raise ViewModelError('%s: the switch-name table is not 36 distinct names' % rel)
    return names


_FLOAT = r'(-?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?)f?'


def gun_records(root=ROOT):
    """The game's gun record table in index order: [dict(index, name, pos,
    rot, origin) | None], None for a record with no model. Read from
    assets/obseg/gun/gunModelFileRecord.inc.c and each record it includes."""
    rel = 'assets/obseg/gun/gunModelFileRecord.inc.c'
    text = _read(root, rel)
    body = text[text.index('{') + 1:]
    out = []
    for line in body.splitlines():
        s = line.strip()
        inc = re.match(r'#include\s*<([^>]+)>', s)
        if inc:
            s = _read(root, inc.group(1)).strip().splitlines()[-1].strip()
        m = re.match(r'(GUNFILERECORD|SUIT_LFRECORD)\(\s*(\w+)\s*,(.*)\)\s*$', s)
        if m:
            args = [a.strip() for a in stagechars._split_top(m.group(3))]
            if len(args) != 14:
                raise ViewModelError('%s: record %d has %d fields' % (rel, len(out), len(args) + 1))
            f = [float(re.fullmatch(_FLOAT, a).group(1)) for a in
                 (args[4], args[5], args[6], args[7], args[8], args[11], args[12], args[13])]
            st = re.fullmatch(r'GUNSTATS\(\s*(\w+)\s*\)', args[1])
            out.append(dict(index=len(out), name=m.group(2),
                            prefix='C' if m.group(1) == 'SUIT_LFRECORD' else 'G',
                            stats=st.group(1) if st else None,
                            pos=f[0:3], rot=f[3:5], origin=f[5:8]))
        elif s.startswith('{'):
            out.append(None)
    if len(out) < 8:
        raise ViewModelError('%s: only %d records read' % (rel, len(out)))
    return out


def weapon_flags(root=ROOT):
    """{stats name: frozenset of WEAPONSTATBITFLAG_ names} for every
    WeaponStats record the game compiles: assets/obseg/gun/gunWeaponStats.inc.c
    (gun.c includes it) and the files IT includes - not every per-gun stats
    file in the tree, several of which nothing includes."""
    rel = 'assets/obseg/gun/gunWeaponStats.inc.c'
    text = _read(root, rel)
    for inc in re.findall(r'#include\s*<([^>]+)>', text):
        text += '\n' + _read(root, inc)
    text = stagechars._strip_comments(text)
    out = {}
    for m in re.finditer(r'WeaponStats\s+(\w+)_stats\s*=\s*\{(.*?)\}\s*;', text, re.S):
        if m.group(1) in out:
            raise ViewModelError('%s: two compiled records for %s_stats' % (rel, m.group(1)))
        out[m.group(1)] = frozenset(re.findall(r'WEAPONSTATBITFLAG_(\w+)', m.group(2)))
    if len(out) < 8:
        raise ViewModelError('%s: only %d weapon records read' % (rel, len(out)))
    return out


def all_guns(root=ROOT):
    """[(item enum name, value)] of every item the game's ALL GUNS cheat puts
    in reach: bondinv.c bondinvItemAvailable answers TRUE, with equipallguns
    set, for every weapon id that is not ITEM_UNARMED and is below
    ITEM_BOMBCASE. The bound is read from that function, not assumed."""
    rel = 'src/game/bondinv.c'
    text = stagechars._strip_comments(_read(root, rel))
    m = re.search(r's32\s+bondinvItemAvailable\s*\([^)]*\)\s*\{(.*?)\n\}', text, re.S)
    rule = m and re.search(r'equipallguns\s*\)\s*&&\s*\(\s*weaponid\s*!=\s*(ITEM_\w+)\s*\)'
                           r'\s*&&\s*\(\s*weaponid\s*<\s*(ITEM_\w+)\s*\)', m.group(1))
    if not rule:
        raise ViewModelError('%s: bondinvItemAvailable does not read as the all-guns rule '
                             'this reader takes' % rel)
    items = stagechars.enum_values(root, ['ITEM_IDS'])
    lo, hi = items[rule.group(1)], items[rule.group(2)]
    return [(k, v) for k, v in sorted(items.items(), key=lambda kv: kv[1])
            if v != lo and 0 <= v < hi]


# The switches a weapon's stats carry that decide whether its first-person
# model is drawn at all (gunfire.c, the view-model tick: field_87F is cleared
# for an item with no model, without SHOW_FIRST_PERSON or with
# HIDE_FIRST_PERSON_HAND - a grenade or a mine is thrown from an empty view).
DRAW_FLAG, HIDE_FLAG = 'SHOW_FIRST_PERSON', 'HIDE_FIRST_PERSON_HAND'
DUAL_FLAG, MIRROR_FLAG = 'CAN_DUAL_WIELD', 'MIRROR_DUAL'
WATCH_HAND = 'ITEM_SUIT_LF_HAND'


def first_person_set(level, root=ROOT, country='u'):
    """Every first-person model the player can see on `level`, in item order:
    the stage's own loadout (stage_loadout), every item the ALL GUNS cheat
    reaches (all_guns), and the WATCH HAND - the left hand the watch is worn
    on, which bondview2.c puts in the left-hand buffer when the watch opens.
    One row per item:

        dict(item, index, file, key, cls, drawn, why, dual, mirror)

    `drawn` is the game's own rule (DRAW_FLAG, HIDE_FLAG); `dual` whether the
    single-player ALL GUNS cycle offers the item in both hands (bondinv.c
    tests CAN_DUAL_WIELD); `mirror` whether the left hand's copy is the same
    model mirrored (gunfire.c negates the root's x for MIRROR_DUAL in the
    left hand). `key` is the runtime's provider key: gun/<the model's own
    name, lowered>, the watch hand's included."""
    records = gun_records(root)
    flags = weapon_flags(root)
    items = stagechars.enum_values(root, ['ITEM_IDS'])
    got = {}
    for name, v in stage_loadout(level, root, country):
        got.setdefault(v, (name, 'the stage loadout'))
    for name, v in all_guns(root):
        got.setdefault(v, (name, 'ALL GUNS'))
    if WATCH_HAND in items:
        got.setdefault(items[WATCH_HAND], (WATCH_HAND, 'the watch'))
    rows = []
    for v in sorted(got):
        name, src = got[v]
        r = records[v] if 0 <= v < len(records) else None
        fl = flags.get(r['stats'], frozenset()) if r is not None else frozenset()
        row = dict(item=name, index=v, file=None, key=None, cls='gun', via=src,
                   dual=DUAL_FLAG in fl, mirror=MIRROR_FLAG in fl, drawn=False, why='')
        if r is None:
            row['why'] = 'no first-person model (the record names none)'
        else:
            row['file'] = '%s%sZ' % (r['prefix'], r['name'])
            row['key'] = 'gun/%s' % r['name'].lower()
            if name == WATCH_HAND:
                row['drawn'], row['why'] = True, 'the watch hand, drawn while the watch is open'
                row['dual'] = row['mirror'] = False
            elif DRAW_FLAG not in fl or HIDE_FLAG in fl:
                row['why'] = 'not drawn in first person (%s)' % (
                    HIDE_FLAG if HIDE_FLAG in fl else 'no ' + DRAW_FLAG)
            else:
                row['drawn'], row['why'] = True, src
        rows.append(row)
    return rows


# ---- the remaster's executable ---------------------------------------------

def xex_base(data):
    """The image base address from an XEX2 header's optional-header
    directory (key 0x00010201, its value inline)."""
    if data[:4] != b'XEX2' or len(data) < 0x18:
        raise ViewModelError('the executable is not an XEX2 file')
    n = struct.unpack('>I', data[0x14:0x18])[0]
    if n > 256 or 0x18 + 8 * n > len(data):
        raise ViewModelError('the executable header directory is not framed as read')
    for k in range(n):
        key, val = struct.unpack('>II', data[0x18 + 8 * k:0x20 + 8 * k])
        if key == 0x00010201:
            return val
    raise ViewModelError('the executable header names no image base address')


RECORD_SIZE = 56          # GunModelFileRecord, bondtypes.h
POS_AT = 20               # WatchPos, WatchRot: five floats
ORIGIN_AT = 44            # WatchRotationOrigin: three floats
FILENAME_AT = 4
# A table is the one where MORE THAN HALF of the game's modelled records land on
# one stride: several records share a watch position (the null and document
# records all read 0, 0, 1000), so a start one record away also collects a
# handful of matches - measured, 21 of 89 against the table's own 83.
MIN_SHARE = 0.5


def locate_gun_names(image, base, records):
    """{game record index: the remaster's item name}. Raises ViewModelError
    unless the game's record table is found exactly once, on RECORD_SIZE,
    in the game's order, with more than MIN_SHARE of the records matched."""
    hits = {}
    for r in records:
        if r is None:
            continue
        sig = struct.pack('>5f', *(r['pos'] + r['rot']))
        tail = struct.pack('>3f', *r['origin'])
        at = image.find(sig)
        while at >= 0:
            b = at - POS_AT
            if b >= 0 and image[b + ORIGIN_AT:b + ORIGIN_AT + 12] == tail:
                hits.setdefault(r['index'], []).append(b)
            at = image.find(sig, at + 4)
    starts = {}
    for idx, bs in hits.items():
        for b in bs:
            starts.setdefault(b - idx * RECORD_SIZE, set()).add(idx)
    best = sorted(starts.items(), key=lambda kv: -len(kv[1]))
    need = MIN_SHARE * sum(1 for r in records if r is not None)
    if not best or len(best[0][1]) <= need:
        raise ViewModelError('the executable carries no gun record table this reader '
                             'recognises (%d records matched at best)'
                             % (len(best[0][1]) if best else 0))
    if len(best) > 1 and len(best[1][1]) > need:
        raise ViewModelError('the executable carries two candidate gun record tables; '
                             'refused rather than chosen between')
    table = best[0][0]
    names = {}
    for r in records:
        if r is None:
            continue
        o = table + r['index'] * RECORD_SIZE + FILENAME_AT
        p = struct.unpack('>I', image[o:o + 4])[0]
        q = p - base
        if p == 0 or not (0 <= q < len(image)):
            continue
        e = image.find(b'\0', q)
        s = image[q:e]
        if 0 < len(s) <= 31 and re.fullmatch(rb'[A-Za-z0-9_]+', s):
            names[r['index']] = s.decode('ascii')
    return names


def source_names(xex_path, root=ROOT):
    """{game record index: remaster item name} from the user's executable."""
    import xexenv                                             # noqa: E402
    data = open(xex_path, 'rb').read()
    try:
        image = xexenv.xex_image(data)
    except xexenv.XexError as exc:
        raise ViewModelError(str(exc))
    return locate_gun_names(image, xex_base(data), gun_records(root))


# ---- the stage's player loadout --------------------------------------------

INTRO_ITEM = 1
INTRO_WORDS = {0: 3, 1: 4, 2: 4, 3: 8, 4: 2, 5: 2, 6: 10, 7: 3, 8: 2}
INTRO_END = 9


def stage_loadout(level, root=ROOT, country='u'):
    """[(item enum name, item index)] the player holds on `level` in ordinary
    play: ITEM_FIST, then the intro list's INTROTYPE_ITEM records with demo
    flag 0 (both hands of a pair), in list order, each once."""
    lid, setup, rel = stagechars.setup_of(root, level, country)
    text = stagechars._strip_comments(_read(root, rel))
    words = [stagechars._evaluate(e, {}, stagechars.word_macros(root)) & 0xffffffff
             for e in stagechars._split_top(stagechars._array_body(text, 'intro', rel)) if e]
    items = stagechars.enum_values(root, ['ITEM_IDS'])
    name_of = {v: k for k, v in items.items()}
    got = [items['ITEM_FIST']]
    at = 0
    while True:
        if at >= len(words):
            raise ViewModelError('%s: the intro list ends without its end marker' % rel)
        kind = words[at] & 0xffff
        if kind == INTRO_END:
            break
        n = INTRO_WORDS.get(kind)
        if n is None or at + n > len(words):
            raise ViewModelError('%s: intro record type %d at word %d is not one this '
                                 'reader walks' % (rel, kind, at))
        if kind == INTRO_ITEM and words[at + 3] == 0:
            for w in (words[at + 1], words[at + 2]):
                v = w - (1 << 32) if w & 0x80000000 else w
                if v > 0 and v not in got:
                    got.append(v)
        at += n
    return [(name_of.get(v, str(v)), v) for v in got]


INTRO_CUFF = 5

# THE PLAYER'S EXTERNAL BODY, as bondview2.c solo_char_load chooses it in
# single-player: the intro list's INTROTYPE_CUFF record names the costume, and
# the body and head follow from it (the Bond of the retail folder, Brosnan).
# The runtime logs the choice it makes (sl_chrpres: player body ...), which is
# the check that this reading and the game agree.
CUFF_BODY = {'CUFF_BOILER': 'BODY_Special_Operations_Uniform',
             'CUFF_JUNGLE': 'BODY_Jungle_Fatigues', 'CUFF_SNOW': 'BODY_Parka',
             'CUFF_BROSNAN': 'BODY_Brosnan_Tuxedo', 'CUFF_CONNERY': 'BODY_Brosnan_Tuxedo',
             'CUFF_DALTON': 'BODY_Brosnan_Tuxedo', 'CUFF_MOORE': 'BODY_Brosnan_Tuxedo',
             'CUFF_FOLDER': 'BODY_Brosnan_Tuxedo'}
CUFF_HEAD = {'CUFF_BOILER': 'HEAD_Male_Brosnan_Boiler', 'CUFF_JUNGLE': 'HEAD_Male_Brosnan_Jungle',
             'CUFF_BROSNAN': 'HEAD_Male_Brosnan_Tuxedo', 'CUFF_CONNERY': 'HEAD_Male_Brosnan_Tuxedo',
             'CUFF_DALTON': 'HEAD_Male_Brosnan_Tuxedo', 'CUFF_MOORE': 'HEAD_Male_Brosnan_Tuxedo',
             'CUFF_FOLDER': 'HEAD_Male_Brosnan_Tuxedo'}
BODY_DEFAULT, HEAD_DEFAULT = 'BODY_Formal_Wear', 'HEAD_Male_Brosnan_Default'


def _intro_words(level, root, country):
    lid, setup, rel = stagechars.setup_of(root, level, country)
    text = stagechars._strip_comments(_read(root, rel))
    words = [stagechars._evaluate(e, {}, stagechars.word_macros(root)) & 0xffffffff
             for e in stagechars._split_top(stagechars._array_body(text, 'intro', rel)) if e]
    return rel, words


def player_body(level, root=ROOT, country='u'):
    """(body file, head file) of the player's external body on `level`."""
    rel, words = _intro_words(level, root, country)
    cuffs = stagechars.enum_values(root, ['CUFF_TYPES'])
    name_of = {v: k for k, v in cuffs.items()}
    cuff = None
    at = 0
    while at < len(words) and (words[at] & 0xffff) != INTRO_END:
        kind = words[at] & 0xffff
        n = INTRO_WORDS.get(kind)
        if n is None:
            raise ViewModelError('%s: intro record type %d is not one this reader walks' % (rel, kind))
        if kind == INTRO_CUFF:
            cuff = name_of.get(words[at + 1])
        at += n
    ids = stagechars.enum_values(root, ['BODIES', 'HEADS'])
    recs = stagechars.model_records(root)
    body = ids[CUFF_BODY.get(cuff, BODY_DEFAULT)]
    head = ids[CUFF_HEAD.get(cuff, HEAD_DEFAULT)]
    return cuff, recs[body]['file'], recs[head]['file']


def inventory(level, root=ROOT, country='u'):
    """The stage's first-person set (first_person_set), as the inventory
    command prints it."""
    return first_person_set(level, root, country)


ORIGINAL = 'original'


def source_files(paths, cls, item, gen=REMASTERED):
    """The item's files of one of the source's generations (the REMASTERED
    one unless named) in an archive index, the item named without regard to
    case (the remaster's own names mix it: silverPPK)."""
    want = ('%s/%s/%s' % (gen, cls, item)).lower()
    got = []
    for p in paths:
        q = p.replace('\\', '/')
        d, _, leaf = q.rpartition('/')
        dl = d.lower()
        if (dl == want or dl.endswith('/' + want)) and leaf.lower().endswith(('.bin', '.rba')):
            got.append(p)
    return sorted(set(got))


# A row's outcome is recorded beside the bundles, under the same stamp a
# bundle carries: a bundle and its .stamp for a prepared item, a .refused or
# .absent record for one that yielded none. An unchanged source and unchanged
# tools therefore answer every row from these records, and a rerun extracts
# nothing and converts nothing.
OUTCOME = {stagechars.REFUSED: 'refused', stagechars.ABSENT: 'absent'}


def _record(stamps, r):
    return os.path.join(stamps, '%s-%s' % (r['cls'], r['item']))


def plan(rows, out, stamps, stamp_now, force=False):
    """Give each row its bundle and stamp paths and, where the recorded
    outcome answers for this source and these tools, its state: CURRENT, or
    the REFUSED / SOURCE ABSENT it was. Every other row is left without a
    state, for `resolve`."""
    for r in rows:
        r['bundle'] = os.path.join(out, 'xbla', r['cls'], r['item'] + '.slwa')
        r['stamp'] = _record(stamps, r) + '.stamp'
        r['reason'] = r.get('reason', '')
        r['state'] = None
        if force:
            continue
        if os.path.isfile(r['bundle']) and os.path.isfile(r['stamp']):
            head, _, src = open(r['stamp'], encoding='utf-8').read().partition('\n')
            if head.strip() == stamp_now:
                r['state'] = stagechars.CURRENT
                r['source'] = r['source'] or src.strip() or None
                continue
        for state, ext in OUTCOME.items():
            p = '%s.%s' % (_record(stamps, r), ext)
            if os.path.isfile(p):
                head, _, why = open(p, encoding='utf-8').read().partition('\n')
                if head.strip() == stamp_now and not os.path.isfile(r['bundle']):
                    r['state'], r['reason'] = state, why.strip()
    return rows


def resolve(rows, paths):
    """Every row `plan` left open: its source files from the archive index,
    and so BUILD or SOURCE ABSENT."""
    for r in rows:
        if r['state'] is not None:
            continue
        r['files'] = source_files(paths, r['source_cls'], r['source']) if r['source'] else []
        if r['files']:
            r['state'] = stagechars.BUILD
            # the original generation rides along: a static weapon item is
            # placed by its box (prepare.convert_viewmodel)
            r['files'] += source_files(paths, r['source_cls'], r['source'], ORIGINAL)
        else:
            r['state'] = stagechars.ABSENT
            r['reason'] = ('the source carries no %s/%s' % (r['source_cls'], r['source'])
                           if r['source'] else "the executable's table names no item for it")
    return rows


def keep_outcomes(rows, stamps, stamp_now):
    """Record the REFUSED and SOURCE ABSENT rows under this stamp, and drop a
    record a prepared row has outgrown."""
    for r in rows:
        if r['state'] == stagechars.PREPARED and r['source']:
            # the prepared bundle's stamp also names the item it came from
            with open(r['stamp'], 'w', encoding='utf-8', newline='\n') as f:
                f.write('%s\n%s\n' % (stamp_now, r['source']))
        for state, ext in OUTCOME.items():
            p = '%s.%s' % (_record(stamps, r), ext)
            if r['state'] == state:
                with open(p, 'w', encoding='utf-8', newline='\n') as f:
                    f.write('%s\n%s\n' % (stamp_now, r['reason']))
            elif r['state'] in (stagechars.PREPARED, stagechars.CURRENT) and os.path.isfile(p):
                os.remove(p)


def view_rows(level, root=ROOT):
    """A row per first-person model the game DRAWS on `level`
    (first_person_set), keyed and bundled by the game's own model name, its
    source item still to be named."""
    rows = []
    for fp in first_person_set(level, root):
        if not fp['drawn']:
            continue
        name = fp['file'][1:-1].lower()
        rows.append(dict(key=fp['key'], cls='gun', item=name, source=None,
                         source_cls=SOURCE_CLASS.get(fp['file'][0], 'gun'), index=fp['index'],
                         why='%s, %s' % (fp['item'], fp['file']), fp=fp))
    return rows


# The class of the source item that stands for a first-person model, by the
# game's file prefix: a G file is a weapon's (gun/), the watch hand's C file
# is a character-class item in the source (char/).
SOURCE_CLASS = {'G': 'gun', 'C': 'char'}


def body_rows(level, root=ROOT):
    """The player's external body and head (char/, head/ by the model's own
    name)."""
    rows = []
    cuff, bfile, hfile = player_body(level, root)
    for cls, f in (('char', bfile), ('head', hfile)):
        it = stagechars.item_of(f)
        rows.append(dict(key='%s/%s' % (cls, it), cls=cls, item=it, source=it,
                         source_cls=cls,
                         why="the player's %s under %s, %s"
                         % ('body' if cls == 'char' else 'head', cuff or 'the default costume', f)))
    return rows


def player_rows(level, names, root=ROOT):
    """The player's items on `level`: every first-person model (view_rows)
    with its source item named, and the external body and head."""
    rows = view_rows(level, root)
    for r in rows:
        r['source'] = names.get(r['index'])
    return rows + body_rows(level, root)


def _convert(out, sources, switch_names, rom=None):
    def convert(cls, item, tree):
        import prepare                                        # noqa: E402
        if cls == 'gun':
            src_cls, src = sources[item]
            if src_cls == 'char':
                return prepare.convert_watchhand(item, src, tree, out, rom)
            return prepare.convert_viewmodel(item, src, tree, out, switch_names, src_cls)
        return prepare.convert_character(cls, item, tree, out)
    return convert


def _names(a, paths, scratch):
    """{game record index: source item name} from the user's executable,
    extracted in a pass of its own (the items it names are only known after
    it is read)."""
    xexes = [p for p in paths if p.lower().endswith('.xex')]
    if len(xexes) != 1:
        raise ViewModelError('the source holds %d executables where one was expected; the '
                             'view models cannot be named' % len(xexes))
    os.makedirs(scratch, exist_ok=True)
    lst = os.path.join(scratch, 'xex.txt')
    with open(lst, 'w', encoding='ascii', newline='\n') as f:
        f.write(xexes[0] + '\n')
    if stagechars.sevenzip_extract(a.sevenzip, a.archive, scratch, lst) != 0:
        raise ViewModelError('the executable could not be extracted')
    return source_names(os.path.join(scratch, *xexes[0].replace('\\', '/').split('/')))


def prepare_main(a):
    with open(a.index, 'r', encoding='utf-8-sig') as f:
        paths = [ln.rstrip('\r\n') for ln in f if ln.strip()]
    scratch = a.scratch or os.path.join(a.stamps, 'player')
    stamp_now = a.stamp.strip()
    try:
        switch_names = gun_switch_names()
        if a.level:
            rows = view_rows(a.level)
            if a.player:
                rows += body_rows(a.level)
        else:
            by_file = {r['name'].lower(): r['index'] for r in gun_records() if r is not None}
            rows = []
            for k in [k.strip() for k in a.items.split(',') if k.strip()]:
                m = re.fullmatch(r'(gun|char|head)/([a-z0-9_]{1,30})', k)
                if not m:
                    print("prepare-xbla-assets: '%s' is not gun/, char/ or head/<item>." % k)
                    return 1
                cls, it = m.groups()
                rows.append(dict(key=k, cls=cls, item=it,
                                 source=None if cls == 'gun' else it,
                                 source_cls=cls, index=by_file.get(it), why='named'))
        plan(rows, a.out, a.stamps, stamp_now, a.force)
        if any(r['state'] is None and r['cls'] == 'gun' for r in rows):
            names = _names(a, paths, scratch)
            for r in rows:
                if r['cls'] == 'gun' and r['source'] is None:
                    r['source'] = names.get(r['index'])
        resolve(rows, paths)
    except (ViewModelError, stagechars.InventoryError) as exc:
        print('prepare-xbla-assets: %s' % exc)
        return 1
    print('')
    print('player: %s - %d item(s)' % (a.level or 'named items', len(rows)))
    sources = {r['item']: (r['source_cls'], r['source']) for r in rows if r['cls'] == 'gun'}
    stagechars.execute(rows, stamp_now, os.path.join(scratch, 'items'),
                       lambda dest, lst: stagechars.sevenzip_extract(a.sevenzip, a.archive, dest, lst),
                       _convert(a.out, sources, switch_names, a.rom), a.keep_scratch)
    keep_outcomes(rows, a.stamps, stamp_now)
    if not a.keep_scratch:
        import shutil
        shutil.rmtree(scratch, ignore_errors=True)
    counts = {}
    for r in rows:
        counts[r['state']] = counts.get(r['state'], 0) + 1
        size = ''
        if r['state'] in (stagechars.PREPARED, stagechars.CURRENT) and os.path.isfile(r['bundle']):
            size = '%d KB' % (os.path.getsize(r['bundle']) // 1024)
        src = '' if r['cls'] != 'gun' else ' <- %s/%s' % (r['source_cls'], r['source'] or '?')
        print('  %-38s %-19s %9s  %s' % (r['key'] + src, r['state'], size, r['reason'] or r['why']))
    print('  %s' % ', '.join('%d %s' % (n, s) for s, n in sorted(counts.items())))
    if a.items and any(r['state'] in (stagechars.REFUSED, stagechars.ABSENT) for r in rows):
        return 1
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    a_inv = sub.add_parser('inventory', help="list the stage's first-person models")
    a_inv.add_argument('level')
    a_inv.add_argument('--root', default=ROOT, help=argparse.SUPPRESS)
    a_inv.add_argument('--json', action='store_true')
    a_nm = sub.add_parser('names', help='the remaster item name of each game gun record')
    a_nm.add_argument('--xex', required=True)
    a_pr = sub.add_parser('prepare', help="prepare the player's items for a stage")
    g = a_pr.add_mutually_exclusive_group(required=True)
    g.add_argument('--level')
    g.add_argument('--items', help='gun/<model>,char/<item>,head/<item> (diagnostic)')
    a_pr.add_argument('--player', action='store_true',
                      help='with --level: the external body and head as well')
    for opt in ('--index', '--archive', '--sevenzip', '--out', '--stamps', '--stamp'):
        a_pr.add_argument(opt, required=True)
    a_pr.add_argument('--scratch')
    a_pr.add_argument('--rom', help="the user's ROM: the watch hand's cuffs are read from it")
    a_pr.add_argument('--force', action='store_true')
    a_pr.add_argument('--keep-scratch', action='store_true')
    a = ap.parse_args(argv)
    if a.cmd == 'inventory':
        rows = inventory(a.level, a.root)
        if a.json:
            print(json.dumps(rows, indent=1))
        else:
            for r in rows:
                print('  %-20s %3d  %-16s %-20s %-5s %-4s %-6s %s' % (
                    r['item'], r['index'], r['file'] or '-', r['key'] or '-',
                    'drawn' if r['drawn'] else '-', 'dual' if r['dual'] else '-',
                    'mirror' if r['mirror'] else '-', r['why']))
        return 0
    if a.cmd == 'names':
        for idx, nm in sorted(source_names(a.xex).items()):
            print('  %3d %s' % (idx, nm))
        return 0
    return prepare_main(a)


if __name__ == '__main__':
    sys.exit(main())
