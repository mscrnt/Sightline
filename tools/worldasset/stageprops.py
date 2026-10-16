"""A stage's RIGID PROPS, read from the game's own tables, and the preparation
of the remaster items that stand in for them (#71).

    stageprops.py inventory dam [--json]
    stageprops.py names --xex <executable>
    stageprops.py prepare --level dam --index <paths> --archive <source>
                          --sevenzip <7z> --out <root> --stamps <dir>
                          --stamp <stamp> [--rom <rom>] [--force]

tools/windows/prepare-xbla-assets.ps1 -Level dam -Props is the command a
developer runs; this is the derivation behind it.

WHICH PROPS. A stage's setup lists its objects (the propdef list stagechars
walks); the ones the runtime seam takes are the standard props, the single
and multi-screen monitors, the vehicles, the gas props, the hats and the body
armour (PROPDEF_PROP, PROPDEF_MONITOR, PROPDEF_MULTI_MONITOR,
PROPDEF_VEHICHLE, PROPDEF_GAS_RELEASING, PROPDEF_HAT, PROPDEF_ARMOUR -
src/sl_chr_asset.h, THE STAGE'S RIGID PROPS) and the doors (the DYNAMIC and
RIGID DOORS, door_taken below), each naming its model by index
into the game's prop file table
(PitemZ_entries, assets/obseg/prop/propItemModelFileRecord.inc.c): its file
is P<name>Z, and the runtime's provider key is prop/<name>, the name lowered.

WHICH SOURCE ITEM. The remaster names its prop items in its own words (the
game's Pswivel_chair1Z is its prop/swivelchair1, Pcard_box4_lgZ its
prop/cardbox4) and the pairing is not a table here: the remaster's
EXECUTABLE carries the game's prop file table, record for record, each
record's name field pointing at its own item name. `locate_prop_names` finds
it by the decomp's own record values - the sequence of the records' scales -
on one stride in the game's order, and reads the names through the name
field; an executable where that table is not found exactly once is refused.

WHAT THE ITEM CARRIES, and how the runtime reads it, is prepare.convert_prop
and src/native/sl_chr_asset.c. Both of the source's generations of each item
are read: the remastered one is drawn, the original one is the reference box.

WHICH PARTS MOVE. A vehicle's model moves parts of itself - its wheels -
under matrices of their own, and the remaster's item carries a joint for
each, named as the game names that matrix. The names are the decomp's: a
prop model's header names its skeleton (assets/obseg/prop/<name>/
ModelFileHeader.inc.c, SKELETON(car)), and the skeleton's joint list is kept
beside its names, in matrix order (assets/embedded/skeletons/props.inc.c:
base, wheel1..wheel4). `matrix_names` reads them; the preparation marks each
item joint that bears one as riding that matrix.

A STAGE'S ACCEPTED SET. The derivation grows as the runtime takes more kinds
(the rigid doors, the worn hats), and a stage whose look has been accepted
must not change because its props were prepared again. A stage may name, in
data/overrides/<stage>/props-held.txt, the models the derivation takes that
are HELD BACK on it - one item per line, a reason after it, `#` comments.
`prepare` lists them and prepares nothing for them; `inventory` marks them.
A bundle is shared by every stage that draws the model, so a held item is
only not ADDED by this stage's preparation.

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
import viewmodels                                           # noqa: E402

ROOT = stagechars.ROOT
CLASS = 'prop'
PROPDEF_PROP, PROPDEF_MONITOR, PROPDEF_MULTI_MONITOR, PROPDEF_VEHICHLE = 0x03, 0x0a, 0x0b, 0x27
PROPDEF_GAS_RELEASING, PROPDEF_HAT, PROPDEF_ARMOUR = 0x24, 0x11, 0x15
# GLASS (setup type 42, PROPDEF_GLASS): breakable glass - Facility's lab
# glassware, and the Dam's window panes. Taken as a one-piece prop; a PANE is
# refused by the preparation (prepare.convert_prop, `pane`), so a window is
# the game's own glazing on every stage.
PROPDEF_GLASS = 0x2a
# THE AMMO BOX (type 20, PROPDEF_AMMO): a crate the player collects, drawn by
# the object render as a one-piece model until the game removes it. The
# AUTOGUN (13), the AIRCRAFT (40) and the TANK (45) are moving props: the
# object render turns their parts under matrices the game computes (the
# autogun's yaw and pitch, the aircraft's rotor and animation, the tank's
# turret, gun and hatch), and the item's joints ride those matrices by the
# skeleton's names, as a vehicle's do (#78).
PROPDEF_AUTOGUN, PROPDEF_AMMO, PROPDEF_AIRCRAFT, PROPDEF_TANK = 0x0d, 0x14, 0x28, 0x2d
# THE KEY (4), the ALARM (5) and the COLLECTABLE (8 - a weapon, on the floor
# or carried by a guard) (#80): one-piece models the object render draws like
# a standard prop's, the collectable's also inside its carrier's walk while a
# guard holds it (chr.c, chrRenderHeldWeapon). The game keeps the pickup, the
# alarm's state and the weapon's firing; a weapon's muzzle flash is the
# game's own lists (src/sl_chr_asset.h, THE COLLECTABLE).
PROPDEF_KEY, PROPDEF_ALARM, PROPDEF_COLLECTABLE = 0x04, 0x05, 0x08
TAKEN = {PROPDEF_PROP: 'standard prop', PROPDEF_MONITOR: 'single monitor',
         PROPDEF_KEY: 'key', PROPDEF_ALARM: 'alarm', PROPDEF_COLLECTABLE: 'collectable',
         PROPDEF_MULTI_MONITOR: 'multi-screen monitor',
         PROPDEF_VEHICHLE: 'vehicle', PROPDEF_GAS_RELEASING: 'gas prop',
         PROPDEF_HAT: 'hat', PROPDEF_ARMOUR: 'body armour', PROPDEF_GLASS: 'glass',
         PROPDEF_AMMO: 'ammo box', PROPDEF_AUTOGUN: 'autogun',
         PROPDEF_AIRCRAFT: 'aircraft', PROPDEF_TANK: 'tank'}
ORIGINAL = 'original'
PROPDEF_DOOR = 0x01
# THE DYNAMIC DOORS (#71): a door is taken when the game reshapes it as it
# moves - DOORFLAG_CLIP_TO_BBOX (src/bondconstants.h) on a sliding or vertical
# door (DOORTYPE_SLIDING 0, DOORTYPE_VERTICAL 4) - which the runtime seam
# follows by cutting the item at the game's own shrinking box
# (sl_door_takes, src/sl_chr_asset.h). The record's word 38 is the note's
# "98: open technique (2+2 bytes)" (Setup File Microcodes and Paths/objects/
# doors.txt), read as the decomp reads it (bondtypes.h DoorRecord):
# doorFlags in its high half and doorType in its low half.
# THE RIGID DOORS: a sliding, vertical or swinging door WITHOUT the flag is
# never reshaped - the game moves (or, swinging, turns) the whole model under
# its matrix - and its item rides that matrix whole (sl_door_rides).
# THE PARTED DOORS (#96): an eye or iris door without the flag - Caverns'
# airlocks - keeps its box and turns its leaves under matrices of their own
# (propobj.c, the door branch of the object render); its item's joints ride
# them by the skeleton's names (matrix_names), as a vehicle's do
# (sl_door_parts).
DOOR_WORD = 0x98 // 4
DOORFLAG_CLIP_TO_BBOX = 0x4
DOORTYPE_SLIDING, DOORTYPE_VERTICAL, DOORTYPE_SWINGING = 0, 4, 5
DOORTYPE_EYE, DOORTYPE_IRIS = 6, 7
#: Aztec's boardroom chairs (#96): turned about their hinge by the swinging
#: door's matrix (propobj.c door7F0526EC), so taken as a rigid door is.
DOORTYPE_AZTECCHAIR = 9


def door_taken(words):
    """A door propdef's words -> whether the runtime seam takes its model:
    'dynamic door', 'rigid door', 'parted door', or None."""
    if len(words) <= DOOR_WORD:
        return None
    flags, dtype = (words[DOOR_WORD] >> 16) & 0xffff, words[DOOR_WORD] & 0xffff
    if flags & DOORFLAG_CLIP_TO_BBOX:
        return 'dynamic door' if dtype in (DOORTYPE_SLIDING, DOORTYPE_VERTICAL) else None
    if dtype in (DOORTYPE_EYE, DOORTYPE_IRIS):
        return 'parted door'
    return 'rigid door' if dtype in (DOORTYPE_SLIDING, DOORTYPE_VERTICAL, DOORTYPE_SWINGING,
                                     DOORTYPE_AZTECCHAIR) else None


HELD = 'data/overrides/%s/props-held.txt'


def held_items(level, root=ROOT):
    """{item: reason} from the stage's held list (A STAGE'S ACCEPTED SET),
    empty when the stage has none."""
    p = os.path.join(root, *(HELD % level).split('/'))
    out = {}
    if not os.path.isfile(p):
        return out
    with open(p, 'r', encoding='utf-8') as f:
        for ln in f:
            ln = ln.split('#', 1)[0].strip()
            if not ln:
                continue
            parts = ln.split(None, 1)
            out[parts[0].lower()] = parts[1].strip() if len(parts) > 1 else 'held'
    return out


class PropError(Exception):
    """The game's tables or the source say something this reader refuses."""


