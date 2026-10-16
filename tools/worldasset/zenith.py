"""The generated cloud field for a polar skydome (#68, #76, D-023).

WHAT THIS IS. A deliberate, documented divergence from the remaster. The Dam's
skydome is an equirectangular sky wrapped about a pole: the top of its image is
a plain blue gradient with no cloud in it, the cloud band starts below that, and
wrapped on the dome the two read as a clear cap with a RING of cloud around it,
and the whole of it is still - the remaster's sky does not move. D-023 draws
the game's own moving cloud layer over it instead, textured from that band.

WHAT IT PRODUCES. Nothing of the source is altered. The prepared item gains
one extra, flagged draw - the CLOUD FIELD - drawn over the dome:

  * geometry: a cap of directions about the vertical, from a low elevation to
    the zenith (sky_cap). The dome is drawn with its origin on the eye, so a
    direction is all a vertex has to carry. Its vertex alpha is the coverage
    profile: none at LOW degrees of elevation, whole from LOW + FADE up, a
    smoothstep between, so the field fades into the dome where the scenery and
    the haze in front of it already cover the horizon;
  * image: ONE TILE of cloud, grown from the band (cloud_tile) and periodic in
    both directions;
  * coordinates: none worth storing. The runtime casts every vertex onto the
    game's own cloud plane - sky.c's, at the environment's cloud height, with
    s = 0.1 x, t = 0.1 z + g_SkyCloudOffset - so the clouds stand where the
    game's stand, show the game's parallax, and move exactly as the game's
    move, tick for tick, frozen when the game's phase is frozen
    (src/gfx/sl_gfx_worldasset.h, sl_wa_cloud_coord and SL_WA_CLOUD_TILES).

The runtime draws it only while the divergence is on (SL_SKY_ZENITH, default
on); off, the frame is the source dome exactly. See docs/divergences.md D-023.

HOW THE TILE IS MADE, and why each step.

  1. EVERYTHING IS MEASURED. The pole, the axis, the ring table (v -> polar
     angle) and the u -> azimuth rule are read from the mesh; the clear cap,
     the band's start and its extent are read from per-row statistics of the
     image (each texel row is one ring about the pole). No row number, level
     or item name is known here, and a texture without that structure - no
     flat cap at the pole, no settled band below it - is REFUSED: nothing is
     added and the bundle is exactly what it would have been without this.
  2. THE EXEMPLARS ARE UNDISTORTED. The band is read in the azimuthal-
     equidistant plane about the pole, where it is not stretched toward the
     pole as it is in the image, as PATCHES square windows evenly round its
     settled ring, at the density the band image resolves there.
  3. THE TEXTURE IS THE BAND'S OWN. Spot noise (ADSN): each window, in a
     decorrelated (PCA) and Gaussianised colour space and tapered at its edges
     (a hard edge would print a grid into the spectrum), is a texton driven by
     its own white-noise field, one field for all three channels so the
     window's colour correlations survive; the band's own histograms map the
     sum back. Nothing is copied, so nothing repeats inside the tile.
  4. THE TILE WRAPS. The convolution is circular on the tile's own torus, so
     the tile is periodic by construction: laid edge to edge on the game's
     plane it has no seam in either direction.

DETERMINISTIC: a fixed seed and plain numpy float arithmetic, rounded once at
the end; the same source bytes produce the same bundle bytes on the same
toolchain (the self-test checks it; the pinned numpy is requirements.txt's).
Bounded: about two seconds for a 1024-square source.
"""
import numpy as np

SEED = 0x5A11

# The relative constants. Each is a fraction of something MEASURED on the
# item, never an absolute row, angle or colour.
FLAT = 0.10          # a cap row's luminance spread, as a fraction of the band's
SETTLED_STD = 0.90   # the band has settled once its spread reaches this ...
SETTLED_MEAN = 0.75  # ... and its row mean is this close (x the spread) to the band's
BAND_LO, BAND_HI = 0.5, 1.5   # the band ends where the spread leaves this range
HANDOVER = 0.15      # hand-over width, as a fraction of the band's start angle
EXEMPLAR = 0.5       # exemplar annulus width, same unit
RIM = 0.5            # the band's exemplar density is read to this far past it
MIN_CAP_RINGS = 3    # a cap narrower than this many of the dome's rings is not one
MIN_RINGS = 6
MAX_CARRIER_TEX = 1024

