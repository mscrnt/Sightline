"""Carry the remaster's own texture coordinates onto the game's room triangles.

A direct XBLA entry replaces a native image with the remaster's image of the
same surface, and the native triangles go on sampling it with the CARTRIDGE'S
coordinates. Where the cartridge's coordinates bend from one triangle to the
next - a floor whose tile changes scale and direction across an edge, hidden
on the cartridge because its tile is noise - any structured image bends with
them, and no single image can be right on both sides of such an edge
(docs/texture-packs.md, "Re-tiled pairs"). The remaster draws the same
surfaces with coordinates of its own. This module carries those onto the
native triangles, as data the renderer substitutes per triangle.

THE INPUTS ARE THE LEVEL'S OWN.

- The ROM's background file for the stage (the room table, each room's point
  table and its primary and secondary lists - 'Background File Data/
  background data.txt'; the room record's field ORDER is the decomp's
  `bg_room_data`, three scoped offsets then the position, which is what the
  file holds - the note's table lists the position first). Each triangle is
  named by its room and the three point-table entries its corners were loaded
  from: the vertex command's operand is a segment-14 offset into the room's
  point table (bg.c binds SPSEGMENT_BG_VTX to the room's vertices), 16 bytes
  a point, so entry = offset / 16 + the vertex's place in the load. The image
  is the C0 command's id ('Texture application (Rooms).txt': low 12 bits;
  the clamp and mirror flags are bits 20-23 of the first word).
- The remaster's own background for the stage (`new/background/<stage>`),
  read by tools/worldasset/xblaworld.py. It is authored in the room
  coordinates times one scale per stage, which is FITTED here from the two
  vertex sets (and refused when they do not agree) rather than carried as a
  constant.

THE RULE, for each direct entry of that bundle (no `sample` block: the pack
image IS the remastered image, so the remaster's coordinates address it as
they are):

1. Each native triangle drawing the entry's id is looked for on the
   remastered surface drawing the entry's image: a remastered triangle in the
   same plane (normals within 0.999, 2 units apart) containing the triangle's
   centroid - or, failing that, one containing most of six interior sample
   points, or failing that the nearest such triangle within 96 units (the
   remaster's own background has seams where two of its triangles do not
   meet exactly). Its texture-coordinate map, which is affine over the
   triangle, is evaluated at the native corners: s = 32 w u, t = 32 h (1 - v)
   for an image of w x h texels, the same relation that makes the original
   generation's coordinates equal the cartridge's.
2. A triangle is REFUSED when another remastered triangle under it gives a
   different coordinate there (beyond whole repeats): the remaster changes
   its own mapping inside the triangle, and no single affine carry is right.
3. An ID is carried only when the cartridge's coordinates bend and the
   carried ones do not: over the coplanar edges two of its triangles share,
   at least 4 change the coordinate gradient by more than 10% under the
   cartridge's coordinates, and at most half as many under the carried ones.
   An id whose cartridge coordinates already follow one relation keeps them.
4. Of a carried id, only triangles whose carried coordinates differ from the
   cartridge's by more than whole repeats get a row; each row is shifted by
   an even number of repeats (so a mirrored axis keeps its parity) to sit
   nearest the cartridge's own values, and refused if it leaves the vertex
   format's 16-bit range. An id whose tile clamps is never carried.

THE TABLE (SLUV, little-endian), one per stage, at <set>\\uv\\<LEVELID>.sluv:

    +0  'S' 'L' 'U' 'V'
    +4  u32 version      1
    +8  u32 stage        the game's LEVELID
    +12 u32 count
    +16 count x 24 bytes: u16 room, u16 id, u16 p0, u16 p1, u16 p2, u16 0,
                          s16 s0, t0, s1, t1, s2, t2
        sorted by (room, p0, p1, p2), p0 < p1 < p2, the coordinates in
        that corner order.

Coordinates and indices only - nothing derived from the remaster's pixels,
and the file lives with the rest of the generated pack (gitignored,
never committed or packaged).
"""
import collections
import math
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
# The world reader lives beside this package. Appended, never prepended: both
# directories hold a prepare.py, and this package's own modules come first.
if os.path.join(ROOT, 'tools', 'worldasset') not in sys.path:
    sys.path.append(os.path.join(ROOT, 'tools', 'worldasset'))