def prop_records(root=ROOT):
    """The game's prop file table in index order: [dict(index, name, scale)]
    (assets/obseg/prop/propItemModelFileRecord.inc.c and each record it
    includes; PROPFILERECORD's scale defaults to 0.1)."""
    rel = 'assets/obseg/prop/propItemModelFileRecord.inc.c'
    text = stagechars._read(root, rel)
    out = []
    for inc in re.findall(r'#include\s*<([^>]+)>', text):
        body = stagechars._strip_comments(stagechars._read(root, inc))
        m = re.search(r'PROPFILERECORD\(\s*(\w+)\s*,\s*([^)]*)\)', body)
        if not m:
            raise PropError('%s: no PROPFILERECORD' % inc)
        s = m.group(2).strip()
        out.append(dict(index=len(out), name=m.group(1),
                        scale=float(s.rstrip('fF')) if s else 0.1))
    if len(out) < 8:
        raise PropError('%s: only %d records read' % (rel, len(out)))
    return out


def matrix_names(name, root=ROOT):
    """The game's names for prop model `name`'s matrices, in matrix order,
    lower-case as the decomp keeps them - or None when its skeleton's joint
    list carries no names (a one-piece prop's standard_object). The header
    names the skeleton; the skeleton files keep each joint list's names in
    the name table directly above it (a block under `#if 0` is not the
    game's). A skeleton may serve models with fewer matrices than it has
    joints - the autogun skeleton's six joints serve a five-matrix gun, the
    aircraft's five a three-matrix plane - and its names then name the
    model's matrices in order. PropError when the names cannot name every
    matrix of the model, or name more joints than the skeleton has."""
    rel = 'assets/obseg/prop/%s/ModelFileHeader.inc.c' % name
    try:
        text = stagechars._strip_comments(stagechars._read(root, rel))
    except (OSError, stagechars.InventoryError):
        return None
    m = re.search(r'MODELFILEHEADER\(\s*\w+\s*,(.*)\)', text)
    if not m:
        return None
    a = stagechars._split_top(m.group(1))
    sk = re.search(r'SKELETON\(\s*(\w+)\s*\)', a[1]) if len(a) > 4 else None
    if not sk:
        return None
    skel, nmtx = sk.group(1), int(a[4], 0)
    d = os.path.join(root, 'assets', 'embedded', 'skeletons')
    for fn in sorted(os.listdir(d)):
        body = open(os.path.join(d, fn), encoding='utf-8', errors='replace').read()
        body = re.sub(r'(?ms)^#if 0\b.*?^#endif\b', '', body)
        j = re.search(r'ModelJoint\s+JOINTLIST\(\s*%s\s*\)' % re.escape(skel), body)
        if not j:
            continue
        before = body[:j.start()]
        k = before.rfind('JOINTLIST(')
        tab = list(re.finditer(r'_JointNames\s*\[\s*\]\s*=\s*\{(.*?)\}', before[k + 1:], re.S))
        if not tab:
            return None
        names = re.findall(r'"([A-Za-z0-9_]+)"', tab[-1].group(1))
        c = re.search(r'MODELSKELETON\(\s*%s\s*,\s*(\w+)' % re.escape(skel), body)
        count = int(c.group(1), 0) if c else len(names)
        if len(names) > count or nmtx > len(names) or len(set(names)) != len(names):
            raise PropError('skeleton %s: %d names for %d joints and %d matrices'
                            % (skel, len(names), count, nmtx))
        return names[:nmtx]
    return None