# The cloud tile and the carrier.
PATCHES = 8          # band windows the tile is grown from, evenly round the ring
TILE = 1024          # the tile's side (the runtime lays SL_WA_CLOUD_TILES a cloud-image repeat)
RING_STEP = 3.0      # degrees of elevation between the carrier's rings
SEGMENTS = 48        # carrier columns round the vertical
LOW = 8.0            # the coverage profile: transparent at and below this elevation ...
FADE = 10.0          # ... opaque from this many degrees above it


class NotApplicable(Exception):
    """This item does not have the structure the fill is defined for."""


def _lum(rgb):
    return rgb @ np.array([0.299, 0.587, 0.114])


# ---- 1. the mesh: pole, axis, rings --------------------------------------

def analyse_mesh(pos, uv):
    """Read the polar structure out of a dome's vertices. Refuses anything that
    is not rings of constant v about one axis through a single pole."""
    pos = np.asarray(pos, np.float64)
    uv = np.asarray(uv, np.float64)
    if len(pos) < 16:
        raise NotApplicable('too few vertices for a dome')
    scale = float(np.ptp(pos, axis=0).max()) or 1.0
    vmin = uv[:, 1].min()
    polem = np.abs(uv[:, 1] - vmin) < 1e-6
    pole = pos[polem].mean(0)
    if np.abs(pos[polem] - pole).max() > 1e-3 * scale:
        raise NotApplicable('the smallest v is not one point (no single pole)')
    keys = np.round(uv[:, 1], 6)
    rings = []
    for v in np.unique(keys[~polem]):
        m = keys == v
        if m.sum() >= 8:
            rings.append((float(uv[m, 1].mean()), m))
    if len(rings) < MIN_RINGS:
        raise NotApplicable('fewer than %d rings of constant v' % MIN_RINGS)
    # the axis: through the pole, along the first rings' plane normals (a ring
    # repeats its seam vertex, so a vertex MEAN is biased toward the seam; a
    # plane fit to its distinct points is not)
    a = np.zeros(3)
    for _, m in rings[:MIN_RINGS]:
        q = np.unique(pos[m], axis=0)
        nrm = np.linalg.svd(q - q.mean(0))[2][2]
        a += nrm if nrm @ (pole - q.mean(0)) > 0 else -nrm
    a /= np.linalg.norm(a)
    # centre on the axis: a sphere fit constrained to the axis, over the first rings
    sel = np.concatenate([np.nonzero(m)[0] for _, m in rings[:MIN_RINGS * 2]])
    d = pos[sel] - pole
    A = np.c_[-2.0 * (d @ a), np.ones(len(sel))]
    t = np.linalg.lstsq(A, (d * d).sum(1), rcond=None)[0][0]
    cen = pole - a * t
    e1 = np.array([1.0, 0.0, 0.0]) - a * a[0]
    if np.linalg.norm(e1) < 1e-6:
        e1 = np.array([0.0, 0.0, 1.0]) - a * a[2]
    e1 /= np.linalg.norm(e1)
    e2 = np.cross(e1, a)
    # the ring table, in order, while the rings stay circles about the axis
    table = []
    for v, m in rings:
        p = pos[m] - cen
        h = p @ a
        rad = np.linalg.norm(p - np.outer(h, a), axis=1)
        if rad.mean() <= 0 or np.ptp(rad) > 0.01 * rad.mean() or np.ptp(h) > 0.01 * rad.mean():
            break
        th = float(np.degrees(np.arctan2(rad.mean(), h.mean())))
        if table and th <= table[-1][1]:
            break
        table.append((v, th, float(np.linalg.norm(p, axis=1).mean()), m))
    if len(table) < MIN_RINGS:
        raise NotApplicable('the rings stop being circles about one axis after %d'
                            % len(table))
    # u against azimuth: u = u0 + s * phi / 360, checked on every ring vertex
    mall = np.zeros(len(pos), bool)
    for *_, m in table:
        mall |= m
    p = pos[mall] - cen
    phi = np.degrees(np.arctan2(p @ e2, p @ e1))
    best = None
    for s in (1.0, -1.0):
        u0 = np.angle(np.mean(np.exp(2j * np.pi * (uv[mall, 0] - s * phi / 360.0)))) / (2 * np.pi)
        r = ((uv[mall, 0] - (u0 + s * phi / 360.0)) + 0.5) % 1.0 - 0.5
        if best is None or np.abs(r).max() < best[2]:
            best = (s, u0, float(np.abs(r).max()))
    if best[2] > 2e-3:
        raise NotApplicable('u is not linear in azimuth (worst %.4f)' % best[2])
    tv = np.array([vmin] + [x[0] for x in table])
    tt = np.array([0.0] + [x[1] for x in table])
    tr = np.array([float(np.linalg.norm(pole - cen))] + [x[2] for x in table])
    return dict(pole=pole, axis=a, centre=cen, e1=e1, e2=e2, sign=best[0], u0=best[1],
                ring_v=tv, ring_theta=tt, ring_radius=tr)