MAGIC = b'SLUV'
VERSION = 1
HEADER = 16
ROW = 24
ROW_FMT = '<HHHHHHhhhhhh'
MAX_ROWS = 1 << 20

PLANE_DOT = 0.999
PLANE_DIST = 2.0
NEAR_LIMIT = 96.0
KINK = 0.10
KINK_MIN = 4

SEG_BG_VTX = 0x0E


class UvCarryError(Exception):
    pass


# ------------------------------------------------------------------ ROM ----

def inflate_1172(blob):
    """Rare's 1172 stream: two magic bytes, then a raw deflate stream."""
    if blob[:2] != b'\x11\x72':
        return None
    try:
        return zlib.decompressobj(-15).decompress(blob[2:])
    except zlib.error:
        return None


def rom_file(rom, rel, root):
    """A ROM file by its path in scripts/filelist.u.csv (inflated when 1172)."""
    off = ln = None
    with open(os.path.join(root, 'scripts', 'filelist.u.csv'), encoding='utf-8') as f:
        for line in f:
            p = line.strip().split(',')
            if len(p) >= 3 and p[2] == rel:
                off, ln = int(p[0]), int(p[1])
                break
    if off is None:
        raise UvCarryError('%s is not in the file list' % rel)
    with open(rom, 'rb') as f:
        f.seek(off)
        blob = f.read(ln)
    if len(blob) != ln:
        raise UvCarryError('the ROM is shorter than the file list says')
    out = inflate_1172(blob)
    return blob if out is None else out


def stage_of(root, stage):
    """(LEVELID, ROM path of the stage's background file) for a launcher stage
    name: the launcher's level table (tools/trace/sltrace/levelboot.py) for the
    number, the game's own levelinfotable (src/game/bg.c) for the file."""
    with open(os.path.join(root, 'tools', 'trace', 'sltrace', 'levelboot.py'), encoding='utf-8') as f:
        text = f.read()
    m = re.search(r'LEVEL_IDS\s*=\s*\{(.*?)\}', text, re.S)
    ids = {k: int(v) for k, v in re.findall(r'"([a-z0-9_]+)"\s*:\s*(\d+)', m.group(1))} if m else {}
    if stage not in ids:
        raise UvCarryError('no launcher stage named %r' % stage)
    lid = ids[stage]
    with open(os.path.join(root, 'src', 'bondconstants.h'), encoding='utf-8') as f:
        hdr = f.read()
    m = re.search(r'typedef\s+enum\s+LEVELID\s*\{(.*?)\}', hdr, re.S)
    if not m:
        raise UvCarryError('src/bondconstants.h has no LEVELID enum')
    body = re.sub(r'//[^\n]*', '', m.group(1))
    names = {}
    nxt = 0
    for ent in body.split(','):
        ent = ent.strip()
        if not ent:
            continue
        if '=' in ent:
            nm, ex = [x.strip() for x in ent.split('=', 1)]
            if not re.fullmatch(r'-?\d+', ex):
                break                     # derived values follow; not needed
            v = int(ex)
        else:
            nm, v = ent, nxt
        names[v] = nm
        nxt = v + 1
    enum = names.get(lid)
    if enum is None:
        raise UvCarryError('LEVELID %d has no enumerator' % lid)
    with open(os.path.join(root, 'src', 'game', 'bg.c'), encoding='utf-8') as f:
        bgc = f.read()
    m = re.search(r'\{\s*%s\s*,\s*"bg/([A-Za-z0-9_]+)\.seg"' % re.escape(enum), bgc)
    if not m:
        raise UvCarryError('%s has no background in levelinfotable' % enum)
    return lid, 'assets/obseg/bg/%s.bin' % m.group(1)