SCALE_AT = 8               # the record's scale, after its header and name
NAME_AT = 4
STRIDES = (12, 16, 20, 24)
MIN_SHARE = 0.5


def locate_prop_names(image, base, records):
    """{game record index: the remaster's item name}. Raises PropError unless
    the game's prop table is found exactly once: the records' scales in the
    game's order on one stride, more than MIN_SHARE of them matched."""
    pats = [struct.pack('>f', r['scale']) for r in records]
    need = MIN_SHARE * len(records)
    found = []
    for stride in STRIDES:
        at = image.find(pats[0])
        while at >= 0:
            start = at - SCALE_AT
            if start >= 0 and start + len(records) * stride <= len(image):
                score = sum(1 for i, p in enumerate(pats)
                            if image[start + i * stride + SCALE_AT:
                                     start + i * stride + SCALE_AT + 4] == p)
                if score > need:
                    found.append((score, start, stride))
            at = image.find(pats[0], at + 4)
    if not found:
        raise PropError('the executable carries no prop table this reader recognises')
    found.sort(key=lambda f: -f[0])
    if len(found) > 1 and found[1][0] == found[0][0]:
        raise PropError('the executable carries two candidate prop tables; refused '
                        'rather than chosen between')
    _score, start, stride = found[0]
    names = {}
    for r in records:
        o = start + r['index'] * stride + NAME_AT
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
        raise PropError(str(exc))
    return locate_prop_names(image, viewmodels.xex_base(data), prop_records(root))


