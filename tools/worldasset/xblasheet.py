"""Contact sheets for prepared world-asset bundles (.slwa).

Reads bundles that tools/windows/prepare-xbla-assets.ps1 already wrote and
draws, per bundle, five orthographic views (front, side, two three-quarter,
top) with each material's image modulated by vertex colour, the bundle's
images, and a per-mesh table: material, image, blend, flags, triangles,
extent and how much of the mesh carries a black vertex colour. Writes
<out>/<class>-<name>.png per bundle, <out>/index.png (one three-quarter view
per bundle) and <out>/report.json (the same table, machine-readable).

    xblasheet.py --root <worldassets> --out <dir> [--items prop/plane,prop/tank]
                 [--class prop] [--size 256]

This answers "what does the prepared asset hold" without running anything:
which image a mesh draws, whether its vertex colour zeroes it, how large it is
in item units. It is a developer view, not the renderer: it samples nearest,
ignores fog, normal maps and the material extension, and lights nothing - a
mesh that is black here is black in its own data.

Nothing is read except the bundles named; with none present it says so and
exits 0. Needs numpy and Pillow (requirements.txt, developer tools).
"""
import argparse
import json
import math
import os
import struct
import sys

try:
    import numpy as np
    from PIL import Image, ImageDraw
    MISSING = None
except ImportError as exc:                                     # pragma: no cover
    np = Image = ImageDraw = None
    MISSING = str(exc)

BLEND = {0: 'opaque', 1: 'alpha', 2: 'add', 3: 'cutout'}


def read_slwa(path):
    b = open(path, 'rb').read()
    if b[:4] != b'SLWA':
        raise ValueError('%s: not an SLWA bundle' % path)
    ver, cls = struct.unpack_from('<II', b, 4)
    nv, ni, nm, nt, nh = struct.unpack_from('<IIIII', b, 72)
    v_off, i_off, m_off, t_off, h_off = struct.unpack_from('<IIIII', b, 92)
    vt = np.dtype([('p', '<f4', 3), ('uv', '<f4', 2), ('c', 'u1', 4)])
    V = np.frombuffer(b, vt, nv, v_off)
    I = np.frombuffer(b, '<u4', ni, i_off).reshape(-1, 3)
    M = [struct.unpack_from('<IIII', b, m_off + 16 * k) for k in range(nm)]
    T = [struct.unpack_from('<IIII', b, t_off + 16 * k) for k in range(nt)]
    H = [struct.unpack_from('<IIII', b, h_off + 16 * k) for k in range(nh)]
    tex = []
    for (w, h, off, n) in T:
        tex.append(np.frombuffer(b, 'u1', n, off).reshape(h, w, 4) if n == w * h * 4 else None)
    return dict(ver=ver, cls=cls, V=V, I=I, M=M, T=T, H=H, tex=tex)


def view_matrix(yaw, pitch):
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    ry = np.array([[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]])
    rx = np.array([[1, 0, 0], [0, cp, sp], [0, -sp, cp]])
    return rx @ ry


