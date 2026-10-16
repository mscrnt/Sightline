"""THE GAME'S OWN TINT WHERE THE REMASTER WHITENED IT (#82).

The remaster converted some of the game's vertex colours to plain white, and
where the game draws an image TINTED by its vertex colour the remaster then
draws (or painted) the image untinted. Silo shows it: the game draws its steel
door's hazard bars as a black-and-white bar image under vertex colour e4e42d
(yellow), and the remaster's door is one image with the bars painted into it
black and white.

This module derives, from the GAME'S OWN DATA in the user's ROM, where the
game tints and by what, and carries that onto the remastered item. Nothing
here names a source image, an item's draw or a coordinate of any particular
model; which items it is applied to is a decision recorded in
docs/divergences.md and listed in prepare.DIVERGENCE_ITEMS.

D-032, A PROP'S TINTED BARS (restore_prop_tint). For a prop model:

  1. the game's model file is read from the ROM (model_triangles): every
     triangle of its display lists, with the texture it is drawn with and its
     three vertex colours. A TINTED triangle is a textured one whose corners
     carry one colour that is not white;
  2. the model's box is related to the item's reference box (the source's
     ORIGINAL generation, the box convert_prop already carries), one scale
     and offset per axis - the relation the runtime itself draws the item by;
  3. each tinted triangle's corners, so placed, are found on the remastered
     item's own triangles (coplanar, containing the corner), and read through
     THEIR texture coordinates into the texels of the image those triangles
     draw. A footprint is taken only where the remastered triangles carry a
     pure white vertex colour - where the remaster kept a colour, there is
     nothing to restore;
  4. the tinted triangles whose texel boxes overlap are one footprint (a
     quad's halves; the front and back faces that read the same texels), and
     a footprint must be an axis-aligned rectangle of texels - its triangles
     cover TINT_COVER of the texel centres in its box - or it is refused;
  5. an edge of a rectangle that is not the image border is moved to the
     boundary within TINT_SNAP texels of it where the image's luminance
     rises most into the rectangle, averaged along the edge (snap_edge: the
     remaster's art is registered to the game's footprint to within that, and
     paints its bars inside a dark frame line), and an edge within TINT_SNAP
     of the border goes to the border;
  6. inside the rectangles the image is multiplied by the tint, texel by
     texel - what the game's combiner does to its bar image (texel x shade):
     white becomes the tint, black stays black. Nothing outside them moves.
"""
import copy
import math
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import stagechars                                           # noqa: E402
import watchhand                                            # noqa: E402

ROOT = stagechars.ROOT

#: A remastered vertex colour the conversion left white.
WHITE = (255, 255, 255)

#: Model display-list nodes (bondconstants.h MODELNODE_OPCODE): DL keeps its
#: vertex table at +0xC, DLCOLLISION at +0x8; both carry primary and
#: secondary lists at +0x0 and +0x4.
OP_DL, OP_DLCOLLISION = 4, 24
VERTEX_TABLE = {OP_DL: 0xC, OP_DLCOLLISION: 0x8}

#: G_VTX operands in segment 4 address the node's own vertex table.
SEG_MODEL_VTX = 0x04

#: The model box and the item's reference box agree on one scale per axis to
#: within this fraction, or the relation is refused.
BOX_AGREE = 0.02

#: A placed corner lies on a remastered triangle when the planes agree to
#: this cosine and the corner is this fraction of the item's extent from it.
TINT_PLANE_COS = 0.999
TINT_PLANE_DIST = 0.005
TINT_INSIDE = 1e-3

#: How far, in texels, a footprint edge may move to the image's own edge.
#: Silo's steel door: the game's bars and the remaster's painted bars differ
#: by at most 2.8 texels at every edge (round silo4).
TINT_SNAP = 3

#: A footprint is a rectangle when its triangles cover this share of the
#: texel centres in its box (the game places its vertices to about a unit).
TINT_COVER = 0.95


class ShadeError(Exception):
    """The game's data cannot be read or related; the message says why."""


# ---- the game's model ------------------------------------------------------

def rom_rel(name, root=ROOT):
    """The file-list path of the game's model file P<name>Z, matched without
    regard to case (the item key is the game's name lowered)."""
    want = ('assets/obseg/prop/p%sz.bin' % name).lower()
    with open(os.path.join(root, 'scripts', 'filelist.u.csv'), encoding='utf-8') as f:
        for line in f:
            p = line.strip().split(',')
            if len(p) >= 3 and p[2].lower() == want:
                return p[2]
    raise ShadeError('P%sZ is not in the file list' % name)


def prop_header(name, root=ROOT):
    """(numSwitches, numTextures) from the decomp's header for prop `name`
    (assets/obseg/prop/<name>/ModelFileHeader.inc.c)."""
    rel = 'assets/obseg/prop/%s/ModelFileHeader.inc.c' % name
    try:
        text = stagechars._read(root, rel)
    except stagechars.InventoryError as exc:
        raise ShadeError(str(exc))
    m = re.search(r'MODELFILEHEADER\(\s*\w+\s*,(.*)\)\s*$', text, re.M)
    if not m:
        raise ShadeError('%s: no MODELFILEHEADER' % rel)
    a = stagechars._split_top(m.group(1))
    if len(a) != 8:
        raise ShadeError('%s: MODELFILEHEADER has %d fields' % (rel, len(a) + 1))
    return int(a[3], 0), int(a[7], 0)


def model_triangles(data, nswitch, ntex):
    """Every triangle of a model file's DL and DLCOLLISION nodes:
    [dict(tex, P=(p0, p1, p2), C=(rgba0, rgba1, rgba2), ST=(st0, st1, st2),
    scale=(s, t), mode)], positions as the vertex table holds them (the box
    relation absorbs the node origins); ST the vertices' raw texture
    coordinates (s10.5 texels), scale the G_TEXTURE scale in force (1 where
    none is set) and mode the othermode_l the list's own B9 commands set
    (None until one does), read as read_bg_walk reads it."""
    def u32(o):
        if o + 4 > len(data):
            raise ShadeError('the model file is shorter than its records')
        return struct.unpack_from('>I', data, o)[0]

    def fo(a):
        return 0 if a == 0 else (a - watchhand.MODEL_BASE if a >= watchhand.MODEL_BASE else a)

    out = []

    def walk_dl(start, vtab):
        tex, batch, scale, mode = None, {}, (1.0, 1.0), None
        for k in range(8192):
            o = start + 8 * k
            if o + 8 > len(data):
                raise ShadeError('a display list runs off the file')
            w0, w1 = struct.unpack_from('>II', data, o)
            op = w0 >> 24
            if op == 0xB8:
                return
            if op == 0x06:
                raise ShadeError('a nested display list; not read')
            if op == 0xBB:                   # G_TEXTURE: s and t scale, 0.16
                scale = (((w1 >> 16) & 0xFFFF) / 65536.0, (w1 & 0xFFFF) / 65536.0)
            elif op == 0xB9:                 # G_SETOTHERMODE_L: shift, length, bits
                sh, ln = (w0 >> 8) & 0xFF, w0 & 0xFF
                mask = ((1 << ln) - 1) << sh
                mode = ((mode or 0) & ~mask & 0xFFFFFFFF) | (w1 & mask)
            elif op == 0xC0:
                tex = w1 & 0xFFF
            elif op == 0x04:
                n = ((w0 >> 20) & 0xF) + 1
                v0 = (w0 >> 16) & 0xF
                a = vtab + (w1 & 0xFFFFFF) if (w1 >> 24) == SEG_MODEL_VTX else fo(w1)
                if a + 16 * n > len(data):
                    raise ShadeError('a vertex load runs off the file')
                for i in range(n):
                    x, y, z, _fl, s, t = struct.unpack_from('>hhhhhh', data, a + 16 * i)
                    batch[v0 + i] = ((float(x), float(y), float(z)),
                                     tuple(data[a + 16 * i + 12:a + 16 * i + 16]), (s, t))
            elif op in (0xB1, 0xBF):
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
                    if any(j not in batch for j in t):
                        raise ShadeError('a triangle before its vertices')
                    out.append(dict(tex=tex, P=tuple(batch[j][0] for j in t),
                                    C=tuple(batch[j][1] for j in t),
                                    ST=tuple(batch[j][2] for j in t), scale=scale, mode=mode))
        raise ShadeError('a display list without its end')

    root = 4 * nswitch + 12 * ntex
    seen = set()

    def walk(o, depth):
        guard = 0
        while o and guard < 4096:
            guard += 1
            if o in seen or o + 24 > len(data) or depth > 64:
                raise ShadeError('a node outside the file')
            seen.add(o)
            opc = struct.unpack_from('>H', data, o)[0] & 0xFF
            d = fo(u32(o + 4))
            if opc in VERTEX_TABLE and d:
                vtab = fo(u32(d + VERTEX_TABLE[opc]))
                for dl in (fo(u32(d)), fo(u32(d + 4))):
                    if dl:
                        walk_dl(dl, vtab)
            child = fo(u32(o + 20))
            if child:
                walk(child, depth + 1)
            o = fo(u32(o + 12))

    walk(root, 0)
    return out


def tinted(tris):
    """The TINTED triangles: textured, one colour on all three corners, and
    that colour not white. -> [(P, rgb)]. A triangle whose corners disagree
    is refused (a gradient is shading, not a tint)."""
    out = []
    for t in tris:
        rgb = {tuple(c[:3]) for c in t['C']}
        if t['tex'] is None or rgb == {WHITE}:
            continue
        if len(rgb) != 1:
            raise ShadeError('a triangle whose corners carry different colours')
        out.append((t['P'], rgb.pop()))
    return out


# ---- geometry ----------------------------------------------------------------

def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit_normal(P):
    n = _cross(_sub(P[1], P[0]), _sub(P[2], P[0]))
    a = math.sqrt(_dot(n, n))
    return None if a < 1e-9 else (n[0] / a, n[1] / a, n[2] / a)


def bary(P, p):
    """Barycentric coordinates of p's projection onto triangle P."""
    e1, e2 = _sub(P[1], P[0]), _sub(P[2], P[0])
    n = _cross(e1, e2)
    d = _dot(n, n)
    if d < 1e-12:
        return None
    w = _sub(p, P[0])
    b1 = _dot(_cross(w, e2), n) / d
    b2 = _dot(_cross(e1, w), n) / d
    return (1.0 - b1 - b2, b1, b2)


def box_relation(game_pts, ref):
    """The per-axis scale and offset taking the model's box onto the item's
    reference box `ref` ((lo), (hi)). Refused when the axes disagree."""
    lo = [min(p[a] for p in game_pts) for a in range(3)]
    hi = [max(p[a] for p in game_pts) for a in range(3)]
    k = []
    for a in range(3):
        ge, re_ = hi[a] - lo[a], ref[1][a] - ref[0][a]
        if ge <= 0 or re_ <= 0:
            raise ShadeError('a flat box on axis %d' % a)
        k.append(re_ / ge)
    if max(k) > (1.0 + BOX_AGREE) * min(k):
        raise ShadeError('the model box and the item box disagree on their scale (%s)'
                         % ', '.join('%.4f' % v for v in k))
    return lambda p: tuple(ref[0][a] + (p[a] - lo[a]) * k[a] for a in range(3))


# ---- D-032 -------------------------------------------------------------------

def _luma(rgba, w, h):
    return [[(rgba[4 * (r * w + c)] + rgba[4 * (r * w + c) + 1] + rgba[4 * (r * w + c) + 2]) / 3.0
             for c in range(w)] for r in range(h)]


def snap_edge(L, e, lo, hi, size, horizontal, inside_after):
    """Edge `e` (texels) of a rectangle spanning [lo, hi) along the edge: the
    image border when within TINT_SNAP of it, else the boundary b within
    TINT_SNAP where the luminance RISES most into the rectangle, averaged
    along the span (`inside_after`: the rectangle lies at b and beyond).

    Why a rise and not any step: the remaster paints its bars inside a DARK
    frame line (Silo's door: the divider column and the separator rows), so
    the frame line has two strong steps and only the inner one, dark into
    the bars, is the bars' edge. Measured on all four inner edges of the
    door (round silo4); a plain largest step took the outer one on one."""
    if e <= TINT_SNAP:
        return 0
    if e >= size - TINT_SNAP:
        return size
    best = None
    for b in range(max(1, int(round(e)) - TINT_SNAP), min(size - 1, int(round(e)) + TINT_SNAP) + 1):
        if horizontal:
            s = sum(L[b][c] - L[b - 1][c] for c in range(lo, hi)) / max(1, hi - lo)
        else:
            s = sum(L[r][b] - L[r][b - 1] for r in range(lo, hi)) / max(1, hi - lo)
        if not inside_after:
            s = -s
        if best is None or s > best[0] + 1e-9:
            best = (s, b)
    return best[1]