def stage_props(level, root=ROOT, country='u'):
    """Every model the stage's standard props and single monitors are drawn
    from, in model order: [dict(index, file, key, item, count, why)]."""
    lid, setup, rel = stagechars.setup_of(root, level, country)
    text = stagechars._preprocess(stagechars._strip_comments(stagechars._read(root, rel)))
    recs = prop_records(root)
    got = {}
    for kind, w in stagechars.propdef_records(text, setup, stagechars.word_macros(root), rel):
        if kind == PROPDEF_DOOR and door_taken(w):
            label = door_taken(w)
        elif kind in TAKEN:
            label = TAKEN[kind]
        else:
            continue
        ix = w[1] >> 16
        if ix >= 0x8000:
            continue                      # an object placed inside another
        if not (0 <= ix < len(recs)):
            raise PropError('%s names prop model %d; the table has %d' % (rel, ix, len(recs)))
        e = got.setdefault(ix, dict(count=0, kinds=set()))
        e['count'] += 1
        e['kinds'].add(label)
    rows = []
    for ix in sorted(got):
        nm = recs[ix]['name']
        rows.append(dict(index=ix, file='P%sZ' % nm, key='%s/%s' % (CLASS, nm.lower()),
                         item=nm.lower(), count=got[ix]['count'],
                         glass=got[ix]['kinds'] == {TAKEN[PROPDEF_GLASS]},
                         matrices=matrix_names(nm, root),
                         why='%d %s' % (got[ix]['count'], ', '.join(sorted(got[ix]['kinds'])))))
    return rows


def resolve(rows, paths):
    """Every row viewmodels.plan left open: BUILD with BOTH generations'
    files, or SOURCE ABSENT when either is missing."""
    for r in rows:
        if r['state'] is not None:
            continue
        if not r['source']:
            r['state'] = stagechars.ABSENT
            r['reason'] = "the executable's table names no item for it"
            continue
        new = viewmodels.source_files(paths, CLASS, r['source'])
        old = viewmodels.source_files(paths, CLASS, r['source'], ORIGINAL)
        if new and old:
            r['files'], r['state'] = new + old, stagechars.BUILD
        else:
            r['state'] = stagechars.ABSENT
            r['reason'] = 'the source carries no %s prop/%s' % (
                'remastered' if not new else 'original', r['source'])
    return rows


