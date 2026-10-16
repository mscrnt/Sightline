"""Remaster artwork resampled into a native texture's own coordinate domain.

The XBLA set replaces a native texture id with a source image. For most ids
the source image IS the native image at a higher resolution, and the mapping
entry's two flips are the whole conversion. Some remaster items were
re-UV'd: the model that draws the native texture samples it with the
cartridge's coordinates, while the remaster draws its own artwork with its
own. An entry for such an id carries a `sample` block: the native domain
(the texels the native model's coordinates reach) and the source window
the remaster draws over the same geometry, both measured from the two models
(docs/texture-packs.md, "Native-domain entries").

    "sample": {"size": [w, h],                 output image, pixels
               "u": {"n64": [a, b], "src": [c, d], "wrap": W},
               "v": {"n64": [a, b], "src": [c, d]},
               "seam": f}

For each output pixel, x in native texels (0..n64_w) is taken into the band
[a, b) - adding `wrap` (the native width) once when x < a and a wrap is
given - and mapped linearly onto source pixels [c, d]; outside the band the
source is clamped to the window, or with "outside": "mirror" reflected back
into it (for other models that sample the same id past the measured band). v maps the same way, and the window REPEATS
along v: with `seam` > 0 the last fraction f of each repeat cross-fades into
the rows just before the window, so the output tiles with no seam row. The
source is sampled bilinearly, wrapping as the remaster's material does.

`level` scales the colour so the mean luminance of the covered texels is the
given value - the native image's own mean. The native model shades its
texture with vertex colours authored for the native image's brightness; the
remaster lights its artwork differently, so its image can land several times
darker under the native shading than the image it replaces.

`bleed` fills each fully transparent texel's colour from the nearest texel
with coverage, so a cut-out's bilinear edge never mixes towards the colour
under its transparent part (a dark fringe round every wire). Alpha is
untouched.

`repeat_cells` and `retile` build the `sample` block for a remaster image
that tiles on another period than the native texture (a wall the remaster
re-tiled): the image's own repeat grid, and the whole number of its cells one
native repeat holds (docs/texture-packs.md, "Re-tiled pairs").

Everything here is arithmetic on arrays: nothing reads or names a source.
"""


def _np():
    import numpy as np                                     # noqa: PLC0415
    return np


def _axis(n_out, n64, spec, name):
    """Source coordinates (float, pixels) for n_out output pixels along one
    axis of a native image n64 texels long."""
    np = _np()
    a, b = (float(x) for x in spec['n64'])
    c, d = (float(x) for x in spec['src'])
    if not (b > a):
        raise ValueError('%s: native band %r is empty' % (name, spec['n64']))
    if c == d:
        raise ValueError('%s: source window %r is empty' % (name, spec['src']))
    t = (np.arange(n_out, dtype=np.float64) + 0.5) * (float(n64) / n_out)
    w = spec.get('wrap')
    if w:
        t = np.where(t < a, t + float(w), t)
    how = spec.get('outside', 'clamp')
    if how == 'mirror':
        # Reflect back into the band, so a model that reaches past the
        # measured band sees the same artwork continue rather than one edge
        # column smeared flat.
        L = b - a
        tt = np.mod(t - a, 2.0 * L)
        t = a + np.where(tt > L, 2.0 * L - tt, tt)
    elif how != 'clamp':
        raise ValueError('%s: outside %r is not clamp or mirror' % (name, how))
    s = c + (t - a) * (d - c) / (b - a)
    return s, t, (a, b, c, d)


def _bilinear(img, xs, ys):
    """img HxWxC float; xs, ys broadcastable pixel-centre coordinates; wraps."""
    np = _np()
    h, w = img.shape[0], img.shape[1]
    x = xs - 0.5
    y = ys - 0.5
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx = (x - x0)[..., None]
    fy = (y - y0)[..., None]
    x0m, x1m = x0 % w, (x0 + 1) % w
    y0m, y1m = y0 % h, (y0 + 1) % h
    top = img[y0m, x0m] * (1 - fx) + img[y0m, x1m] * fx
    bot = img[y1m, x0m] * (1 - fx) + img[y1m, x1m] * fx
    return top * (1 - fy) + bot * fy


def sample(img, spec, n64):
    """Resample `img` (HxWx4 uint8, the decoded source) into the native domain
    described by `spec`; `n64` is the native (width, height) in texels.
    Returns an HxWx4 uint8 array of spec['size']."""
    np = _np()
    ow, oh = (int(x) for x in spec['size'])
    if not (0 < ow <= 1024 and 0 < oh <= 1024):
        raise ValueError('sample: output size %r out of range' % (spec['size'],))
    src = img.astype(np.float64)
    sx, _tx, (_a, _b, c, d) = _axis(ow, n64[0], spec['u'], 'u')
    sx = np.clip(sx, min(c, d), max(c, d))
    sy, ty, (va, vb, vc, vd) = _axis(oh, n64[1], spec['v'], 'v')
    # v repeats: fold the native coordinate into one period of the band.
    period = vb - va
    ph = np.mod(ty - va, period) / period                  # 0..1 along the repeat
    sy = vc + ph * (vd - vc)
    seam = float(spec.get('seam', 0.0))
    if not (0.0 <= seam < 1.0):
        raise ValueError('sample: seam %r is not in [0, 1)' % (seam,))
    X, Y = np.meshgrid(sx, sy)
    out = _bilinear(src, X, Y)
    if seam > 0.0:
        # Over the last `seam` of the repeat, fade to the rows that precede
        # the window: at ph -> 1 the output equals the window's first row,
        # which is exactly what the next repeat starts with.
        k = np.clip((ph - (1.0 - seam)) / seam, 0.0, 1.0)
        prev = vc + (ph - 1.0) * (vd - vc)
        Xp, Yp = np.meshgrid(sx, prev)
        alt = _bilinear(src, Xp, Yp)
        k = k[:, None, None]
        out = out * (1.0 - k) + alt * k
    return np.clip(np.rint(out), 0, 255).astype(np.uint8)