def _theta_of_v(g, v):
    return np.interp(v, g['ring_v'], g['ring_theta'])


def _v_of_theta(g, t):
    return np.interp(t, g['ring_theta'], g['ring_v'])


def _u_of_phi(g, phi):
    return (g['u0'] + g['sign'] * phi / 360.0) % 1.0



# ---- 2. the image: cap, band --------------------------------------------

def analyse_texture(rgb, g):
    """Find the clear cap and the band by per-row statistics (a row is a ring)."""
    h = rgb.shape[0]
    s = _lum(rgb).std(1)
    rm = rgb.mean(1)
    r0 = int(np.ceil(g['ring_v'][0] * h))
    if r0 >= h - 4:
        raise NotApplicable('the pole sits at the bottom of the image')
    plateau = float(np.median(s[r0:]))
    if plateau <= 0:
        raise NotApplicable('the image has no row-to-row structure')
    r = r0
    while r < h and s[r] <= FLAT * plateau:
        r += 1
    cap_end = r
    th_cap = float(_theta_of_v(g, (cap_end + 0.5) / h))
    if th_cap < MIN_CAP_RINGS * g['ring_theta'][1]:
        raise NotApplicable('no clear cap at the pole (flat to %.1f deg only)' % th_cap)
    r = cap_end                                   # through the fade-in ...
    while r < h and s[r] < SETTLED_STD * plateau:
        r += 1
    while r < h and BAND_LO * plateau <= s[r] <= BAND_HI * plateau:
        r += 1                                    # ... then along the band
    band_end = r
    if band_end <= cap_end + 2:
        raise NotApplicable('no cloud band below the cap')
    mref = np.median(rm[cap_end:band_end], axis=0)
    r = cap_end
    while r < band_end and not (s[r] >= SETTLED_STD * plateau and
                                np.linalg.norm(rm[r] - mref) <= SETTLED_MEAN * plateau):
        r += 1
    if r >= band_end:
        raise NotApplicable('the band below the cap never settles')
    th_bs = float(_theta_of_v(g, (r + 0.5) / h))
    th_be = float(min(_theta_of_v(g, (band_end + 0.5) / h), g['ring_theta'][-1]))
    beta = HANDOVER * th_bs
    dex = min(EXEMPLAR * th_bs, 0.95 * (th_be - th_bs))
    if dex < 2.0 * beta:
        raise NotApplicable('the band is too narrow to learn from (%.1f deg)' % (th_be - th_bs))
    # The carrier's image spans the fill, the hand-over and the rim; the
    # plane is synthesised at that image's own density, so nothing is made
    # that the carrier cannot show.
    th_in = th_bs + beta
    th_c = th_in + RIM * beta
    span = th_c * 1.05
    size = int(min(MAX_CARRIER_TEX, rgb.shape[1]))
    return dict(plateau=plateau, cap_end=cap_end, band_row=r, band_end=band_end,
                th_cap=th_cap, th_bs=th_bs, th_be=th_be, beta=beta, dex=dex,
                th_in=th_in, th_c=th_c, span=span, size=size,
                ppd=size / (2.0 * span))


# ---- 3. the fill ---------------------------------------------------------

def _bilinear(img, x, y, wrap_x=False):
    h, w = img.shape[:2]
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx = (x - x0)[..., None]
    fy = (y - y0)[..., None]
    if wrap_x:
        xa, xb = x0 % w, (x0 + 1) % w
    else:
        xa, xb = np.clip(x0, 0, w - 1), np.clip(x0 + 1, 0, w - 1)
    ya, yb = np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1)
    return (img[ya, xa] * (1 - fx) * (1 - fy) + img[ya, xb] * fx * (1 - fy)
            + img[yb, xa] * (1 - fx) * fy + img[yb, xb] * fx * fy)