def footprint_rects(vertices, draws, textures, placed):
    """The tinted footprints on the remastered item, as texel rectangles:
    {image: [(c0, r0, c1, r1, rgb)]} (unsnapped, floats). `placed`: [(P, rgb)]
    in item space. Raises ShadeError for a footprint that is not a rectangle
    or that two tints claim."""
    ext = max(max(v[a] for v in vertices) - min(v[a] for v in vertices) for a in range(3))
    tris = []
    for dr in draws:
        img = dr.get('image')
        if img is None or not (0 <= img < len(textures)):
            continue
        ind = dr['indices']
        for t in range(0, len(ind) - 2, 3):
            V = [vertices[ind[t + j]] for j in range(3)]
            P = tuple(tuple(v[:3]) for v in V)
            n = _unit_normal(P)
            if n is None:
                continue
            tris.append(dict(img=img, P=P, n=n, UV=[tuple(v[3:5]) for v in V],
                             white=all(tuple(int(x) for x in v[5:8]) == WHITE for v in V)))

    def locate(p, n):
        hits = []
        for T in tris:
            if abs(_dot(T['n'], n)) < TINT_PLANE_COS:
                continue
            if abs(_dot(_sub(p, T['P'][0]), T['n'])) > TINT_PLANE_DIST * ext:
                continue
            b = bary(T['P'], p)
            if b is None or min(b) < -TINT_INSIDE:
                continue
            hits.append((T, b))
        return hits

    found = []
    for P, rgb in placed:
        n = _unit_normal(P)
        if n is None:
            continue
        corners = []
        for p in P:
            hits = locate(p, n)
            if not hits:
                break
            corners.append(hits)
        if len(corners) != 3:
            continue                        # not on the remastered surface
        imgs = {T['img'] for hs in corners for T, _b in hs}
        if len(imgs) != 1 or not all(T['white'] for hs in corners for T, _b in hs):
            continue                        # another image, or a colour kept
        img = imgs.pop()
        w, h = textures[img]['w'], textures[img]['h']
        tex = []
        for hs in corners:
            T, b = hs[0]
            u = sum(b[j] * T['UV'][j][0] for j in range(3))
            v = sum(b[j] * T['UV'][j][1] for j in range(3))
            tex.append((u * w, v * h))
        box = (min(q[0] for q in tex), min(q[1] for q in tex),
               max(q[0] for q in tex), max(q[1] for q in tex))
        found.append(dict(img=img, rgb=rgb, tex=tex, box=box))
    # ONE FOOTPRINT is every tinted triangle of one image whose texel box
    # overlaps another's: a quad's two halves, and the front and back faces
    # that read the same texels. The game's own vertices are placed to about
    # a unit, so the halves' boxes need not agree exactly.
    def overlap(a, b):
        return min(a[2], b[2]) > max(a[0], b[0]) and min(a[3], b[3]) > max(a[1], b[1])
    parent = list(range(len(found)))

    def top(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for i in range(len(found)):
        for j in range(i):
            if found[i]['img'] == found[j]['img'] and overlap(found[i]['box'], found[j]['box']):
                parent[top(i)] = top(j)
    groups = {}
    for i, f in enumerate(found):
        groups.setdefault(top(i), []).append(f)
    out = {}
    for _k, g in sorted(groups.items()):
        tints = {f['rgb'] for f in g}
        if len(tints) != 1:
            raise ShadeError('one footprint carries two tints')
        c0 = min(f['box'][0] for f in g)
        r0 = min(f['box'][1] for f in g)
        c1 = max(f['box'][2] for f in g)
        r1 = max(f['box'][3] for f in g)
        # A RECTANGLE: the footprint's triangles cover its box, texel centre
        # by texel centre, to within TINT_COVER.
        inside = total = 0
        for r in range(int(math.floor(r0)), int(math.ceil(r1))):
            for c in range(int(math.floor(c0)), int(math.ceil(c1))):
                p = (c + 0.5, r + 0.5)
                if not (c0 <= p[0] <= c1 and r0 <= p[1] <= r1):
                    continue
                total += 1
                for f in g:
                    b = bary(tuple((q[0], q[1], 0.0) for q in f['tex']), (p[0], p[1], 0.0))
                    if b is not None and min(b) >= -1e-6:
                        inside += 1
                        break
        if total == 0 or inside < TINT_COVER * total:
            raise ShadeError('a footprint that is not a rectangle of texels '
                             '(%d of %d texels covered)' % (inside, total))
        out.setdefault(g[0]['img'], []).append((c0, r0, c1, r1, tints.pop()))
    return out


def tint_image(rgba, w, h, rects):
    """`rgba` (bytes) with each snapped rectangle multiplied by its tint.
    -> (new bytes, [(c0, r0, c1, r1, rgb)] as applied, texels changed)."""
    L = _luma(rgba, w, h)
    snapped = []
    for c0, r0, c1, r1, rgb in rects:
        lo_c, hi_c = int(math.floor(c0)), int(math.ceil(c1))
        lo_r, hi_r = int(math.floor(r0)), int(math.ceil(r1))
        sc0 = snap_edge(L, c0, max(0, lo_r), min(h, hi_r), w, False, True)
        sc1 = snap_edge(L, c1, max(0, lo_r), min(h, hi_r), w, False, False)
        sr0 = snap_edge(L, r0, max(0, lo_c), min(w, hi_c), h, True, True)
        sr1 = snap_edge(L, r1, max(0, lo_c), min(w, hi_c), h, True, False)
        if sc1 > sc0 and sr1 > sr0:
            snapped.append((sc0, sr0, sc1, sr1, rgb))
    mask = {}
    for c0, r0, c1, r1, rgb in snapped:
        for r in range(r0, r1):
            for c in range(c0, c1):
                if mask.get((r, c), rgb) != rgb:
                    raise ShadeError('two tints meet on one texel')
                mask[(r, c)] = rgb
    out = bytearray(rgba)
    changed = 0
    for (r, c), rgb in mask.items():
        o = 4 * (r * w + c)
        new = [(out[o + k] * rgb[k] + 127) // 255 for k in range(3)]
        if new != list(out[o:o + 3]):
            changed += 1
        out[o:o + 3] = bytes(new)
    return bytes(out), sorted(set(snapped)), changed


def restore_prop_tint(rom, name, vertices, draws, textures, ref, root=ROOT):
    """D-032 for the prop model `name` (the game's name, any case): the
    item's textures with the game's tinted footprints multiplied in.
    -> (textures, report); report is None when the game tints nothing on the
    remastered item. Raises ShadeError when the game's data cannot be read
    or related."""
    nswitch, ntex = prop_header(name, root)
    try:
        data = watchhand.rom_file(rom, rom_rel(name, root), root)
    except watchhand.WatchHandError as exc:
        raise ShadeError(str(exc))
    tris = model_triangles(data, nswitch, ntex)
    if not tris:
        raise ShadeError('P%sZ draws no triangle' % name)
    place = box_relation([p for t in tris for p in t['P']], ref)
    placed = [(tuple(place(p) for p in P), rgb) for P, rgb in tinted(tris)]
    if not placed:
        return textures, None
    rects = footprint_rects(vertices, draws, textures, placed)
    if not rects:
        return textures, None
    out = list(textures)
    report = []
    for img, rs in sorted(rects.items()):
        t = textures[img]
        rgba, applied, changed = tint_image(bytes(t['rgba']), t['w'], t['h'], rs)
        out[img] = dict(t, rgba=rgba)
        report.append(dict(image=img, w=t['w'], h=t['h'], footprints=rs, applied=applied,
                           changed=changed))
    return out, report


def prop_game_shade(rom, name, root=ROOT):
    """D-052 for the prop model `name` (the game's name, any case): the shade
    the game lays over the model's textured triangles - the mean of their
    corner colours weighted by each triangle's area - as (r, g, b) in 0 ..
    255, with the number of textured triangles and the distinct corner
    colours. None for the shade when every corner is white (the game shades
    nothing). Raises ShadeError when the game's data cannot be read."""
    nswitch, ntex = prop_header(name, root)
    try:
        data = watchhand.rom_file(rom, rom_rel(name, root), root)
    except watchhand.WatchHandError as exc:
        raise ShadeError(str(exc))
    tris = [t for t in model_triangles(data, nswitch, ntex) if t['tex'] is not None]
    if not tris:
        raise ShadeError('P%sZ draws no textured triangle' % name)
    acc, wsum, seen = [0.0, 0.0, 0.0], 0.0, set()
    for t in tris:
        e, g = _sub(t['P'][1], t['P'][0]), _sub(t['P'][2], t['P'][0])
        n = _cross(e, g)
        area = 0.5 * math.sqrt(_dot(n, n))
        for c in t['C']:
            seen.add(tuple(c[:3]))
            for k in range(3):
                acc[k] += area * c[k] / 3.0
        wsum += area
    if not wsum > 0.0:
        raise ShadeError('P%sZ: its textured triangles have no area' % name)
    if seen == {WHITE}:
        return None, len(tris), sorted(seen)
    return tuple(acc[k] / wsum for k in range(3)), len(tris), sorted(seen)


#: D-052 THE DOOR'S WALL: the wall a door is set in is the surface lying in
#: the door's own plane - its normal within DOOR_WALL_ALIGN of the door's,
#: its centroid within DOOR_WALL_SLAB room units of the door's face - round
#: the door, within DOOR_WALL_REACH times the door's larger half-extent of its
#: centre in the plane, and outside the door's own footprint.
DOOR_WALL_ALIGN = 0.9
DOOR_WALL_SLAB = 15.0
DOOR_WALL_REACH = 2.0


def _floats(text):
    return [float(x) for x in re.findall(r'[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?', text)]


def door_placements(item, level, root=ROOT, country='u'):
    """The placements of the door model `item` in `level`'s setup: [dict(
    centre, normal, axes=(u, v), half=(hu, hv), pad)], in room units - the
    door's bound pad (doors index the setup's bound pads, door7F0526EC),
    its face centre, the normal of its thinnest box axis (the pad's
    cross(up, look), whose extent the box's x carries), that axis as `thin`
    (0 side, 1 up, 2 look) and the face's two in-plane axes and
    half-extents. Raises ShadeError when the setup cannot
    be read or names no such door."""
    try:
        lid, setup, rel = stagechars.setup_of(root, level, country)
        text = stagechars._preprocess(stagechars._strip_comments(stagechars._read(root, rel)))
        recs = stagechars.propdef_records(text, setup, stagechars.word_macros(root), rel)
        import stageprops                                   # noqa: E402
        names = [r['name'].lower() for r in stageprops.prop_records(root)]
        body = stagechars._array_body(text, 'pad3dlist', rel)
    except Exception as exc:                                   # noqa: BLE001
        raise ShadeError('the setup of %s: %s' % (level, exc))
    rows = [r for r in re.findall(r'\{\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}\s*,'
                                  r'\s*"[^"]*"\s*,\s*[^,]*,\s*\{([^}]*)\}\s*\}', body)]
    out = []
    for kind, w in recs:
        if kind != 1 or len(w) < 2:
            continue
        ix, pad = w[1] >> 16, w[1] & 0xFFFF
        if not (0 <= ix < len(names)) or names[ix] != item.lower():
            continue
        if not (0 <= pad < len(rows)):
            raise ShadeError('door %s names bound pad %d; the list has %d' % (item, pad, len(rows)))
        pos, up, look, box = (_floats(x) for x in rows[pad])
        if len(pos) != 3 or len(up) != 3 or len(look) != 3 or len(box) != 6:
            raise ShadeError('bound pad %d does not read as a position, two axes and a box' % pad)
        side = _cross(up, look)
        axes = [side, up, look]
        ext = [(box[0], box[1]), (box[2], box[3]), (box[4], box[5])]
        thin = min(range(3), key=lambda k: ext[k][1] - ext[k][0])
        centre = list(pos)
        for k in range(3):
            mid = (ext[k][0] + ext[k][1]) / 2.0
            centre = [centre[j] + axes[k][j] * mid for j in range(3)]
        n = axes[thin]
        ln = math.sqrt(_dot(n, n))
        rest = [k for k in range(3) if k != thin]
        out.append(dict(pad=pad, centre=tuple(centre), normal=tuple(x / ln for x in n), thin=thin,
                        axes=tuple(axes[k] for k in rest),
                        half=tuple((ext[k][1] - ext[k][0]) / 2.0 for k in rest)))
    if not out:
        raise ShadeError('%s places no door %s' % (level, item))
    return out


def in_door_wall(cen, normal_vec, place, scale=1.0):
    """Whether a triangle (centroid `cen`, normal `normal_vec`, in units of
    `scale` per room unit) lies in the wall round the door `place`
    (door_placements; DOOR_WALL_ALIGN). Pure."""
    nl = math.sqrt(_dot(normal_vec, normal_vec))
    if not nl > 0.0 or abs(_dot(normal_vec, place['normal'])) < DOOR_WALL_ALIGN * nl:
        return False
    d = _sub([c / scale for c in cen], place['centre'])
    if abs(_dot(d, place['normal'])) > DOOR_WALL_SLAB:
        return False
    coords = [_dot(d, a) / math.sqrt(_dot(a, a)) for a in place['axes']]
    reach = DOOR_WALL_REACH * max(place['half'])
    if any(abs(c) > reach for c in coords):
        return False
    return not all(abs(c) <= h for c, h in zip(coords, place['half']))


def door_wall_shade(rom, item, level, root=ROOT):
    """D-052: (the game's door placements, the area-weighted vertex shade of
    the game's room triangles in the wall round each door, and its area)."""
    places = door_placements(item, level, root)
    uc = _uvcarry()
    lid, rel, scale = stage_bg(level, root)
    full = read_bg_full(uc.rom_file(rom, rel, root))
    acc, wsum = [0.0, 0.0, 0.0], 0.0
    for (room, kind, mode, tex, P3, C, ST) in full:
        n = _cross(_sub(P3[1], P3[0]), _sub(P3[2], P3[0]))
        cen = [sum(p[k] for p in P3) / 3.0 for k in range(3)]
        if not any(in_door_wall(cen, n, pl) for pl in places):
            continue
        area = 0.5 * math.sqrt(_dot(n, n))
        for c in C:
            for k in range(3):
                acc[k] += area * c[k] / 3.0
        wsum += area
    if not wsum > 0.0:
        raise ShadeError('no room triangle lies in the wall round %s' % item)
    return places, scale, tuple(a / wsum for a in acc), wsum


def door_plane(place, p):
    """The door-face coordinates (s, t) of the room-unit point `p` for the
    placement `place` (door_placements): along its first and second in-plane
    axes from its centre. Pure."""
    d = _sub(p, place['centre'])
    return tuple(_dot(d, a) / math.sqrt(_dot(a, a)) for a in place['axes'])


def _faces_out(P, place):
    """Whether triangle `P` (room units) faces along the placement's normal
    (the side the door model's +z face looks to) and lies in the wall round
    it (in_door_wall). Pure."""
    n = _cross(_sub(P[1], P[0]), _sub(P[2], P[0]))
    cen = [sum(p[k] for p in P) / 3.0 for k in range(3)]
    return _dot(n, place['normal']) > 0.0 and in_door_wall(cen, n, place)


def wall_maps(place, scale, level):
    """D-054 (a door's face continues its wall): the remastered background
    `level`'s triangles in the wall round the door `place` facing the way
    its +z face does (_faces_out; `scale` level units per room unit), each
    as (image, (t_lo, t_hi), u map, v map) - its height band in the door's
    t and its texture map, u = a*s + b*t + c, as (a, b, c) over the door's
    (s, t) (door_plane). The level's pool vertices carry (x, y, z, u, v).
    Degenerate triangles are left out. Pure."""
    out = []
    for d in level['draws']:
        im = d.get('image')
        if not isinstance(im, int):
            continue
        V = level['pools'][d['pool']]['vertices']
        ind = d['indices']
        for q in range(0, len(ind) - 2, 3):
            pts = [V[i] for i in ind[q:q + 3]]
            if any(len(p) < 5 for p in pts):
                continue
            P = [tuple(float(x) / scale for x in p[:3]) for p in pts]
            if not _faces_out(P, place):
                continue
            st = [door_plane(place, p) for p in P]
            (s0, t0), (s1, t1), (s2, t2) = st
            det = (s1 - s0) * (t2 - t0) - (s2 - s0) * (t1 - t0)
            if not abs(det) > 1e-9:
                continue
            maps = []
            for k in (3, 4):
                f0, f1, f2 = (float(p[k]) for p in pts)
                a = ((f1 - f0) * (t2 - t0) - (f2 - f0) * (t1 - t0)) / det
                b = ((s1 - s0) * (f2 - f0) - (s2 - s0) * (f1 - f0)) / det
                maps.append((a, b, f0 - a * s0 - b * t0))
            out.append((im, (min(t0, t1, t2), max(t0, t1, t2)), maps[0], maps[1]))
    return out


def wall_textures(game, place):
    """D-054: the game's room triangles `game` ([(texture, P)], room units)
    in the wall round the door `place` facing the way its +z face does
    (_faces_out), each as (texture, (t_lo, t_hi)). Pure."""
    out = []
    for tex, P in game:
        if tex is None or not _faces_out(P, place):
            continue
        ts = [door_plane(place, p)[1] for p in P]
        out.append((tex, (min(ts), max(ts))))
    return out


def door_walls(rom, item, stage, bg_item, level, root=ROOT):
    """D-054: [(placement, wall_maps, wall_textures)] for each placement of
    the door model `item` in `stage`'s setup (door_placements), over the
    game's rooms of the background item `bg_item` and its remastered
    `level`. Raises ShadeError when the game's data cannot be read or the
    setup places no such door."""
    places = door_placements(item, stage, root)
    uc = _uvcarry()
    _lid, rel, scale = stage_bg(bg_item, root)
    try:
        game = [(g[3], g[4]) for g in read_bg_walk(uc.rom_file(rom, rel, root))]
    except uc.UvCarryError as exc:
        raise ShadeError(str(exc))
    return [(pl, wall_maps(pl, scale, level), wall_textures(game, pl)) for pl in places]


def shade_vertices(vertices, shade):
    """D-052: every vertex colour of `vertices` ((x, y, z, u, v, r, g, b, a,
    ...)) multiplied by `shade` / 255, rounded; alpha and everything else
    kept. Pure."""
    out = []
    for v in vertices:
        rgb = tuple(int(round(v[5 + k] * shade[k] / 255.0)) for k in range(3))
        out.append(tuple(v[:5]) + rgb + tuple(v[8:]))
    return out


# ---- D-054 -------------------------------------------------------------------

def prop_blends(rom, name, root=ROOT):
    """D-054: (blended triangles, all triangles) of the game's model `name`
    (the game's name, any case) - blended meaning drawn under a render mode
    that forces blending or the translucent depth mode, or with a corner alpha
    below 255. Raises ShadeError when the game's data cannot be read."""
    nswitch, ntex = prop_header(name, root)
    try:
        data = watchhand.rom_file(rom, rom_rel(name, root), root)
    except watchhand.WatchHandError as exc:
        raise ShadeError(str(exc))
    tris = model_triangles(data, nswitch, ntex)
    if not tris:
        raise ShadeError('P%sZ draws no triangle' % name)
    blended = sum(1 for t in tris
                  if (t['mode'] is not None and ((t['mode'] & RM_FORCE_BL)
                                                 or (t['mode'] & RM_ZMODE) == RM_ZMODE_XLU))
                  or min(c[3] for c in t['C']) < 255)
    return blended, len(tris)


def prop_draw_textures(rom, name, vertices, draws, ref, root=ROOT):
    """D-054: {draw index: the game's texture} for an item that is the game's
    model in the item's space - for each draw, the texture of the game
    triangles its triangles lie on (draw_textures), the game's model `name`
    placed onto the reference box `ref`. Raises ShadeError when the game's
    data cannot be read or related."""
    nswitch, ntex = prop_header(name, root)
    try:
        data = watchhand.rom_file(rom, rom_rel(name, root), root)
    except watchhand.WatchHandError as exc:
        raise ShadeError(str(exc))
    tris = model_triangles(data, nswitch, ntex)
    if not tris:
        raise ShadeError('P%sZ draws no triangle' % name)
    place = box_relation([p for t in tris for p in t['P']], ref)
    game = [(tuple(place(p) for p in t['P']), t['tex']) for t in tris if t['tex'] is not None]
    extent = max(ref[1][a] - ref[0][a] for a in range(3))
    return draw_textures(game, extent, vertices, draws)


def prop_triangle_textures(rom, name, vertices, draws, ref, root=ROOT):
    """D-054: triangle_textures for an item that is the game's model `name`
    in the item's space (placed as prop_draw_textures places it). Raises
    ShadeError when the game's data cannot be read or related."""
    nswitch, ntex = prop_header(name, root)
    try:
        data = watchhand.rom_file(rom, rom_rel(name, root), root)
    except watchhand.WatchHandError as exc:
        raise ShadeError(str(exc))
    tris = model_triangles(data, nswitch, ntex)
    if not tris:
        raise ShadeError('P%sZ draws no triangle' % name)
    place = box_relation([p for t in tris for p in t['P']], ref)
    game = [(tuple(place(p) for p in t['P']), t['tex']) for t in tris if t['tex'] is not None]
    extent = max(ref[1][a] - ref[0][a] for a in range(3))
    return triangle_textures(game, extent, vertices, draws)


def triangle_textures(game, extent, vertices, draws):
    """{(draw, triangle): texture} - the texture of the placed game triangle
    (`game` [(P, texture)]) each draw triangle lies on: planes within
    TINT_PLANE_COS, the centroid within TINT_PLANE_DIST of `extent` of the
    plane and inside within TINT_INSIDE, the first such game triangle.
    Triangles on none are left out. Pure."""
    G = []
    for P, tex in game:
        m = _unit_normal(P)
        if m is not None:
            G.append((P, tex, m))
    out = {}
    for di, d in enumerate(draws):
        ind = d.get('indices') or []
        for t in range(0, len(ind) - 2, 3):
            P = [tuple(float(x) for x in vertices[i][:3]) for i in ind[t:t + 3]]
            n = _unit_normal(P)
            if n is None:
                continue
            c = tuple(sum(p[a] for p in P) / 3.0 for a in range(3))
            for Q, tex, m in G:
                if abs(_dot(m, n)) < TINT_PLANE_COS or abs(_dot(_sub(c, Q[0]), m)) > TINT_PLANE_DIST * extent:
                    continue
                b = bary(Q, c)
                if b is not None and min(b) >= -TINT_INSIDE:
                    out[(di, t // 3)] = tex
                    break
    return out


def draw_textures(game, extent, vertices, draws):
    """prop_draw_textures' rule on the placed game triangles `game` ([(P,
    texture)]): a draw's texture is the one most of its triangles lie on
    (triangle_textures). A draw none of whose triangles lies on a game
    triangle is left out. Pure."""
    votes = {}
    for (di, _t), tex in triangle_textures(game, extent, vertices, draws).items():
        row = votes.setdefault(di, {})
        row[tex] = row.get(tex, 0) + 1
    return {di: max(sorted(v), key=lambda k: v[k]) for di, v in votes.items()}


#: D-054: the remaster's image for a game texture is the one its background
#: draws over at least this share of the rooms' triangles in that texture.
BG_IMAGE_SHARE = 0.75


def bg_texture_images(rom, item, level, root=ROOT):
    """D-054: {game texture: (image index, triangles, the texture's room
    triangles)} - for each texture the stage's rooms draw, the image of the
    remastered background `level` (xblaworld.read_level of the background item
    `item`) that draws the most of the room triangles in it (texture_images).
    Raises ShadeError when the game's data cannot be read."""
    uc = _uvcarry()
    _lid, rel, scale = stage_bg(item, root)
    try:
        game = read_bg_walk(uc.rom_file(rom, rel, root))
    except uc.UvCarryError as exc:
        raise ShadeError(str(exc))
    return texture_images([(g[3], g[4]) for g in game], scale, level)


def texture_images(game, scale, level):
    """bg_texture_images' rule on the game's room triangles `game` ([(texture,
    P)], room units) and an item `scale` times them: a room triangle is drawn
    by the item's image whose triangle it lies on (planes within
    SHADE_PLANE_COS, the centroid within SHADE_PLANE_DIST, inside within
    SHADE_INSIDE - _Coincide's test). -> {texture: (image, triangles, the
    texture's room triangles)} for the image drawing the most. Pure."""
    tris = []
    for d in level['draws']:
        im = d.get('image')
        if not isinstance(im, int):
            continue
        V = level['pools'][d['pool']]['vertices'] if 0 <= d.get('pool', -1) < len(level['pools']) else None
        if not V:
            continue
        ind = d['indices']
        for t in range(0, len(ind) - 2, 3):
            P = tuple(tuple(float(x) for x in V[i][:3]) for i in ind[t:t + 3])
            n = _unit_normal(P)
            if n is not None:
                tris.append((P, n, im))
    cell = 64.0
    grid = {}
    for k, (P, _n, _im) in enumerate(tris):
        lo = [int(math.floor(min(p[a] for p in P) / cell)) for a in range(3)]
        hi = [int(math.floor(max(p[a] for p in P) / cell)) for a in range(3)]
        if max(hi[a] - lo[a] for a in range(3)) > 64:
            continue
        for x in range(lo[0], hi[0] + 1):
            for y in range(lo[1], hi[1] + 1):
                for z in range(lo[2], hi[2] + 1):
                    grid.setdefault((x, y, z), []).append(k)
    total, hits = {}, {}
    for tex, P0 in game:
        if tex is None:
            continue
        total[tex] = total.get(tex, 0) + 1
        Q = tuple(tuple(v * scale for v in p) for p in P0)
        m = _unit_normal(Q)
        if m is None:
            continue
        c = tuple(sum(p[a] for p in Q) / 3.0 for a in range(3))
        g = tuple(int(math.floor(c[a] / cell)) for a in range(3))
        for k in sorted(set(grid.get(g, ()))):
            P, n, im = tris[k]
            if abs(_dot(n, m)) < SHADE_PLANE_COS or abs(_dot(_sub(c, P[0]), n)) > SHADE_PLANE_DIST:
                continue
            b = bary(P, c)
            if b is not None and min(b) >= -SHADE_INSIDE:
                h = hits.setdefault(tex, {})
                h[im] = h.get(im, 0) + 1
                break
    out = {}
    for tex, h in hits.items():
        im = max(sorted(h), key=lambda k: h[k])
        out[tex] = (im, h[im], total[tex])
    return out


# ---- D-053 -------------------------------------------------------------------

def prop_decal_alpha(rom, name, vertices, skin, draws, ref, root=ROOT):
    """D-053 for the prop model `name` (the game's name, any case) on an item
    that is the game's model in the item's space (the source's original
    generation, whose box is `ref`): the item's decal-pass draws take the
    alpha the game blends its own decals at - the triangles its lists draw
    in the decal depth mode with blending forced (othermode_l RM_ZMODE_DEC
    and RM_FORCE_BL, model_triangles' mode) under corner alphas below 255.
    -> (vertices, skin, draws, report) as decal_alpha_draws returns them.
    Raises ShadeError when the game's data cannot be read or related."""
    nswitch, ntex = prop_header(name, root)
    try:
        data = watchhand.rom_file(rom, rom_rel(name, root), root)
    except watchhand.WatchHandError as exc:
        raise ShadeError(str(exc))
    tris = model_triangles(data, nswitch, ntex)
    if not tris:
        raise ShadeError('P%sZ draws no triangle' % name)
    place = box_relation([p for t in tris for p in t['P']], ref)
    blended = [(tuple(place(p) for p in t['P']), tuple(c[3] for c in t['C'])) for t in tris
               if t['mode'] is not None and (t['mode'] & RM_ZMODE) == RM_ZMODE_DEC
               and (t['mode'] & RM_FORCE_BL) and min(c[3] for c in t['C']) < 255]
    extent = max(ref[1][a] - ref[0][a] for a in range(3))
    return decal_alpha_draws(blended, extent, vertices, skin, draws)


def _alpha_under(game, P, extent):
    """The game's alpha at the centroid of the item triangle P - from the
    blended game triangle it lies on (planes within TINT_PLANE_COS, the
    centroid within TINT_PLANE_DIST of `extent` of its plane and inside it
    within TINT_INSIDE; the most inside of several) - or None when it lies
    on none or the alpha there is 255."""
    n = _unit_normal(P)
    if n is None:
        return None
    c = tuple(sum(p[a] for p in P) / 3.0 for a in range(3))
    best = None
    for Q, A, m in game:
        if abs(_dot(m, n)) < TINT_PLANE_COS or abs(_dot(_sub(c, Q[0]), m)) > TINT_PLANE_DIST * extent:
            continue
        b = bary(Q, c)
        if b is None or min(b) < -TINT_INSIDE:
            continue
        if best is None or min(b) > best[0]:
            best = (min(b), max(0, min(255, int(round(sum(b[k] * A[k] for k in range(3)))))))
    return None if best is None or best[1] >= 255 else best[1]


def decal_alpha_draws(blended, extent, vertices, skin, draws):
    """prop_decal_alpha's rule on the game's blended decal triangles
    `blended` ([(P, corner alphas)], placed in the item) for an item whose
    largest extent is `extent`. Each triangle of a draw under the decal pass
    (style bit DECAL_STYLE; a two-layer draw's layer excepted) lying on one
    of them takes the game's alpha there, a (_alpha_under): its corners'
    alpha times a / 255, on copies appended to the vertex list - one per
    corner and alpha, the skin entry copied - so no other triangle's corner
    moves. A draw in the opaque pass all of whose triangles take one moves to
    the translucent pass (the gate's low bits, xblaworld.PASS_TRANSLUCENT);
    one only some of whose triangles do gives those to a translucent copy
    placed straight after it. Pure: new lists are returned.
    -> (vertices, skin, draws, report [dict(draw, alphas {a: tris}, tris,
    opaque, moved)]) - `draw` the index in `draws`, `moved` the triangles an
    opaque draw gave to its translucent copy (0 when it moved whole)."""
    import xblaworld                                         # noqa: E402
    gate = xblaworld.PASS_OPAQUE | xblaworld.PASS_TRANSLUCENT
    game = []
    for P, A in blended:
        m = _unit_normal(P)
        if m is not None:
            game.append((P, A, m))
    vertices, skin = list(vertices), list(skin)
    out, report, done = [], [], {}
    for di, dr in enumerate(draws):
        pm = dr.get('pass_mask') or 0
        ind = list(dr.get('indices') or [])
        if not (pm & DECAL_STYLE) or dr.get('layer') or not ind or not game:
            out.append(dr)
            continue
        per = [_alpha_under(game, [tuple(float(x) for x in vertices[i][:3])
                                   for i in ind[3 * t:3 * t + 3]], extent)
               for t in range(len(ind) // 3)]
        alphas = {}
        for a in per:
            if a is not None:
                alphas[a] = alphas.get(a, 0) + 1
        if not alphas:
            out.append(dr)
            continue
        new = list(ind)
        for t, a in enumerate(per):
            if a is None:
                continue
            for k in range(3):
                i = ind[3 * t + k]
                j = done.get((i, a))
                if j is None:
                    v = list(vertices[i])
                    v[8] = type(v[8])(int(round(v[8] * a / 255.0)))
                    j = len(vertices)
                    vertices.append(tuple(v))
                    skin.append(skin[i])
                    done[(i, a)] = j
                new[3 * t + k] = j
        opaque = bool(pm & xblaworld.PASS_OPAQUE)
        xlu = (pm & ~gate) | xblaworld.PASS_TRANSLUCENT
        rd = dict(draw=di, alphas=alphas, tris=len(per), opaque=opaque, moved=0)
        if not opaque or all(a is not None for a in per):
            out.append(dict(dr, indices=new, pass_mask=xlu if opaque else pm))
        else:
            keep = [i for t, a in enumerate(per) if a is None for i in ind[3 * t:3 * t + 3]]
            take = [i for t, a in enumerate(per) if a is not None for i in new[3 * t:3 * t + 3]]
            out.append(dict(dr, indices=keep))
            out.append(dict(dr, indices=take, pass_mask=xlu))
            rd['moved'] = len(take) // 3
        report.append(rd)
    return vertices, skin, out, report


def describe_decal_alpha(report):
    """The preparation's summary for prop_decal_alpha's report."""
    if not report:
        return "the game blends no decal under the item's decal draws"
    return '; '.join('draw %d: %s of %d triangles%s'
                     % (d['draw'], ', '.join('%d at %d/255' % (c, a) for a, c in sorted(d['alphas'].items())),
                        d['tris'], (' (opaque: drawn translucent)' if not d['moved'] else
                                    ' (opaque: %d to a translucent copy)' % d['moved'])
                        if d['opaque'] else '')
                     for d in report)


def describe_tint(report):
    """One line per tinted image, for the preparation's log."""
    lines = []
    for r in report:
        tints = sorted({'%02x%02x%02x' % rgb for _a, _b, _c, _d, rgb in r['applied']})
        lines.append('image %d (%dx%d): %d footprint(s) -> %s, tint %s, %d texels changed'
                     % (r['image'], r['w'], r['h'], len(r['footprints']),
                        ' '.join('cols %d..%d rows %d..%d' % (c0, c1, r0, r1)
                                 for c0, r0, c1, r1, _rgb in r['applied']),
                        '/'.join(tints), r['changed']))
    return lines


# ---- THE GAME'S REPEATS (#95) ------------------------------------------------
#
# A prop item's texture coordinates are stored at a scale the source does not
# record; xblachar.texcoord_scale measures it, and its last rule, THE WHOLE
# IMAGE, is the one it calls ambiguous between powers of two (a draw laid over
# a quarter of its image reads whole at twice the scale). The game's own model
# says how often its images repeat over each face, and the remaster re-drew
# those faces: where its triangles lie on the game's, the two repeat at the
# same rate, or at a rate a whole power of two apart when the scale was read
# wrong.
#
# MEASURED (the source's prop corpus against the user's ROM): mainframe1,
# read at 1/8192 by the reach rule, lies on Pmainframe1Z at a per-axis log2
# of +0.000 (83% of its matched area within a quarter of it); mainframe2,
# read at 1/16384 by THE WHOLE IMAGE, lies on Pmainframe2Z at -1.000 (78%) -
# half the game's repeats on each axis, its back face carrying mainframe1's
# raw coordinates on the image the two share, and the game's two models
# carrying the same repeats on the textures they share. Over the whole
# corpus the measure is NOT a scale rule on its own: 46 of 159 items that
# earlier rules decide sit a power of two or more from the game, where the
# remaster gathered several of the game's textures into one image or
# re-tiled a face. Of the items THE WHOLE IMAGE decides, the Jungle tree
# (-0.017) and the wooden table (-0.303, 44%) agree with it, the filing
# cabinet (-0.791) has only 64% of its area near a power of two, and the
# mainframe is the one that disagrees clearly.
#
# So the measure only CHECKS THE WHOLE IMAGE's answer: the correction is
# 2^-k when the area-weighted median per-axis log2 lies within REPEAT_NEAR of
# a whole k != 0 and at least REPEAT_SHARE of the matched area lies within
# REPEAT_NEAR of k; otherwise the answer stands. A triangle is matched as
# D-032 places the game's model (box_relation into the item's reference box,
# TINT_PLANE_COS / TINT_PLANE_DIST coplanar, centroid inside the game's
# triangle). Pure apart from reading the ROM.

#: Per-axis log2 of the remastered/game repeat rate within this of a whole k.
REPEAT_NEAR = 0.25
#: The share of the matched remastered area that must lie within it.
REPEAT_SHARE = 0.75


def model_texture_sizes(data, nswitch, ntex):
    """{texture id: (width, height)} from a model file's texture table
    (bondtypes.h ModelFileTextures: 
tex records of 12 bytes after the
    switch list); the id is what the model's 0xC0 texture command names."""
    out = {}
    for k in range(ntex):
        o = 4 * nswitch + 12 * k
        if o + 6 > len(data):
            raise ShadeError('the texture table runs off the file')
        tid, w, h = struct.unpack_from('>IBB', data, o)
        if w and h:
            out.setdefault(tid & 0xFFF, (w, h))
    return out


def _area2(U):
    return 0.5 * abs((U[1][0] - U[0][0]) * (U[2][1] - U[0][1])
                     - (U[2][0] - U[0][0]) * (U[1][1] - U[0][1]))


def _area3(P):
    n = _cross(_sub(P[1], P[0]), _sub(P[2], P[0]))
    return 0.5 * math.sqrt(_dot(n, n))


def repeat_rows(game, vertices, draws, place, extent):
    """[(per-axis log2 of remastered / game repeats per unit area, remastered
    area)] for every remastered textured triangle lying on a game triangle.
    `game` [(P, UV)] the game's triangles in its own space with UV in image
    repeats; `place` the box relation; `extent` the item's largest extent."""
    G = []
    for P, UV in game:
        Q = tuple(place(p) for p in P)
        a = _area3(Q)
        n = _unit_normal(Q)
        if n is None or a <= 0.0:
            continue
        G.append((Q, n, _dot(n, Q[0]), _area2(UV) / a))
    rows = []
    for dr in draws:
        if dr.get('image') is None:
            continue
        ind = dr['indices']
        for t in range(0, len(ind) - 2, 3):
            P = [tuple(vertices[ind[t + k]][:3]) for k in range(3)]
            U = [tuple(vertices[ind[t + k]][3:5]) for k in range(3)]
            n = _unit_normal(P)
            a = _area3(P)
            if n is None or a <= 0.0:
                continue
            c = tuple(sum(p[i] for p in P) / 3.0 for i in range(3))
            dens = _area2(U) / a
            for Q, gn, gd, gdens in G:
                if abs(_dot(n, gn)) < TINT_PLANE_COS or abs(_dot(gn, c) - gd) > TINT_PLANE_DIST * extent:
                    continue
                b = bary(Q, c)
                if b is None or min(b) < -TINT_INSIDE:
                    continue
                if dens > 0.0 and gdens > 0.0:
                    rows.append((0.5 * math.log2(dens / gdens), a))
                break
    return rows


def repeat_correction(rows):
    """THE GAME'S REPEATS' answer for `rows` (repeat_rows): (factor, median,
    share). The factor is 2^-k for the whole k != 0 nearest the weighted
    median when the median lies within REPEAT_NEAR of it and REPEAT_SHARE of
    the area lies within REPEAT_NEAR of k; 1.0 otherwise (and with no rows)."""
    if not rows:
        return 1.0, None, 0.0
    rows = sorted(rows)
    tot = sum(w for _v, w in rows)
    acc, med = 0.0, rows[-1][0]
    for v, w in rows:
        acc += w
        if acc >= 0.5 * tot:
            med = v
            break
    k = int(round(med))
    share = sum(w for v, w in rows if abs(v - k) <= REPEAT_NEAR) / tot if tot > 0 else 0.0
    if k == 0 or abs(med - k) > REPEAT_NEAR or share < REPEAT_SHARE:
        return 1.0, med, share
    return 2.0 ** -k, med, share


#: THE GAME'S COORDINATES (game_coords_scale): a coordinate matches when it
#: lies within this much of one of the game's (a quarter texel of the game's
#: 32-texel door images); the item must reach this share of the game's
#: coordinates of each texture it is matched to.
COORD_EPS = 1.0 / 128.0
COORD_COVER = 0.9


def model_coords(rom, name, root=ROOT):
    """{texture id: [(s, 1 - t)]} - every vertex coordinate of the game's model
    P<name>Z in image repeats (s x the G_TEXTURE scale / 32 / the image's
    width), t turned (1 - t) to the remaster's convention."""
    nswitch, ntex = prop_header(name, root)
    try:
        data = watchhand.rom_file(rom, rom_rel(name, root), root)
    except watchhand.WatchHandError as exc:
        raise ShadeError(str(exc))
    sizes = model_texture_sizes(data, nswitch, ntex)
    out = {}
    for t in model_triangles(data, nswitch, ntex):
        wh = sizes.get(t['tex']) if t['tex'] is not None else None
        if wh is None or wh[0] < 2 or wh[1] < 2:
            continue                    # untextured, or a 1x1 colour image
        s, u = t['scale']
        out.setdefault(t['tex'], []).extend(
            (st[0] * s / 32.0 / wh[0], 1.0 - st[1] * u / 32.0 / wh[1]) for st in t['ST'])
    return out


def coords_scale(game, vertices, draws, doublings):
    """THE GAME'S COORDINATES on `game` (model_coords' form) and an item's
    `vertices` (coordinates as read, 1/32768) and `draws` -> (factor, matches
    {item image: game texture}) or (None, why).

    A scale QUALIFIES when every textured draw's every coordinate, read at
    it, lies within COORD_EPS of a coordinate of ONE game texture - the same
    texture for every draw of the same image, and a different texture for each
    image - and each matched texture's coordinates are reached by the image's
    to COORD_COVER both ways. Exactly one power of two (1/32768 .. 1/32768 x
    2^doublings) must qualify."""
    if not game:
        return None, "the game's model draws no textured triangle"
    q = 1.0 / COORD_EPS

    def keyset(pts):
        return {(int(round(a * q)), int(round(b * q))) for a, b in pts}

    def near(k, ks):
        return any((k[0] + dx, k[1] + dy) in ks for dx in (-1, 0, 1) for dy in (-1, 0, 1))
    gk = {tex: keyset(pts) for tex, pts in game.items()}
    imgs = {}
    for d in draws:
        if d.get('image') is None:
            continue
        imgs.setdefault(d['image'], set()).update(d['indices'])
    if not imgs:
        return None, 'the item draws no textured triangle'
    found = []
    for k in range(doublings + 1):
        f = float(1 << k)
        match = {}
        for im, ix in sorted(imgs.items()):
            ik = keyset((vertices[i][3] * f, vertices[i][4] * f) for i in ix)
            hit = [tex for tex, ks in gk.items()
                   if all(near(c, ks) for c in ik)
                   and sum(1 for c in ks if near(c, ik)) >= COORD_COVER * len(ks)]
            if len(hit) != 1:
                break
            match[im] = hit[0]
        if len(match) == len(imgs) and len(set(match.values())) == len(match):
            found.append((f, match))
    if len(found) != 1:
        return None, ('%d scales carry the game\'s coordinates' % len(found))
    return found[0]


def game_coords_scale(rom, name, vertices, draws, doublings, root=ROOT):
    """coords_scale for the game's prop model `name` (P<name>Z) -> (factor,
    matches) or (None, why)."""
    return coords_scale(model_coords(rom, name, root), vertices, draws, doublings)


def game_repeats(rom, name, vertices, draws, ref, root=ROOT):
    """THE GAME'S REPEATS for the prop model `name` (the game's name): the
    remastered item's vertices and draws (texture coordinates at the scale
    read) against P<name>Z placed into `ref`, the item's reference box.
    -> (factor, median, share, matched triangles). Raises ShadeError when the
    game's model cannot be read or related."""
    nswitch, ntex = prop_header(name, root)
    try:
        data = watchhand.rom_file(rom, rom_rel(name, root), root)
    except watchhand.WatchHandError as exc:
        raise ShadeError(str(exc))
    sizes = model_texture_sizes(data, nswitch, ntex)
    tris = model_triangles(data, nswitch, ntex)
    game = []
    for t in tris:
        wh = sizes.get(t['tex']) if t['tex'] is not None else None
        if wh is None:
            continue
        s, u = t['scale']
        game.append((t['P'], tuple((st[0] * s / 32.0 / wh[0], st[1] * u / 32.0 / wh[1])
                                   for st in t['ST'])))
    if not game:
        raise ShadeError('P%sZ draws no textured triangle' % name)
    place = box_relation([p for t in tris for p in t['P']], ref)
    extent = max(ref[1][a] - ref[0][a] for a in range(3))
    rows = repeat_rows(game, vertices, draws, place, extent)
    f, med, share = repeat_correction(rows)
    return f, med, share, len(rows)


# ---- D-033: a background's untextured surfaces -------------------------------
#
# The remaster's stage background draws some surfaces with no image at all -
# by vertex colour alone (prepare.prepare_background's UNTEXTURED path) - and
# on Silo it whitened half of the rail-end caps of that kind: 30 triangles
# carry 0xFFFFFFFF where the game's room geometry under them is shaded teal.
# restore_background_shade, for the stages DIVERGENCE_ITEMS['D-033'] names:
#
#   1. the game's rooms are read from the ROM's background file with every
#      corner's vertex colour (read_bg_shaded: the room table, point tables
#      and primary and secondary lists as uvcarry.read_bg reads them), and
#      the remaster's item is related to them by the game's own scale for
#      the stage (stage_bg: 100 / levelinfotable's last column, the factor
#      the runtime draws the item by);
#   2. an untextured remastered triangle COINCIDES with the game triangle
#      that contains its centroid's projection, in a plane within
#      SHADE_PLANE_COS and SHADE_PLANE_DIST of its own (the remaster tilts
#      some caps by 11 degrees);
#   3. the remaster's colours relate to the game's through ONE per-channel
#      curve, measured on the untextured triangles it did NOT whiten:
#      hd = 255 (g / 255) ^ gamma, gamma the median over every corner
#      channel inside (0, 255) on both sides (Silo: 1.21 over 85 coinciding
#      triangles - the m2 cap's game 002424 is the remaster's 001818). Too
#      few pairs, or a spread past SHADE_GAMMA_SPREAD, refuses the stage;
#   4. a whitened triangle (pure white on every corner) whose coincident game
#      triangle is not white takes, corner by corner, the game's shade at
#      that corner carried through the curve. A vertex another triangle
#      uses (a textured one, or one taking another colour) is not changed,
#      and the triangle is reported and left white.
#
# The rule is general in form; which stages it runs on is a decision.

#: The remaster's draw lies on the game's: planes within ~15 degrees (the
#: remaster tilts some of Silo's caps by 11) and this many item units apart.
SHADE_PLANE_COS = 0.966
SHADE_PLANE_DIST = 2.0
SHADE_INSIDE = 0.02

#: The colour curve is taken from at least this many corner channels, and
#: its middle half lies within this much of its median, or the stage is
#: refused.
SHADE_GAMMA_MIN = 24
SHADE_GAMMA_SPREAD = 0.10

#: The game's bg vertex loads come from segment 14 (bg.c SPSEGMENT_BG_VTX).
SEG_BG_VTX = 0x0E

#: Render-mode bits of othermode_l (include/PR/gbi.h FORCE_BL, Z_UPD).
RM_FORCE_BL = 0x4000
RM_Z_UPD = 0x0020
RM_ZMODE = 0x0c00
RM_ZMODE_XLU = 0x0800


def _uvcarry():
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'texpack'))
    import uvcarry                                          # noqa: E402
    return uvcarry


def stage_bg(item, root=ROOT):
    """(LEVELID, ROM path of the background file, item units per room unit)
    for a background item. The item is the launcher's stage name, or the
    stage spelt without the game's number (the source's `bunker` is Bunker 1
    - sl_world_assets.c). The scale is the game's own: the source authors a
    stage background at the room coordinates times 100 / the stage's last
    levelinfotable column (bg.c; sl_world_assets.c sl_wa_source_scale reads
    the same column at runtime) - not fitted, because the remaster's
    backgrounds reach past the game's rooms and no two extents agree.

    The runtime's stage table is asked first: the source's `temple` is
    Egyptian's background (stage 32), while the launcher's `temple` is the
    multiplayer Temple (stage 38, bg_dish_all_p). Every item the table names
    before Egyptian resolves to the same stage either way."""
    uc = _uvcarry()
    got = None
    for name in _table_stages(item, root) + (item, item + '1'):
        try:
            got = uc.stage_of(root, name)
            break
        except uc.UvCarryError:
            continue
    if got is None:
        raise ShadeError('no stage for background item %r' % item)
    lid, rel = got
    seg = os.path.splitext(os.path.basename(rel))[0]
    with open(os.path.join(root, 'src', 'game', 'bg.c'), encoding='utf-8') as f:
        bgc = f.read()
    cols = set(re.findall(r'\{\s*LEVELID_\w+\s*,\s*"bg/%s\.seg"\s*,\s*"[^"]*"\s*,\s*[-0-9.eE]+\s*,'
                          r'\s*[-0-9.eE]+\s*,\s*([-0-9.eE]+)\s*\}' % re.escape(seg), bgc))
    if len(cols) != 1 or not float(cols.copy().pop()) > 0:
        raise ShadeError('levelinfotable gives %s no single scale column' % seg)
    return lid, rel, 100.0 / float(cols.pop())


def _table_stages(item, root=ROOT):
    """The launcher stage names the runtime's stage table gives this
    background item (sl_world_assets.c sl_wa_stage_item_path: `case
    LEVELID_CAVERNS: ... "cavern"`), as the enumerator's lower-case name - the
    source spells some items as the model does (`cavern`), not as the stage.
    Only the name of a sole stage is returned: an item two stages share
    (`surface`) already resolves by its own name."""
    try:
        with open(os.path.join(root, 'src', 'native', 'sl_world_assets.c'), encoding='utf-8') as f:
            text = f.read()
    except OSError:
        return ()
    found = re.findall(r'case\s+LEVELID_(\w+)\s*:[^;\n]*"%s"' % re.escape(item), text)
    return (found[0].lower(),) if len(found) == 1 else ()


def read_bg_shaded(bg):
    """The background file -> [(room, (p0, p1, p2), (c0, c1, c2))]: every
    triangle of every room's primary and secondary lists, corners in room
    units (point + room position), colours (r, g, b, a)."""
    return [(r, P, C) for r, _kind, _blend, P, C in read_bg_lists(bg)]


def read_bg_lists(bg):
    """read_bg_shaded's triangles with the list each is drawn from:
    [(room, kind, blend, (p0, p1, p2), (c0, c1, c2))], kind 'primary' (the
    room's opaque list, bgRenderRoomPrimary) or 'secondary' (its translucent
    list, bgRenderRoomSecondary), and blend True when the render mode in force
    (the list's B9 setothermode_l words, ucode05.txt) is a TRANSLUCENT SURFACE:
    FORCE_BL set, Z_UPD clear and the XLU depth mode - not a decal (ZMODE_DEC,
    which the game draws on another surface) and not an alpha cutout (whose
    mode writes depth)."""
    return [(r, kind, mode is not None and bool(mode & RM_FORCE_BL) and not (mode & RM_Z_UPD)
             and (mode & RM_ZMODE) == RM_ZMODE_XLU, P, C)
            for r, kind, mode, _tex, P, C, _st in read_bg_full(bg)]


def read_bg_full(bg):
    """Every triangle of every room's primary and secondary lists:
    [(room, kind, mode, tex, (p0, p1, p2), (c0, c1, c2), (st0, st1, st2))] -
    corners in room units, colours (r, g, b, a), the othermode_l in force (None
    before the list's first B9) and the texture number of the list's last C0
    (None before one), and each corner's raw s, t."""
    return [t[:7] for t in read_bg_walk(bg)]


def read_bg_walk(bg):
    """read_bg_full's triangles with one more field: the environment alpha
    in force (the list's last FB setenvcolor, ucode05.txt, its low byte; 255
    before one). The game's room lists set it per decal group (Frigate: 0x66,
    0x7f, 0x4c on the translucent lists), and at runtime the blended decal's
    alpha is the image's coverage times it (measured on the engine-room
    arrows and checker: combiner alpha TEXEL0 x ENV, or ENV alone)."""
    uc = _uvcarry()
    if len(bg) < 16:
        raise ShadeError('the background file is too short')
    u32 = lambda o: struct.unpack_from('>I', bg, o)[0]        # noqa: E731
    table = u32(4) & 0xffffff
    ents = []
    i = 0
    while True:
        o = table + i * 0x18
        if o + 0x18 > len(bg):
            raise ShadeError('the room table runs off the file')
        ents.append((u32(o) & 0xffffff, u32(o + 4) & 0xffffff, u32(o + 8) & 0xffffff,
                     struct.unpack_from('>fff', bg, o + 12)))
        if i >= 2 and ents[i][0] + 0x10 >= ents[1][1]:
            break
        i += 1
        if i > 1024:
            raise ShadeError('the room table does not end')
    out = []
    for r in range(1, len(ents) - 1):
        pt, pri, sec, pos = ents[r]
        if not pt:
            continue
        pts = uc.inflate_1172(bg[pt:])
        if pts is None:
            raise ShadeError('room %d: the point table does not inflate' % r)
        npt = len(pts) // 16
        P = [struct.unpack_from('>hhh', pts, k * 16) for k in range(npt)]
        C = [tuple(pts[k * 16 + 12:k * 16 + 16]) for k in range(npt)]
        ST = [struct.unpack_from('>hh', pts, k * 16 + 8) for k in range(npt)]
        for kind, start in (('primary', pri), ('secondary', sec)):
            if not start:
                continue
            dl = uc.inflate_1172(bg[start:])
            if dl is None:
                raise ShadeError('room %d: a list does not inflate' % r)
            slots = [None] * 16
            mode = None                     # othermode_l, unknown until a B9 sets it
            tex = None                      # the texture number, unknown until a C0
            env = 255                       # the environment alpha, until an FB sets it
            for o in range(0, len(dl) - 7, 8):
                w0, w1 = struct.unpack_from('>II', dl, o)
                op = w0 >> 24
                if op == 0xB8:
                    break
                if op == 0xFB:
                    env = w1 & 0xff
                elif op == 0xB9:
                    sh, ln = (w0 >> 8) & 0xff, w0 & 0xff
                    mask = ((1 << ln) - 1) << sh
                    mode = ((mode or 0) & ~mask & 0xffffffff) | (w1 & mask)
                elif op == 0xC0:
                    tex = w1 & 0xFFF
                elif op == 0x04:
                    n = ((w0 >> 20) & 0xf) + 1
                    v0 = (w0 >> 16) & 0xf
                    if (w1 >> 24) != SEG_BG_VTX or v0 + n > 16:
                        slots = [None] * 16
                        continue
                    base = (w1 & 0xffffff) // 16
                    for k in range(n):
                        slots[v0 + k] = base + k if base + k < npt else None
                elif op in (0xB1, 0xBF):
                    if op == 0xB1:
                        tl = []
                        for q in range(4):
                            x = (w1 >> (8 * q)) & 0xf
                            y = (w1 >> (8 * q + 4)) & 0xf
                            z = (w0 >> (4 * q)) & 0xf
                            if x | y | z:
                                tl.append((x, y, z))
                    else:
                        tl = [(((w1 >> 16) & 0xff) // 10, ((w1 >> 8) & 0xff) // 10,
                               (w1 & 0xff) // 10)]
                    for a, b, c in tl:
                        idx = (slots[a], slots[b], slots[c])
                        if None in idx or len(set(idx)) != 3:
                            continue
                        out.append((r, kind, mode, tex,
                                    tuple(tuple(P[j][q] + pos[q] for q in range(3)) for j in idx),
                                    tuple(C[j] for j in idx), tuple(ST[j] for j in idx), env))
    return out


class _Coincide(object):
    """The game's triangles, scaled into the item, bucketed for the search."""

    def __init__(self, game, scale, cell=64.0):
        self.T = []
        for room, P, C in game:
            Q = tuple(tuple(v * scale for v in p) for p in P)
            n = _unit_normal(Q)
            if n is not None:
                self.T.append((room, Q, C, n))
        self.cell = cell
        self.grid = {}
        for i, (_r, Q, _C, _n) in enumerate(self.T):
            lo = [int(math.floor(min(p[a] for p in Q) / cell)) for a in range(3)]
            hi = [int(math.floor(max(p[a] for p in Q) / cell)) for a in range(3)]
            if max(hi[a] - lo[a] for a in range(3)) > 64:
                continue                    # a sliver across the level
            for x in range(lo[0], hi[0] + 1):
                for y in range(lo[1], hi[1] + 1):
                    for z in range(lo[2], hi[2] + 1):
                        self.grid.setdefault((x, y, z), []).append(i)

    def under(self, P):
        """The game triangle a remastered triangle lies on, or None."""
        n = _unit_normal(P)
        if n is None:
            return None
        return self.at(n, tuple(sum(p[a] for p in P) / 3.0 for a in range(3)))

    def at(self, n, c):
        """The game triangle in a plane like the unit normal `n` that
        contains the point `c`, or None."""
        g = tuple(int(math.floor(c[a] / self.cell)) for a in range(3))
        best = None
        for i in sorted(set(self.grid.get(g, ()))):
            _r, Q, _C, m = self.T[i]
            if abs(_dot(m, n)) < SHADE_PLANE_COS or abs(_dot(_sub(c, Q[0]), m)) > SHADE_PLANE_DIST:
                continue
            b = bary(Q, c)
            if b is None or min(b) < -SHADE_INSIDE:
                continue
            if best is None or min(b) > best[0]:
                best = (min(b), i)
        return None if best is None else self.T[best[1]]


def _shade_at(T, p):
    """The game triangle T's vertex colour at point p (projected, clamped)."""
    b = bary(T[1], p) or (1.0, 0.0, 0.0)
    b = [max(0.0, v) for v in b]
    s = sum(b) or 1.0
    return tuple(sum(b[k] / s * T[2][k][ch] for k in range(3)) for ch in range(3))


def colour_curve(pairs):
    """gamma of hd = 255 (g / 255) ^ gamma from (hd, game) channel pairs in
    (0, 255) on both sides -> (gamma, n, spread) or None with too few."""
    gs = sorted(math.log(h / 255.0) / math.log(g / 255.0) for h, g in pairs
                if 0 < h < 255 and 0 < g < 255)
    if len(gs) < SHADE_GAMMA_MIN:
        return None
    med = gs[len(gs) // 2]
    spread = max(med - gs[len(gs) // 4], gs[(3 * len(gs)) // 4] - med)
    return med, len(gs), spread


def carry(shade, gamma):
    return tuple(int(round(255.0 * (v / 255.0) ** gamma)) if v > 0 else 0 for v in shade)


def restore_background_shade(rom, item, pools, draws, root=ROOT):
    """D-033 on a background item read by xblaworld.read_level: the whitened
    untextured triangles take the game's shade, carried through the stage's
    colour curve. Pool vertices are replaced IN PLACE. -> report dict:
    scale, gamma, n, spread, fixed [dict(room, centre, game, colour, tris)],
    refused [why], left (white triangles with no shaded game surface)."""
    uc = _uvcarry()
    _lid, rel, scale = stage_bg(item, root)
    try:
        game = read_bg_shaded(uc.rom_file(rom, rel, root))
    except uc.UvCarryError as exc:
        raise ShadeError(str(exc))
    return shade_pools(game, scale, pools, draws)


def shade_pools(game, scale, pools, draws):
    """restore_background_shade's rule on the game's triangles `game`
    (read_bg_shaded's form, room units) and an item `scale` times them."""
    tris = []                               # (draw, pool, (i0, i1, i2), untextured)
    for di, dr in enumerate(draws):
        p = pools[dr['pool']] if 0 <= dr.get('pool', -1) < len(pools) else None
        if p is None or p['vertices'] is None:
            continue
        ind = dr['indices']
        untex = dr.get('image') is None and not dr.get('slots')
        for t in range(0, len(ind) - 2, 3):
            tris.append((di, dr['pool'], (ind[t], ind[t + 1], ind[t + 2]), untex))
    look = _Coincide(game, scale)

    def corners(pl, idx):
        return [pools[pl]['vertices'][i] for i in idx]
    white, pairs = [], []
    for k, (di, pl, idx, untex) in enumerate(tris):
        if not untex:
            continue
        V = corners(pl, idx)
        P = tuple(tuple(v[:3]) for v in V)
        rgb = [tuple(int(x) for x in v[5:8]) for v in V]
        T = look.under(P)
        if all(c == WHITE for c in rgb):
            white.append((k, P, T))
        elif T is not None:
            for v, c in zip(P, rgb):
                s = _shade_at(T, v)
                pairs.extend((c[ch], s[ch]) for ch in range(3))
    rep = dict(scale=scale, gamma=None, n=0, spread=None, fixed=[], refused=[], left=0,
               white=len(white))
    curve = colour_curve(pairs)
    if curve is None:
        raise ShadeError('too few coinciding untextured surfaces to relate the colours '
                         '(%d channel pairs)' % len(pairs))
    gamma, n, spread = curve
    rep.update(gamma=gamma, n=n, spread=spread)
    if spread > SHADE_GAMMA_SPREAD:
        raise ShadeError('the colours do not follow one curve (gamma %.3f, spread %.3f)'
                         % (gamma, spread))
    # every triangle's use of every vertex, so a shared vertex is never moved
    users = {}
    for k, (di, pl, idx, _u) in enumerate(tris):
        for i in idx:
            users.setdefault((pl, i), set()).add(k)
    target = {}
    plan = {}
    for k, P, T in white:
        if T is None or all(_shade_at(T, v) == (255.0, 255.0, 255.0) for v in P):
            rep['left'] += 1
            continue
        plan[k] = (T, [carry(_shade_at(T, v), gamma) for v in P])
    for k, (T, cols) in plan.items():
        _di, pl, idx, _u = tris[k]
        for i, c in zip(idx, cols):
            target.setdefault((pl, i), set()).add(c)
    done = set()
    for k, (T, cols) in sorted(plan.items()):
        _di, pl, idx, _u = tris[k]
        bad = [i for i in idx if not users[(pl, i)] <= set(plan) or len(target[(pl, i)]) != 1]
        if bad:
            rep['refused'].append('triangle at %s: a vertex another surface shares'
                                  % [round(float(x), 1) for x in corners(pl, idx)[0][:3]])
            continue
        for i, c in zip(idx, cols):
            if (pl, i) in done:
                continue
            v = pools[pl]['vertices'][i]
            pools[pl]['vertices'][i] = type(v)(tuple(v[:5]) + c + tuple(v[8:]))
            done.add((pl, i))
        rep['fixed'].append(dict(room=T[0], tri=k, centre=tuple(sum(p[a] for p in corners(pl, idx))
                                                                    / 3.0 for a in range(3)),
                                 game=tuple(int(round(x)) for x in _shade_at(T, T[1][0])),
                                 colour=cols[0]))
    return rep


def describe_shade(rep):
    """The preparation's log lines for restore_background_shade's report,
    one per surface (triangles joined by the room and colour they took)."""
    caps = {}
    for f in rep['fixed']:
        key = (f['room'], f['game'], f['colour'])
        caps.setdefault(key, []).append(f['centre'])
    lines = ['D-033: %d whitened untextured triangle%s; colours carried through gamma %.3f '
             '(%d channel pairs, spread %.3f); scale %.6f; %d fixed, %d left (no shaded game '
             'surface), %d refused'
             % (rep['white'], '' if rep['white'] == 1 else 's', rep['gamma'], rep['n'],
                rep['spread'], rep['scale'], len(rep['fixed']), rep['left'], len(rep['refused']))]
    for (room, g, c), cs in sorted(caps.items()):
        lines.append('  room %d: %d triangle%s, game %02x%02x%02x -> %02x%02x%02x'
                     % (room, len(cs), '' if len(cs) == 1 else 's', g[0], g[1], g[2],
                        c[0], c[1], c[2]))
    lines.extend('  refused %s' % w for w in rep['refused'])
    return lines


# ---- D-035: the remaster's hull below the game's waterline ------------------
#
# The game draws nothing opaque below a stage's lowest opaque room surface;
# what it draws below is translucent. Frigate's hull is the case: room 35's
# opaque hull side stops at world -259 - the waterline, where the boarding
# boat's bottom also sits (-254.7) - and below it the room's SECONDARY list
# carries a skirt down to -876, textured with a vertical alpha ramp (image
# 0x4fd, alpha 7 .. 250) under a shade that runs from black at the waterline
# to white at depth, so the hull fades into the sea. Measured on Frigate's
# rooms: every game triangle with a corner below world -262 is one of those
# 30 skirt triangles (24 on the port side, 6 at the bow). The remaster's
# background reproduces the skirt as OPAQUE hull paint (one image, brighter
# with depth), so drawn as authored the hull continues some 600 units under
# the boat and the sea reads as standing far below it; and its hull
# triangles do not follow the waterline - 171 of them run from above it to
# below it - so withholding whole triangles leaves a jagged hull edge that
# follows the remaster's triangulation (playtest run 20261006-185746, mark
# 1). withhold_translucent_only, for the stages DIVERGENCE_ITEMS['D-035']
# names:
#
#   1. the game's rooms are read from the ROM's background file with the list
#      each triangle is drawn from (read_bg_lists), and related to the item by
#      the game's own scale for the stage (stage_bg), as D-033 does;
#   2. the WATERLINE is the lowest corner of any opaque game room triangle (a
#      primary-list triangle not drawn as a translucent surface): the game
#      draws nothing opaque below it. A stage whose rooms carry no game FADE
#      (a translucent secondary-list triangle whose shade runs from FADE_LO
#      or darker to FADE_HI or brighter, is_fade) has no waterline and is
#      left as authored;
#   3. every triangle of an OPAQUE remastered draw (the source's style low
#      byte not 2, and an image that carries no alpha - a cutout's shape is
#      left alone) is cut by the horizontal plane at the waterline: the part
#      below is withheld, the part above is kept, its new corners carrying
#      the vertex fields interpolated along the cut edges. A triangle wholly
#      below is withheld; one wholly above is untouched;
#   4. a pool whose vertices carry more than the vertex record (animation
#      fields, a tangent frame) or a draw with a second layer (its own copy
#      of the pool) takes no new vertex: there a triangle is withheld only
#      when wholly below;
#   5. nothing else of a draw, its pool or its image moves; new corners are
#      appended to the pool.
#
# Under PRESENTATION XBLA the game's own room triangles are suppressed, so
# below the waterline the game's sea shows, at one height along the whole
# hull, as it does through the game's fade. The rule is general in form;
# which stages it runs on is a decision.


#: A fade's shade spans at least this range across its corners (any channel).
FADE_LO = 32
FADE_HI = 224


def is_fade(C):
    """Whether corner colours `C` run from FADE_LO or darker to FADE_HI or
    brighter in some channel (r, g, b, a)."""
    return any(min(c[ch] for c in C) <= FADE_LO and max(c[ch] for c in C) >= FADE_HI
               for ch in range(4))


def withhold_translucent_only(rom, item, draws, pools, textures, root=ROOT):
    """D-035 on a background item read by xblaworld.read_level: the opaque
    remastered triangles are cut at the game's waterline and their part below
    it is withheld, IN PLACE. -> report dict: scale, waterline (item units),
    withheld [dict(draw, centre)] (wholly below), clipped [dict(draw, centre,
    kept)] (cut there), draws (the draws that lost any), added (new
    vertices)."""
    uc = _uvcarry()
    _lid, rel, scale = stage_bg(item, root)
    try:
        game = read_bg_lists(uc.rom_file(rom, rel, root))
    except uc.UvCarryError as exc:
        raise ShadeError(str(exc))
    return withhold_pools(game, scale, draws, pools, textures)


def waterline(game, scale):
    """The lowest corner of an opaque game room triangle, in item units (the
    game's triangles `game` in read_bg_lists' form, room units, the item
    `scale` times them), or None when the rooms carry no fade."""
    if not any(k == 'secondary' and bl and is_fade(C) for _r, k, bl, _P, C in game):
        return None
    low = [p[1] for _r, k, bl, P, _C in game if k == 'primary' and not bl for p in P]
    return min(low) * scale if low else None


def _split(poly, s, d):
    """Convex polygon `poly` [(point, weights)] cut by the plane s.x = d:
    (the part with s.x >= d, the part with s.x <= d), each None when empty."""
    eps = 1e-6
    dist = [_dot(s, p) - d for p, _w in poly]
    if all(v >= -eps for v in dist):
        return poly, None
    if all(v <= eps for v in dist):
        return None, poly
    front, back = [], []
    k = len(poly)
    for i in range(k):
        a, b = poly[i], poly[(i + 1) % k]
        da, db = dist[i], dist[(i + 1) % k]
        if da >= -eps:
            front.append(a)
        if da <= eps:
            back.append(a)
        if (da > eps and db < -eps) or (da < -eps and db > eps):
            t = da / (da - db)
            q = (tuple(a[0][j] + t * (b[0][j] - a[0][j]) for j in range(3)),
                 tuple(a[1][j] + t * (b[1][j] - a[1][j]) for j in range(3)))
            front.append(q)
            back.append(q)
    return (front if len(front) >= 3 else None), (back if len(back) >= 3 else None)


def _blend_vertex(V, w):
    """The pool vertex at weights `w` over the corner vertices `V`: every
    field interpolated, the integer fields (the colour) rounded."""
    out = []
    for k in range(len(V[0])):
        v = w[0] * V[0][k] + w[1] * V[1][k] + w[2] * V[2][k]
        if all(isinstance(q[k], int) for q in V):
            v = int(round(v))
        out.append(v)
    return tuple(out)


def withhold_pools(game, scale, draws, pools, textures):
    """withhold_translucent_only's rule on the game's triangles `game`
    (read_bg_lists' form, room units) and an item `scale` times them."""
    wl = waterline(game, scale)
    rep = dict(scale=scale, waterline=wl, withheld=[], clipped=[], draws=0, added=0)
    if wl is None:
        return rep
    up = (0.0, 1.0, 0.0)
    for di, dr in enumerate(draws):
        p = pools[dr['pool']] if 0 <= dr.get('pool', -1) < len(pools) else None
        if p is None or p['vertices'] is None:
            continue
        if ((dr.get('style') or 0) & 0xff) == 2:
            continue                                # the source blends it already
        ti = dr.get('image')
        if ti is None or not 0 <= ti < len(textures) or textures[ti].get('has_alpha'):
            continue                                # untextured, or a cutout's shape
        cut_ok = p.get('aux') is None and p.get('frame') is None and dr.get('layer') is None
        ind = dr['indices']
        keep, lost, made = [], 0, {}
        for t in range(0, len(ind) - 2, 3):
            idx = (ind[t], ind[t + 1], ind[t + 2])
            V = [p['vertices'][i] for i in idx]
            P = tuple(tuple(float(x) for x in v[:3]) for v in V)
            if min(q[1] for q in P) >= wl:
                keep.extend(idx)
                continue
            c = tuple(sum(q[a] for q in P) / 3.0 for a in range(3))
            if max(q[1] for q in P) <= wl:
                lost += 1
                rep['withheld'].append(dict(draw=di, centre=c))
                continue
            if not cut_ok:
                keep.extend(idx)
                continue
            above, _below = _split([(P[0], (1.0, 0.0, 0.0)), (P[1], (0.0, 1.0, 0.0)),
                                    (P[2], (0.0, 0.0, 1.0))], up, wl)
            lost += 1
            rep['clipped'].append(dict(draw=di, centre=c,
                                       kept=0 if above is None else len(above) - 2))
            if above is None:
                continue
            vi = []
            for _pt, w in above:
                j = next((k for k in range(3) if w[k] > 1.0 - 1e-9), None)
                if j is not None:
                    vi.append(idx[j])
                    continue
                # a cut corner lies on one edge: keyed by that edge's two
                # vertices and the weight of the lower-numbered one, so the
                # neighbour across the edge reuses it
                e = sorted((idx[k], w[k]) for k in range(3) if w[k] > 1e-9)
                key = (e[0][0], e[-1][0], round(e[0][1], 9))
                if key not in made:
                    made[key] = len(p['vertices'])
                    p['vertices'].append(_blend_vertex(V, w))
                    rep['added'] += 1
                vi.append(made[key])
            for k in range(1, len(vi) - 1):
                keep.extend((vi[0], vi[k], vi[k + 1]))
        if lost:
            rep['draws'] += 1
            dr['indices'] = type(ind)(keep) if not isinstance(ind, list) else keep
    return rep


def describe_withheld(rep):
    """The preparation's log lines for withhold_translucent_only's report."""
    if rep.get('waterline') is None:
        return ["D-035: the game's rooms carry no fade - no waterline, nothing withheld"]
    cl = rep['clipped']
    kept = sum(c['kept'] for c in cl)
    return ["D-035: the game's waterline at item y %.2f (the lowest opaque room corner); "
            '%d opaque triangle%s wholly below it withheld, %d cut there (%d triangle%s kept '
            'above it), %d vertices added, %d draw%s touched'
            % (rep['waterline'], len(rep['withheld']), '' if len(rep['withheld']) == 1 else 's',
               len(cl), kept, '' if kept == 1 else 's', rep['added'], rep['draws'],
               '' if rep['draws'] == 1 else 's')]


# ---- D-036: a decal placed the other way round from the game's -------------
#
# The game paints a decal - a room triangle drawn in the decal depth mode
# (ZMODE_DEC) - with its texture in one orientation wherever it uses it:
# Frigate's yellow "-06-" deck number (texture 0x1e0) reads the same way at
# all ten of its placements. The remaster draws those ten from one draw and
# one image, and authored four of them with the image's coordinates turned
# half round (both axes reversed against the other six), so the number reads
# "-90-" on four walls (playtest run 20261006-185746, mark 3 - the red wall
# beside the stair in room 25). orient_decals, for the stages
# DIVERGENCE_ITEMS['D-036'] names:
#
#   1. the game's decal triangles are read from the ROM's background file
#      with each corner's s and t (read_bg_full), scaled into the item, and
#      each gives the directions +s and +t run in its plane;
#   2. a triangle of a remastered draw in the source's decal pass (style bit
#      DECAL_STYLE) that lies on a game decal triangle (coplanar within
#      SHADE_PLANE_COS and SHADE_PLANE_DIST, its centroid inside) relates its
#      own +u and +v to the game's +s and +t: one of the four sign pairs when
#      +u runs along +s or -s and +v along +t or -t, or no relation;
#   3. the relation the placements of one image on one game texture share is
#      the one MORE THAN HALF of them hold (counted per placement - a group
#      of triangles of one draw sharing vertices - with every triangle of the
#      placement in agreement);
#   4. a placement holding another sign pair has the axes that differ
#      reversed within its own coordinate range (u' = umin + umax - u), on
#      its own vertices: a vertex another triangle also uses is copied
#      first. Nothing else of the draw, its pool or its image moves.
#
# The rule is general in form; which stages it runs on is a decision.

#: The source's decal pass: style bit 0x2000 (prepare.prepare_background).
DECAL_STYLE = 0x2000
RM_ZMODE_DEC = 0x0c00


def _uv_frame(P, U):
    """The directions (+u, +v) of a triangle with corners P and coordinates
    U, in its plane (unnormalised gradients), or None when degenerate."""
    e1 = _sub(P[1], P[0]); e2 = _sub(P[2], P[0])
    d1 = (U[1][0] - U[0][0], U[1][1] - U[0][1]); d2 = (U[2][0] - U[0][0], U[2][1] - U[0][1])
    det = d1[0] * d2[1] - d1[1] * d2[0]
    if abs(det) < 1e-12:
        return None
    tu = tuple((e1[k] * d2[1] - e2[k] * d1[1]) / det for k in range(3))
    tv = tuple((e2[k] * d1[0] - e1[k] * d2[0]) / det for k in range(3))
    return tu, tv


def _relation(fr, gfr):
    """(su, sv): +u along su * +s and +v along sv * +t, or None."""
    def unit(a):
        n = math.sqrt(_dot(a, a))
        return tuple(x / n for x in a) if n > 1e-12 else None
    tu, tv = unit(fr[0]), unit(fr[1])
    ts, tt = unit(gfr[0]), unit(gfr[1])
    if None in (tu, tv, ts, tt):
        return None
    us, ut, vs, vt = _dot(tu, ts), _dot(tu, tt), _dot(tv, ts), _dot(tv, tt)
    if abs(us) < 0.9 or abs(vt) < 0.9 or abs(ut) > 0.45 or abs(vs) > 0.45:
        return None
    return (1 if us > 0 else -1, 1 if vt > 0 else -1)


def orient_decals(rom, item, draws, pools, root=ROOT):
    """D-036 on a background item read by xblaworld.read_level: decal
    placements turned against the game's orientation are turned back, IN
    PLACE. -> report dict (orient_pools)."""
    uc = _uvcarry()
    _lid, rel, scale = stage_bg(item, root)
    try:
        game = read_bg_full(uc.rom_file(rom, rel, root))
    except uc.UvCarryError as exc:
        raise ShadeError(str(exc))
    return orient_pools(game, scale, draws, pools)


def orient_pools(game, scale, draws, pools):
    """orient_decals' rule on the game's triangles `game` (read_bg_full's
    form, room units) and an item `scale` times them. -> dict(scale,
    images [dict(draw, image, tex, relation, placements, turned [centre]))."""
    dec = []
    for r, _kind, mode, tex, P, _C, ST in game:
        if mode is None or (mode & RM_ZMODE) != RM_ZMODE_DEC:
            continue
        Q = tuple(tuple(v * scale for v in p) for p in P)
        fr = _uv_frame(Q, ST)
        if fr is not None:
            dec.append((r, P, (tex, fr)))
    grid = _Coincide(dec, scale)
    rep = dict(scale=scale, images=[])
    used = {}
    for dr in draws:
        for i in dr.get('indices') or ():
            used[(dr['pool'], i)] = used.get((dr['pool'], i), 0) + 1
    for di, dr in enumerate(draws):
        if not ((dr.get('style') or 0) & DECAL_STYLE) or dr.get('image') is None:
            continue
        p = pools[dr['pool']] if 0 <= dr.get('pool', -1) < len(pools) else None
        if p is None or p['vertices'] is None or p.get('sphere') is not None \
                or dr.get('layer') is not None:
            continue
        ind = list(dr['indices'])
        # placements: triangles of this draw joined by shared vertices
        parent = list(range(len(ind) // 3))

        def find(a):
            while parent[a] != a:
                parent[a] = parent[parent[a]]
                a = parent[a]
            return a
        first = {}
        for t in range(len(ind) // 3):
            for i in ind[3 * t:3 * t + 3]:
                if i in first:
                    parent[find(t)] = find(first[i])
                else:
                    first[i] = t
        groups = {}
        for t in range(len(ind) // 3):
            groups.setdefault(find(t), []).append(t)
        place = []                                   # (tris, tex, relation)
        for tris in groups.values():
            rels, texs = set(), set()
            for t in tris:
                V = [p['vertices'][i] for i in ind[3 * t:3 * t + 3]]
                P = tuple(tuple(float(x) for x in v[:3]) for v in V)
                fr = _uv_frame(P, [(float(v[3]), float(v[4])) for v in V])
                G = grid.under(P)
                rl = _relation(fr, G[2][1]) if fr is not None and G is not None else None
                rels.add(rl)
                texs.add(G[2][0] if G is not None else None)
            if len(rels) == 1 and None not in rels and len(texs) == 1:
                place.append((tris, texs.pop(), rels.pop()))
        by = {}
        for tris, tex, rl in place:
            by.setdefault(tex, []).append((tris, rl))
        for tex, pl in sorted(by.items()):
            count = {}
            for _tris, rl in pl:
                count[rl] = count.get(rl, 0) + 1
            rl_major, n = max(count.items(), key=lambda kv: (kv[1], kv[0]))
            if 2 * n <= len(pl):
                continue
            turned = []
            for tris, rl in pl:
                if rl[0] == rl_major[0] or rl[1] == rl_major[1]:
                    continue                # agrees, or is a mirror (left alone)
                flip = (True, True)
                vs = sorted({i for t in tris for i in ind[3 * t:3 * t + 3]})
                lo = [min(float(p['vertices'][i][3 + a]) for i in vs) for a in range(2)]
                hi = [max(float(p['vertices'][i][3 + a]) for i in vs) for a in range(2)]
                remap = {}
                for i in vs:
                    v = list(p['vertices'][i])
                    for a in range(2):
                        if flip[a]:
                            v[3 + a] = type(v[3 + a])(lo[a] + hi[a] - float(v[3 + a]))
                    mine = sum(1 for t in tris for j in ind[3 * t:3 * t + 3] if j == i)
                    if used.get((dr['pool'], i), 0) > mine:
                        remap[i] = len(p['vertices'])
                        p['vertices'].append(tuple(v))
                    else:
                        p['vertices'][i] = tuple(v)
                for t in tris:
                    for k in range(3):
                        j = ind[3 * t + k]
                        ind[3 * t + k] = remap.get(j, j)
                c = [sum(float(p['vertices'][ind[3 * t + k]][a]) for t in tris for k in range(3))
                     / (3.0 * len(tris)) for a in range(3)]
                turned.append(tuple(c))
            if turned:
                dr['indices'] = type(dr['indices'])(ind) if not isinstance(dr['indices'], list) else ind
            rep['images'].append(dict(draw=di, image=dr['image'], tex=tex, relation=rl_major,
                                      placements=len(pl), turned=turned))
    return rep


#: D-037's counterpart test: a remastered decal placement takes a game
#: decal's alpha only when its extent and the extent of the game triangles
#: under it, in their plane, overlap by at least this intersection over
#: union. Measured on Frigate: the placements that redraw a game decal (the
#: deck dashes, arrows, signs, the red wall arrow) at 0.44 .. 0.99; the ones
#: lying on a wall-sized game overlay they do not redraw (a remastered vent,
#: 20-unit white studs on the 0x21a grain tile under 0x4c) at 0.05 and below.
DECAL_EXTENT_IOU = 0.25


def decal_extent_iou(a, b):
    """The intersection over union of the bounding boxes of the point sets
    `a` and `b` in the plane of `a` (the axis its normal leans on most is
    dropped); 0.0 when either is degenerate."""
    n = _unit_normal(tuple(a[:3])) if len(a) >= 3 else None
    if n is None or not b:
        return 0.0
    drop = max(range(3), key=lambda k: abs(n[k]))
    inter = area_a = area_b = 1.0
    for k in range(3):
        if k == drop:
            continue
        la, ha = min(v[k] for v in a), max(v[k] for v in a)
        lb, hb = min(v[k] for v in b), max(v[k] for v in b)
        inter *= max(0.0, min(ha, hb) - max(la, lb))
        area_a *= ha - la
        area_b *= hb - lb
    union = area_a + area_b - inter
    return inter / union if union > 0 else 0.0


def _placements(ind):
    """The placements of a draw's index list: its triangles joined by shared
    vertices -> [[triangle, ...], ...] in order of first triangle."""
    parent = list(range(len(ind) // 3))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    first = {}
    for t in range(len(ind) // 3):
        for i in ind[3 * t:3 * t + 3]:
            if i in first:
                parent[find(t)] = find(first[i])
            else:
                first[i] = t
    groups = {}
    for t in range(len(ind) // 3):
        groups.setdefault(find(t), []).append(t)
    return [groups[k] for k in sorted(groups, key=lambda k: groups[k][0])]


def decal_opacity(rom, item, draws, pools, root=ROOT):
    """D-037 on a background item read by xblaworld.read_level: a remastered
    decal (the source's decal pass, style bit DECAL_STYLE), translucent or
    opaque, lying on a game decal the game blends under an environment alpha
    below 255 takes that alpha into its vertex alpha and is drawn blended,
    IN PLACE. -> report dict (opacity_pools)."""
    uc = _uvcarry()
    _lid, rel, scale = stage_bg(item, root)
    try:
        game = read_bg_walk(uc.rom_file(rom, rel, root))
    except uc.UvCarryError as exc:
        raise ShadeError(str(exc))
    return opacity_pools(game, scale, draws, pools)


def opacity_pools(game, scale, draws, pools):
    """decal_opacity's rule on the game's triangles `game` (read_bg_walk's
    form, room units) and an item `scale` times them. The game's decal (a
    room triangle in the decal depth mode, blended - FORCE_BL) is found under
    each triangle of a remastered decal draw as D-036 finds it (coplanar
    within 15 degrees and 2 item units, centroid inside); its environment
    alpha `a` < 255 scales the alpha of the triangle's corners by a / 255, on
    a copy of the corner appended to its pool (one copy per corner and
    alpha), so no other triangle's corner moves.

    The source's blend (style low byte: 2 translucent, else opaque) does not
    decide which decals the game paints see-through - the game's list does.
    Frigate's deck markings come from both: the engine-room arrows and
    checker from translucent draws, the yellow dashed arcs and circles on
    the decks (image 61, 178 triangles over the game's textures 0x7 and 0x8
    under 0x66 / 0x7f) from an OPAQUE decal draw, where a vertex alpha is
    never read. So the triangles of an opaque decal draw that take an alpha
    are drawn translucent: the draw's style low byte becomes 2 when every
    triangle of it takes one, and otherwise those triangles leave it for a
    copy of the draw with low byte 2 placed straight after it (the decal
    order is kept), the rest staying opaque. -> dict(scale, draws
    [dict(draw, image, alphas {a: tris}, tris, opaque, moved)]) - `draw` the
    index in the list as it was read, `moved` the triangles an opaque draw
    gave to its translucent copy (0 when it turned translucent whole)."""
    dec = []
    for r, _kind, mode, _tex, P, _C, _ST, env in game:
        if mode is None or (mode & RM_ZMODE) != RM_ZMODE_DEC or not (mode & RM_FORCE_BL):
            continue
        dec.append((r, P, env))
    grid = _Coincide(dec, scale)
    rep = dict(scale=scale, draws=[])
    plan = []
    for di, dr in enumerate(draws):
        st = dr.get('style') or 0
        if not (st & DECAL_STYLE) or dr.get('image') is None:
            continue
        p = pools[dr['pool']] if 0 <= dr.get('pool', -1) < len(pools) else None
        if p is None or p['vertices'] is None or p.get('sphere') is not None \
                or dr.get('layer') is not None:
            continue
        ind = dr['indices']
        per, under = [], []
        for t in range(len(ind) // 3):
            V = [p['vertices'][i] for i in ind[3 * t:3 * t + 3]]
            G = grid.under(tuple(tuple(float(x) for x in v[:3]) for v in V))
            per.append(G[2] if G is not None and G[2] < 255 else None)
            under.append(G[1] if G is not None else None)
        # a placement (the draw's triangles joined by shared vertices, as
        # D-036 counts them) takes the alpha whole or not at all: one
        # triangle of a sign that happens to lie on another, fainter game
        # decal must not fade half the sign. And only from the game decal
        # it REPRODUCES - one of about its extent, not a wall-sized overlay
        # it happens to lie on (decal_extent_iou).
        for tris in _placements(ind):
            if any(per[t] is None for t in tris) or decal_extent_iou(
                    [tuple(float(x) for x in p['vertices'][i][:3])
                     for t in tris for i in ind[3 * t:3 * t + 3]],
                    [q for t in tris for q in under[t]]) < DECAL_EXTENT_IOU:
                for t in tris:
                    per[t] = None
        alphas = {}
        for a in per:
            if a is not None:
                alphas[a] = alphas.get(a, 0) + 1
        if alphas:
            plan.append((di, dr, per))
            rep['draws'].append(dict(draw=di, image=dr['image'], alphas=alphas,
                                     tris=len(ind) // 3, opaque=(st & 0xff) != 2,
                                     moved=0))
    done = {}                                        # (pool, vertex, alpha) -> index
    split = []                                       # (draw index, translucent copy)
    for (di, dr, per), rd in zip(plan, rep['draws']):
        p = pools[dr['pool']]
        kind = type(dr['indices']) if not isinstance(dr['indices'], list) else list
        src = list(dr['indices'])
        ind = list(src)
        for t, a in enumerate(per):
            if a is None:
                continue
            for k in range(3):
                i = ind[3 * t + k]
                key = (dr['pool'], i, a)
                j = done.get(key)
                if j is None:
                    v = list(p['vertices'][i])
                    v[8] = type(v[8])(int(round(int(v[8]) * a / 255.0)))
                    j = len(p['vertices'])
                    p['vertices'].append(tuple(v))
                    done[key] = j
                ind[3 * t + k] = j
        st = dr.get('style') or 0
        if (st & 0xff) == 2 or all(a is not None for a in per):
            dr['indices'] = kind(ind)
            dr['style'] = (st & ~0xff) | 2
            continue
        keep, take = [], []
        for t, a in enumerate(per):
            if a is None:
                keep.extend(src[3 * t:3 * t + 3])
            else:
                take.extend(ind[3 * t:3 * t + 3])
        cp = copy.deepcopy(dr)
        cp['indices'] = kind(take)
        cp['style'] = (st & ~0xff) | 2
        dr['indices'] = kind(keep)
        rd['moved'] = len(take) // 3
        split.append((di, cp))
    for di, cp in reversed(split):
        draws.insert(di + 1, cp)
    return rep


def describe_opacity(rep):
    """The preparation's log lines for decal_opacity's report."""
    n = sum(sum(d['alphas'].values()) for d in rep['draws'])
    lines = ['D-037: %d decal draw%s on the game\'s blended decals, %d triangle%s '
             'take the game\'s decal alpha'
             % (len(rep['draws']), '' if len(rep['draws']) == 1 else 's', n, '' if n == 1 else 's')]
    for d in rep['draws']:
        how = ''
        if d['opaque']:
            how = (' (opaque: drawn translucent)' if not d['moved'] else
                   ' (opaque: %d to a translucent copy)' % d['moved'])
        lines.append('  draw %d image %d: %s of %d triangles%s'
                     % (d['draw'], d['image'],
                        ', '.join('%d at %d/255' % (c, a) for a, c in sorted(d['alphas'].items())),
                        d['tris'], how))
    return lines


def describe_oriented(rep):
    """The preparation's log lines for orient_decals' report."""
    turned = [im for im in rep['images'] if im['turned']]
    lines = ['D-036: %d decal image%s placed on the game\'s decals, %d placement%s turned to the '
             'orientation the others share'
             % (len(rep['images']), '' if len(rep['images']) == 1 else 's',
                sum(len(im['turned']) for im in turned),
                '' if sum(len(im['turned']) for im in turned) == 1 else 's')]
    for im in turned:
        lines.append('  draw %d image %d on game texture %#x: %d of %d placements, at %s'
                     % (im['draw'], im['image'], im['tex'] if im['tex'] is not None else -1,
                        len(im['turned']), im['placements'],
                        ', '.join('(%.0f %.0f %.0f)' % c for c in im['turned'])))
    return lines


# ---- D-048: the game's water moves on the remaster's ------------------------
#
# The game animates one room image by its number: texture 1508, Caverns' water
# (tex.c, the room-list conversion's "Caverns water" branch). Its draws stand
# under MipMap2C_Something_Setup (unk_092E50.c): two wrapping 64-texel tiles of
# the image, whose origins the water controller sub_GAME_7F092E50 advances
# every tick - S by 0.25 and T by 0.1 quarter-texels per g_ClockTimer step,
# tile 1 at (+90, +150) from tile 0 - blended by a PRIM_LOD_FRAC that follows
# the sine of a third phase (0.04 a step). The remaster's background item draws
# the same surfaces with the game's own coordinates in its convention, u =
# s / 2048 + c and v = c' - t / 2048 (s, t the game's 10.5 coordinates; 2048 is
# 32 times the 64-texel tile), and nothing in its records moves them: its
# draws carry the rock's pixel program, no constant upload, no wave pool and no
# material record. Measured on Caverns: both draws that name the water image
# (0x854 and 0x864, 41 triangles) lie on the game's texture-1508 triangles of
# rooms 15, 16, 37 and 61, their plane up to 3.6 item units (0.48 room units)
# above the game's, and every triangle's coordinate gradient is the relation
# above to 0.1%.
#
# water_draws marks a draw every triangle of which lies on such a surface and
# follows that relation; the preparation gives its material
# slwa.MAT_GAME_WATER, and the runtime moves the coordinates by the game's own
# phases (sl_wa_water_shift). A draw whose coordinates follow anything else is
# reported and left as it is. Texture 1511 (the Dam's and Complex's water,
# MipMap2C_Something2_Setup, a 32-texel tile) is not covered: its relation
# would be another, and the Dam's remaster water carries its own animation.

#: The game's room images the water controller moves, and the tile (texels)
#: the setup list wraps them at (MipMap2C_Something_Setup, mask 6).
WATER_TEXTURES = (1508,)
WATER_TILE = 64
#: The remaster's plane may stand this many room units off the game's (0.48
#: measured), and a centroid this far outside the game's triangle still lies
#: on it.
WATER_PLANE_ROOM = 1.0
WATER_INSIDE = 0.02
#: The coordinate gradient: one unit of u per 32 x WATER_TILE of s, and of v
#: against t; this much relative slack on each term.
WATER_PER_ST = 1.0 / (32.0 * WATER_TILE)
WATER_SLOPE_TOL = 0.02


def _solve3(M, y):
    """x with M x = y for a 3x3 M (rows), or None when M is singular."""
    def det(A):
        return (A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
                - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
                + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]))
    d = det(M)
    if abs(d) < 1e-9:
        return None
    out = []
    for c in range(3):
        A = [list(r) for r in M]
        for r in range(3):
            A[r][c] = y[r]
        out.append(det(A) / d)
    return out


def game_water(rom, item, draws, pools, root=ROOT):
    """D-048 on a background item read by xblaworld.read_level: marks each
    draw that redraws the game's moving water (water_draws) with
    `game_water` = True. -> water_draws' report."""
    uc = _uvcarry()
    _lid, rel, scale = stage_bg(item, root)
    try:
        full = read_bg_full(uc.rom_file(rom, rel, root))
    except uc.UvCarryError as exc:
        raise ShadeError(str(exc))
    marked, rep = water_draws(full, scale, draws, pools)
    for di in marked:
        draws[di]['game_water'] = True
    return rep


def water_draws(full, scale, draws, pools):
    """The rule on the game's triangles `full` (read_bg_full's form, room
    units) and an item `scale` times them -> (marked draw indices, report:
    game triangles, marked [dict(at, tris)], refused [dict(at, tris, on,
    why)])."""
    W = []
    for t in full:
        if t[3] not in WATER_TEXTURES:
            continue
        Q = tuple(tuple(v * scale for v in p) for p in t[4])
        n = _unit_normal(Q)
        if n is not None:
            W.append((Q, n, t[6]))
    rep = dict(game=len(W), marked=[], refused=[])
    marked = []
    if not W:
        return marked, rep
    tol = WATER_PLANE_ROOM * scale
    lo, hi = WATER_PER_ST * (1.0 - WATER_SLOPE_TOL), WATER_PER_ST * (1.0 + WATER_SLOPE_TOL)
    cross = WATER_PER_ST * WATER_SLOPE_TOL
    for di, dr in enumerate(draws):
        if dr.get('image') is None or any(dr.get(k) is not None
                                          for k in ('layer', 'xform', 'wave', 'material')):
            continue
        p = pools[dr['pool']] if 0 <= dr.get('pool', -1) < len(pools) else None
        if p is None or not p['vertices']:
            continue
        ind = dr['indices']
        on = off = 0
        why = None
        for t in range(0, len(ind) - 2, 3):
            V = [p['vertices'][ind[t + k]] for k in range(3)]
            P = [tuple(float(x) for x in v[:3]) for v in V]
            n = _unit_normal(P)
            if n is None:
                continue
            c = tuple(sum(q[a] for q in P) / 3.0 for a in range(3))
            hit = None
            for Q, m, st in W:
                if abs(_dot(m, n)) < SHADE_PLANE_COS or abs(_dot(_sub(c, Q[0]), m)) > tol:
                    continue
                b = bary(Q, c)
                if b is not None and min(b) >= -WATER_INSIDE:
                    hit = (Q, st)
                    break
            if hit is None:
                off += 1
                continue
            on += 1
            if why is not None:
                continue
            Q, st = hit
            S = []
            for q in P:
                b = bary(Q, q)
                S.append((sum(b[k] * st[k][0] for k in range(3)),
                          sum(b[k] * st[k][1] for k in range(3)), 1.0))
            gu = _solve3(S, [float(v[3]) for v in V])
            gv = _solve3(S, [float(v[4]) for v in V])
            if gu is None or gv is None:
                why = "the game's coordinates are degenerate under a triangle"
            elif not (lo <= gu[0] <= hi and lo <= -gv[1] <= hi
                      and abs(gu[1]) <= cross and abs(gv[0]) <= cross):
                why = ("the coordinates are not the game's (du/ds %.3g du/dt %.3g dv/ds %.3g "
                       "dv/dt %.3g, want %.3g 0 0 %.3g)"
                       % (gu[0], gu[1], gv[0], gv[1], WATER_PER_ST, -WATER_PER_ST))
        if on == 0:
            continue
        if off == 0 and why is None:
            marked.append(di)
            rep['marked'].append(dict(at=dr['at'], tris=on))
        else:
            rep['refused'].append(dict(at=dr['at'], tris=on + off, on=on,
                                       why=why or '%d triangle%s lie on no water surface'
                                       % (off, '' if off == 1 else 's')))
    return marked, rep


def describe_water(rep):
    """The preparation's log lines for water_draws' report."""
    lines = ["D-048: %d game water triangle%s (texture %s); %d draw%s move with the game's "
             "water (%d triangles)"
             % (rep['game'], '' if rep['game'] == 1 else 's',
                ', '.join(str(t) for t in WATER_TEXTURES),
                len(rep['marked']), '' if len(rep['marked']) == 1 else 's',
                sum(d['tris'] for d in rep['marked']))]
    for d in rep['marked']:
        lines.append('  draw at %#06x: %d triangles' % (d['at'], d['tris']))
    for d in rep['refused']:
        lines.append('  draw at %#06x NOT moved (%d of %d triangles on the water): %s'
                     % (d['at'], d['on'], d['tris'], d['why']))
    return lines