def level(img, target):
    """Scale RGB so the covered texels' mean luminance (Rec. 601) is `target`;
    clamps at 255. Alpha is untouched."""
    np = _np()
    target = float(target)
    if not (0.0 < target <= 255.0):
        raise ValueError('level %r is not in (0, 255]' % (target,))
    out = img.copy()
    cov = out[:, :, 3] > 127
    if not cov.any():
        return out
    rgb = out[:, :, :3].astype(np.float64)
    mean = float((rgb[cov] @ np.array([0.299, 0.587, 0.114])).mean())
    if mean <= 0.0:
        return out
    out[:, :, :3] = np.clip(np.rint(rgb * (target / mean)), 0, 255).astype(np.uint8)
    return out


def bleed(img, rounds=None):
    """Give every alpha-0 texel the colour of the nearest texel with alpha,
    growing one ring per round (4-neighbour, wrapping). Alpha is untouched."""
    np = _np()
    out = img.copy()
    rgb = out[:, :, :3].astype(np.float64)
    have = out[:, :, 3] > 0
    if have.all() or not have.any():
        return out
    h, w = have.shape
    limit = rounds if rounds is not None else max(h, w)
    for _ in range(limit):
        if have.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros((h, w), np.float64)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            hv = np.roll(np.roll(have, dy, 0), dx, 1)
            acc += np.roll(np.roll(rgb, dy, 0), dx, 1) * hv[..., None]
            cnt += hv
        grow = (~have) & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        have = have | grow
    out[:, :, :3] = np.clip(np.rint(rgb), 0, 255).astype(np.uint8)
    return out


def repeat_cells(img, most=8, floor=0.4):
    """The image's own repeat grid: (cells along u, cells along v).

    A remastered wall image often holds several copies of one motif - a
    5 x 5 grid of tiles, three brick courses - where the native texture holds
    one. Along each axis, the image is correlated with itself shifted by
    1/n of its length (n = 2..most, luminance, mean removed); the n that
    correlates best is the cell count when that correlation is at least
    `floor`, else 1 (no repeat inside the image)."""
    np = _np()
    lum = img[:, :, :3].astype(np.float64) @ np.array([0.299, 0.587, 0.114])
    lum = lum - lum.mean()
    out = []
    for axis in (1, 0):                                   # u runs along columns
        n_px = lum.shape[axis]
        best = (None, 1)
        for n in range(2, most + 1):
            sh = np.roll(lum, -int(round(n_px / float(n))), axis=axis)
            den = np.sqrt((lum * lum).sum() * (sh * sh).sum())
            r = float((lum * sh).sum() / den) if den > 0 else 0.0
            if best[0] is None or r > best[0]:
                best = (r, n)
        out.append(best[1] if best[0] is not None and best[0] >= floor else 1)
    return tuple(out)


def retile(n64, src_size, cells, scale, origin=(0.0, 0.0), most=1024):
    """A `sample` block for a remaster image that tiles on another period
    than the native texture (docs/texture-packs.md, "Re-tiled pairs").

    `scale` is how many source images one native repeat spans, per axis, as
    measured on the geometry both generations share; `cells` the source's
    own repeat grid (repeat_cells). One native repeat can only hold a WHOLE
    number of source cells and still tile, so it holds
    k = max(1, round(scale * cells)) of them: the window is k cells long,
    starting at `origin` (source pixels). Returns (spec, k, residual), the
    residual being how much larger the drawn artwork is than the remaster
    draws it (k / cells / scale - 1, per axis). The output is the window's
    length rounded up to a power of two, at most `most`."""
    import math                                            # noqa: PLC0415
    spec = {}
    ks, res, size = [], [], []
    for i, ax in enumerate(('u', 'v')):
        if not (scale[i] > 0 and cells[i] >= 1):
            raise ValueError('retile: %s scale %r / cells %r' % (ax, scale[i], cells[i]))
        k = max(1, int(round(scale[i] * cells[i])))
        win = k * float(src_size[i]) / cells[i]
        a = float(origin[i])
        spec[ax] = {'n64': [0, int(n64[i])], 'src': [round(a, 3), round(a + win, 3)]}
        ks.append(k)
        res.append(k / float(cells[i]) / scale[i] - 1.0)
        size.append(min(most, 1 << int(math.ceil(math.log2(max(16.0, win))))))
    spec['size'] = size
    return ({'size': spec['size'], 'u': spec['u'], 'v': spec['v']}, tuple(ks), tuple(res))


def check_entry(e):
    """Validate an entry's optional native-domain fields; raise ValueError."""
    s = e.get('sample')
    if s is not None:
        for k in ('size', 'u', 'v'):
            if k not in s:
                raise ValueError('%s: sample lacks %r' % (e['id'], k))
        for ax in ('u', 'v'):
            for k in ('n64', 'src'):
                if len(s[ax].get(k, ())) != 2:
                    raise ValueError('%s: sample.%s.%s is not a pair' % (e['id'], ax, k))
        if 'wrap' in s['v']:
            raise ValueError('%s: v repeats by construction; it takes no wrap' % e['id'])
    b = e.get('bleed')
    if b is not None and not isinstance(b, bool):
        raise ValueError('%s: bleed is not true/false' % e['id'])
    lv = e.get('level')
    if lv is not None and (isinstance(lv, bool) or not isinstance(lv, (int, float))
                           or not (0 < lv <= 255)):
        raise ValueError('%s: level %r is not in (0, 255]' % (e['id'], lv))