def reproject(rgb, g, th, ph):
    """The dome image at polar angle th / azimuth ph: the band, top-down."""
    h, w = rgb.shape[:2]
    v = _v_of_theta(g, np.minimum(th, g['ring_theta'][-1]))
    return _bilinear(rgb, _u_of_phi(g, ph) * w - 0.5, v * h - 0.5, wrap_x=True)


def _ndtri(p):
    # inverse normal CDF (Acklam's rational approximation), vectorised
    a = (-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
         1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00)
    b = (-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
         6.680131188771972e+01, -1.328068155288572e+01)
    c = (-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
         -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00)
    d = (7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
         3.754408661907416e+00)
    p = np.clip(p, 1e-9, 1 - 1e-9)
    x = np.empty_like(p)
    lo, hi = p < 0.02425, p > 1 - 0.02425
    mid = ~(lo | hi)
    for m, q, sg in ((lo, np.sqrt(-2 * np.log(p[lo])), 1.0),
                     (hi, np.sqrt(-2 * np.log(1 - p[hi])), -1.0)):
        x[m] = sg * (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / \
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1)
    q = p[mid] - 0.5
    r = q * q
    x[mid] = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q / \
        (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1)
    return x


def _ncdf(x):
    # normal CDF through Abramowitz-Stegun 7.1.26 (|err| < 1.5e-7)
    z = np.abs(x) / np.sqrt(2.0)
    t = 1.0 / (1.0 + 0.3275911 * z)
    e = 1 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t
             + 0.254829592) * t * np.exp(-z * z)
    return 0.5 * (1 + np.sign(x) * e)


class _Gauss:
    """PCA decorrelation, then each channel's histogram <-> N(0, 1)."""

    def __init__(self, px):
        self.mu = px.mean(0)
        w, E = np.linalg.eigh(np.cov((px - self.mu).T))
        E = E[:, np.argsort(w)[::-1]]
        E *= np.where(E.sum(0) < 0, -1.0, 1.0)          # a fixed sign per axis
        self.E = E
        q = (px - self.mu) @ E
        self.sorted = [np.sort(q[:, k]) for k in range(3)]

    def fwd(self, px):
        q = (px - self.mu) @ self.E
        out = np.empty_like(q)
        for k in range(3):
            s = self.sorted[k]
            lo = np.searchsorted(s, q[:, k], 'left')
            hi = np.searchsorted(s, q[:, k], 'right')
            out[:, k] = _ndtri(np.clip((lo + hi) * 0.5 / len(s), 0.5 / len(s), 1 - 0.5 / len(s)))
        return out

    def inv(self, gz):
        q = np.empty_like(gz)
        for k in range(3):
            s = self.sorted[k]
            q[:, k] = np.interp(_ncdf(gz[:, k]) * (len(s) - 1), np.arange(len(s)), s)
        return q @ self.E.T + self.mu




def _smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


# ---- 3. the cloud tile ---------------------------------------------------