def read_bg(bg):
    """The background file -> [dict(room, pos, points, tris)], rooms 1..n.

    points: [(x, y, z, s, t)] of the room's point table; tris: [dict(idx,
    tex, wrap)] - the point-table entries of the three corners, the C0 image
    id and its wrap flags (bit 3 mirror S, 2 clamp S, 1 mirror T, 0 clamp T)."""
    if len(bg) < 16:
        raise UvCarryError('the background file is too short')
    u32 = lambda o: struct.unpack_from('>I', bg, o)[0]
    table = u32(4) & 0xffffff
    ents = []
    i = 0
    while True:
        o = table + i * 0x18
        if o + 0x18 > len(bg):
            raise UvCarryError('the room table runs off the file')
        pt, pri, sec = u32(o) & 0xffffff, u32(o + 4) & 0xffffff, u32(o + 8) & 0xffffff
        pos = struct.unpack_from('>fff', bg, o + 12)
        ents.append((pt, pri, sec, pos))
        # Room 0 is the empty record; the rooms' point tables come first and
        # end where room 1's primary list begins - the record whose point
        # offset reaches it is the closing one.
        if i >= 2 and pt + 0x10 >= ents[1][1]:
            break
        i += 1
        if i > 1024:
            raise UvCarryError('the room table does not end')
    rooms = []
    for r in range(1, len(ents) - 1):
        pt, pri, sec, pos = ents[r]
        if not pt:
            continue                           # a room record with no geometry
        pts = inflate_1172(bg[pt:])            # the stream carries its own end
        if pts is None:
            raise UvCarryError('room %d: the point table does not inflate' % r)
        points = [struct.unpack_from('>hhhHhh', pts, k * 16) for k in range(len(pts) // 16)]
        points = [(p[0], p[1], p[2], p[4], p[5]) for p in points]
        tris = []
        for start in (pri, sec):
            if not start:
                continue
            dl = inflate_1172(bg[start:])
            if dl is None:
                raise UvCarryError('room %d: a list does not inflate' % r)
            tris.extend(walk_list(dl, len(points)))
        rooms.append(dict(room=r, pos=pos, points=points, tris=tris))
    return rooms


def walk_list(dl, npoints):
    slots = [None] * 16
    tex = None
    wrap = 0
    out = []
    for o in range(0, len(dl) - 7, 8):
        w0, w1 = struct.unpack_from('>II', dl, o)
        op = w0 >> 24
        if op == 0xB8:
            break
        if op == 0xC0:
            tex, wrap = w1 & 0xfff, (w0 >> 20) & 0xf
        elif op == 0x04:
            n = ((w0 >> 20) & 0xf) + 1
            v0 = (w0 >> 16) & 0xf
            if (w1 >> 24) != SEG_BG_VTX or v0 + n > 16:
                slots = [None] * 16
                continue
            base = (w1 & 0xffffff) // 16
            for k in range(n):
                slots[v0 + k] = base + k if base + k < npoints else None
        elif op in (0xB1, 0xBF):
            if op == 0xB1:
                tl = []
                for i in range(4):
                    x, y, z = (w1 >> (8 * i)) & 0xf, (w1 >> (8 * i + 4)) & 0xf, (w0 >> (4 * i)) & 0xf
                    if x | y | z:
                        tl.append((x, y, z))
            else:
                tl = [(((w1 >> 16) & 0xff) // 10, ((w1 >> 8) & 0xff) // 10, (w1 & 0xff) // 10)]
            for a, b, c in tl:
                idx = (slots[a], slots[b], slots[c])
                if tex is None or None in idx or len(set(idx)) != 3:
                    continue
                out.append(dict(idx=idx, tex=tex, wrap=wrap))
    return out


# ---------------------------------------------------------------- geometry --

def _sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def _scale(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def _add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def _norm(a): return math.sqrt(_dot(a, a))


def normal(P):
    n = _cross(_sub(P[1], P[0]), _sub(P[2], P[0]))
    a = _norm(n)
    return None if a < 1e-6 else _scale(n, 1.0 / a)


def bary(P, p):
    e1, e2 = _sub(P[1], P[0]), _sub(P[2], P[0])
    n = _cross(e1, e2)
    d = _dot(n, n)
    if d < 1e-9:
        return None
    w = _sub(p, P[0])
    b1 = _dot(_cross(w, e2), n) / d
    b2 = _dot(_cross(e1, w), n) / d
    return (1.0 - b1 - b2, b1, b2)


def uv_at(T, p):
    b = bary(T['P'], p)
    return (sum(b[k] * T['UV'][k][0] for k in range(3)), sum(b[k] * T['UV'][k][1] for k in range(3)))


def gradient(P, ST):
    """d(s, t)/d(world) as two 3-vectors, or None for a degenerate triangle."""
    n = normal(P)
    if n is None:
        return None
    e1, e2 = _sub(P[1], P[0]), _sub(P[2], P[0])
    ux = _scale(e1, 1.0 / _norm(e1))
    uy = _cross(n, ux)
    m = ((_dot(e1, ux), _dot(e1, uy)), (_dot(e2, ux), _dot(e2, uy)))
    det = m[0][0] * m[1][1] - m[0][1] * m[1][0]
    if abs(det) < 1e-9:
        return None
    out = []
    for c in range(2):
        d1, d2 = ST[1][c] - ST[0][c], ST[2][c] - ST[0][c]
        gx = (d1 * m[1][1] - d2 * m[0][1]) / det
        gy = (m[0][0] * d2 - m[1][0] * d1) / det
        out.append(_add(_scale(ux, gx), _scale(uy, gy)))
    return out


def bends(Pa, Sa, Pb, Sb):
    ga, gb = gradient(Pa, Sa), gradient(Pb, Sb)
    if ga is None or gb is None:
        return False
    num = math.sqrt(sum(_dot(_sub(ga[c], gb[c]), _sub(ga[c], gb[c])) for c in range(2)))
    den = math.sqrt(sum(_dot(ga[c], ga[c]) for c in range(2)))
    return num > KINK * max(den, 1e-9)


def fit_scale(native_pts, hd_pts, cell=8.0):
    """The one scale k with hd = k * native, by least squares over the pairs
    of vertices that coincide once scaled. Started from the ratio of the two
    sets' horizontal extents; refused when the axes disagree or too few
    vertices pair."""
    def ext(pts, a):
        v = [p[a] for p in pts]
        return max(v) - min(v)
    kx = ext(hd_pts, 0) / max(ext(native_pts, 0), 1e-9)
    kz = ext(hd_pts, 2) / max(ext(native_pts, 2), 1e-9)
    if abs(kx - kz) > 0.005 * kx:
        raise UvCarryError('the two backgrounds disagree on their scale (%.5f, %.5f)' % (kx, kz))
    k = (kx + kz) / 2.0
    grid = collections.defaultdict(list)
    for p in hd_pts:
        grid[(int(p[0] // cell), int(p[1] // cell), int(p[2] // cell))].append(p)
    for _ in range(2):
        num = den = 0.0
        n = 0
        for q in native_pts:
            s = _scale(q, k)
            g = (int(s[0] // cell), int(s[1] // cell), int(s[2] // cell))
            best = None
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for p in grid.get((g[0] + dx, g[1] + dy, g[2] + dz), ()):
                            d = _norm(_sub(p, s))
                            if d < 3.0 and (best is None or d < best[0]):
                                best = (d, p)
            if best is not None:
                num += _dot(q, best[1]); den += _dot(q, q); n += 1
        if n < max(16, len(native_pts) // 10):
            raise UvCarryError('only %d of %d background vertices pair' % (n, len(native_pts)))
        k = num / den
    return k


class Surface(object):
    """The remastered triangles of one image, bucketed for the plane search."""

    def __init__(self, tris, cell=128.0):
        self.tris = [T for T in tris if normal(T['P']) is not None]
        self.cell = cell
        self.grid = collections.defaultdict(list)
        for i, T in enumerate(self.tris):
            lo = [min(p[a] for p in T['P']) for a in range(3)]
            hi = [max(p[a] for p in T['P']) for a in range(3)]
            l = [int(math.floor(v / cell)) for v in lo]
            h = [int(math.floor(v / cell)) for v in hi]
            if max(h[a] - l[a] for a in range(3)) > 64:
                continue                       # a sliver across the level
            for x in range(l[0], h[0] + 1):
                for y in range(l[1], h[1] + 1):
                    for z in range(l[2], h[2] + 1):
                        self.grid[(x, y, z)].append(i)

    def coplanar(self, P):
        n = normal(P)
        if n is None:
            return n, []
        c = _scale(_add(_add(P[0], P[1]), P[2]), 1.0 / 3.0)
        cand = set()
        for p in list(P) + [c]:
            g = [int(math.floor(v / self.cell)) for v in p]
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        cand.update(self.grid.get((g[0] + dx, g[1] + dy, g[2] + dz), ()))
        out = []
        for i in sorted(cand):
            T = self.tris[i]
            m = normal(T['P'])
            if abs(_dot(m, n)) < PLANE_DOT or abs(_dot(_sub(c, T['P'][0]), m)) > PLANE_DIST:
                continue
            out.append(i)
        return n, out

    def carry(self, P):
        """-> (uv at the three corners, status) or None. status 'one', 'part',
        'near', or 'mixed' (refused)."""
        n, cop = self.coplanar(P)
        if not cop:
            return None
        c = _scale(_add(_add(P[0], P[1]), P[2]), 1.0 / 3.0)
        samples = [c] + [_add(c, _scale(_sub(p, c), 0.8)) for p in P] + \
                  [_add(c, _scale(_sub(_scale(_add(P[k], P[(k + 1) % 3]), 0.5), c), 0.8)) for k in range(3)]
        hits = []
        for s in samples:
            best = None
            for i in cop:
                b = bary(self.tris[i]['P'], s)
                if b is None or min(b) < -1e-3:
                    continue
                if best is None or min(b) > best[0]:
                    best = (min(b), i)
            hits.append(best[1] if best else None)
        if hits[0] is not None:
            i0, status = hits[0], 'one'
        else:
            found = [h for h in hits if h is not None]
            if found:
                i0, status = collections.Counter(found).most_common(1)[0][0], 'part'
            else:
                best = None
                for i in cop:
                    Q = self.tris[i]['P']
                    for k in range(3):
                        for j in range(9):
                            q = _add(Q[k], _scale(_sub(Q[(k + 1) % 3], Q[k]), j / 8.0))
                            d = _norm(_sub(q, c))
                            if best is None or d < best[0]:
                                best = (d, i)
                if best is None or best[0] > NEAR_LIMIT:
                    return None
                i0, status = best[1], 'near'
        T0 = self.tris[i0]
        for s, h in zip(samples, hits):
            if h is None or h == i0:
                continue
            a, b = uv_at(T0, s), uv_at(self.tris[h], s)
            if any(abs((a[q] - b[q]) - round(a[q] - b[q])) > 0.01 for q in range(2)):
                return None, 'mixed'
        return [uv_at(T0, p) for p in P], status


# ---------------------------------------------------------------- derive ----

def derive(rooms, hd_tris, entries, scale):
    """rooms: read_bg's; hd_tris: [dict(name, P, UV)] in the bundle's frame;
    entries: the direct entries of this bundle as {id: (res, (w, h))}.
    -> (rows, report): rows [(room, id, (p0, p1, p2), ((s, t) x 3))] for the
    carried ids, report {id: dict(...)} for every id looked at."""
    by_name = collections.defaultdict(list)
    for T in hd_tris:
        by_name[T['name']].append(T)
    surfaces = {}
    rows = []
    report = {}
    for tid in sorted(entries):
        res, (w, h) = entries[tid]
        mine = []
        for R in rooms:
            for t in R['tris']:
                if t['tex'] != tid:
                    continue
                P = [_scale(_add(R['points'][i][:3], R['pos']), scale) for i in t['idx']]
                ST = [R['points'][i][3:5] for i in t['idx']]
                mine.append(dict(room=R['room'], idx=t['idx'], wrap=t['wrap'], P=P, ST=ST,
                                 key=[tuple(int(round(v)) for v in _add(R['points'][i][:3], R['pos']))
                                      for i in t['idx']]))
        rep = dict(triangles=len(mine), carried=0, equal=0, refused=0, uncovered=0,
                   bends_native=0, bends_carried=0, edges=0, taken=False, why='')
        report[tid] = rep
        if not mine:
            rep['why'] = 'not drawn by this background'
            continue
        if any(t['wrap'] & 0x5 for t in mine):
            rep['why'] = 'a clamped tile'
            continue
        if res not in surfaces:
            surfaces[res] = Surface(by_name.get(res, []))
        S = surfaces[res]
        for t in mine:
            t['carry'] = None
            r = S.carry(t['P'])
            if r is None:
                rep['uncovered'] += 1
                continue
            uv, status = r
            if uv is None:
                rep['refused'] += 1
                continue
            t['carry'] = [(32.0 * w * u, 32.0 * h * (1.0 - v)) for u, v in uv]
        # bends over coplanar shared native edges, before and after
        edges = collections.defaultdict(list)
        for j, t in enumerate(mine):
            for k in range(3):
                edges[tuple(sorted((t['key'][k], t['key'][(k + 1) % 3])))].append(j)
        for lst in edges.values():
            if len(lst) != 2:
                continue
            a, b = mine[lst[0]], mine[lst[1]]
            na, nb = normal(a['P']), normal(b['P'])
            if na is None or nb is None or abs(_dot(na, nb)) < PLANE_DOT:
                continue
            rep['edges'] += 1
            if bends(a['P'], a['ST'], b['P'], b['ST']):
                rep['bends_native'] += 1
            sa = a['carry'] if a['carry'] is not None else a['ST']
            sb = b['carry'] if b['carry'] is not None else b['ST']
            if bends(a['P'], sa, b['P'], sb):
                rep['bends_carried'] += 1
        if rep['bends_native'] < KINK_MIN:
            rep['why'] = 'its coordinates already follow one relation'
            continue
        if 2 * rep['bends_carried'] > rep['bends_native']:
            rep['why'] = 'the remaster\'s coordinates bend there as well'
            continue
        rep['taken'] = True
        for t in mine:
            if t['carry'] is None:
                continue
            per = (32.0 * w, 32.0 * h)
            d = [((t['carry'][k][0] - t['ST'][k][0]) / per[0], (t['carry'][k][1] - t['ST'][k][1]) / per[1])
                 for k in range(3)]
            if all(abs(d[k][q] - round(d[0][q])) < 0.01 for k in range(3) for q in range(2)):
                rep['equal'] += 1
                continue
            st = []
            for q in range(2):
                mc = sum(t['carry'][k][q] for k in range(3)) / 3.0
                mn = sum(t['ST'][k][q] for k in range(3)) / 3.0
                shift = 2.0 * round((mc - mn) / per[q] / 2.0) * per[q]
                st.append([int(round(t['carry'][k][q] - shift)) for k in range(3)])
            if any(not -32768 <= v <= 32767 for v in st[0] + st[1]):
                rep['refused'] += 1
                continue
            rows.append((t['room'], tid, tuple(t['idx']), tuple((st[0][k], st[1][k]) for k in range(3))))
            rep['carried'] += 1
    return rows, report


# ------------------------------------------------------------------ file ----

def pack(stage, rows):
    """rows -> the SLUV bytes: corners sorted by point entry, rows sorted."""
    out = []
    for room, tid, idx, st in rows:
        order = sorted(range(3), key=lambda k: idx[k])
        p = [idx[k] for k in order]
        s = [st[k] for k in order]
        if len(set(p)) != 3 or not (0 <= room <= 0xffff and 0 <= tid <= 0xffff) or max(p) > 0xffff:
            raise UvCarryError('row out of range: room %d id %d %r' % (room, tid, idx))
        out.append((room, p[0], p[1], p[2], tid, s))
    out.sort()
    for a, b in zip(out, out[1:]):
        if a[:4] == b[:4] and a[4] == b[4]:
            raise UvCarryError('two rows for one triangle: room %d %r' % (a[0], a[1:4]))
    if len(out) > MAX_ROWS:
        raise UvCarryError('too many rows')
    blob = bytearray(MAGIC + struct.pack('<III', VERSION, stage, len(out)))
    for room, p0, p1, p2, tid, s in out:
        blob += struct.pack(ROW_FMT, room, tid, p0, p1, p2, 0,
                            s[0][0], s[0][1], s[1][0], s[1][1], s[2][0], s[2][1])
    return bytes(blob)


def unpack(blob):
    if len(blob) < HEADER or blob[:4] != MAGIC:
        raise UvCarryError('not an SLUV table')
    ver, stage, n = struct.unpack_from('<III', blob, 4)
    if ver != VERSION:
        raise UvCarryError('version %d' % ver)
    if len(blob) != HEADER + n * ROW:
        raise UvCarryError('size does not match the row count')
    rows = []
    for i in range(n):
        v = struct.unpack_from(ROW_FMT, blob, HEADER + i * ROW)
        rows.append((v[0], v[1], (v[2], v[3], v[4]), ((v[6], v[7]), (v[8], v[9]), (v[10], v[11]))))
    return stage, rows


def write(path, stage, rows):
    blob = pack(stage, rows)
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(blob)
    os.replace(tmp, path)
    return len(blob)


# --------------------------------------------------------------- prepare ----

def hd_triangles(level):
    """xblaworld.read_level's level -> [dict(name, P, UV)].

    A draw's `indices` are already the flat triangle list its primitive
    expands into (read_level expands every fan and quad list as it reads
    them), so they are taken three at a time as they stand. Expanding them a
    second time - as a fan again, or a quad list again - tiles triangles that
    are not the remaster's: a hub fan turned about its first triangle's first
    corner, and quads cut across the wrong diagonal. Facility's background
    draws 67 quad lists and 13 fans among its 116 draws: read twice it gave
    19603 triangles where the item has 13913, 13708 of them not the
    remaster's, and 8200 of the remaster's own missing from the surface the
    coordinates are carried from."""
    out = []
    for d in level['draws']:
        p = level['pools'][d['pool']]
        if p['vertices'] is None or d['image'] is None:
            continue
        name = level['textures'][d['image']]['name']
        ind = d['indices']
        for t in range(0, len(ind) - 2, 3):
            vs = [p['vertices'][ind[t + k]] for k in range(3)]
            out.append(dict(name=name, P=[tuple(v[:3]) for v in vs], UV=[tuple(v[3:5]) for v in vs]))
    return out


def prepare_stage(stage, level_path, entries, rom, root, out_dir):
    """Derive and write one stage's table. entries: the stage bundle's direct
    mapping entries. -> (path or None, rows, report, scale)."""
    import xblaworld                                               # noqa: E402
    lid, bgrel = stage_of(root, stage)
    rooms = read_bg(rom_file(rom, bgrel, root))
    level = xblaworld.read_level(level_path)
    hd = hd_triangles(level)
    native_pts = sorted({_add(R['points'][i][:3], R['pos']) for R in rooms
                         for t in R['tris'] for i in t['idx']})
    hd_pts = sorted({p for T in hd for p in T['P']})
    scale = fit_scale(native_pts, hd_pts)
    want = {}
    for e in entries:
        if 'sample' in e:
            continue
        want[int(e['id'], 16)] = (e['res'], tuple(e['n64']))
    rows, report = derive(rooms, hd, want, scale)
    path = os.path.join(out_dir, '%d.sluv' % lid)
    if rows:
        write(path, lid, rows)
    elif os.path.exists(path):
        os.remove(path)
        path = None
    else:
        path = None
    return path, rows, report, scale