def render(d, rot, size, centre, radius):
    """Orthographic z-buffered render; +y up, looking down -z after `rot`."""
    V, I = d['V'], d['I']
    p = (V['p'] - centre) @ rot.T
    s = (size * 0.46) / max(radius, 1e-6)
    sx = p[:, 0] * s + size / 2
    sy = -p[:, 1] * s + size / 2
    sz = p[:, 2]
    img = np.full((size, size, 3), 40, np.float32)
    img[:, :] = (52, 56, 64)
    zb = np.full((size, size), -np.inf, np.float32)
    uv = V['uv']
    col = V['c'].astype(np.float32) / 255.0
    for hi, (mi, first, cnt, _) in enumerate(d['H']):
        ti, blend = d['M'][mi][0], d['M'][mi][1]
        tex = d['tex'][ti] if ti < len(d['tex']) else None
        for t in range(first // 3, (first + cnt) // 3):
            a, b_, c = I[t]
            x0, x1, x2 = sx[a], sx[b_], sx[c]
            y0, y1, y2 = sy[a], sy[b_], sy[c]
            area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if abs(area) < 1e-9:
                continue
            minx = max(int(math.floor(min(x0, x1, x2))), 0)
            maxx = min(int(math.ceil(max(x0, x1, x2))), size - 1)
            miny = max(int(math.floor(min(y0, y1, y2))), 0)
            maxy = min(int(math.ceil(max(y0, y1, y2))), size - 1)
            if minx > maxx or miny > maxy:
                continue
            gx, gy = np.meshgrid(np.arange(minx, maxx + 1) + 0.5, np.arange(miny, maxy + 1) + 0.5)
            w0 = ((x1 - gx) * (y2 - gy) - (x2 - gx) * (y1 - gy)) / area
            w1 = ((x2 - gx) * (y0 - gy) - (x0 - gx) * (y2 - gy)) / area
            w2 = 1 - w0 - w1
            m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not m.any():
                continue
            z = w0 * sz[a] + w1 * sz[b_] + w2 * sz[c]
            sub = zb[miny:maxy + 1, minx:maxx + 1]
            m &= z > sub
            if not m.any():
                continue
            cc = (w0[..., None] * col[a] + w1[..., None] * col[b_] + w2[..., None] * col[c])
            rgb = cc[..., :3]
            alpha = np.ones(m.shape, np.float32)
            if tex is not None:
                u = w0 * uv[a, 0] + w1 * uv[b_, 0] + w2 * uv[c, 0]
                v = w0 * uv[a, 1] + w1 * uv[b_, 1] + w2 * uv[c, 1]
                th, tw = tex.shape[:2]
                tx = (np.floor(u * tw).astype(np.int64)) % tw
                ty = (np.floor(v * th).astype(np.int64)) % th
                texel = tex[ty, tx].astype(np.float32) / 255.0
                rgb = rgb * texel[..., :3]
                alpha = texel[..., 3]
            if blend == 3:
                m &= alpha >= 0.5
            if not m.any():
                continue
            out = img[miny:maxy + 1, minx:maxx + 1]
            if blend in (1, 2):
                a_ = (alpha * cc[..., 3])[..., None] if blend == 1 else 1.0
                newc = out + rgb * 255 * a_ if blend == 2 else out * (1 - a_) + rgb * 255 * a_
                out[m] = np.clip(newc[m], 0, 255)
            else:
                out[m] = rgb[m] * 255
                sub[m] = z[m]
    return Image.fromarray(img.astype(np.uint8))


def mesh_table(d):
    rows = []
    V, I = d['V'], d['I']
    for hi, (mi, first, cnt, _) in enumerate(d['H']):
        tris = I[first // 3:(first + cnt) // 3]
        vi = np.unique(tris)
        p = V['p'][vi]
        c = V['c'][vi]
        black = float(np.mean((c[:, 0] == 0) & (c[:, 1] == 0) & (c[:, 2] == 0)))
        ti, blend, flags = d['M'][mi][0], d['M'][mi][1], d['M'][mi][2]
        rows.append(dict(mesh=hi, material=mi, image=ti, blend=BLEND.get(blend, blend),
                         flags=flags, triangles=int(len(tris)),
                         lo=[round(float(x), 1) for x in p.min(0)],
                         hi=[round(float(x), 1) for x in p.max(0)],
                         vertex_colour_black=round(black, 3),
                         vertex_colour_mean=[round(float(x), 1) for x in c.mean(0)]))
    return rows


def sheet(path, label, size):
    d = read_slwa(path)
    P = d['V']['p']
    lo, hi = P.min(0), P.max(0)
    centre = (lo + hi) / 2
    radius = float(np.max(np.linalg.norm(P - centre, axis=1)))
    if d['cls'] == 2:
        # A background's panorama shell spans far more than the playable
        # world: frame the views on where most of the vertices are.
        centre = np.median(P, axis=0)
        radius = float(np.percentile(np.linalg.norm(P - centre, axis=1), 90))
    views = [('front', view_matrix(0, 0)), ('side', view_matrix(90, 0)),
             ('3/4', view_matrix(35, 25)), ('3/4 back', view_matrix(215, 25)),
             ('top', view_matrix(0, 90))]
    imgs = [render(d, r, size, centre, radius) for _, r in views]
    rows = mesh_table(d)
    thumbs = []
    for ti, t in enumerate(d['tex']):
        if t is None:
            continue
        im = Image.fromarray(t[..., :3].copy()).resize((64, 64), Image.NEAREST)
        thumbs.append((ti, t.shape[1], t.shape[0], im))
    W = size * len(views)
    tex_rows = max(1, math.ceil(len(thumbs) * 72 / W))
    H = 22 + size + 16 + tex_rows * 86 + 14 * (len(rows) + 2)
    canvas = Image.new('RGB', (W, H), (24, 24, 28))
    dr = ImageDraw.Draw(canvas)
    dr.text((4, 4), '%s   extent %s .. %s   %d tris %d meshes %d images'
            % (label, [round(float(x), 1) for x in lo], [round(float(x), 1) for x in hi],
               len(d['I']), len(d['H']), len(d['tex'])), fill=(230, 230, 230))
    for k, (name, _) in enumerate(views):
        canvas.paste(imgs[k], (k * size, 22))
        dr.text((k * size + 4, 24), name, fill=(255, 255, 0))
    y = 22 + size + 16
    x = 0
    for ti, w, h, im in thumbs:
        if x + 72 > W:
            x = 0
            y += 86
        canvas.paste(im, (x + 4, y))
        dr.text((x + 4, y + 66), 'i%d %dx%d' % (ti, w, h), fill=(200, 200, 200))
        x += 72
    y += 86
    for r in rows:
        dr.text((4, y), 'mesh %(mesh)d mat %(material)d img %(image)d %(blend)s flags %(flags)#x '
                        'tris %(triangles)d black-vc %(vertex_colour_black).2f lo %(lo)s hi %(hi)s' % r,
                fill=(255, 120, 120) if r['vertex_colour_black'] > 0.5 else (210, 210, 210))
        y += 14
    return canvas, dict(bundle=label, extent_lo=[float(x) for x in lo], extent_hi=[float(x) for x in hi],
                        triangles=int(len(d['I'])), images=[dict(index=t[0], w=t[1], h=t[2]) for t in thumbs],
                        meshes=rows), imgs[2]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', required=True, help='worldassets root (holds xbla/<class>/<name>.slwa)')
    ap.add_argument('--out', required=True)
    ap.add_argument('--items', default='', help='comma list of class/name')
    ap.add_argument('--class', dest='klass', default='', help='every bundle of one class')
    ap.add_argument('--size', type=int, default=256)
    a = ap.parse_args(argv)
    if MISSING:
        print('xblasheet: needs numpy and Pillow (%s)' % MISSING)
        return 2
    base = os.path.join(a.root, 'xbla')
    items = [s.strip() for s in a.items.split(',') if s.strip()]
    if a.klass and os.path.isdir(os.path.join(base, a.klass)):
        items += ['%s/%s' % (a.klass, f[:-5]) for f in sorted(os.listdir(os.path.join(base, a.klass)))
                  if f.endswith('.slwa')]
    found = [(it, os.path.join(base, it + '.slwa')) for it in items]
    found = [(it, p) for it, p in found if os.path.isfile(p)]
    if not found:
        print('xblasheet: no prepared bundles found under %s (prepare them first)' % base)
        return 0
    os.makedirs(a.out, exist_ok=True)
    report, tiles = [], []
    for it, p in found:
        cv, rep, tile = sheet(p, it, a.size)
        fn = os.path.join(a.out, it.replace('/', '-') + '.png')
        cv.save(fn)
        report.append(rep)
        tiles.append((it, tile))
        print('%-28s %6d tris  %d meshes  -> %s' % (it, rep['triangles'], len(rep['meshes']), fn))
    cols = min(6, len(tiles))
    rows = math.ceil(len(tiles) / cols)
    idx = Image.new('RGB', (cols * a.size, rows * (a.size + 16)), (24, 24, 28))
    dr = ImageDraw.Draw(idx)
    for k, (it, tile) in enumerate(tiles):
        x, y = (k % cols) * a.size, (k // cols) * (a.size + 16)
        idx.paste(tile, (x, y + 16))
        dr.text((x + 4, y + 2), it, fill=(230, 230, 230))
    idx.save(os.path.join(a.out, 'index.png'))
    with open(os.path.join(a.out, 'report.json'), 'w') as f:
        json.dump(report, f, indent=1)
    print('index %s, report %s' % (os.path.join(a.out, 'index.png'), os.path.join(a.out, 'report.json')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
