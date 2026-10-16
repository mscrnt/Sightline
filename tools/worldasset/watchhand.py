"""THE WATCH HAND (#70): which of the remaster's alternative sleeves each of
the game's costume cuffs wears, derived from the game's own watch-hand model
and both generations of the source's item.

WHAT THE GAME DOES. The watch is worn on a model of its own, Csuit_lf_handZ
(bondview2.c bondviewRenderWatch). Its Switches[4..9] are SWITCH nodes, one
per costume cuff - boiler suit, Brosnan tuxedo, Connery tuxedo, blue suit,
jungle fatigues, snow suit ('GE Object Offsets.txt', ARM, 0x10..0x24) - and
bondviewSelectCuff turns exactly one of them on, by the player's costume.
Everything else in the model (the arm, the hand, the watch, its hands) is
drawn always.

WHAT THE SOURCE DOES. The remaster's item (char/suitlfhand) carries one
sleeve per costume too, but gates none of them: no condition table, no gated
block. Drawn as the source draws it, every sleeve is drawn at once and they
fight by depth. The source's ORIGINAL generation of the same item is the
game's own model re-packed, its cuffs likewise ungated.

THE DERIVATION, from data alone (nothing here names a source image or an
item's draw):

  1. the game's model is read from the user's ROM (read_game_arm): its
     GROUP / GROUPSIMPLE nodes by matrix, each cuff switch's triangles with
     the game texture each run is drawn with, and the texture table;
  2. the item's space is related to the model's by the joints both name
     (relate): the decomp's matrix-order joint names for the model
     (assets/embedded/skeletons/<model>.inc.c) against the item's own, one
     scale and one quarter turn about z - the relation the runtime measures
     again (sl_vm_derive);
  3. each ORIGINAL-generation image group is identified with the game
     texture whose triangles it lies on (same place, same image width); a
     cuff whose own textures are each one original group is DETERMINED, and
     its features are read from those groups' texels;
  4. the REMASTERED generation's alternative sleeves are the image groups
     lying beyond the cuffs' wrist end (sleeve_groups);
  5. each sleeve and each determined cuff is described by three features
     (describe): where it begins along the forearm (a sleeve rolled up the
     arm, or one that reaches the wrist), the tone of its wrist end (a white
     shirt cuff, or a dark band), and the colour of its body;
  6. a cuff and a sleeve are paired (pair) only when BOTH class features
     agree and, among the candidates so agreeing, each is the other's
     nearest by colour with a clear margin - or is the only candidate. Every
     other cuff is left unmapped, and the runtime draws the game's own watch
     hand for it.

The result is carried per draw in the bundle (prepare.convert_watchhand): a
sleeve draw is marked as one of the item's alternatives and gated by the
game switch of the cuff it serves.
"""
import math
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import stagechars                                           # noqa: E402

ROOT = stagechars.ROOT
MODEL_BASE = 0x05000000
OP_HEADER, OP_GROUP, OP_DL, OP_SWITCH, OP_GROUPSIMPLE = 1, 2, 4, 18, 21

# The cuffs: bondviewSelectCuff(model, header, 4) sets Switches[4 + k] from
# the player's costume, k in this order ('GE Object Offsets.txt', ARM).
CUFF_FIRST = 4
CUFFS = ('BOILER', 'TUX', 'CONNERY', 'BLUE', 'JUNGLE', 'SNOW')

# Tolerances and thresholds of the derivation; each is a fraction of a
# measured extent or a count, never a coordinate of a particular item.
COINCIDE = 1.0          # item units: a triangle lies on another's centroid
COVER = 0.95            # of an original group's triangles on one texture
WRIST_BAND = 0.10       # of the cuffs' length: the wrist end's depth
ROLLED = 0.20           # of the cuffs' length: a sleeve begun that far up
SLEEVE_SHARE = 0.90     # of a group's triangles beyond the cuffs' wrist end
SLEEVE_REACH = 0.05     # of the cuffs' length: slack before the wrist end
RADIAL = 2.0            # times the cuffs' widest radius
LIGHT, DARK = 0.20, 0.05    # white share of the wrist end
MARGIN = 1.5            # colour: second-nearest / nearest


class WatchHandError(Exception):
    """The game's model or the item says something this reader refuses."""


# ---- the game's model -------------------------------------------------------