def prepare_main(a):
    with open(a.index, 'r', encoding='utf-8-sig') as f:
        paths = [ln.rstrip('\r\n') for ln in f if ln.strip()]
    scratch = a.scratch or os.path.join(a.stamps, 'props')
    stamp_now = a.stamp.strip()
    try:
        held = held_items(a.level)
        props = stage_props(a.level)
        kept = [p for p in props if p['item'] not in held]
        rows = [dict(key=p['key'], cls=CLASS, item=p['item'], source=None, index=p['index'],
                     why='%s, %s' % (p['file'], p['why'])) for p in kept]
        props_held = [p for p in props if p['item'] in held]
        matrices = {p['item']: p['matrices'] for p in props}
        glass = {p['item'] for p in props if p['glass']}
        viewmodels.plan(rows, a.out, a.stamps, stamp_now, a.force)
        if any(r['state'] is None for r in rows):
            xexes = [p for p in paths if p.lower().endswith('.xex')]
            if len(xexes) != 1:
                raise PropError('the source holds %d executables where one was expected'
                                % len(xexes))
            os.makedirs(scratch, exist_ok=True)
            lst = os.path.join(scratch, 'xex.txt')
            with open(lst, 'w', encoding='ascii', newline='\n') as f:
                f.write(xexes[0] + '\n')
            if stagechars.sevenzip_extract(a.sevenzip, a.archive, scratch, lst) != 0:
                raise PropError('the executable could not be extracted')
            names = source_names(os.path.join(scratch, *xexes[0].replace('\\', '/').split('/')))
            for r in rows:
                if r['source'] is None:
                    r['source'] = names.get(r['index'])
        resolve(rows, paths)
    except (PropError, viewmodels.ViewModelError, stagechars.InventoryError) as exc:
        print('prepare-xbla-assets: %s' % exc)
        return 1
    print('')
    print('props: %s - %d model(s)' % (a.level, len(rows)))
    for p in props_held:
        print('  %-36s %-19s %9s  %s' % (p['key'], 'HELD', '', '%s (%s)'
                                          % (held[p['item']], HELD % a.level)))
    sources = {r['item']: r['source'] for r in rows}

    def convert(cls, item, tree):
        import prepare                                        # noqa: E402
        return prepare.convert_prop(item, sources[item], tree, a.out, matrices.get(item),
                                    pane=item in glass, rom=a.rom)
    stagechars.execute(rows, stamp_now, os.path.join(scratch, 'items'),
                       lambda dest, lst: stagechars.sevenzip_extract(a.sevenzip, a.archive, dest, lst),
                       convert, a.keep_scratch)
    viewmodels.keep_outcomes(rows, a.stamps, stamp_now)
    if not a.keep_scratch:
        import shutil
        shutil.rmtree(scratch, ignore_errors=True)
    counts = {}
    for r in rows:
        counts[r['state']] = counts.get(r['state'], 0) + 1
        size = ''
        if r['state'] in (stagechars.PREPARED, stagechars.CURRENT) and os.path.isfile(r['bundle']):
            size = '%d KB' % (os.path.getsize(r['bundle']) // 1024)
        print('  %-36s %-19s %9s  %s' % ('%s <- prop/%s' % (r['key'], r['source'] or '?'),
                                          r['state'], size, r['reason'] or r['why']))
    print('  %s' % ', '.join('%d %s' % (n, s) for s, n in sorted(counts.items())))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    a_inv = sub.add_parser('inventory', help="list a stage's rigid prop models")
    a_inv.add_argument('level')
    a_inv.add_argument('--json', action='store_true')
    a_nm = sub.add_parser('names', help='the remaster item name of each game prop record')
    a_nm.add_argument('--xex', required=True)
    a_pr = sub.add_parser('prepare', help="prepare a stage's prop items")
    a_pr.add_argument('--level', required=True)
    for opt in ('--index', '--archive', '--sevenzip', '--out', '--stamps', '--stamp'):
        a_pr.add_argument(opt, required=True)
    a_pr.add_argument('--scratch')
    a_pr.add_argument('--rom', help="the user's ROM: the game's own tints (D-032)")
    a_pr.add_argument('--force', action='store_true')
    a_pr.add_argument('--keep-scratch', action='store_true')
    a = ap.parse_args(argv)
    if a.cmd == 'inventory':
        try:
            rows = stage_props(a.level)
            held = held_items(a.level)
        except (PropError, stagechars.InventoryError) as exc:
            print('stageprops: %s' % exc)
            return 1
        if a.json:
            print(json.dumps(rows, indent=1))
        else:
            for r in rows:
                print('  %3d %-26s %-24s %s%s%s' % (r['index'], r['file'], r['key'], r['why'],
                                                    ' - matrices %s' % ', '.join(r['matrices'])
                                                    if r['matrices'] else '',
                                                    ' - HELD: %s' % held[r['item']]
                                                    if r['item'] in held else ''))
        return 0
    if a.cmd == 'names':
        for idx, nm in sorted(source_names(a.xex).items()):
            print('  %3d %s' % (idx, nm))
        return 0
    return prepare_main(a)


if __name__ == '__main__':
    sys.exit(main())