def exemplars(rgb, g, b):
    """Square windows of the settled band, taken in the plane about the pole
    (where the band is undistorted) at the density the band image resolves,
    evenly round the ring. Each is (side, side, 3)."""
    ppd = b['ppd']
    side = int(min(b['dex'] * ppd, TILE // 4))
    rc = b['th_bs'] + 0.5 * b['dex']
    c = (np.arange(side) + 0.5 - side / 2.0) / ppd
    X0, Y0 = np.meshgrid(c, c)
    out = []
    for k in range(PATCHES):
        a = np.radians(360.0 * k / PATCHES)
        X, Y = X0 + rc * np.cos(a), Y0 + rc * np.sin(a)
        out.append(reproject(rgb, g, np.hypot(X, Y), np.degrees(np.arctan2(Y, X))))
    return out


def cloud_tile(rgb, g, b, seed=SEED, size=TILE):
    """ONE PERIOD of the cloud field: spot noise (ADSN) on a size x size torus,
    every band window its own texton driven by its own white field (one field
    for all three channels of a window, which keeps its colour correlations),
    summed and mapped back through the band's own histograms. A circular
    convolution on the torus is periodic by construction, so the tile repeats
    with no seam in either direction. Returns uint8 RGBA, alpha 255."""
    pats = exemplars(rgb, g, b)
    G = _Gauss(np.concatenate([p.reshape(-1, 3) for p in pats]))
    rng = np.random.default_rng(seed)
    z = np.zeros((size, size, 3))
    for p in pats:
        s = p.shape[0]
        gp = G.fwd(p.reshape(-1, 3)).reshape(p.shape)
        gp -= gp.mean((0, 1))
        # tapered: a window's hard edge would put a cross of its own into the
        # spectrum, and the tile would show it as a grid
        t = np.zeros((size, size, 3))
        t[:s, :s] = gp * np.outer(np.hanning(s), np.hanning(s))[..., None] / s
        W = np.fft.rfft2(rng.standard_normal((size, size)))
        for ch in range(3):
            z[..., ch] += np.fft.irfft2(W * np.fft.rfft2(t[..., ch]), s=(size, size))
    z /= z.reshape(-1, 3).std(0)
    img = G.inv(z.reshape(-1, 3)).reshape(size, size, 3)
    return np.concatenate([np.clip(np.round(img), 0, 255),
                           np.full((size, size, 1), 255.0)], -1).astype(np.uint8)


# ---- 4. the carrier ------------------------------------------------------

def sky_cap(radius, low, fade):
    """The carrier: a cap of directions about the vertical, from elevation
    `low` to the zenith, at `radius` about the dome's origin (which is where
    the eye is). Vertex alpha is 0 at `low`, 1 from `low + fade` up, a
    smoothstep between. Texture coordinates are not stored: the runtime casts
    each vertex onto the game's cloud plane (see sl_wa_cloud_coord); the
    stored pair is that cast for an eye at the plane's own height minus one
    unit, which only a runtime without the rule would ever sample.
    Returns (vertices, triangle indices)."""
    top = low + fade
    els = sorted({round(float(e), 6) for e in np.arange(low, 90.0 - 0.5 * RING_STEP, RING_STEP)}
                 | {round(low, 6), round(top, 6)}, reverse=True)
    az = np.arange(SEGMENTS) * (360.0 / SEGMENTS)
    verts = [(0.0, float(radius), 0.0, 0.0, 0.0, 255, 255, 255, 255)]
    for e in els:
        er = np.radians(e)
        al = int(np.round(255.0 * _smooth(low, top, e))) if fade > 0 else 255
        for a in az:
            ar = np.radians(a)
            d = (np.cos(er) * np.sin(ar), np.sin(er), np.cos(er) * np.cos(ar))
            verts.append((float(radius * d[0]), float(radius * d[1]), float(radius * d[2]),
                          float(d[0] / d[1]), float(d[2] / d[1]), 255, 255, 255, al))
    idx = []
    for k in range(SEGMENTS):
        idx += [0, 1 + k, 1 + (k + 1) % SEGMENTS]
    for i in range(len(els) - 1):
        a0, a1 = 1 + i * SEGMENTS, 1 + (i + 1) * SEGMENTS
        for k in range(SEGMENTS):
            k1 = (k + 1) % SEGMENTS
            idx += [a0 + k, a1 + k, a1 + k1, a0 + k, a1 + k1, a0 + k1]
    return verts, idx


def generate(rgba, vertices, seed=SEED, low=None, fade=None):
    """The whole generator. `rgba` is the dome's image (h, w, 4 uint8, rows
    top first), `vertices` its draw's (x, y, z, u, v, ...) tuples. Returns
    (carrier vertices, triangle indices, cloud tile rgba, report) - or raises
    NotApplicable, in which case the caller adds nothing."""
    low = LOW if low is None else low
    fade = FADE if fade is None else fade
    v = np.asarray([x[:5] for x in vertices], np.float64)
    g = analyse_mesh(v[:, :3], v[:, 3:5])
    rgb = np.asarray(rgba)[..., :3].astype(np.float64)
    b = analyse_texture(rgb, g)
    tile = cloud_tile(rgb, g, b, seed)
    radius = float(np.median(np.linalg.norm(v[:, :3], axis=1)))
    cv, ci = sky_cap(radius, low, fade)
    report = dict(g=g, b=b, low=low, fade=fade, radius=radius)
    return cv, ci, tile, report