def model_header(name, root=ROOT):
    """(numSwitches, numMatrices, numTextures) from the decomp's header for
    `name` (assets/obseg/chr/<name>/modelFileHeader.inc.c)."""
    rel = 'assets/obseg/chr/%s/modelFileHeader.inc.c' % name
    text = stagechars._read(root, rel)
    m = re.search(r'MODELFILEHEADER\(\s*\w+\s*,(.*)\)\s*$', text, re.M)
    if not m:
        raise WatchHandError('%s: no MODELFILEHEADER' % rel)
    a = stagechars._split_top(m.group(1))
    if len(a) != 8:
        raise WatchHandError('%s: MODELFILEHEADER has %d fields' % (rel, len(a) + 1))
    return int(a[3], 0), int(a[4], 0), int(a[7], 0)


def matrix_names(name, root=ROOT):
    """The model's joint names in MATRIX order - the decomp keeps them beside
    the skeleton (assets/embedded/skeletons/<name>.inc.c, <name>_JointNames),
    'SKEL_' included."""
    rel = 'assets/embedded/skeletons/%s.inc.c' % name
    text = stagechars._read(root, rel)
    m = re.search(r'%s_JointNames\s*\[\s*\]\s*=\s*\{(.*?)\}' % re.escape(name), text, re.S)
    if not m:
        raise WatchHandError('%s: no joint-name table' % rel)
    names = re.findall(r'"([A-Za-z0-9_]+)"', m.group(1))
    if not names or len(set(names)) != len(names):
        raise WatchHandError('%s: the joint names are not distinct' % rel)
    return names


def rom_file(rom_path, rel, root=ROOT):
    """A file of the ROM by its path in the decomp's committed file list
    (scripts/filelist.u.csv), inflated when it is Rare's 1172 stream."""
    off = ln = None
    with open(os.path.join(root, 'scripts', 'filelist.u.csv'), encoding='utf-8') as f:
        for line in f:
            p = line.strip().split(',')
            if len(p) >= 3 and p[2] == rel:
                off, ln = int(p[0]), int(p[1])
                break
    if off is None:
        raise WatchHandError('%s is not in the file list' % rel)
    with open(rom_path, 'rb') as f:
        f.seek(off)
        blob = f.read(ln)
    if len(blob) != ln:
        raise WatchHandError('the ROM is shorter than the file list says')
    if blob[:2] == b'\x11\x72':
        try:
            return zlib.decompressobj(-15).decompress(blob[2:])
        except zlib.error as exc:
            raise WatchHandError('%s does not inflate: %s' % (rel, exc))
    return blob


def read_game_arm(data, nswitch, ntex):
    """Parse a character model file (pointers relative to 0x05000000):

        dict(textures=[(id, w, h)], switches=[offset or 0],
             nodes={matrix: model-space origin},
             cuffs={switch index: [(texture id, [(p0, p1, p2)]), ...]},
             common={texture ids drawn outside every switch})

    Every DL's triangles are in MODEL space: the vertex plus the origins of
    every GROUP / GROUPSIMPLE above it (the runtime's model_origin)."""
    def u32(o):
        return struct.unpack_from('>I', data, o)[0]

    def fo(a):
        return 0 if a == 0 else (a - MODEL_BASE if a >= MODEL_BASE else a)

    if len(data) < 4 * nswitch + 12 * ntex + 24:
        raise WatchHandError('the model file is shorter than its tables')
    switches = [fo(u32(4 * i)) for i in range(nswitch)]
    textures = []
    for i in range(ntex):
        o = 4 * nswitch + 12 * i
        textures.append((u32(o), data[o + 4], data[o + 5]))
    by_id = {t[0]: t for t in textures}
    root = 4 * nswitch + 12 * ntex
    nodes, cuffs, common = {}, {}, set()

    def walk_dl(start, origin, runs, batch):
        tex, o = None, start
        for _ in range(8192):
            if o + 8 > len(data):
                raise WatchHandError('a display list runs off the file')
            w0, w1 = struct.unpack_from('>II', data, o)
            o += 8
            op = w0 >> 24
            if op == 0xB8:
                return
            if op == 0xC0:                  # the game's texture: id in the low 12 bits
                tex = w1 & 0xFFF
            elif op == 0x04:                # G_VTX: n-1 in 20..23, v0 in 16..19
                n = ((w0 >> 20) & 0xF) + 1
                v0 = (w0 >> 16) & 0xF
                a = fo(w1)
                if a + 16 * n > len(data):
                    raise WatchHandError('a vertex load runs off the file')
                for i in range(n):
                    x, y, z = struct.unpack_from('>hhh', data, a + 16 * i)
                    batch[v0 + i] = (x + origin[0], y + origin[1], z + origin[2])
            elif op in (0xB1, 0xBF):        # GE_TRI4 / G_TRI1
                if op == 0xB1:
                    tris = []
                    for i in range(4):
                        x = (w1 >> (8 * i)) & 0xF
                        y = (w1 >> (8 * i + 4)) & 0xF
                        z = (w0 >> (4 * i)) & 0xF
                        if x | y | z:
                            tris.append((x, y, z))
                else:
                    tris = [(((w1 >> 16) & 0xFF) // 10, ((w1 >> 8) & 0xFF) // 10,
                             (w1 & 0xFF) // 10)]
                for t in tris:
                    if any(k not in batch for k in t):
                        raise WatchHandError('a triangle before its vertices')
                    if not runs or runs[-1][0] != tex:
                        runs.append((tex, []))
                    runs[-1][1].append(tuple(batch[k] for k in t))
            elif op == 0x06:
                raise WatchHandError('a nested display list; not read')
        raise WatchHandError('a display list without its end')

    def walk(o, origin, switch, depth):
        guard = 0
        while o and guard < 4096:
            guard += 1
            if o + 24 > len(data) or depth > 64:
                raise WatchHandError('a node outside the file')
            opc = struct.unpack_from('>H', data, o)[0] & 0xFF
            d = fo(u32(o + 4))
            here = origin
            sw = switch
            if o in switches[CUFF_FIRST:CUFF_FIRST + len(CUFFS)]:
                sw = switches.index(o)
            if opc in (OP_GROUP, OP_GROUPSIMPLE) and d:
                org = struct.unpack_from('>fff', data, d)
                here = (origin[0] + org[0], origin[1] + org[1], origin[2] + org[2])
                mtx = struct.unpack_from('>h', data, d + 14)[0] if opc == OP_GROUP \
                    else struct.unpack_from('>h', data, d + 12)[0]
                if mtx >= 0:
                    nodes[mtx] = here
            if opc == OP_DL and d:
                runs, batch = [], {}
                for dl in (fo(u32(d)), fo(u32(d + 4))):
                    if dl:
                        walk_dl(dl, here, runs, batch)
                if sw is None:
                    common.update(t for t, _ in runs)
                else:
                    cuffs.setdefault(sw, []).extend(runs)
            child = fo(u32(o + 20))
            if child:
                walk(child, here, sw, depth + 1)
            o = fo(u32(o + 12))

    walk(root, (0.0, 0.0, 0.0), None, 0)
    for s, runs in cuffs.items():
        for t, _ in runs:
            if t not in by_id:
                raise WatchHandError('cuff %d draws texture %#x, which the model does not list'
                                     % (s, t))
    return dict(textures=textures, switches=switches, nodes=nodes, cuffs=cuffs,
                common=common)


# ---- the relation -----------------------------------------------------------

def turn(v, k):
    """v turned k quarter turns about z (counter-clockwise): the runtime's
    Rz(rot) (src/gfx/sl_gfx_charskin.c, sl_chr_joint_matrices)."""
    x, y, z = v
    for _ in range(k & 3):
        x, y = -y, x
    return (x, y, z)


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _len(v):
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def relate(joints, carried, names, nodes):
    """Relate the item's space to the model's by the joints both name.

    joints   the item's [dict(name, bind)]; carried: the joint indices that
             carry a vertex; names: the model's joint names in matrix order;
             nodes: {matrix: model-space origin}.

    Every carried joint must be a positioned node of the model by name.
    Returns dict(anchor, scale, turn, pairs={item joint: matrix}); with game
    offset g and item offset a from the anchor, a = scale * turn(g, -turn) -
    the runtime's rot is `turn`."""
    pairs = {}
    for j in sorted(carried):
        nm = joints[j]['name']
        if nm not in names or names.index(nm) not in nodes:
            raise WatchHandError('item joint %s carries vertices and is no part the model '
                                 'positions' % nm)
        pairs[j] = names.index(nm)
    if len(pairs) < 2:
        raise WatchHandError('fewer than two positioned joints to relate the spaces by')
    anchor = min(pairs)
    sa = sg = 0.0
    offs = []
    for j, m in pairs.items():
        if j == anchor:
            continue
        a = _sub(joints[j]['bind'], joints[anchor]['bind'])
        g = _sub(nodes[m], nodes[pairs[anchor]])
        offs.append((a, g))
        sa += _len(a)
        sg += _len(g)
    if not (sa > 0.0 and sg > 0.0):
        raise WatchHandError('the positioned joints do not spread')
    s = sa / sg
    fits = []
    for k in range(4):
        worst = max(_len(_sub(turn(a, k), (g[0] * s, g[1] * s, g[2] * s))) / max(_len(g) * s, 1e-6)
                    for a, g in offs)
        fits.append((worst, k))
    fits.sort()
    if fits[0][0] > 0.02:
        raise WatchHandError('no quarter turn about z relates the item to the model '
                             '(best misfit %.3f)' % fits[0][0])
    return dict(anchor=anchor, scale=s, turn=fits[0][1], pairs=pairs,
                origin_item=joints[anchor]['bind'], origin_game=nodes[pairs[anchor]])


def to_item(rel, p):
    """A model-space point into the item's space."""
    g = _sub(p, rel['origin_game'])
    a = turn((g[0] * rel['scale'], g[1] * rel['scale'], g[2] * rel['scale']), -rel['turn'])
    o = rel['origin_item']
    return (o[0] + a[0], o[1] + a[1], o[2] + a[2])


# ---- the item's image groups -----------------------------------------------

def image_groups(item):
    """{image: [(p0, p1, p2, uv0, uv1, uv2)]} - an item's triangles by image,
    positions in item space (xblachar.read_character's result)."""
    V = item['vertices']
    out = {}
    for d in item['draws']:
        ix = d['indices']
        for t in range(0, len(ix), 3):
            vs = [V[ix[t + k]] for k in range(3)]
            out.setdefault(d['image'], []).append(
                tuple(v[:3] for v in vs) + tuple((v[3], v[4]) for v in vs))
    return out


def _centroid(tri):
    return tuple((tri[0][k] + tri[1][k] + tri[2][k]) / 3.0 for k in range(3))


def _grid(points, cell):
    g = {}
    for i, p in enumerate(points):
        g.setdefault(tuple(int(math.floor(c / cell)) for c in p), []).append(i)
    return g


def _near(grid, points, p, cell, tol):
    base = tuple(int(math.floor(c / cell)) for c in p)
    got = []
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for i in grid.get((base[0] + dx, base[1] + dy, base[2] + dz), ()):
                    if _len(_sub(points[i], p)) <= tol:
                        got.append(i)
    return got


def identify_original(groups, widths, arm, rel):
    """{image: game texture id} for every ORIGINAL-generation image group
    that lies on exactly one of the cuffs' textures: COVER of its triangles
    each on the centroid of a cuff triangle drawn with a texture of the
    group image's width, and those textures all the same one."""
    cents, texs = [], []
    for s, runs in arm['cuffs'].items():
        for t, tris in runs:
            if t in arm['common']:
                continue
            for tri in tris:
                cents.append(_centroid([to_item(rel, p) for p in tri]))
                texs.append(t)
    width = {t[0]: t[1] for t in arm['textures']}
    grid = _grid(cents, 4.0)
    out = {}
    for img, tris in groups.items():
        hit, ids = 0, set()
        for tri in tris:
            got = {texs[i] for i in _near(grid, cents, _centroid(tri[:3]), 4.0, COINCIDE)
                   if width[texs[i]] == widths[img]}
            if got:
                hit += 1
                ids |= got
        if tris and hit >= COVER * len(tris) and len(ids) == 1:
            out[img] = ids.pop()
    return out


# ---- the features -----------------------------------------------------------

class Frame:
    """The forearm frame of the game's cuffs in item space: the axis from the
    relation's anchor (the wrist) toward the cuffs' centroid, where the cuffs
    begin and end along it, and their widest radius."""

    def __init__(self, arm, rel):
        pts = [to_item(rel, p) for s, runs in arm['cuffs'].items() for t, tris in runs
               if t not in arm['common'] for tri in tris for p in tri]
        if not pts:
            raise WatchHandError('the model carries no cuff geometry')
        o = rel['origin_item']
        c = tuple(sum(p[k] for p in pts) / len(pts) for k in range(3))
        ax = _sub(c, o)
        n = _len(ax)
        if not n > 0.0:
            raise WatchHandError('the cuffs sit on the wrist joint')
        self.o, self.u = o, (ax[0] / n, ax[1] / n, ax[2] / n)
        ts = [self.t(p) for p in pts]
        self.t0, self.t1 = min(ts), max(ts)
        self.length = self.t1 - self.t0
        self.radius = max(self.r(p) for p in pts)
        if not self.length > 0.0:
            raise WatchHandError('the cuffs have no length along the forearm')

    def t(self, p):
        d = _sub(p, self.o)
        return d[0] * self.u[0] + d[1] * self.u[1] + d[2] * self.u[2]

    def r(self, p):
        d = _sub(p, self.o)
        t = self.t(p)
        return _len(_sub(d, (self.u[0] * t, self.u[1] * t, self.u[2] * t)))


def sleeve_groups(groups, frame):
    """The REMASTERED image groups that are alternative sleeves: SLEEVE_SHARE
    of the triangles lie at or beyond the cuffs' wrist end (SLEEVE_REACH of
    their length before it) and within RADIAL times their widest radius."""
    out = []
    for img, tris in groups.items():
        ok = 0
        for tri in tris:
            c = _centroid(tri[:3])
            if frame.t(c) >= frame.t0 - SLEEVE_REACH * frame.length \
                    and frame.r(c) <= RADIAL * frame.radius:
                ok += 1
        if tris and ok >= SLEEVE_SHARE * len(tris):
            out.append(img)
    return sorted(out)


def _texel(tex, u, v):
    w, h = tex['w'], tex['h']
    x = int((u % 1.0) * w) % w
    y = int((v % 1.0) * h) % h
    o = 4 * (y * w + x)
    return tex['rgba'][o], tex['rgba'][o + 1], tex['rgba'][o + 2]


def describe(tris_by_image, textures, frame):
    """The three features of one sleeve or cuff, from its triangles (by
    image) and their texels, sampled at 15 barycentric points a triangle:

        start   where it begins along the forearm, from the cuffs' wrist end,
                as a fraction of the cuffs' length
        tone    the white share (luma > 150) of the wrist end's texels - the
                triangles within WRIST_BAND of the start
        colour  the mean colour of the rest (the sleeve's body)"""
    ts = [frame.t(p) for tris in tris_by_image.values() for tri in tris for p in tri[:3]]
    if not ts:
        raise WatchHandError('an empty sleeve')
    start = min(ts)
    band = start + WRIST_BAND * frame.length
    wrist, body = [], []
    for img, tris in tris_by_image.items():
        tex = textures[img]
        for tri in tris:
            dest = wrist if frame.t(_centroid(tri[:3])) <= band else body
            for i in range(5):
                for j in range(5 - i):
                    k = 4 - i - j
                    u = (tri[3][0] * i + tri[4][0] * j + tri[5][0] * k) / 4.0
                    v = (tri[3][1] * i + tri[4][1] * j + tri[5][1] * k) / 4.0
                    dest.append(_texel(tex, u, v))
    # a piece with no triangle in its wrist band has no tone (it matches
    # nothing); one with no body is coloured by its wrist end
    white = (sum(1 for p in wrist if 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] > 150)
             / len(wrist)) if wrist else None
    body = body or wrist
    colour = tuple(sum(p[k] for p in body) / len(body) for k in range(3))
    return dict(start=(start - frame.t0) / frame.length, tone=white, colour=colour)


def classes(f):
    """The two class features: 'rolled' or 'full', and 'light', 'dark' or
    None (an undecided wrist end matches nothing)."""
    reach = 'rolled' if f['start'] >= ROLLED else 'full'
    t = f['tone']
    tone = None if t is None else ('light' if t >= LIGHT else ('dark' if t <= DARK else None))
    return reach, tone


def colour_distance(a, b):
    return sum(abs(a[k] - b[k]) for k in range(3))


def pair(cuffs, sleeves):
    """{cuff switch: sleeve image} and the evidence, one line per cuff.

    cuffs    {switch: features or None (undetermined)}
    sleeves  {image: features}

    A cuff's candidates are the sleeves that agree with it on BOTH class
    features; a sleeve's are the determined cuffs that agree with it. The
    pair stands when each is the other's only candidate, or the other's
    nearest candidate by colour at MARGIN or better."""
    def pick(me, cands, feats_of):
        if not cands:
            return None, 'no candidate'
        if len(cands) == 1:
            return cands[0], 'the only candidate'
        ds = sorted((colour_distance(me['colour'], feats_of[c]['colour']), c) for c in cands)
        if ds[0][0] * MARGIN > ds[1][0]:
            return None, 'colour %.0f vs %.0f is no clear margin' % (ds[0][0], ds[1][0])
        return ds[0][1], 'nearest by colour %.0f vs %.0f' % (ds[0][0], ds[1][0])

    det = {s: f for s, f in cuffs.items() if f is not None}
    out, why = {}, {}
    for s in sorted(cuffs):
        f = cuffs[s]
        if f is None:
            why[s] = 'undetermined: its textures are not each one original image'
            continue
        cands = [g for g in sleeves if classes(sleeves[g]) == classes(f)
                 and classes(f)[1] is not None]
        g, how = pick(f, cands, sleeves)
        if g is None:
            why[s] = 'unmapped: %s' % how
            continue
        back = [c for c in det if classes(det[c]) == classes(sleeves[g])
                and classes(det[c])[1] is not None]
        c, how2 = pick(sleeves[g], back, det)
        if c != s:
            why[s] = 'unmapped: sleeve %d prefers %s (%s)' % (g, c, how2)
            continue
        out[s] = g
        why[s] = 'sleeve %d: %s, %s; %s; back: %s' % (g, classes(f)[0], classes(f)[1], how, how2)
    return out, why


def derive(new_item, original_item, arm, names):
    """The whole derivation: (relation, {cuff switch: sleeve image},
    [sleeve images], evidence lines). new_item / original_item are
    xblachar.read_character results of the two generations."""
    carried = {jj for e in new_item['skin'] for jj, w in e if w}
    rel = relate(new_item['joints'], carried, names, arm['nodes'])
    frame = Frame(arm, rel)
    ogroups = image_groups(original_item)
    widths = {i: t['w'] for i, t in enumerate(original_item['textures'])}
    ident = identify_original(ogroups, widths, arm, rel)
    cuffs = {}
    for s, runs in sorted(arm['cuffs'].items()):
        own = [t for t, _ in runs if t not in arm['common']]
        imgs = {}
        ok = bool(own)
        for t in set(own):
            got = [i for i, tid in ident.items() if tid == t]
            if len(got) != 1:
                ok = False
                break
            imgs[got[0]] = ogroups[got[0]]
        cuffs[s] = describe(imgs, original_item['textures'], frame) if ok else None
    ngroups = image_groups(new_item)
    sl = sleeve_groups(ngroups, frame)
    sleeves = {g: describe({g: ngroups[g]}, new_item['textures'], frame) for g in sl}
    mapping, why = pair(cuffs, sleeves)
    def feats(f):
        return 'start %.2f tone %s colour (%.0f,%.0f,%.0f)' % (
            f['start'], '-' if f['tone'] is None else '%.2f' % f['tone'],
            f['colour'][0], f['colour'][1], f['colour'][2])
    lines = ['relation: scale %.4f, %d quarter turn(s) about z; cuffs %.0f..%.0f along the '
             'forearm, radius %.0f' % (rel['scale'], rel['turn'], frame.t0, frame.t1, frame.radius)]
    for s in sorted(cuffs):
        f = cuffs[s]
        nm = CUFFS[s - CUFF_FIRST] if CUFF_FIRST <= s < CUFF_FIRST + len(CUFFS) else str(s)
        lines.append('cuff Switches[%d] %-7s %s -> %s' % (
            s, nm, 'undetermined' if f is None else feats(f), why[s]))
    for g in sl:
        lines.append('sleeve image %d: %s%s' % (
            g, feats(sleeves[g]), '' if g in mapping.values() else ' - serves no cuff'))
    return rel, mapping, sl, lines
