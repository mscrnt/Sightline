"""Build the project-local SLWA world assets from the user's own source (#68).

    prepare.py skydome --item dam --tree <an extracted source tree> --out <root>

Driven by tools/windows/prepare-xbla-assets.ps1, which is the command a
developer runs; this is the conversion itself.

WHAT IT READS is always something the USER supplies. WHAT IT WRITES is
<root>/xbla/<class>/<item>.slwa, under a gitignored directory, and nothing
else: no tracked file is touched, and neither a source nor a converted asset
is ever committed, packaged or exported.

Deterministic and idempotent: the same source produces the same bytes, and the
file is written through a temporary and renamed, so a bundle is only ever
complete or unchanged.

THE ONE PAIRING THIS MAKES, and where each half of it comes from. The skydome
class is prepared only when the item's DRAWS and its item-local images pair one
for one.

  * the ORDER is the item's own. `xblaworld.read_item` returns the meshes in
    the order the item's command list draws them, each resolved to its vertex
    and index range through the rendergraph's buffer table. It is parsed, not
    inferred from where a run happens to lie in the blob.
  * the IMAGE is chosen by the blend the draw needs, against the decoded
    pixels of each image. A background's opaque shell is drawn first and
    whatever blends over it after, so the opaque image goes to the first draw
    and the image carrying alpha to the one after it.

The container does NOT encode a sampler-to-resource link this reader can
follow - there is no texture-name hash anywhere in its records - so the second
half above is a derivation and is written down as one. It is only ever applied
to a set that splits UNAMBIGUOUSLY into opaque images and alpha images, one
per draw; anything else is REFUSED rather than guessed at, and widening that
is a later increment's business.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import slwa                                                  # noqa: E402

CLASSES = {'skydome': slwa.CLASS_SKYDOME,
           'background': slwa.CLASS_BACKGROUND,
           'char': slwa.CLASS_CHAR,
           'head': slwa.CLASS_HEAD,
           'gun': slwa.CLASS_GUN}


def need(module, what):
    try:
        return __import__(module)
    except ImportError:
        sys.stderr.write(
            'prepare-xbla-assets: this conversion needs the Python package %r (%s).\n'
            '  It is a DEVELOPER tool prerequisite, not a Sightline dependency:\n'
            '  nothing in the engine, the build, CI or a release package links it.\n'
            '  Install it into the project venv with:\n'
            '    .venv\\Scripts\\python.exe -m pip install %s\n' % (module, what, module))
        sys.exit(3)


def find_item(tree, rel):
    """Locate <tree>/.../<rel> by directory path SUFFIX, so the source's own
    top-level prefix - whatever the user's copy happens to use - never has to
    be known here or spelled anywhere. A path spelled exactly wins; failing
    that, one that differs only in case (the source's own item names mix it -
    silverPPK - where the executable that names them does not)."""
    exact, suffix = {}, {}
    for root, dirs, _files in os.walk(tree):
        dirs.sort()
        r = os.path.relpath(root, tree).replace('\\', '/')
        if r == '.':
            continue
        exact.setdefault(r, root)
        parts = r.split('/')
        for i in range(1, len(parts)):
            suffix.setdefault('/'.join(parts[i:]), root)
    index = dict(suffix)
    index.update(exact)
    got = index.get(rel)
    if got is None:
        folded = {}
        for k in sorted(index):
            folded.setdefault(k.lower(), index[k])
        got = folded.get(rel.lower())
    return got


def instance_vertices(vertices, xform):
    """The pool's vertices with every position carried through the instance
    matrix (xblaworld.instance_apply); the coordinate and colour words are the
    pool's own. Pure."""
    import xblaworld                                         # noqa: E402
    out = []
    for v in vertices:
        x, y, z = xblaworld.instance_apply(xform, v[0], v[1], v[2])
        out.append((x, y, z) + tuple(v[3:]))
    return out


#: #81 the image an UNTEXTURED draw is given: one opaque white texel, so the
#: material's colour is the vertex colour (xblaworld._pool_sphere).
UNTEXTURED = dict(name='untextured', w=1, h=1, fmt=0, has_alpha=False,
                  rgba=None)


def untextured_texel(colour):
    """#90 the texel (r, g, b, 0..255) an untextured draw's material colour
    (xblaworld.untextured_colour, 0..1 a channel) gives its image, or None
    when there is no colour or it rounds to white - the white image stands.
    Pure."""
    if colour is None:
        return None
    t = tuple(max(0, min(255, int(round(float(c) * 255.0)))) for c in colour[:3])
    return None if t == (255, 255, 255) else t


def translucent_base(slots, style, textures):
    """The base image of a TRANSLUCENT draw that names several sampler slots,
    or None when this rule does not decide it.

    A draw the source marks translucent (style low byte 2) is blended with
    SRC_ALPHA / ONE_MINUS_SRC_ALPHA, so what makes it translucent is an alpha
    channel. An image whose every texel is opaque cannot supply one; when
    exactly ONE of the draw's slot images carries alpha, that image is the
    one the blend consumes, and it is taken as the base.

    Measured on the one population it applies to, the Dam item's water (four
    slots: a tangent-space normal map, a sphere-map reflection image, an
    offset map and a blue surface image with alpha). Its pixel program, with
    the constants the running game gives it, outputs alpha = the slot-3
    image's alpha exactly and colour = 0.926 x vertex colour x the slot-3
    image + 0.074 x the reflection - so slot 3 is the base. The normal-map
    rule below would pick the reflection image. No other multi-slot draw of
    that item is translucent, so nothing else changes."""
    if style is None or (style & 0xff) != 2 or not slots or len(slots) < 2:
        return None
    alpha = [i for _s, i in sorted(slots.items())
             if 0 <= i < len(textures) and textures[i].get('has_alpha')]
    return alpha[0] if len(alpha) == 1 else None


#: D-047 A COVERAGE IMAGE AUTHORED THE OTHER WAY ROUND. A two-image draw
#: (xblaworld.two_image_coverage) is drawn with its colour image's rgb and its
#: coverage image's channel as alpha - exactly what its program exports. On
#: Depot's yard graffiti that coverage is a black-on-white silhouette, and the
#: colour image is black wherever the silhouette is white (mean luma 2.5 of
#: 255) and carries the whole tag where it is black (135.4): drawn as the
#: program reads it, the tag is the one part of the image left out and its
#: black margin is what draws. For the items DIVERGENCE_ITEMS['D-047'] names,
#: a coverage image whose bright texels (above COVER_BRIGHT) cover none of the
#: colour image's content (mean luma below COVER_EMPTY) while its dark ones
#: (below COVER_DARK) carry it (mean luma above COVER_FULL), each over at least
#: COVER_MIN of the image, is taken inverted.
COVER_BRIGHT, COVER_DARK = 191, 64
COVER_EMPTY, COVER_FULL = 8.0, 32.0
COVER_MIN = 0.05


def coverage_inverted(colour_rgba, cov):
    """D-047: whether coverage `cov` (H x W uint8) leaves out exactly the part
    of `colour_rgba` (H x W x 4) that carries content - see above. Pure."""
    import numpy as np                                       # noqa: E402
    lum = np.asarray(colour_rgba)[:, :, :3].astype(np.float64).mean(axis=2)
    cov = np.asarray(cov)
    bright, dark = cov > COVER_BRIGHT, cov < COVER_DARK
    if bright.mean() < COVER_MIN or dark.mean() < COVER_MIN:
        return False
    return float(lum[bright].mean()) < COVER_EMPTY and float(lum[dark].mean()) > COVER_FULL


def coverage_composite(colour, coverage, channel, inverted):
    """#92 the one image a two-image coverage draw is drawn with: `colour`'s
    rgb and `coverage`'s `channel` ('x'..'w') as alpha, inverted for D-047.
    Both are image records (xblaworld.read_level) of the same size. Pure."""
    import numpy as np                                       # noqa: E402
    cov = np.asarray(coverage['rgba'])[:, :, 'xyzw'.index(channel)]
    rgba = np.array(colour['rgba'], dtype=np.uint8, copy=True)
    rgba[:, :, 3] = (255 - cov) if inverted else cov
    return dict(name='%s+%s.%s%s' % (colour['name'], coverage['name'], channel,
                                     '-inverted' if inverted else ''),
                w=int(rgba.shape[1]), h=int(rgba.shape[0]), fmt=0,
                has_alpha=bool((rgba[:, :, 3] != 255).any()), rgba=rgba)


def material_frames(pools, base, nverts):
    """The per-vertex TANGENT FRAME (#69 material), one entry per vertex of
    the bundle: the pool's `frame` where a pool carries one, zeros elsewhere.
    `base` maps a pool index to where its vertices start."""
    frames = [(0.0, 0.0, 0.0, 0.0, 0.0, 0.0)] * nverts
    n = 0
    for pi, p in enumerate(pools):
        if p.get('frame') is None or pi not in base:
            continue
        b = base[pi]
        for i, fr in enumerate(p['frame']):
            frames[b + i] = tuple(fr)
            n += 1
    return frames, n


def material_animation(pools, draws, base, nverts):
    """The bundle's MATERIAL ANIMATION (#69): (aux, anims, report).

    `pools` and `draws` are xblaworld.read_level's; `base` maps a pool index
    to where its vertices start in the bundle's array of `nverts`. A draw
    that carries an animation record (`wave`, see xblaworld.wave_record)
    moves every vertex it references; a record is written once however many
    draws share it, and a vertex's fields (its wave pair and normal, from the
    pool's `aux`) travel beside the record's 1-based index.

    Refused as a whole, never partly: a vertex two draws move with DIFFERENT
    records has no single displacement, and a draw whose pool carries no
    fields cannot be moved at all - either says the item is not what the
    reader took it for, and a surface that animates in one place and not
    another is worse than one that stands still.

    `aux` is None and `anims` empty when nothing animates."""
    aux = [(0, 0, 0.0, 0.0, 0.0, 0)] * nverts
    for pi, p in enumerate(pools):
        if p.get('aux') is None or pi not in base:
            continue
        b = base[pi]
        for i, (wa, wb, nx, ny, nz) in enumerate(p['aux']):
            aux[b + i] = (wa, wb, nx, ny, nz, 0)
    anims = []
    keys = {}
    owner = {}
    moved = 0
    conflicts = 0
    nofields = 0
    for dr in draws:
        rec = dr.get('wave')
        if rec is None or dr['pool'] not in base:
            continue
        if pools[dr['pool']].get('aux') is None:
            nofields += 1
            continue
        key = repr(sorted(rec.items()))
        ai = keys.get(key)
        if ai is None:
            anims.append(rec)
            ai = len(anims)
            keys[key] = ai
        b = base[dr['pool']]
        for i in set(dr['indices']):
            g = b + i
            prev = owner.get(g)
            if prev is not None and prev != ai:
                conflicts += 1
                continue
            if prev is None:
                owner[g] = ai
                moved += 1
                e = aux[g]
                aux[g] = (e[0], e[1], e[2], e[3], e[4], ai)
    rep = dict(records=len(anims), vertices=moved, conflicts=conflicts,
               nofields=nofields,
               draws=sum(1 for dr in draws if dr.get('wave') is not None))
    if conflicts or nofields or not anims:
        return None, [], rep
    return aux, anims, rep


def add_zenith_clouds(draws, vertices, indices, mats, tex_recs, mesh_recs):
    """#68 #76 D-023 THE GENERATED CLOUD FIELD - a documented divergence, not
    the source.

    `draws` is [(slot, draw vertices, image record), ...] for the dome draws
    the sky is walked with. For each OPAQUE one whose image is a clear cap at
    the pole with a cloud band below it, zenith.py grows a periodic cloud tile
    from that band and a cap of directions to carry it, and it is appended
    here as ONE MORE draw - after every source draw, with its own image,
    flagged MAT_GENERATED | MAT_CLOUD_PLANE - so the runtime lays it on the
    game's own cloud plane and phase, and can leave it out (SL_SKY_ZENITH=0)
    to draw the source dome exactly. The records already in the lists are
    never touched. An image without that structure adds nothing, and the line
    printed says why. Returns how many were added."""
    import numpy as np                                       # noqa: E402
    import zenith                                            # noqa: E402
    added = 0
    for slot, dverts, t in draws:
        if t['has_alpha']:
            continue
        try:
            cv, ci, crgba, rep = zenith.generate(t['rgba'], dverts)
        except zenith.NotApplicable as exc:
            print('    draw %d: no generated zenith clouds - %s' % (slot, exc))
            continue
        b = rep['b']
        print('    draw %d: generated cloud field (D-023): clear cap to %.1f deg, '
              'band settled at %.1f deg (row %d); tile %dx%d grown from %d band '
              'windows; carrier from %.1f deg elevation, opaque from %.1f; '
              '%d vertices, %d triangles'
              % (slot, b['th_cap'], b['th_bs'], b['band_row'], crgba.shape[1],
                 crgba.shape[0], zenith.PATCHES, rep['low'], rep['low'] + rep['fade'],
                 len(cv), len(ci) // 3))
        base = len(vertices)
        first = len(indices)
        vertices.extend(cv)
        indices.extend(base + i for i in ci)
        tex_recs.append((crgba.shape[1], crgba.shape[0],
                         np.ascontiguousarray(crgba).tobytes()))
        mats.append((len(tex_recs) - 1, slwa.BLEND_ALPHA,
                     slwa.MAT_GENERATED | slwa.MAT_CLOUD_PLANE))
        mesh_recs.append((len(mats) - 1, first, len(ci)))
        added += 1
    return added


#: The divergences the preparation writes into a bundle, and the items each was
#: decided for (docs/divergences.md). D-023 (the generated zenith clouds) and
#: D-026 (the panorama shell that fades into the dome) were both decided on the
#: Dam's sky; D-023 was extended to Runway's, Surface's, Surface 2's and Frigate's domes, whose clear caps
#: are the same kind as the Dam's (Frigate's under D-034's game cloud layer), and D-026 to Surface's background, whose outermost ring is
#: the same painted panorama and which the game's own atmosphere fogs out
#: completely (D-030). Every other stage's dome and background are prepared as
#: the source authors them until a decision names that stage too. Adding a
#: stage is adding its item here, and nothing else.
#: D-032 (the game's tint on a prop's hazard bars, gameshade.restore_prop_tint)
#: was decided for Silo's steel door, the game's Psteel_door1Z - a prop key,
#: not a stage: the bundle is shared by every stage that draws the model, and
#: Silo is the only one that does. D-033 (the game's shade on the background's
#: whitened untextured surfaces, gameshade.restore_background_shade) was
#: decided for Silo's rail-end caps; the rule would change other stages'
#: backgrounds too (round silo4 measured what), so it names Silo alone.
#: D-035 (the remaster's opaque triangles are cut at the game's waterline -
#: the lowest opaque room corner of a stage whose rooms fade - and their part
#: below is withheld, gameshade.withhold_translucent_only) was decided for
#: Frigate's underwater hull and names Frigate alone. D-036 (a decal placement
#: the remaster turned against the orientation the game paints it in, and the
#: other placements of its image share, is turned back,
#: gameshade.orient_decals) was decided for Frigate's deck numbers and names
#: Frigate alone.
#: D-037 (a remastered decal placement redrawing a game decal the game blends
#: under an environment alpha takes that alpha and is drawn translucent,
#: gameshade.decal_opacity) was decided for Frigate's deck paint and names
#: Frigate alone.
#: D-042 (a shaped layer laid as a band onto its base image's own surface
#: fades to nothing there, and its image is turned to run across the band,
#: band_fade) was decided for Bunker 2's helipad rim and names Bunker 2 alone.
#: D-048 (a draw lying on the game's moving water with the game's own
#: coordinates moves with the game's water phases, gameshade.game_water) was
#: decided for Caverns' water and names Caverns' background alone.
#: D-049 (a shadow overlay - a shaped draw every vertex of which is pure
#: black - is drawn blended at the remaster's own shadow alpha in place of
#: opaque, shadow_overlays) was decided for Cradle's truss shadows and names
#: Cradle's background alone.
#: D-051 (a draw whose base image is the source's magenta placeholder checker
#: is withheld, placeholder_checker) was decided for Aztec's vines and strip
#: lights and names Aztec's background; Egyptian's (`temple`, its palm
#: trunks on the placeholder's fill, placeholder_fill) is named too.
#: D-052 (a pale item's vertex colours take the shade the game lays over its
#: own model, gameshade.prop_game_shade) was decided for Aztec's stone door
#: and names `door_aztec` alone.
#: D-053 (the item is the source's original generation, its decals at the
#: game's alpha, and the background's image-less stand-ins of them withheld,
#: ORIGINAL_GENERATION, decal_stand_ins) was decided for Aztec's shuttle and
#: names Aztec's background and `shuttle` alone.
#: D-054 (a door item wholly in the translucent pass over a game model that
#: blends nothing is not prepared, so the game's door draws, translucent_only,
#: gameshade.prop_blends) was decided for Egyptian's crypt doors and names
#: those five models alone.
DIVERGENCE_ITEMS = {
    'D-023': ('dam', 'runway', 'surface', 'surface2', 'frigate'),
    'D-026': ('dam', 'surface'),
    'D-032': ('steel_door1',),
    'D-033': ('silo',),
    'D-034': ('frigate',),
    'D-035': ('frigate',),
    'D-036': ('frigate',),
    'D-037': ('frigate',),
    'D-042': ('bunker2',),
    'D-047': ('depot',),
    'D-048': ('cavern',),
    'D-049': ('cradle',),
    'D-051': ('aztec', 'temple'),
    'D-052': ('door_aztec',),
    'D-053': ('aztec', 'shuttle'),
    'D-054': ('cryptdoor1a', 'cryptdoor2a', 'cryptdoor2b', 'cryptdoor3', 'cryptdoor4'),
}

# ---- D-051: the source's placeholder image withheld ------------------------
#
# Aztec's background names five 16x16 images (the source's 32, 50, 56, 57 and
# 58) that are no texture at all: a 2 x 2 checker of 8 x 8 squares, full
# magenta (255, 0, 255) on one diagonal and dark magenta (117 .. 120, 0, same)
# on the other, opaque - the authoring tool's missing-image fill. Draws on
# them carry the vines and the ceiling strip lights over the temple's upper
# halls and the lights around the exhaust bay, 2809 triangles. The remaster
# draws them magenta (private emulator, room 72); the game draws nothing
# there. A scan of the 19 prepared backgrounds finds the pattern in Aztec's
# five images and nowhere else.
#
# So for the items DIVERGENCE_ITEMS['D-051'] names, a draw whose base image is
# that checker no longer samples it. The rule is the pattern, not the colour
# alone: every texel has no green (at most PLACEHOLDER_GREEN) and equal red
# and blue (within PLACEHOLDER_GREEN), alpha 255, and the bright texels (red
# at least PLACEHOLDER_BRIGHT) fill exactly two diagonal quadrants.
#
# What the draw keeps depends on what decides its shape. An OPAQUE draw
# (style low byte 1, outside the foliage group PLACEHOLDER_SHAPED_GROUP) is
# whole in its geometry: it is drawn by its own vertex colour over one white
# image, the untextured contract (#81). Aztec's ceiling tubes are such draws
# (0x2018, 0x484c, style 0x4701): withheld, the tubes lost their sleeves and
# only the glowing cores inside them drew (playtest run 20261012-195917,
# mark 4). A draw in the foliage group (0x4b01, the alpha-tested cutouts) or
# a translucent one (low byte 2) takes its outline or its coverage from the
# image's alpha, which the source does not hold, and is left out of the
# bundle, as a draw whose image is not carried already is.
#
# Egyptian's background (`temple`) names one more placeholder, its image 15
# (16x16, format 8_8_8_8 with a mip chain): it decodes to full magenta
# (255, 0, 255) and dark magenta (120, 0, 120) over its texels with any
# alpha, and every other texel clear (alpha 0) - the same fill, read with
# clear texels round it. The remaster's palm trunks (pool 3, 703 vertices:
# 19 trunks, style 0x4701) are drawn on it, and the remaster draws them
# magenta under their fronds (private emulator, start courtyard, room 31);
# the game has no palms. So the rule takes that form too (placeholder_fill):
# alpha 0 or 255 on every texel, every opaque texel the magenta of the rule
# above, both full and dark present. The trunks are opaque draws and draw by
# their vertex colour.
PLACEHOLDER_GREEN = 8
PLACEHOLDER_BRIGHT = 200
PLACEHOLDER_SHAPED_GROUP = 0x0800


def placeholder_checker(rgba):
    """D-051: whether `rgba` (H x W x 4 uint8) is the magenta placeholder
    checker (see PLACEHOLDER_GREEN). Pure."""
    import numpy as np                                       # noqa: E402
    a = np.asarray(rgba).astype(np.int32)
    if a.ndim != 3 or a.shape[2] != 4 or a.shape[0] != a.shape[1] or a.shape[0] < 2 \
            or a.shape[0] % 2:
        return False
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    if (g > PLACEHOLDER_GREEN).any() or (np.abs(r - b) > PLACEHOLDER_GREEN).any() \
            or (al != 255).any():
        return False
    n = a.shape[0] // 2
    q = np.zeros(a.shape[:2], dtype=bool)
    q[:n, n:] = q[n:, :n] = True
    bright = r >= PLACEHOLDER_BRIGHT
    return bool((bright == q).all() or (bright == ~q).all())


def placeholder_fill(rgba):
    """D-051: whether `rgba` (H x W x 4 uint8) is the placeholder's magenta
    with clear texels round it (Egyptian's image 15): alpha 0 or 255 on every
    texel, every opaque texel without green and with equal red and blue
    (within PLACEHOLDER_GREEN), both full (red at least PLACEHOLDER_BRIGHT)
    and dark ones present. Pure."""
    import numpy as np                                       # noqa: E402
    a = np.asarray(rgba).astype(np.int32)
    if a.ndim != 3 or a.shape[2] != 4:
        return False
    al = a[..., 3]
    if not ((al == 0) | (al == 255)).all():
        return False
    o = a[al == 255]
    if not len(o) or (o[:, 1] > PLACEHOLDER_GREEN).any() \
            or (np.abs(o[:, 0] - o[:, 2]) > PLACEHOLDER_GREEN).any():
        return False
    bright = o[:, 0] >= PLACEHOLDER_BRIGHT
    return bool(bright.any() and (~bright).any())


def placeholder_opaque(style):
    """D-051: whether a placeholder draw of style word `style` is whole in its
    geometry - the opaque pass, outside the foliage group. Pure."""
    s = style or 0
    return (s & 0xff) == 1 and not (s & PLACEHOLDER_SHAPED_GROUP)


def withhold_placeholders(draws, textures):
    """D-051: every draw whose base image is a placeholder (the checker, or
    its fill with clear texels round it), in place: an opaque one
    (placeholder_opaque) names no image from here on and draws by its vertex
    colour; every other one is removed from `draws`. Returns ((draws,
    triangles) withheld, (draws, triangles) drawn by colour, the placeholder
    images)."""
    hit = {}
    out = []
    coloured = [0, 0]
    for dr in draws:
        im = dr.get('image')
        if isinstance(im, int) and 0 <= im < len(textures):
            if im not in hit:
                hit[im] = placeholder_checker(textures[im]['rgba']) \
                    or placeholder_fill(textures[im]['rgba'])
            if hit[im]:
                if not placeholder_opaque(dr.get('style')):
                    continue
                dr['image'] = None
                coloured[0] += 1
                coloured[1] += dr['tris']
        out.append(dr)
    kept = set(id(dr) for dr in out)
    tris = sum(dr['tris'] for dr in draws if id(dr) not in kept)
    n = len(draws) - len(out)
    draws[:] = out
    return (n, tris), tuple(coloured), sorted(i for i, v in hit.items() if v)

# ---- D-049: a shadow overlay drawn as a shadow -----------------------------
#
# Cradle's background lays the antenna's truss shadows onto its floors as
# separate draws of the truss images under a vertex colour of pure black:
# the lower platform's (draws 0x300 / 0x310, the 512x512 brace image, in the
# source's cutout gate 0x4b01, alpha test on) and the hub's and catwalks'
# (the 256x512 truss image, style 0x02, translucent). Every vertex carries
# colour word 0xff000000 (8_8_8_8 at the record's last word, as the item's
# programs declare it), the programs export texel x colour, so the shapes
# draw OPAQUE BLACK - measured identically in the private emulator at the
# marked floor (round crd5) - where the game shades the same floors to 0.544
# of their lit brightness (ORIGINAL at playtest run 20261012-131506 mark 1:
# floor luma 67.9 in shadow against 124.7 lit) and the floor shows through.
# The remaster's finished stages author their black shadow overlays
# translucent: vertex alpha 139 on the Dam, 143 on Archives, 107 and 147 on
# Depot (every all-black blended mesh of those bundles), median 143.
#
# The same shadows come a second way: as the LAYER of a two-layer draw
# (xblaworld.layer_blend), the truss or rail image laid over a floor image
# at layer colour 0x000000 (draw 0x23ec, the three arms' floors, 36
# triangles) or 0x040404 (draws 0x25a8 .. 0x26ac, the top catwalks' rail and
# cable shadows, 22 triangles), alpha 255 - the program's weight is that
# alpha times the image's, so the shape is laid on at full strength, black.
#
# And a third: inside a draw of the truss image whose other triangles are
# the truss's own decals (draw 0x210, 116 triangles under vertex colours
# 0xffffff, 0x333333, 0x040404 and 0x000000; draw 0x200 shares its black
# vertices).
#
# So for the items DIVERGENCE_ITEMS['D-049'] names, every triangle of a
# shaped draw (its image carries alpha) whose three vertices have every
# colour channel at most SHADOW_BLACK - and every such two-layer draw's
# layer - is drawn BLENDED with SHADOW_ALPHA as its vertex alpha: the floor
# darkens to 0.44 of itself under the shape, its detail kept. The triangles
# move onto copies of their vertices, so no other triangle changes.
SHADOW_ALPHA = 143
SHADOW_BLACK = 8


def _black(vs, ix):
    return bool(ix) and all(max(vs[i][5:8]) <= SHADOW_BLACK for i in ix)


def shadow_overlays(draws, pools, textures):
    """D-049: carry every shadow overlay of a level item (see SHADOW_ALPHA)
    at SHADOW_ALPHA, in place. A draw's triangles whose three vertices are
    black become a draw of their own (`shadow`=True, right after the draw
    they came from, which keeps the rest) on COPIES of their vertices with
    the alpha written - so no other triangle's colour moves; a two-layer
    draw's black layer takes the alpha on its own layer surface (one copy
    per pool and words, keyed apart from the unchanged surface). Returns
    the triangles marked per draw."""
    def shaped(im):
        return im is not None and 0 <= im < len(textures) and bool(textures[im].get('has_alpha'))

    def faded(v):
        return tuple(v[:8]) + (SHADOW_ALPHA,) + tuple(v[9:])
    marked = []
    layers = {}                                    # (pool, words) -> the faded surface
    out = []
    for dr in draws:
        out.append(dr)
        ly = dr.get('layer')
        if ly is not None:
            li, words, lv = ly['layer']
            lix = set(dr.get('indices') or ())
            if shaped(li) and _black(lv, lix):
                key = (dr['pool'], tuple(words))
                if key not in layers:
                    layers[key] = list(lv)
                fl = layers[key]
                for i in lix:
                    fl[i] = faded(lv[i])
                ly['layer'] = (li, tuple(words) + ('D-049',), fl)
                marked.append(len(dr['indices']) // 3)
            continue
        if not shaped(dr.get('image')) or dr.get('xform') is not None:
            continue
        pv = pools[dr['pool']].get('vertices')
        ind = dr.get('indices') or []
        if pv is None or not ind:
            continue
        keep, dark = [], []
        for t in range(0, len(ind) - 2, 3):
            tri = ind[t:t + 3]
            (dark if _black(pv, tri) else keep).extend(tri)
        if not dark:
            continue
        copy = {}
        for i in dark:
            if i not in copy:
                copy[i] = len(pv)
                pv.append(faded(pv[i]))
        sd = dict(dr, indices=[copy[i] for i in dark], shadow=True)
        if keep:
            dr['indices'] = keep
            out.append(sd)
        else:
            out[-1] = sd
        marked.append(len(dark) // 3)
    draws[:] = out
    return marked


#: D-034 THE DOME AS A CLOUD COVER. Frigate's dome is the clear daylight image
#: every source dome carries; under the game's own night-navy record (D-034's
#: runtime shade, sl_wa_stage_game_atmosphere) its blue sky would read as
#: bright cloud, because the shade interpolates the cloud colour over the fog
#: colour BY the image. So the preparation writes, for each of the dome's
#: images, the fraction of each texel that is CLOUD rather than sky: a cloud
#: is white or grey (low saturation), the sky is blue (high saturation) -
#: coverage = clamp(1 - saturation / COVER_SAT) * clamp(value / COVER_VALUE),
#: saturation = (max - min) / max over r g b, value = max. Grey RGB, alpha
#: kept. The runtime then lays the game's cloud colour over its sky colour by
#: that coverage, as sky.c lays its own intensity cloud image. Measured on the
#: dome image: blue sky saturation 0.35 .. 0.55, white and pink cloud 0.0 ..
#: 0.2.
COVER_SAT = 0.35
COVER_VALUE = 200.0


def cloud_cover(rgba):
    """D-034: `rgba` (H x W x 4 uint8) as a grey cloud-coverage image (see
    COVER_SAT), alpha kept. Pure."""
    import numpy as np                                       # noqa: E402
    a = rgba[:, :, :3].astype(np.float64)
    mx = a.max(axis=2)
    mn = a.min(axis=2)
    sat = np.where(mx > 0.0, (mx - mn) / np.maximum(mx, 1.0), 0.0)
    cov = np.clip(1.0 - sat / COVER_SAT, 0.0, 1.0) * np.clip(mx / COVER_VALUE, 0.0, 1.0)
    g = np.rint(cov * 255.0).astype(np.uint8)
    out = np.empty_like(rgba)
    out[:, :, 0] = g
    out[:, :, 1] = g
    out[:, :, 2] = g
    out[:, :, 3] = rgba[:, :, 3]
    return out


def grade_cloud_cover(tex_recs):
    """D-034: every image record of a dome as its cloud coverage, in place.
    Returns how many were graded."""
    import numpy as np                                       # noqa: E402
    for i, (w, h, data) in enumerate(tex_recs):
        a = np.frombuffer(data, dtype=np.uint8).reshape(h, w, 4)
        tex_recs[i] = (w, h, np.ascontiguousarray(cloud_cover(a)).tobytes())
    return len(tex_recs)

#: THE SWITCH BACK for the divergences the preparation writes into a bundle's
#: own bytes: a comma-separated list of codes (`SL_WA_AS_AUTHORED=D-032`)
#: prepares those as the source authors them. prepare-xbla-assets.ps1 folds
#: the variable into its stamp, so setting or clearing it re-prepares.
AS_AUTHORED = 'SL_WA_AS_AUTHORED'


def divergence_applies(code, item):
    """Whether the divergence `code` is written into the bundle for `item`."""
    off = {c.strip().upper() for c in os.environ.get(AS_AUTHORED, '').split(',') if c.strip()}
    return item in DIVERGENCE_ITEMS.get(code, ()) and code.upper() not in off


# ---- D-042: a shaped layer laid as a band onto its base's own surface -------
#
# A two-layer draw (#72, `layer` in xblaworld.read_level, by its program or by
# LAYER_CONTRACT) lerps its base image toward its layer image by the layer
# image's alpha times the vertex alpha of the layer's colour word. MEASURED on
# Bunker 2's helipad rim (pool 9, 56 vertices, 14 quads; the remaster's own
# programs dumped in round bk2d): every quad maps the whole snow image once,
# its first coordinate across the rim and its second along it, while the
# image's alpha edge runs along its rows (opaque rows 0 .. about 140) - so the
# edge cuts every quad across the rim, and the remaster draws hard white
# wedges where the game draws a soft snow border. Every vertex alpha is 255:
# nothing in the source fades the layer toward the pad.
#
# band_fade, for the items DIVERGENCE_ITEMS['D-042'] names. A draw qualifies
# when its layer is SHAPED - weighted by its image's alpha, and the image
# carries alpha - and it is a BAND onto its base image's own surface: every
# one of its triangles has a corner ON that surface and a corner off it. A
# corner is on the surface when a corner of another draw drawn with the same
# image (a two-layer draw's base counts) stands within BAND_EPS item units of
# it and carries the same base coordinate within BAND_COORD_EPS, so the base
# image continues across it. Then:
#
# 1. THE FADE. A corner on the surface takes layer weight 0 (the layer's
#    vertex alpha); the others keep theirs, so the layer fades out across
#    each triangle toward the base's surface and the base reads continuous
#    there.
# 2. THE TURN. The weight's gradient in the layer's coordinate, per
#    triangle, runs along one coordinate (u or v) when the summed unit
#    gradients along it are BAND_AXIS_RATIO times those along the other; the
#    image's alpha runs along one axis when the spread (standard deviation)
#    of its row means is BAND_AXIS_RATIO times that of its column means (v,
#    the rows) or the converse (u, the columns). When the two axes differ the
#    image's edge cuts across the fade, and the layer's two coordinates are
#    swapped - provided the swap puts the image's opaque half on the side
#    away from the base's surface (the half of the image whose mean alpha is
#    higher lies toward the mean coordinate of the off-surface corners);
#    otherwise the fade alone is applied and the reason reported.
#
# On Bunker 2: the rim alone - 28 of its 56 corners stand on the pad's (0 to
# 0.00025 units, every other corner 850 units or more), the weight runs along
# u (summed unit gradients 25.5 against 6.6), the snow's alpha along v (row
# spread 116.3 against 3.5), and the snow's opaque rows lie toward the
# snowfield side. The turn is decided for the draw as a whole: one of the
# 14 quads (corners 40-43) charts the rim the other way round, and its
# turned edge runs along the rim, ending at the pad by the fade. The base copy of the draw is untouched; the layer's own
# copy of the pool is changed and appended under its own key, so no other
# draw reading the pool moves.
BAND_EPS = 0.01
BAND_COORD_EPS = 1e-3
BAND_AXIS_RATIO = 2.0


def _surface_corners(draws, pools, dr, image):
    """(position, base coordinate) of every corner of a draw other than `dr`
    drawn with `image` (a two-layer draw by its base), as an N x 5 array."""
    import numpy as np                                       # noqa: E402
    import xblaworld                                         # noqa: E402
    rows = []
    for e in draws:
        if e is dr or pools[e['pool']]['vertices'] is None:
            continue
        ely = e.get('layer')
        if (ely['base'][0] if ely is not None else e['image']) != image:
            continue
        vs = ely['base'][2] if ely is not None else pools[e['pool']]['vertices']
        xf = e.get('xform')
        for i in sorted(set(e['indices'])):
            v = vs[i]
            p = xblaworld.instance_apply(xf, v[0], v[1], v[2]) if xf is not None else v[:3]
            rows.append((p[0], p[1], p[2], v[3], v[4]))
    return np.array(rows, dtype=np.float64).reshape(-1, 5)


def _alpha_axis(rgba):
    """('u' | 'v' | None, the spread along u, along v, the image's opaque
    half: -1 the low coordinates, +1 the high ones, along the axis found)."""
    import numpy as np                                       # noqa: E402
    al = rgba[:, :, 3].astype(np.float64)
    su = float(al.mean(axis=0).std())                        # column means: along u
    sv = float(al.mean(axis=1).std())                        # row means: along v
    axis = 'u' if su >= BAND_AXIS_RATIO * sv and su > 0.0 else \
           'v' if sv >= BAND_AXIS_RATIO * su and sv > 0.0 else None
    side = 0
    if axis is not None:
        prof = al.mean(axis=0) if axis == 'u' else al.mean(axis=1)
        h = len(prof) // 2
        side = 1 if prof[len(prof) - h:].mean() > prof[:h].mean() else -1
    return axis, su, sv, side


def band_fade(draws, pools, textures):
    """D-042 on a background item read by xblaworld.read_level, in place:
    the layer copy of every qualifying two-layer draw takes the fade, and the
    turn where it applies (see BAND_EPS). Returns the report, a list of
    dicts: at, pool, tris, on, corners, swap (True / False), why."""
    import numpy as np                                       # noqa: E402
    rep = []
    for dr in draws:
        ly = dr.get('layer')
        if ly is None or pools[dr['pool']]['vertices'] is None:
            continue
        li = ly['layer'][0]
        if not ly['texel_alpha'] or not (0 <= li < len(textures)) \
                or not textures[li]['has_alpha']:
            continue
        surf = _surface_corners(draws, pools, dr, ly['base'][0])
        if not len(surf):
            continue
        bv = ly['base'][2]
        used = sorted(set(dr['indices']))
        on = set()
        for i in used:
            v = bv[i]
            d = surf - np.array([v[0], v[1], v[2], v[3], v[4]])
            hit = (np.sqrt((d[:, :3] ** 2).sum(axis=1)) <= BAND_EPS) \
                & (np.abs(d[:, 3:5]).max(axis=1) <= BAND_COORD_EPS)
            if hit.any():
                on.add(i)
        ix = dr['indices']
        tris = [ix[t:t + 3] for t in range(0, len(ix) - 2, 3)]
        if not on or not tris or any(all(i in on for i in t) or not any(i in on for i in t)
                                     for t in tris):
            continue
        lv = ly['layer'][2]
        gu = gv = 0.0
        for t in tris:
            (u0, v0), (u1, v1), (u2, v2) = [lv[i][3:5] for i in t]
            w0, w1, w2 = [0.0 if i in on else 1.0 for i in t]
            det = (u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0)
            if abs(det) < 1e-12:
                continue
            a = ((w1 - w0) * (v2 - v0) - (w2 - w0) * (v1 - v0)) / det
            b = ((u1 - u0) * (w2 - w0) - (u2 - u0) * (w1 - w0)) / det
            n = (a * a + b * b) ** 0.5
            if n > 0.0:
                gu += abs(a) / n
                gv += abs(b) / n
        fade_axis = 'u' if gu >= BAND_AXIS_RATIO * gv and gu > 0.0 else \
                    'v' if gv >= BAND_AXIS_RATIO * gu and gv > 0.0 else None
        img_axis, su, sv, side = _alpha_axis(textures[li]['rgba'])
        swap, why = False, ''
        if fade_axis is None or img_axis is None:
            why = 'the fade (u %.1f, v %.1f) or the image\'s alpha (u %.1f, v %.1f) runs along ' \
                  'no one coordinate' % (gu, gv, su, sv)
        elif fade_axis == img_axis:
            why = 'the image\'s alpha already runs along the fade (%s)' % fade_axis
        else:
            k = 3 if fade_axis == 'u' else 4             # the coordinate the image's axis takes
            off_mean = float(np.mean([lv[i][k] for i in used if i not in on]))
            on_mean = float(np.mean([lv[i][k] for i in used if i in on]))
            if (off_mean - on_mean) * side > 0.0:
                swap = True
                why = 'the fade runs along %s, the image\'s alpha along %s (spread u %.1f, v %.1f)' \
                      % (fade_axis, img_axis, su, sv)
            else:
                why = 'turned, the image\'s opaque half would face the base\'s surface - not turned'
        out = []
        for i, v in enumerate(lv):
            if swap:
                v = v[:3] + (v[4], v[3]) + v[5:]
            if i in on:
                v = v[:8] + (0,)
            out.append(v)
        ly['layer'] = (li, tuple(ly['layer'][1]) + ('D-042',), out)
        rep.append(dict(at=dr['at'], pool=dr['pool'], on=len(on), corners=len(used),
                        swap=swap, why=why, tris=len(tris)))
    return rep


def describe_band_fade(rep):
    """band_fade's report as lines."""
    if not rep:
        return ['D-042: no shaped layer lies as a band onto its base image\'s surface - nothing faded']
    lines = ['D-042: %d band%s faded onto the base image\'s surface'
             % (len(rep), '' if len(rep) == 1 else 's')]
    for r in rep:
        lines.append('  draw at +%#06x (pool %d, %d triangles): %d of %d corners on the surface '
                     'take layer weight 0; %s; %s'
                     % (r['at'], r['pool'], r['tris'], r['on'], r['corners'],
                        'the layer\'s coordinates swapped' if r['swap'] else 'not turned', r['why']))
    return lines


#: D-026: a panorama shell closes the circle round the item - no gap between
#: its vertices, seen from the item's centre, wider than this (degrees).
SHELL_SURROUND = 60.0


#: #78 an OVERLAY lies in the plane of the surface under it: plane offsets
#: within this many item units, normals within OVERLAY_COS (the runtime's
#: coplanar thresholds, SL_WA_COPLANAR_EPS / _COS).
OVERLAY_EPS = 0.01
OVERLAY_COS = 0.9999


def overlay_meshes(vertices, indices, meshes, mats, skip=()):
    """#78 A SHAPED DRAW LAID WHOLLY ON AN EARLIER SURFACE: the decal pass the
    style word does not mark.

    The source marks its decal pass with a style bit (xblaworld.STYLE_DECAL),
    and a draw it marks takes the depth priority a surface authored to lie ON
    another needs. Runway's dock numbers are not marked: two alpha-shaped
    quads (style 0x4701, their own vertex pool) lying exactly in the plane of
    the dock wall drawn before them. With no priority the two resolve per
    pixel by floating-point noise - the numbers fight the wall.

    Such a draw is recognised from the geometry alone: a CUTOUT mesh (an alpha
    shape, depth written) every one of whose triangles has its centroid
    inside a triangle of an EARLIER opaque, non-decal mesh lying in the same
    plane. Nothing of it stands off that surface, so the only way it is ever
    seen is on top of it - which is what the decal pass gives. A shaped mesh
    with any triangle off such a surface (foliage resting on the ground, a
    grating meeting a floor) is not an overlay and is left as it is.

    Returns the set of mesh indices that are overlays; `skip` names meshes not
    to consider (the two-layer draws' layers, already decals)."""
    import numpy as np                                       # noqa: E402
    if not meshes:
        return set()
    P = np.array([v[:3] for v in vertices], dtype=np.float64)
    I = np.array(indices, dtype=np.int64).reshape(-1, 3)
    A, B, C = P[I[:, 0]], P[I[:, 1]], P[I[:, 2]]
    n = np.cross(B - A, C - A)
    ln = np.linalg.norm(n, axis=1)
    good = ln > 1e-9
    n[good] /= ln[good][:, None]
    d = (n * A).sum(1)
    owner = np.full(len(I), -1, dtype=np.int64)
    for k, (_mi, first, cnt) in enumerate(meshes):
        owner[first // 3:(first + cnt) // 3] = k
    base_ok = np.zeros(len(meshes), dtype=bool)
    for k, (mi, _f, _c) in enumerate(meshes):
        _ti, b_, f = mats[mi][:3]
        base_ok[k] = (b_ == slwa.BLEND_OPAQUE and not f & slwa.MAT_DECAL
                      and k not in skip)
    found = set()
    for k, (mi, first, cnt) in enumerate(meshes):
        _ti, b_, f = mats[mi][:3]
        if b_ != slwa.BLEND_CUTOUT or f & slwa.MAT_DECAL or k in skip or cnt < 3:
            continue
        under = good & base_ok[owner] & (owner < k)
        if not under.any():
            continue
        whole = True
        for t in range(first // 3, (first + cnt) // 3):
            if not good[t]:
                whole = False
                break
            c = (A[t] + B[t] + C[t]) / 3.0
            cand = under & ((n * n[t]).sum(1) >= OVERLAY_COS) &                 (np.abs((n * c).sum(1) - d) <= OVERLAY_EPS)
            if not cand.any():
                whole = False
                break
            ax = int(np.argmax(np.abs(n[t])))
            u, v = [q for q in range(3) if q != ax]
            a2, b2, c2 = A[cand][:, (u, v)], B[cand][:, (u, v)], C[cand][:, (u, v)]
            p2 = c[[u, v]]

            def side(p, q, r):
                return (q[:, 0] - p[:, 0]) * (r[1] - p[:, 1]) - (q[:, 1] - p[:, 1]) * (r[0] - p[:, 0])
            s1, s2, s3 = side(a2, b2, p2), side(b2, c2, p2), side(c2, a2, p2)
            inside = ((s1 >= 0) & (s2 >= 0) & (s3 >= 0)) | ((s1 <= 0) & (s2 <= 0) & (s3 <= 0))
            if not inside.any():
                whole = False
                break
        if whole:
            found.add(k)
    return found


def overlay_alone(meshes, mats, over):
    """#95 The overlays (mesh indices in `over`) whose IMAGE no other mesh of
    the item samples - the source draws that image in this overlay alone.
    -> set of mesh indices."""
    alone = set()
    for k in over:
        ti = mats[meshes[k][0]][0]
        if not any(mats[mi][0] == ti for k2, (mi, _f, _c) in enumerate(meshes) if k2 != k):
            alone.add(k)
    return alone


def overlays_withheld(over, mpass, alone):
    """#95 The overlays (`over`) prepare_background withholds: those whose
    draws stand in no decal-pass gate of the source (`mpass[k]`, the pass
    masks of mesh k's draws, none carrying xblaworld.STYLE_DECAL) and whose
    image the source draws nowhere else (`alone`, overlay_alone)."""
    import xblaworld                                         # noqa: E402
    return {k for k in over
            if k in alone and not any(pm & xblaworld.STYLE_DECAL for pm in mpass[k])}


def drop_meshes(meshes, indices, drop):
    """#95 The mesh table and index array with the meshes in `drop` left out
    (their triangles with them, the rest in order). -> (meshes, indices,
    {old mesh index: new mesh index} for every mesh kept). Materials and
    vertices are untouched."""
    out_m, out_i, renum = [], [], {}
    for k, (mi, first, cnt) in enumerate(meshes):
        if k in drop:
            continue
        renum[k] = len(out_m)
        out_m.append((mi, len(out_i), cnt))
        out_i.extend(indices[first:first + cnt])
    return out_m, out_i, renum


def panorama_shell(vertices, indices, meshes, nmats):
    """D-026 THE PANORAMA SHELL: the item's outermost ring, found from its
    geometry alone.

    Every mesh is measured by the horizontal distance of its vertices from the
    centre of the item's bounds. The shell is the set of meshes that lies
    wholly farther out than every other mesh - its nearest vertex beyond the
    farthest vertex of everything else - and that, taken together, surrounds
    the centre (no azimuth gap wider than SHELL_SURROUND). Of the sets that
    qualify, the outermost is taken. Its materials must be its own: one shared
    with a mesh inside the ring would carry the shell's rule onto scenery, so
    such an item gets no shell.

    Returns (material indices, report) or (None, reason). Nothing is named: no
    mesh index, image, level or distance is known here."""
    import math
    per = []
    xs = [v[0] for v in vertices]
    zs = [v[2] for v in vertices]
    cx = (min(xs) + max(xs)) / 2.0
    cz = (min(zs) + max(zs)) / 2.0
    for k, (mi, first, n) in enumerate(meshes):
        ids = set(indices[first:first + n])
        if not ids:
            continue
        rs = [math.hypot(vertices[i][0] - cx, vertices[i][2] - cz) for i in ids]
        per.append((k, mi, min(rs), max(rs), ids))
    if len(per) < 2:
        return None, 'fewer than two meshes'
    for t in sorted({p[2] for p in per}, reverse=True):
        inner = [p for p in per if p[2] < t]
        if not inner:
            break
        reach = max(p[3] for p in inner)
        if reach >= t:
            continue
        ring = [p for p in per if p[2] >= t]
        az = sorted(math.degrees(math.atan2(vertices[i][0] - cx, vertices[i][2] - cz)) % 360.0
                    for p in ring for i in p[4])
        gap = max([b - a for a, b in zip(az, az[1:])] + [az[0] + 360.0 - az[-1]])
        if gap > SHELL_SURROUND:
            continue
        mats = {p[1] for p in ring}
        if any(p[1] in mats for p in inner):
            return None, 'the outermost ring shares a material with the scenery inside it'
        return mats, dict(meshes=[p[0] for p in ring], inner=reach,
                          near=min(p[2] for p in ring), far=max(p[3] for p in ring),
                          gap=gap, nmats=nmats)
    return None, 'nothing lies wholly outside the rest of the item and surrounds it'


def prepare(cls, item, tree, out):
    need('numpy', 'texture decoding')
    import numpy as np                                       # noqa: E402
    import xblaworld                                         # noqa: E402

    d = find_item(tree, '%s/%s' % (cls, item))
    if d is None or not os.path.isdir(d):
        print('prepare-xbla-assets: this source has no %s/%s.' % (cls, item))
        print('  It is refused rather than guessed at. Check that it is the')
        print('  source this pipeline was made against.')
        return 1

    files = sorted(n for n in os.listdir(d) if n.lower().endswith(('.bin', '.rba')))
    if not files:
        print('prepare-xbla-assets: %s/%s holds no bundle file.' % (cls, item))
        return 1

    try:
        got = xblaworld.read_item(os.path.join(d, files[0]))
    except xblaworld.WorldAssetError as exc:
        print('prepare-xbla-assets: %s' % exc)
        return 1

    meshes, textures, draws = got['meshes'], got['textures'], got['draws']
    print('  read %s/%s: %d draw%s, %d item-local image%s'
          % (cls, item, len(meshes), '' if len(meshes) == 1 else 's',
             len(textures), '' if len(textures) == 1 else 's'))
    for i, (v, ix) in enumerate(meshes):
        print('    draw %d: %6d vertices  %6d indices (%d triangles)  '
              '[command list +%#06x]'
              % (i, len(v), len(ix), len(ix) // 3, draws[i]['at']))
    for i, t in enumerate(textures):
        print('    image %d: %4dx%-4d format %-3d %s'
              % (i, t['w'], t['h'], t['fmt'],
                 'carries alpha' if t['has_alpha'] else 'opaque'))

    if len(meshes) != len(textures):
        print('prepare-xbla-assets: %d draws against %d images do not pair one'
              % (len(meshes), len(textures)))
        print('  for one. This slice prepares only the 1:1 case and refuses the')
        print('  rest rather than inventing a pairing.')
        return 1

    # THE PAIRING. The mesh order above is the item's own draw order, parsed
    # from its command list. The image for each draw is the one whose decoded
    # pixels carry the blend that draw needs: a background's opaque shell
    # first, whatever blends over it after. Applied only to a set that splits
    # unambiguously - refused rather than guessed at when it does not.
    opaque = [i for i, t in enumerate(textures) if not t['has_alpha']]
    alpha = [i for i, t in enumerate(textures) if t['has_alpha']]
    if len(opaque) != 1 or len(alpha) != len(textures) - 1:
        print('prepare-xbla-assets: this item\'s %d images do not split into one'
              % len(textures))
        print('  opaque shell and the passes that blend over it (%d opaque, %d'
              % (len(opaque), len(alpha)))
        print('  with alpha), so which image belongs to which draw is not')
        print('  derivable here. Refused rather than guessed at.')
        return 1
    order = opaque + alpha        # draw N takes order[N]
    for slot, mi in enumerate(order):
        us = [v[3] for v in meshes[slot][0]]
        vs = [v[4] for v in meshes[slot][0]]
        print('    draw %d <- image %d (%s)   u %.4f..%-8.4f v %.4f..%.4f'
              % (slot, mi, 'alpha' if textures[mi]['has_alpha'] else 'opaque',
                 min(us), max(us), min(vs), max(vs)))

    # THE PASS THE SKY IS WALKED IN (#68). Each draw stands under a pass gate
    # (xblaworld.draw_passes), and the remaster walks a stage's skydome item in
    # its OPAQUE pass only. MEASURED on the console in two independent
    # instrumented frames at the Dam: the dome's opaque-gated draw (0x4701,
    # 7350 indices) is the frame's first world draw, once per EDRAM tile, and
    # its translucent-gated draw (0x4702, 2850 indices) is issued in NEITHER -
    # while the same frames do issue the stage background's own translucent-
    # gated draws. A draw the source never submits is not part of the asset,
    # so it is not prepared; drawn anyway it painted a grey cloud cap over the
    # whole upper sky (Sightline mark-004: upper sky (140,148,154) with it,
    # (56,97,133) without, against the console's (47,97,145)). Read per draw
    # from the item's own gates: no item, mesh index or colour decides it, and
    # a draw no gate governs is kept.
    if cls == 'skydome':
        keep = []
        for slot in range(len(order)):
            pm = draws[slot].get('pass_mask')
            walked = pm is None or bool(pm & xblaworld.PASS_OPAQUE)
            print('    draw %d: pass gate %s -> %s'
                  % (slot, 'none' if pm is None else '%#06x' % pm,
                     'walked with the sky' if walked
                     else 'NOT walked (a pass the sky is never drawn in)'))
            if walked:
                keep.append(slot)
        if not keep:
            print('prepare-xbla-assets: no draw of this item is in the pass the')
            print('  sky is walked in. Refused rather than guessed at.')
            return 1
    else:
        keep = list(range(len(order)))

    vertices, indices, mats, mesh_recs, tex_recs = [], [], [], [], []
    lo = hi = None
    for slot, mi in enumerate(order):
        verts, idx = meshes[slot]          # slot = position in the DRAW order
        # The cross-check below is about what was READ, so every draw of the
        # item counts toward it, including one the sky is not walked with.
        for v in verts:
            if lo is None:
                lo = list(v[:3]); hi = list(v[:3])
            else:
                for k in range(3):
                    lo[k] = min(lo[k], v[k]); hi[k] = max(hi[k], v[k])
        if slot not in keep:
            continue
        base = len(vertices)
        first = len(indices)
        vertices.extend(verts)
        indices.extend(base + i for i in idx)
        t = textures[mi]
        rgba = np.ascontiguousarray(t['rgba']).tobytes()
        ti = len(tex_recs)                 # its place in THIS bundle
        tex_recs.append((t['w'], t['h'], rgba))
        mats.append((ti, slwa.BLEND_ALPHA if t['has_alpha'] else slwa.BLEND_OPAQUE))
        mesh_recs.append((ti, first, len(idx)))

    # CROSS-CHECK, not the answer: the bundle's own declared AABB against the
    # one computed from the vertices actually read. A disagreement means the
    # runs found are not the runs the bundle describes, which is exactly the
    # failure this pipeline must not ship quietly.
    d0 = got.get('declared')
    if d0 is not None:
        centre = [(lo[k] + hi[k]) / 2.0 for k in range(3)]
        extent = [(hi[k] - lo[k]) / 2.0 for k in range(3)]
        worst = max(abs(centre[k] - d0['centre'][k]) for k in range(3))
        worste = max(abs(extent[k] - d0['extent'][k]) for k in range(3))
        scale = max(1.0, max(abs(v) for v in d0['extent']))
        print('    bounds: computed centre (%.1f %.1f %.1f) extent (%.1f %.1f %.1f)'
              % (centre[0], centre[1], centre[2], extent[0], extent[1], extent[2]))
        print('            declared centre (%.1f %.1f %.1f) extent (%.1f %.1f %.1f)'
              % (d0['centre'] + d0['extent']))
        if worst / scale > 0.01 or worste / scale > 0.01:
            print('prepare-xbla-assets: the geometry read disagrees with the bounds')
            print('  the bundle declares for itself. Refused rather than shipped.')
            return 1
        print('            they agree (worst %.1f of %.0f) - the runs read are the '
              'runs the bundle describes' % (max(worst, worste), scale))

    if cls == 'skydome' and not divergence_applies('D-023', item):
        print('    generated zenith clouds (D-023): not for skydome/%s - the source '
              'dome as authored' % item)
    elif cls == 'skydome':
        add_zenith_clouds([(slot, meshes[slot][0], textures[order[slot]]) for slot in keep],
                          vertices, indices, mats, tex_recs, mesh_recs)
    if cls == 'skydome' and divergence_applies('D-034', item):
        print('    cloud cover (D-034): %d image(s) written as the fraction of cloud in '
              'each texel, for the game\'s cloud layer to colour'
              % grade_cloud_cover(tex_recs))

    path = os.path.join(out, 'xbla', cls, '%s.slwa' % item)
    try:
        n = slwa.write(path, CLASSES[cls], item, vertices, indices, mats,
                       tex_recs, mesh_recs)
    except slwa.SlwaError as exc:
        print('prepare-xbla-assets: %s' % exc)
        return 1

    print('  wrote %s' % path)
    print('  %d vertices, %d indices (%d triangles), %d materials, %d images, %.1f MB'
          % (len(vertices), len(indices), len(indices) // 3, len(mats),
             len(tex_recs), n / (1024.0 * 1024.0)))
    return 0


def background_blend(style, shadow, untextured, pass_mask, has_alpha):
    """The blend of a background draw (prepare_background's THREE outcomes):
    BLEND_ALPHA for a style word whose low byte is 2, a D-049 shadow overlay,
    or an untextured draw whose innermost pass gate admits the translucent
    pass alone (#96); else BLEND_CUTOUT for an image with alpha, else
    BLEND_OPAQUE. Pure."""
    import xblaworld                                         # noqa: E402
    gate = (pass_mask or 0) & (xblaworld.PASS_OPAQUE | xblaworld.PASS_TRANSLUCENT)
    if ((style or 0) & 0xff) == 2 or shadow:
        return slwa.BLEND_ALPHA
    if untextured and gate == xblaworld.PASS_TRANSLUCENT:
        return slwa.BLEND_ALPHA
    return slwa.BLEND_CUTOUT if has_alpha else slwa.BLEND_OPAQUE


#: D-053 THE ORIGINAL SHUTTLE (#96). The game's PshuttleZ draws a "4" and an
#: emblem over the orbiter's orange flanks - textures 991 and 585, the "4" on
#: the left wing's underside and the emblem on the right - and an orange
#: chevron over its nose, blended in the decal depth mode at vertex alphas 89,
#: 89 and 191 (othermode_l 0xc4104dd8: RM_ZMODE_DEC, RM_FORCE_BL), so they
#: read faint and worn. The source's ORIGINAL generation of `prop/shuttle` is
#: that model in the item's space: 13 draws on 8 images, the "4" (draw 11,
#: `_0x0CDDBCA5.tga`) and the emblem (draw 12, `_0x075DD185.tga`) translucent
#: decal-pass draws (gate 0x2702), the chevron (draw 2) an opaque one (gate
#: 0x2701), every vertex alpha 255. The NEW generation drops both decals and
#: prints the markings into its 512x1024 wing atlas swapped - the emblem on
#: the left wing, a black "4" roundel on the right - over a reworked body
#: whose engine shroud is folded sheet; the private emulator draws it so at
#: the playtest marks (run 20261013-075405, marks 2 and 4). The atlas bakes
#: both markings into one photograph, so no derivation from the new
#: generation restores the game's layout.
#:
#: So for the items ORIGINAL_GENERATION names the item is converted from
#: the source's original generation, and its decal-pass draws take the alpha
#: the game blends its own decals at (gameshade.prop_decal_alpha): the "4"
#: and the emblem at 89, the chevron at 191 and drawn translucent. And the
#: remaster's background, which holds two untextured quads where the decals
#: were (pool 12 of `background/aztec`, triangles 16 .. 19 of draw 0x4af8:
#: the decals' sizes and vertex colours at alpha 128), drawn as two pale
#: squares, withholds them: the untextured triangles under a
#: translucent-only pass gate (the draws background_blend blends by their
#: gate) whose three corners are neutral grey (decal_stand_ins). In Aztec's
#: background that is those four triangles and nothing else (the pad's
#: markings are yellow, the exhaust shapes orange and brown).
ORIGINAL_GENERATION = ('shuttle',)


def decal_stand_ins(draws, pools):
    """D-053: remove, in place, every triangle of an untextured draw under a
    translucent-only pass gate whose three corners are neutral grey (red,
    green and blue equal). Returns (draws touched, triangles removed)."""
    import xblaworld                                         # noqa: E402
    touched = removed = 0
    for dr in draws:
        gate = (dr.get('pass_mask') or 0) & (xblaworld.PASS_OPAQUE | xblaworld.PASS_TRANSLUCENT)
        if dr.get('image') is not None or gate != xblaworld.PASS_TRANSLUCENT:
            continue
        pv = (pools[dr['pool']].get('vertices') if 0 <= dr['pool'] < len(pools) else None) or []
        ind = dr.get('indices') or []
        keep = []
        for t in range(0, len(ind) - 2, 3):
            tri = ind[t:t + 3]
            if all(i < len(pv) and pv[i][5] == pv[i][6] == pv[i][7] for i in tri):
                removed += 1
                continue
            keep.extend(tri)
        if len(keep) != len(ind):
            touched += 1
            dr['indices'] = keep
            dr['tris'] = len(keep) // 3
    draws[:] = [dr for dr in draws if dr.get('image') is not None or dr.get('indices')]
    return touched, removed


def background_decal(style, untextured, pass_mask):
    """The decal-pass bit (slwa.MAT_DECAL or 0) of a background draw: its
    style word's xblaworld.STYLE_DECAL, or - for an untextured draw that
    background_blend blends by its pass gate - the gate's own (#96). Pure.

    Aztec's launch-pad markings (pool 12, 38 triangles: the yellow dashed
    arrow and the white and orange marks, untextured at vertex alpha 128) carry
    style 0x4701 under gate 0x2702, whose 0x2000 is the decal pass: they lie
    exactly on the pad's floor. Blended without the decal pass's depth
    priority, they tied with the floor pixel by pixel in the late translucent
    draw, and while an explosion burned at the shuttle's base the arrow came
    and went from read to read (playtest run 20261012-195917, mark 5; in 120
    consecutive reads its box counted 0 .. 2565 yellow pixels)."""
    import xblaworld                                         # noqa: E402
    if (style or 0) & xblaworld.STYLE_DECAL:
        return slwa.MAT_DECAL
    gate = (pass_mask or 0) & (xblaworld.PASS_OPAQUE | xblaworld.PASS_TRANSLUCENT)
    if untextured and gate == xblaworld.PASS_TRANSLUCENT and (pass_mask & xblaworld.STYLE_DECAL):
        return slwa.MAT_DECAL
    return 0


def prepare_background(cls, item, tree, out, rom=None):
    """The stage's whole STATIC BACKGROUND, as one replacement item.

    WHY THE WHOLE ITEM. The previous round measured that no bounded sub-target
    can be chosen: the original side separates only below room granularity (one
    N64 texture number paints the cliff), the source side is one unlabelled
    bundle of material batches, and no artefact in either names the other. What
    the remaster itself replaces, and the unit its own display-mode switch
    swaps, is the WHOLE background item - so that is the unit here.

    WHAT IS DERIVED AND WHAT IS READ. The geometry, the primitive types, the
    shared pools, the per-draw base image and the per-draw style are all READ
    from the item's own records (see xblaworld.read_level). Exactly one thing
    is derived, and it is written down as one: whether a draw BLENDS. The style
    id's low byte is 1 on 363 of the Dam's draws and 2 on 22, and the draws
    whose low byte is 2 are overwhelmingly the ones whose image carries alpha -
    so a draw blends when its style says so OR when its own image carries
    alpha. Being generous here costs sorting, never a black box where a decal
    should be.
    """
    need('numpy', 'texture decoding')
    import numpy as np                                       # noqa: E402
    import xblaworld                                         # noqa: E402

    d = find_item(tree, '%s/%s' % (cls, item))
    if d is None or not os.path.isdir(d):
        print('prepare-xbla-assets: this source has no %s/%s.' % (cls, item))
        print('  It is refused rather than guessed at. Check that it is the')
        print('  source this pipeline was made against.')
        return 1
    files = sorted(n for n in os.listdir(d) if n.lower().endswith(('.bin', '.rba')))
    if not files:
        print('prepare-xbla-assets: %s/%s holds no bundle file.' % (cls, item))
        return 1

    try:
        got = xblaworld.read_level(os.path.join(d, files[0]),
                                   short=item in CHART_SHORT_ITEMS)
    except xblaworld.WorldAssetError as exc:
        print('prepare-xbla-assets: %s' % exc)
        return 1

    rep, pools, draws, textures = (got['report'], got['pools'], got['draws'],
                                   got['textures'])
    print('  read %s/%s: %d command records (consumed %s), %d draws, %d pools, '
          '%d item-local images'
          % (cls, item, rep['records'],
             'the whole record area' if rep['complete'] else 'PART of the record area',
             rep['draws'], rep['pools'], len(textures)))
    print('    kept %d draws, %d triangles' % (rep['kept'], rep['triangles']))
    for k in ('bad_primitive', 'no_index_buffer', 'ambiguous_index_buffer',
              'no_pool', 'bad_pool', 'out_of_range', 'ambiguous_pool', 'no_image'):
        if rep.get(k):
            print('    dropped/flagged %-24s %d' % (k, rep[k]))
    for i, p in enumerate(pools):
        uv = ('uv at +%d over 1/%g; %.1f units per raw unit along an edge, '
              '%.0f units per repeat; %.1f%% of its values land on the scale'
              % (p['uv_offset'], p['uv_divisor'], p['uv_edge'],
                 p['uv_edge'] * p['uv_divisor'], 100.0 * p['uv_evidence'])
              ) if p['uv_offset'] is not None else 'no texture coordinate found'
        if p.get('sphere_image'):
            uv = ('a coordinate at +%d, and a unit normal at +%d: its draws name a sphere '
                  'image, so it is sphere-mapped by the normal' % (p['uv_offset'], p['sphere']))
        elif p.get('sphere') is not None:
            if any(d['pool'] == i and d['image'] is not None for d in draws):
                uv = ('no texture coordinate: a unit normal at +%d, sphere-mapped by it '
                      '(the reflection held still)' % p['sphere'])
            else:
                uv = ('no texture coordinate: a unit normal at +%d, and no draw on it '
                      'names an image (untextured)' % p['sphere'])
        if p.get('repeat_from') is not None:
            uv += ('; tied with 1/%g, read at the item\'s repeat' % p['repeat_from'])
        if p.get('image_repeat_from') is not None:
            uv += ('; measured 1/%g, read at its images\' repeat elsewhere in the item'
                   % p['image_repeat_from'])
        if p.get('stream') is not None:
            uv += ('; a second stream of %d bytes per vertex at %#x, laid before '
                   'the colour word' % (p['stream'][1], p['stream'][0]))
        print('    pool %2d  off %#010x stride %2d %7d vertices  %s'
              % (i, p['off'], p['stride'], p['count'],
                 ('read; ' + uv) if p['vertices']
                 else ('REFUSED - ' + uv)))

    # ---- D-033 THE GAME'S SHADE on the whitened untextured surfaces ------
    # (gameshade.restore_background_shade), for the stages DIVERGENCE_ITEMS
    # names and with the user's ROM: the pools' colours change in place, so
    # everything below reads them as it always has.
    if divergence_applies('D-033', item):
        if not rom:
            print("    D-033 not applied: no ROM to read the game's shade from")
        else:
            import gameshade                                 # noqa: E402
            try:
                srep = gameshade.restore_background_shade(rom, item, pools, draws)
                for line in gameshade.describe_shade(srep):
                    print('    ' + line)
            except gameshade.ShadeError as exc:
                print('    D-033 not applied: %s' % exc)

    # ---- D-035 THE GAME'S WATERLINE ends the remaster's hull -------------
    # (gameshade.withhold_translucent_only): an opaque remastered triangle is
    # cut at the lowest opaque corner of the game's rooms and its part below
    # leaves its draw, so the game's sea shows there.
    if divergence_applies('D-035', item):
        if not rom:
            print("    D-035 not applied: no ROM to read the game's rooms from")
        else:
            import gameshade                                 # noqa: E402
            try:
                wrep = gameshade.withhold_translucent_only(rom, item, draws, pools, textures)
                for line in gameshade.describe_withheld(wrep):
                    print('    ' + line)
            except gameshade.ShadeError as exc:
                print('    D-035 not applied: %s' % exc)

    # ---- D-036 A DECAL READS THE WAY THE GAME PAINTS IT -------------------
    # (gameshade.orient_decals): a decal placement turned against the game's
    # orientation, and against the other placements of its image, is turned
    # back within its own coordinate range.
    if divergence_applies('D-036', item):
        if not rom:
            print("    D-036 not applied: no ROM to read the game's decals from")
        else:
            import gameshade                                 # noqa: E402
            try:
                orep = gameshade.orient_decals(rom, item, draws, pools)
                for line in gameshade.describe_oriented(orep):
                    print('    ' + line)
            except gameshade.ShadeError as exc:
                print('    D-036 not applied: %s' % exc)

    # ---- D-037 THE GAME'S DECAL ALPHA on the remaster's decals
    # (gameshade.decal_opacity): the game paints its deck markings and signs
    # as decals blended under an environment alpha (0x66, 0x7f); the
    # remaster's decal pass leaves their opacity at one - its translucent
    # draws at vertex alpha 255, its opaque ones (the deck's dashed arcs)
    # never blended - so the paint reads as a solid mat. The game's alpha
    # goes into the vertex alpha, and an opaque placement is drawn translucent.
    if divergence_applies('D-037', item):
        if not rom:
            print("    D-037 not applied: no ROM to read the game's decals from")
        else:
            import gameshade                                 # noqa: E402
            try:
                prep = gameshade.decal_opacity(rom, item, draws, pools)
                for line in gameshade.describe_opacity(prep):
                    print('    ' + line)
            except gameshade.ShadeError as exc:
                print('    D-037 not applied: %s' % exc)

    # ---- D-042 A SHAPED LAYER FADES ONTO ITS BASE'S OWN SURFACE (band_fade):
    # the layer of a two-layer band takes weight 0 where it stands on its base
    # image's surface, and its image is turned to run across the band when
    # its alpha edge would cut across it. The source alone decides it.
    if divergence_applies('D-042', item):
        for line in describe_band_fade(band_fade(draws, pools, textures)):
            print('    ' + line)

    # ---- D-048 THE GAME'S WATER MOVES ON THE REMASTER'S -------------------
    # (gameshade.game_water): a draw lying on the game's moving water with the
    # game's own coordinates is marked, and its material takes
    # slwa.MAT_GAME_WATER below, so the runtime moves it by the game's phases.
    if divergence_applies('D-048', item):
        if not rom:
            print("    D-048 not applied: no ROM to read the game's water from")
        else:
            import gameshade                                 # noqa: E402
            try:
                for line in gameshade.describe_water(gameshade.game_water(rom, item, draws, pools)):
                    print('    ' + line)
            except gameshade.ShadeError as exc:
                print('    D-048 not applied: %s' % exc)

    # ---- D-049 A SHADOW OVERLAY IS DRAWN AS A SHADOW (shadow_overlays): a
    # shaped draw's triangles whose vertices are black are blended at the
    # remaster's own shadow alpha; its material takes BLEND_ALPHA below.
    if divergence_applies('D-049', item):
        sh_marked = shadow_overlays(draws, pools, textures)
        print('    D-049: %d shadow overlay%s (%d triangles) blended at vertex alpha %d'
              % (len(sh_marked), '' if len(sh_marked) == 1 else 's', sum(sh_marked),
                 SHADOW_ALPHA))

    # ---- one vertex array, the pools laid end to end ---------------------
    vertices = []
    base = {}
    for i, p in enumerate(pools):
        if p['vertices'] is None:
            continue
        base[i] = len(vertices)
        vertices.extend(p['vertices'])

    # ---- TWO-LAYER draws (#72): each surface's own run of vertices -------
    # A draw whose program blends two images (xblaworld.LAYER_INPUTS) is
    # carried as its BASE surface and a coplanar blended LAYER over it, and
    # each reads the pool with its own coordinate and colour words - so each
    # gets its own copy of the pool, appended once per (pool, words). A layer
    # whose weight leaves out its image's alpha can only be drawn that way
    # when the image has none, so such a draw is reported and left as it was.
    lbase = {}
    nlayered = 0
    for dr in draws:
        ly = dr.get('layer')
        if ly is None or dr['pool'] not in base:
            continue
        li = ly['layer'][0]
        if not ly['texel_alpha'] and 0 <= li < len(textures) \
                and textures[li]['has_alpha']:
            print('    draw at +%#06x blends its layer without the image alpha, '
                  'and the image carries alpha - left as a single surface' % dr['at'])
            dr['layer'] = None
            continue
        nlayered += 1
        for role in ('base', 'layer'):
            key = (dr['pool'], ly[role][1])
            if key not in lbase:
                lbase[key] = len(vertices)
                vertices.extend(ly[role][2])
    for (at, why) in rep.get('layer_refused', []):
        print('    draw at +%#06x reads as two layers but is refused: %s' % (at, why))
    if nlayered:
        print('    %d draw%s blend two images in the program: carried as a base '
              'surface and a coplanar blended layer, %d vertices re-read'
              % (nlayered, '' if nlayered == 1 else 's',
                 len(vertices) - sum(len(p['vertices']) for i, p in enumerate(pools)
                                     if i in base)))

    # ---- INSTANCED draws (#80): one mesh drawn under many matrices --------
    # A draw standing under an instance matrix (xblaworld.OP_INSTANCE) reads
    # its pool through that placement, so each (pool, matrix) gets its own
    # copy of the pool with every position carried through the matrix
    # (instance_vertices), appended once; the draw's indices then address the
    # copy. The bundle is static geometry, so baking the placement is exact.
    # A draw that also blends two layers, animates or shades through a
    # tangent frame is not baked - none has been measured - and is reported.
    xbase, ninst, nplaced = {}, 0, 0
    for dr in draws:
        xf = dr.get('xform')
        if xf is None or dr['pool'] not in base:
            continue
        p = pools[dr['pool']]
        if dr.get('layer') is not None or p.get('aux') is not None \
                or p.get('frame') is not None:
            print('    draw at +%#06x stands under an instance matrix but carries '
                  'a layer, animation or tangent frame - drawn in place' % dr['at'])
            dr['xform'] = None
            continue
        key = (dr['pool'], xf)
        if key not in xbase:
            xbase[key] = len(vertices)
            vertices.extend(instance_vertices(p['vertices'], xf))
            nplaced += 1
        ninst += 1
    if ninst:
        print('    %d draw%s stand under an instance matrix: %d placement%s of '
              '%d pool%s baked, %d vertices'
              % (ninst, '' if ninst == 1 else 's', nplaced, '' if nplaced == 1 else 's',
                 len(set(k[0] for k in xbase)), '' if len(set(k[0] for k in xbase)) == 1 else 's',
                 sum(len(pools[k[0]]['vertices']) for k in xbase)))

    # ---- MATERIAL ANIMATION (#69): what the item's own programs move ------
    # The record is the vertex program's, read from the item beside the draw
    # (xblaworld.wave_record); the per-vertex fields are the pool's. Printed
    # in full because these are the numbers the runtime will evaluate, and a
    # census that hides them hides the only check a reader has.
    aux, anims, arep = material_animation(pools, draws, base, len(vertices))
    if rep.get('wave_programs'):
        print('    wave programs: %d, literal bank %s'
              % (rep['wave_programs'], rep.get('wave_literal')))
    for (at, why) in rep.get('wave_refused', []):
        print('    draw at +%#06x carries the animated layout but no record: %s'
              % (at, why))
    if rep.get('bad_aux'):
        print('    %d pool(s) with the animated layout refused their animation '
              'fields' % rep['bad_aux'])
    if anims:
        print('    material animation: %d draw%s move %d vertices through %d '
              'record%s' % (arep['draws'], '' if arep['draws'] == 1 else 's',
                            arep['vertices'], len(anims),
                            '' if len(anims) == 1 else 's'))
        for k, r in enumerate(anims):
            live = [i for i in range(len(r['amplitude'])) if r['amplitude'][i] != 0.0]
            print('      record %d: %d live wave%s, coordinate scale 1/%g'
                  % (k + 1, len(live), '' if len(live) == 1 else 's',
                     1.0 / r['coord_scale'] if r['coord_scale'] else 0.0))
            for i in live:
                per = (1.0 / r['speed'][i]) if r['speed'][i] else float('inf')
                print('        wave %d: amplitude %.4f, speed %.4f cycles/s '
                      '(period %.2f s), frequency (%.4f, %.4f) cycles per unit '
                      'pair, phase %.4f'
                      % (i, r['amplitude'][i], r['speed'][i], per,
                         r['freq_a'][i] * r['coord_scale'],
                         r['freq_b'][i] * r['coord_scale'], r['phase'][i]))
    elif arep['conflicts'] or arep['nofields']:
        print('    material animation REFUSED: %d vertex conflict(s), %d draw(s) '
              'without fields - the bundle stands still'
              % (arep['conflicts'], arep['nofields']))

    # ---- THE MATERIAL EXTENSION (#69 material): the source's own pixel ----
    # program for a draw that names four sampler images, carried as data
    # (xblaworld.material_record) beside the per-vertex tangent frame the
    # program shades with. Printed in full for the same reason the animation
    # is: these are the numbers the runtime evaluates.
    frames, nframes = material_frames(pools, base, len(vertices))
    nmatdraw = sum(1 for dr in draws if dr.get('material') is not None)
    if rep.get('pixel_programs'):
        print('    pixel programs: %d walked from their control flow' % rep['pixel_programs'])
    for (at, why) in rep.get('material_refused', []):
        print('    draw at +%#06x names four sampler slots but carries no material '
              'record: %s' % (at, why))
    if rep.get('bad_frame'):
        print('    %d pool(s) with the animated layout refused their tangent frame'
              % rep['bad_frame'])
    if nmatdraw:
        recs = {}
        for dr in draws:
            if dr.get('material') is not None:
                recs.setdefault(repr(sorted(dr['material'].items())), dr['material'])
        print('    material extension: %d draw%s shade through %d record%s, %d '
              'vertices carry a tangent frame'
              % (nmatdraw, '' if nmatdraw == 1 else 's', len(recs),
                 '' if len(recs) == 1 else 's', nframes))
        for k, r in enumerate(recs.values()):
            print('      record %d: base %.4f x colour x base(uv + %.4f x offset) + '
                  '%.4f x sphere(N); normal = map x %g %+.4f; sphere uv = '
                  '(%g N.x %+g, %g N.y %+g); alpha = max(mean, %g) x base.a%s'
                  % (k + 1, r['base_weight'][0], r['warp'], r['reflect'][0],
                     r['normal_scale'], r['normal_bias'],
                     r['sphere_scale'][0], r['sphere_bias'][0],
                     r['sphere_scale'][1], r['sphere_bias'][1], r['alpha_floor'],
                     '; the sphere image clamps' if r.get('sphere_clamp') else ''))
    else:
        frames = None

    # ---- WHICH of a material's images is the BASE ------------------------
    #
    # A material names one image per sampler slot, and slot 0 is the base for
    # all but a handful of this item's materials. The handful matter: one pool
    # of 7081 triangles - every tree in the level - names a TANGENT-SPACE
    # NORMAL MAP in slot 0, and drawn as a base that is a flat violet, which is
    # exactly what the first render of this item showed standing beside every
    # tree.
    #
    # So the base is the lowest slot whose image is not one, and "is one" is
    # MEASURED on the decoded pixels rather than assumed from the slot number:
    # a tangent-space map is flat blue - its mean red and green sit at the
    # middle of the range and its mean blue near the top. Image 66 of this item
    # reads (125, 125, 254). No base texture in the item comes close.
    def normalmapish(t):
        a = np.asarray(t['rgba'])[:, :, :3].reshape(-1, 3).mean(axis=0)
        return abs(a[0] - 128.0) < 30.0 and abs(a[1] - 128.0) < 30.0 and a[2] > 170.0

    # A SLOT THE PROGRAM SAMPLES AT A COORDINATE IT COMPUTED is not the base
    # either (#82, xblaworld.computed_fetches): on Silo's walkway rails slot 1
    # is a landscape the program looks up by the shading normal - a
    # reflection over the lit base in slot 2 - and as the base it painted the
    # rails with trees and sky. A draw with a material record (its base is
    # named) and a draw where nothing else is left keep the rule above.
    ism = {}
    swapped = 0
    alpha_based = 0
    reflected = 0
    for dr in draws:
        slots = dr.get('slots') or {}
        chosen = dr['image']
        if dr.get('layer') is not None:
            dr['image'] = dr['layer']['base'][0]
            continue
        tb = translucent_base(slots, dr.get('style'), textures)
        if tb is not None:
            if tb != chosen:
                alpha_based += 1
            dr['image'] = tb
            continue
        if len(slots) > 1:
            for s in sorted(slots):
                i = slots[s]
                if not (0 <= i < len(textures)):
                    continue
                if i not in ism:
                    ism[i] = normalmapish(textures[i])
                if not ism[i]:
                    if i != chosen:
                        swapped += 1
                    chosen = i
                    refl = dr.get('reflect') or set()
                    if dr.get('material') is None and s in refl:
                        rest = [s2 for s2 in sorted(slots) if s2 > s and s2 not in refl
                                and 0 <= slots[s2] < len(textures)
                                and not ism.setdefault(slots[s2], normalmapish(textures[slots[s2]]))]
                        if rest:
                            chosen = slots[rest[0]]
                            reflected += 1
                    break
        dr['image'] = chosen
    if swapped:
        print('    %d draws took a later sampler slot as their base image, '
              'because slot 0 reads as a tangent-space normal map' % swapped)
    if reflected:
        print('    %d draw%s passed over a slot the program samples at a coordinate '
              'it computes (a reflection), for the next one'
              % (reflected, '' if reflected == 1 else 's'))
    if alpha_based:
        print('    %d translucent draws took as their base the one sampler slot '
              'whose image carries alpha' % alpha_based)

    # ---- #92 A TWO-IMAGE COVERAGE DRAW (xblaworld.two_image_coverage) -----
    # its colour slot's rgb under its coverage slot's channel, one image; a
    # pair whose sizes differ is drawn as before. D-047 takes the coverage
    # inverted where coverage_inverted says it was authored the other way.
    composite = {}
    for dr in draws:
        c2 = dr.get('cover2')
        if c2 is None:
            continue
        ks, chn, cs = c2
        ki, ci = dr['slots'][ks], dr['slots'][cs]
        if not (0 <= ki < len(textures) and 0 <= ci < len(textures)):
            continue
        if textures[ki]['rgba'].shape[:2] != textures[ci]['rgba'].shape[:2]:
            print('    two-image draw at %#x: its coverage image (slot %d) and colour image '
                  '(slot %d) differ in size - drawn as before' % (dr['at'], ks, cs))
            continue
        inv = divergence_applies('D-047', item) and coverage_inverted(
            textures[ci]['rgba'], textures[ki]['rgba'][:, :, 'xyzw'.index(chn)])
        key = ('coverage', ci, ki, chn, inv)
        if key not in composite:
            composite[key] = coverage_composite(textures[ci], textures[ki], chn, inv)
        dr['image'] = key
        print('    two-image draw at %#x (%d triangles): colour slot %d (image %d), coverage '
              'slot %d channel %s (image %d)%s' % (dr['at'], dr['tris'], cs, ci, ks, chn, ki,
                                                   ' - inverted (D-047)' if inv else ''))

    # ---- D-051 THE SOURCE'S PLACEHOLDER IMAGE is withheld (placeholder_checker):
    # a draw whose base image is the magenta checker is left out of the bundle.
    if divergence_applies('D-051', item):
        (pn, ptris), (cn, ctris), pims = withhold_placeholders(draws, textures)
        print('    D-051: %d placeholder image%s %s: %d opaque draw%s (%d triangles) drawn by '
              'their vertex colour, %d shaped draw%s (%d triangles) withheld'
              % (len(pims), '' if len(pims) == 1 else 's',
                 '(%s)' % ', '.join(str(textures[i].get('name', i)) for i in pims) if pims else '',
                 cn, '' if cn == 1 else 's', ctris, pn, '' if pn == 1 else 's', ptris))

    # ---- D-053 THE SHUTTLE'S DECAL STAND-INS are withheld (decal_stand_ins):
    # the shuttle item draws the game's own decals (ORIGINAL_GENERATION); the
    # background's image-less copies of them would lie over them.
    if divergence_applies('D-053', item):
        sn, stris = decal_stand_ins(draws, pools)
        print('    D-053: %d image-less decal stand-in triangle%s in %d draw%s withheld'
              % (stris, '' if stris == 1 else 's', sn, '' if sn == 1 else 's'))

    # ---- the images the draws actually name, and nothing else ------------
    # A draw with a material record names three more - its normal, sphere
    # and offset images - and they are carried the same way, once each.
    used = []
    remap = {}
    for dr in draws:
        if dr['image'] is None or isinstance(dr['image'], tuple) \
                or not (0 <= dr['image'] < len(textures)):
            continue
        if dr['image'] not in remap:
            remap[dr['image']] = len(used)
            used.append(textures[dr['image']])
        if dr.get('layer') is not None:
            li = dr['layer']['layer'][0]
            if 0 <= li < len(textures) and li not in remap:
                remap[li] = len(used)
                used.append(textures[li])
        mr = dr.get('material')
        if mr is not None:
            for k in ('normal', 'sphere', 'offset'):
                if mr[k] not in remap and 0 <= mr[k] < len(textures):
                    remap[mr[k]] = len(used)
                    used.append(textures[mr[k]])
    for key, rec in composite.items():
        remap[key] = len(used)
        used.append(rec)
    if not used:
        print('prepare-xbla-assets: no draw in this item names an image, so no')
        print('  material can be built. Refused rather than guessed at.')
        return 1
    # #81 AN UNTEXTURED DRAW - one naming no image, on a pool that was read -
    # draws by its vertex colour alone: it takes one white image, carried
    # once, as the material's image (UNTEXTURED). A draw on a pool that was
    # not read stays dropped below.
    #
    # #90 ... and by the MATERIAL COLOUR its program multiplies the vertex
    # colour by (xblaworld.colour_times_c13_program: the draw's own c13
    # upload, where the item carries that program): one opaque texel of that
    # colour instead of white, carried once per colour. A draw with no such
    # colour, or a white one, keeps the white image, so an item without the
    # program is prepared exactly as before.
    untex = [dr for dr in draws if dr['image'] is None and dr['pool'] in base]
    for dr in untex:
        dr['untextured'] = True
    for dr in untex:
        col = untextured_texel(dr.get('untex'))
        if col is None:
            continue
        key = ('untextured',) + col
        if key not in remap:
            remap[key] = len(used)
            texel = np.array(col + (255,), dtype=np.uint8).reshape(1, 1, 4)
            used.append(dict(UNTEXTURED, name='untextured-%02x%02x%02x' % col, rgba=texel))
        dr['image'] = key
        print('    untextured draw at %#x (%d triangles) - no image named; its program '
              'multiplies the vertex colour by its material colour %02x%02x%02x'
              % ((dr['at'], dr['tris']) + col))
    untex = [dr for dr in untex if dr['image'] is None]
    if untex:
        remap[None] = len(used)
        used.append(dict(UNTEXTURED, rgba=np.full((1, 1, 4), 255, dtype=np.uint8)))
        print('    %d untextured draw%s (%d triangles) - no image named; drawn by '
              'the vertex colour over one white image'
              % (len(untex), '' if len(untex) == 1 else 's', sum(d['tris'] for d in untex)))

    # ---- the stage-fog contract: which programs consume it (#68) ----------
    # Every material is marked only when every pixel program in the item
    # declares the stage-fog pair - see xblaworld.fog_programs for why the
    # answer is per item and conservative.
    nps, nfog = got.get('fog', (0, 0))
    fogbit = slwa.MAT_FOG if (nps > 0 and nfog == nps) else 0
    print('    stage fog: %d of %d pixel programs declare the stage-fog '
          'constants -> %s'
          % (nfog, nps, 'every material consumes it' if fogbit
             else 'NO material is marked (not unanimous)'))

    # ---- materials, deduplicated, and meshes, merged while adjacent ------
    mats = []
    matkey = {}
    indices = []
    meshes = []
    kept = dropped = 0
    blue_cov, one_alpha = [], []          # #81 the coverage outcomes, reported
    culled = []                           # #96 triangles per draw under the face cull
    gated = []                            # #96 untextured draws blended by their pass gate
    layermeshes = set()
    mpass = []                            # #95 per mesh: its draws' pass gates
    for dr in draws:
        if dr['image'] not in remap:
            dropped += 1
            continue
        if dr['pool'] not in base:
            dropped += 1
            continue
        ti = remap[dr['image']]
        style = dr['style'] if dr['style'] is not None else 0
        # THREE outcomes, and which one is READ from the source's style byte
        # rather than derived from whether the image has an alpha channel.
        # The earlier revision had two, deriving "blends" as "the style says so
        # OR the image carries alpha", and called being generous free: "it
        # costs sorting, never a black box where a decal should be". It is not
        # free, and the cost is measured: a blended mesh draws with DEPTH
        # WRITES OFF, so every draw that reached ALPHA only through its image
        # stopped occluding anything. On the Dam that is 39050 triangles - the
        # level's foliage and its detail shell, 43% of the item - which the
        # world behind then draws straight through.
        #
        # The style byte separates them and is the source's own answer:
        #   low byte 2  -> genuinely translucent. 22 draws here.
        #   otherwise   -> opaque, and an alpha channel on such a draw is a
        #                  SHAPE (foliage, fences, gratings), not translucency.
        #
        # #96 AN UNTEXTURED DRAW IN THE TRANSLUCENT PASS ALONE blends too. Its
        # style word can name the opaque pass while the innermost pass gate
        # over it (xblaworld.draw_passes) admits only the translucent one:
        # Aztec's overlay at the shuttle (pool 12, 38 triangles under white,
        # orange and brown vertex colours, style 0x4701 under gate 0x2702,
        # every vertex at alpha 128) drew as an opaque white slab across the
        # orbiter's wing, where the remaster shows the wing tinted tan through
        # it and no slab (private emulator, rooms 65 and 67). Over the 19
        # prepared stages no other untextured draw stands under such a gate;
        # the textured draws that do (Caverns, the Dam, Runway, Surface,
        # Train) keep the style word's answer.
        blend = background_blend(style, dr.get('shadow'), dr.get('untextured'),
                                 dr.get('pass_mask'), used[ti]['has_alpha'])
        if blend == slwa.BLEND_ALPHA and (style & 0xff) != 2 and not dr.get('shadow'):
            gated.append(len(dr['indices']) // 3)
        # THE DECAL PASS, read from the same style word and previously thrown
        # away with the rest of it. A draw carrying xblaworld.STYLE_DECAL is a
        # surface authored to lie ON another surface, and two exactly
        # coincident surfaces with no depth priority resolve per pixel by
        # floating-point noise. See xblaworld.STYLE_DECAL for what says this
        # bit is that pass, and src/gfx/sl_gfx_worldasset.h for what the
        # renderer does with it.
        #
        # It is part of the MATERIAL KEY, not just a field written after the
        # fact, and that is load-bearing: adjacent draws of the same material
        # are merged into one mesh, so a key that ignored this would merge a
        # decal draw into the ordinary draw beside it and hand both the same
        # state. Three of the Dam's meshes sit exactly on such a boundary.
        decal = background_decal(style, dr.get('untextured'), dr.get('pass_mask'))
        # #81 THE COVERAGE CHANNEL of a blended draw, read from its own pixel
        # program (xblaworld.draw_coverage, the prop reader's #71 rule): the
        # exported alpha is coverage x colour alpha x a factor the running
        # game holds at one. Two outcomes are read from it here.
        #
        #   'z' (the program multiplies the base image's BLUE into its alpha
        #   and never reads the image's alpha) - black is transparent, and
        #   the material says so (MAT_COVERAGE_BLUE). Bunker 1's hazard strips
        #   are 64x1 black-and-white bar images with alpha 255 everywhere,
        #   tinted yellow by the vertex colour: drawn by their alpha they
        #   painted pure black between the yellow bars, where the remaster
        #   (private emulator) and the original both show the wall.
        #
        #   no factor of the image ('') with an opaque vertex colour on every
        #   vertex the draw uses - or the image's alpha ('w') where that alpha
        #   is 255 everywhere - exports an alpha of exactly one: the draw is
        #   OPAQUE, whatever pass the source sorts it into. Drawn blended it
        #   wrote no depth, and the background is the frame's first world
        #   draw, so every prop drawn after it painted over it: Bunker 1's
        #   blue handrails (3216 triangles, vertex alpha 255, no image factor)
        #   vanished behind each crate and desk beyond them. A decal keeps its
        #   pass - it lies on its surface and the decal rules place it - and
        #   so does a draw whose program has a contract of its own (#69: the
        #   animated water, the four-image material, the two-layer draw), for
        #   which the export above was never measured: the Dam's water is
        #   such a draw (19626 triangles, no image factor).
        cov = dr.get('coverage')
        base_slot = (dr.get('slots') or {}).get(0) == dr['image']
        if blend == slwa.BLEND_ALPHA and cov == 'z' and base_slot:
            decal |= slwa.MAT_COVERAGE_BLUE
            blue_cov.append(len(dr['indices']) // 3)
        elif blend == slwa.BLEND_ALPHA and not decal and dr.get('layer') is None \
                and dr.get('xform') is None and dr.get('wave') is None \
                and dr.get('material') is None and (
                    cov == '' or (cov == 'w' and base_slot
                                  and int(used[ti]['rgba'][:, :, 3].min()) == 255)):
            pv = pools[dr['pool']]['vertices']
            if dr['indices'] and all(pv[i][8] == 255 for i in set(dr['indices'])):
                blend = slwa.BLEND_OPAQUE
                one_alpha.append(len(dr['indices']) // 3)
        # THE MATERIAL RECORD is part of the key too, for the same reason the
        # decal bit is: a draw shading through the source's four-image
        # program must not merge with a base-only draw of the same image.
        mr = dr.get('material')
        mkey = None
        if mr is not None and all(0 <= mr[k] < len(textures) and mr[k] in remap
                                  for k in ('normal', 'sphere', 'offset')):
            mkey = repr(sorted(mr.items()))
        # #96 THE FACE CULL standing over the draw (xblaworld.RS_CULL), part
        # of the key for the decal bit's reason: a culled draw must not merge
        # with a two-sided one beside it.
        cull = slwa.MAT_CULL_BACK if dr.get('cull') else 0
        if cull:
            culled.append(len(dr['indices']) // 3)
        # D-048 the game's moving water (gameshade.game_water), part of the key
        # for the decal bit's reason: a moving draw must not merge with a still
        # one beside it.
        water = slwa.MAT_GAME_WATER if dr.get('game_water') else 0
        key = (ti, blend, decal | fogbit | cull | water, mkey)
        mi = matkey.get(key)
        if mi is None:
            mi = len(mats)
            matkey[key] = mi
            mats.append(key)
        first = len(indices)
        ly = dr.get('layer')
        off = base[dr['pool']] if ly is None else lbase[(dr['pool'], ly['base'][1])]
        if dr.get('xform') is not None:
            off = xbase[(dr['pool'], dr['xform'])]
        indices.extend(off + i for i in dr['indices'])
        n = len(indices) - first
        if n == 0 or n % 3:
            del indices[first:]
            dropped += 1
            continue
        kept += 1
        # Adjacent draws of the same material are ONE mesh: the index array is
        # shared and their order is preserved, so merging is exact and it is
        # what keeps the mesh table (and the draw calls) small.
        if meshes and meshes[-1][0] == mi and meshes[-1][1] + meshes[-1][2] == first \
                and len(meshes) - 1 not in layermeshes:
            meshes[-1] = (mi, meshes[-1][1], meshes[-1][2] + n)
            mpass[-1].add(dr.get('pass_mask') or 0)
        else:
            meshes.append((mi, first, n))
            mpass.append({dr.get('pass_mask') or 0})
        if ly is None:
            continue
        # THE LAYER, over the base just emitted: the same triangles read with
        # the layer's own coordinate and colour, blended by source alpha - the
        # program's weight - and flagged as the decal pass, because it lies
        # exactly on the base and is drawn after it (xblaworld.LAYER_INPUTS).
        lkey = (remap[ly['layer'][0]], slwa.BLEND_ALPHA, slwa.MAT_DECAL | fogbit | cull, None)
        lmi = matkey.get(lkey)
        if lmi is None:
            lmi = len(mats)
            matkey[lkey] = lmi
            mats.append(lkey)
        first = len(indices)
        loff = lbase[(dr['pool'], ly['layer'][1])]
        indices.extend(loff + i for i in dr['indices'])
        layermeshes.add(len(meshes))
        meshes.append((lmi, first, len(indices) - first))
        mpass.append({dr.get('pass_mask') or 0})
    print('    %d draws carried, %d dropped; %d materials, %d meshes after '
          'merging adjacent same-material draws' % (kept, dropped, len(mats), len(meshes)))
    if gated:
        print('    %d untextured draw%s (%d triangles) stand in the translucent pass alone '
              'and are blended at their vertex alpha' % (len(gated), '' if len(gated) == 1 else 's',
                                                         sum(gated)))
    if culled:
        print('    face cull: %d draws (%d triangles) stand under the source\'s cull and draw '
              'from their front alone (MAT_CULL_BACK)' % (len(culled), sum(culled)))
    if not meshes:
        print('prepare-xbla-assets: nothing survives. Refused.')
        return 1

    tex_recs = [(t['w'], t['h'], np.ascontiguousarray(t['rgba']).tobytes())
                for t in used]
    # THE MATERIAL TABLE the writer takes (image, blend, flags), and beside it
    # the extension record per material - the draw's record with its image
    # indices remapped into the bundle's own table, or None. The extension is
    # written only when some material carries one (slwa.write's version rule).
    matrecs = {}
    for dr in draws:
        mr = dr.get('material')
        if mr is not None:
            matrecs.setdefault(repr(sorted(mr.items())), mr)
    matx = []
    for (_ti, _b, _f, mkey) in mats:
        r = matrecs.get(mkey) if mkey is not None else None
        if r is None:
            matx.append(None)
        else:
            matx.append(dict(r, normal=remap[r['normal']], sphere=remap[r['sphere']],
                             offset=remap[r['offset']], base=remap[r['base']]))
    mats = [(ti, b_, f) for (ti, b_, f, _k) in mats]
    # #78 THE DECAL PASS THE STYLE WORD DOES NOT MARK: a shaped draw laid
    # wholly on an earlier surface (overlay_meshes). Its mesh takes the decal
    # bit; a material it shares with a mesh that is not one is split, with the
    # same image record, so nothing else moves.
    over = overlay_meshes(vertices, indices, meshes, mats, skip=layermeshes)
    # #95 WHICH OVERLAYS THE SOURCE DRAWS. An overlay is promoted when the
    # source puts it in its decal pass after all - in the PASS GATE standing
    # over the draw rather than its style word: Runway's dock numbers (style
    # 0x4701 under a 0x2702 gate), Silo's and Surface's overlays. One in no
    # decal pass sits in the source's cutout gate (0x4b01), coplanar with an
    # earlier surface and with no depth priority, and the remaster's own
    # frame does not show it: Control's mesh 81 - six quads of a 32x32 grey
    # cloud image under a dark vertex colour, on the WARNING sign of room 13
    # and on two floor patches (one of them under the second mark, room 14) -
    # is absent from the remaster's frame
    # at both playtest marks (private emulator, debug camera, ctl2), where
    # the decal pass drew it as opaque black blotches. Such an overlay is
    # WITHHELD when the source draws its image nowhere else (overlay_alone).
    # Where the image is drawn elsewhere too, the overlay is one layer of a
    # construction the accepted stages carry as measured, and keeps the
    # decal pass as before: Archives' blood pool (meshes 60 and 61, #89 -
    # the remaster's frame shows the pool the decal-pass draws lay), Depot's
    # mesh 64 beside its image's decal draws, and Surface's two-layer base
    # lying on the opaque draws of its own image. Measured over the fifteen
    # stages' items, the overlays withheld are Control's mesh 81 and Silo's
    # mesh 61 (two triangles of a black-coloured image on the floor of room
    # 23, the foot of a shaft no stand tile reaches, seen from no playtest
    # mark).
    withheld = overlays_withheld(over, mpass, overlay_alone(meshes, mats, over))
    for k in sorted(over):
        if k in withheld:
            continue
        mi, first, cnt = meshes[k]
        ti, b_, f = mats[mi]
        shared = any(m2 == mi and k2 != k and k2 not in over
                     for k2, (m2, _f2, _c2) in enumerate(meshes))
        if shared:
            mats.append((ti, b_, f | slwa.MAT_DECAL))
            matx.append(matx[mi])
            meshes[k] = (len(mats) - 1, first, cnt)
        else:
            mats[mi] = (ti, b_, f | slwa.MAT_DECAL)
    promoted = sorted(set(over) - withheld)
    if promoted:
        print('    overlays: mesh%s %s - a shaped draw lying wholly on an earlier '
              'surface, given the decal pass (%d triangle%s)'
              % ('' if len(promoted) == 1 else 'es', ', '.join(str(k) for k in promoted),
                 sum(meshes[k][2] // 3 for k in promoted),
                 '' if sum(meshes[k][2] // 3 for k in promoted) == 1 else 's'))
    elif not over:
        print('    overlays: none - no shaped draw lies wholly on an earlier surface')
    if withheld:
        print('    overlays withheld: mesh%s %s - in no decal pass of the source and '
              'the only draws of their image (%d triangle%s)'
              % ('' if len(withheld) == 1 else 'es', ', '.join(str(k) for k in sorted(withheld)),
                 sum(meshes[k][2] // 3 for k in withheld),
                 '' if sum(meshes[k][2] // 3 for k in withheld) == 1 else 's'))
        meshes, indices, renum = drop_meshes(meshes, indices, withheld)
        layermeshes = {renum[k] for k in layermeshes}
        over = {renum[k] for k in over if k in renum}
    # D-026 THE PANORAMA SHELL, measured on the geometry just assembled; its
    # materials carry the bit and nothing else about the item changes.
    shell, rep = panorama_shell(vertices, indices, meshes, len(mats))
    if shell is None:
        print('    panorama shell: none - %s' % rep)
    elif not divergence_applies('D-026', item):
        print('    panorama shell: meshes %s found, not marked - D-026 is not '
              'decided for background/%s; the ring draws as authored'
              % (', '.join(str(k) for k in rep['meshes']), item))
    else:
        mats = [(ti, b_, f | slwa.MAT_PANORAMA) if k in shell else (ti, b_, f)
                for k, (ti, b_, f) in enumerate(mats)]
        print('    panorama shell: meshes %s (%d material%s) - every vertex %.0f..%.0f '
              'out, the rest of the item within %.0f; largest gap round it %.1f deg'
              % (', '.join(str(k) for k in rep['meshes']), len(shell),
                 '' if len(shell) == 1 else 's', rep['near'], rep['far'], rep['inner'],
                 rep['gap']))
    nblend = sum(1 for _t, b_, _f in mats if b_ == slwa.BLEND_ALPHA)
    ncut = sum(1 for _t, b_, _f in mats if b_ == slwa.BLEND_CUTOUT)
    nmatx = sum(1 for r in matx if r is not None)
    if nmatx:
        print('    %d material%s carr%s the source\'s four-image pixel program (the '
              'bundle is written at version %d)'
              % (nmatx, '' if nmatx == 1 else 's', 'ies' if nmatx == 1 else 'y',
                 slwa.VERSION))
    print('    images carried %d of %d (%.1f MB of RGBA); %d materials are '
          'translucent (no depth write), %d are alpha CUTOUTS (depth written)'
          % (len(used), len(textures),
             sum(len(r[2]) for r in tex_recs) / 1048576.0, nblend, ncut))
    print('    coverage: %d of %d draws placed; %d blended draw%s (%d triangles) take '
          'their coverage from the image\'s blue, %d (%d triangles) export an alpha of '
          'one and are drawn opaque'
          % (sum(1 for dr in draws if dr.get('coverage') is not None), len(draws),
             len(blue_cov), '' if len(blue_cov) == 1 else 's', sum(blue_cov),
             len(one_alpha), sum(one_alpha)))
    # The decal census is printed rather than asserted. A decal pass is by
    # nature a small minority of a level - it is detail laid on the world, not
    # the world - so a split that came out the other way would say the style
    # bit means something else on this item, and the number is what would say
    # so rather than a picture.
    ndecal = len({mi2 for k, (mi2, _f2, _c) in enumerate(meshes)
                  if mats[mi2][2] & slwa.MAT_DECAL and k not in layermeshes and k not in over})
    dtris = sum(c // 3 for k, (mi2, _f2, c) in enumerate(meshes)
                if mats[mi2][2] & slwa.MAT_DECAL and k not in layermeshes and k not in over)
    print('    the source\'s DECAL PASS: %d of %d materials, %d of %d triangles '
          '(style bit %#06x)'
          % (ndecal, len(mats), dtris, len(indices) // 3, xblaworld.STYLE_DECAL))

    # ---- CROSS-CHECK: the geometry read against the bundle's own bounds ---
    lo = [min(v[k] for v in vertices) for k in range(3)]
    hi = [max(v[k] for v in vertices) for k in range(3)]
    d0 = got['declared']
    centre = [(lo[k] + hi[k]) / 2.0 for k in range(3)]
    extent = [(hi[k] - lo[k]) / 2.0 for k in range(3)]
    worst = max(abs(centre[k] - d0['centre'][k]) for k in range(3))
    worste = max(abs(extent[k] - d0['extent'][k]) for k in range(3))
    scale = max(1.0, max(abs(v) for v in d0['extent']))
    print('    bounds: computed centre (%.1f %.1f %.1f) extent (%.1f %.1f %.1f)'
          % (centre[0], centre[1], centre[2], extent[0], extent[1], extent[2]))
    print('            declared centre (%.1f %.1f %.1f) extent (%.1f %.1f %.1f)'
          % (d0['centre'] + d0['extent']))
    if worst / scale > 0.01 or worste / scale > 0.01:
        print('prepare-xbla-assets: the geometry read disagrees with the bounds')
        print('  the bundle declares for itself. Refused rather than shipped.')
        return 1
    print('            they agree (worst %.3f of %.0f) - the runs read are the '
          'runs the bundle describes' % (max(worst, worste), scale))

    path = os.path.join(out, 'xbla', cls, '%s.slwa' % item)
    try:
        n = slwa.write(path, CLASSES[cls], item, vertices, indices, mats,
                       tex_recs, meshes, aux=aux, anims=anims,
                       frames=frames if nmatx else None,
                       matx=matx if nmatx else None)
    except slwa.SlwaError as exc:
        print('prepare-xbla-assets: %s' % exc)
        return 1
    print('  wrote %s' % path)
    print('  %d vertices, %d indices (%d triangles), %d materials, %d meshes, '
          '%d images, %d animation record%s, %d material record%s, %.1f MB'
          % (len(vertices), len(indices), len(indices) // 3, len(mats),
             len(meshes), len(tex_recs), len(anims),
             '' if len(anims) == 1 else 's', nmatx, '' if nmatx == 1 else 's',
             n / (1024.0 * 1024.0)))
    return 0


# ---- #70 CHARACTER AND HEAD ITEMS ------------------------------------------
#
# A character or head item replaces the game's OWN model of that name, drawn
# deformed by the game's own skeleton (src/gfx/sl_gfx_charskin.h). The item is
# the REMASTERED generation's: the source carries both, and the original
# generation is the game's model again, so preparing it would only replace the
# game with a copy of itself. The item path is the model's own name in both
# vocabularies - the game's Cgreatguard2Z is the source's char/greatguard2,
# CheadleeZ is head/headlee - so no pairing table exists or is needed. Which
# items a stage needs is stagechars.py's business, not this module's.

#: The generation directory of the remastered items.
REMASTERED = 'new'

#: The source's own generation of the GAME'S models, beside the remastered
#: one: the same item, the game's geometry, in the item's space.
ORIGINAL = 'original'

#: The game's muzzle-flash switch (Switches[1] of every gun model).
FLASH_SWITCH = 'SW_FLASH'


class CharacterRefused(Exception):
    """This source's item cannot be prepared; the message says why."""


def convert_character(cls, item, tree, out):
    """Convert the REMASTERED `cls/item` found under `tree` into
    <out>/xbla/<cls>/<item>.slwa. Returns a one-line summary; raises
    CharacterRefused, with the reason, for anything it will not convert."""
    need('numpy', 'texture decoding')
    import xblachar                                          # noqa: E402

    d = find_item(tree, '%s/%s/%s' % (REMASTERED, cls, item))
    if d is None or not os.path.isdir(d):
        raise CharacterRefused('this source has no remastered %s/%s' % (cls, item))
    files = sorted(n for n in os.listdir(d) if n.lower().endswith('.bin'))
    if not files:
        raise CharacterRefused('%s/%s holds no bundle file' % (cls, item))
    try:
        got = xblachar.read_character(os.path.join(d, files[0]))
    except xblachar.CharacterError as exc:
        raise CharacterRefused(str(exc))

    vertices, skin, draws, textures = got['vertices'], got['skin'], got['draws'], got['textures']
    # One material per image, in image order; the atlases are opaque.
    mats = [(i, slwa.BLEND_OPAQUE) for i in range(len(textures))]
    indices, meshes = [], []
    # A GATED block is the item's own head: on a body it is the built-in
    # head, drawn only when no separate head is worn; on a head item it is
    # that head, the thing being worn, and is always drawn.
    builtin = [dr['builtin_head'] and cls == 'char' for dr in draws]
    # A BODY that carries no condition at all - no gated block, no head
    # attach point (the remaster's CP_HEADCHILD) - has no place to wear a
    # separate head: the source draws the head it carries, always. Measured:
    # every Dam guard body gates its built-in head on CP_HEADCHILD, Bond's
    # bodies carry no condition table and draw their heads ungated.
    own_head = cls == 'char' and not any(builtin) and not got.get('conditions')
    for dr, bh in zip(draws, builtin):
        meshes.append((dr['image'], len(indices), len(dr['indices']),
                       slwa.MESH_BUILTIN_HEAD if bh else
                       (slwa.MESH_OWN_HEAD if own_head else 0)))
        indices.extend(dr['indices'])
    joints = [(j['name'], j['parent'], j['bind']) for j in got['joints']]
    path = os.path.join(out, 'xbla', cls, item + '.slwa')
    try:
        n = slwa.write_skinned(path, CLASSES[cls], item, vertices, indices, mats,
                               [(t['w'], t['h'], t['rgba']) for t in textures],
                               meshes, skin, joints)
    except slwa.SlwaError as exc:
        raise CharacterRefused(str(exc))
    uv = got.get('texcoord_scale', 1.0)
    return ('%d joints, %d draws (%s), %d images, %d vertices, '
            '%d triangles, %.1f MB%s'
            % (len(joints), len(draws),
               'its own head, no place for another' if own_head
               else '%d the built-in head' % sum(builtin),
               len(textures), len(vertices), len(indices) // 3, n / (1024.0 * 1024.0),
               '' if uv == 1.0 else ', texture coordinates at 1/%d'
               % int(round(1.0 / (xblachar.TEXCOORD_SCALE * uv)))))


def convert_viewmodel(item, source_item, tree, out, switch_names, source_cls='gun'):
    """Convert the REMASTERED gun/`source_item` found under `tree` into the
    FIRST-PERSON view model <out>/xbla/gun/<item>.slwa (#70), `item` being
    the game's own model name (GwppksilZ -> wppksil; viewmodels.py derives
    the pair). `switch_names` is the game's weapon switch vocabulary
    (viewmodels.gun_switch_names).

    WHAT IS CARRIED. The item is one unit, the gun and the hand that holds it,
    rigidly bound to the game's own view-model skeleton (SKEL_TOP and the
    parts the game positions: FINGER, SLIDE, ...). Each joint records the game
    part it is - 1 + its index in the vocabulary, 0 for the root and for the
    leaf markers no vertex is bound to. Each draw records the game switches it
    stands under, read from the item's own condition table: a condition whose
    leaf is a game switch is that switch; one the vocabulary does not index
    (SW_finger, SW_topgrip) is a switch the game never turns off, and gates
    nothing. The OPAQUE pass is carried as it is; the TRANSLUCENT pass is
    carried blended (a clear magazine, a watch glass, a sight lens) EXCEPT a
    draw standing under SW_FLASH: that is the muzzle flash, drawn on the same
    images as the game's own, which the game's flash node goes on drawing.
    Returns a one-line summary; raises CharacterRefused, with the reason, for
    anything it will not convert."""
    need('numpy', 'texture decoding')
    import xblachar                                          # noqa: E402
    import xblaworld                                         # noqa: E402

    if source_cls != 'gun':
        raise CharacterRefused('%s/%s is not a weapon item; the watch hand is prepared by '
                               'its own relation' % (source_cls, source_item))
    d = find_item(tree, '%s/gun/%s' % (REMASTERED, source_item))
    if d is None or not os.path.isdir(d):
        raise CharacterRefused('this source has no remastered gun/%s' % source_item)
    files = sorted(n for n in os.listdir(d) if n.lower().endswith('.bin'))
    if not files:
        raise CharacterRefused('gun/%s holds no bundle file' % source_item)
    # A STATIC weapon item (no skeleton - the Skorpion's): one rigid piece,
    # related to the game's view model as a prop is, by the box of the
    # source's original generation of the same item (convert_prop).
    ref = None
    try:
        if xblachar.is_static(os.path.join(d, files[0])):
            got = xblachar.read_static(os.path.join(d, files[0]))
            od = find_item(tree, '%s/gun/%s' % (ORIGINAL, source_item))
            ofiles = sorted(n for n in os.listdir(od) if n.lower().endswith('.bin')) \
                if od is not None and os.path.isdir(od) else []
            if not ofiles:
                raise CharacterRefused('gun/%s has no skeleton and the source no original '
                                       'generation to place it by' % source_item)
            ref = xblachar.static_bounds(os.path.join(od, ofiles[0]))
        else:
            got = xblachar.read_character(os.path.join(d, files[0]))
    except xblachar.CharacterError as exc:
        raise CharacterRefused(str(exc))

    vocab = {n: i for i, n in enumerate(switch_names)}
    vertices, skin, draws, textures = got['vertices'], got['skin'], got['draws'], got['textures']
    # One opaque material per image, in image order; a translucent draw's
    # image is given a second, blended material after them.
    # A draw that also names a REFLECTION image (a metal weapon's environment
    # image, xblachar.base_image) gets a material of its own that carries it.
    mats = [(i, slwa.BLEND_OPAQUE) for i in range(len(textures))]
    reflections = [None] * len(mats)
    extra = {}
    indices, meshes, gated, flash, clear, shiny = [], [], 0, 0, 0, 0

    def material(image, blend, reflect):
        if blend == slwa.BLEND_OPAQUE and reflect is None:
            return image
        k = (image, blend, reflect)
        if k not in extra:
            extra[k] = len(mats)
            mats.append((image, blend))
            reflections.append(reflect)
        return extra[k]

    for dr in draws:
        pm = dr['pass_mask'] or 0
        if None in dr['conditions']:
            raise CharacterRefused('gun/%s gates a draw on a condition its table does not '
                                   'name' % source_item)
        if pm & xblaworld.PASS_OPAQUE:
            mat = material(dr['image'], slwa.BLEND_OPAQUE, dr.get('reflect'))
            shiny += 1 if dr.get('reflect') is not None else 0
        elif pm & xblaworld.PASS_TRANSLUCENT:
            if FLASH_SWITCH in dr['conditions']:
                flash += 1         # the muzzle flash: the game's own draws
                continue
            mat = material(dr['image'], slwa.BLEND_ALPHA, None)
            clear += 1
        else:
            raise CharacterRefused('gun/%s draws outside its opaque and translucent passes; '
                                   'not carried' % source_item)
        sw = [vocab[c] for c in dr['conditions'] if c in vocab]
        try:
            fl = slwa.mesh_switch_flags(sw)
        except slwa.SlwaError as exc:
            raise CharacterRefused(str(exc))
        gated += 1 if sw else 0
        meshes.append((mat, len(indices), len(dr['indices']), fl))
        indices.extend(dr['indices'])
    joints = []
    for j in got['joints']:
        nm = j['name'][5:] if j['name'].startswith('SKEL_') else None
        part = vocab[nm] + 1 if (nm in vocab and j['parent'] >= 0) else 0
        joints.append((j['name'], j['parent'], j['bind'], part))
    # A joint the game positions no part for (a watch gadget's BEAN_END leaf,
    # bound to its wrist) moves rigidly with the nearest ancestor it DOES
    # position, the root at worst: the vertices are in item space, so the
    # binding moves and nothing else does.
    def carrier(jj):
        while joints[jj][1] >= 0 and joints[jj][3] == 0:
            jj = joints[jj][1]
        return jj
    rebound = sum(1 for e in skin for (jj, _w) in e if carrier(jj) != jj)
    if rebound:
        merged = []
        for e in skin:
            acc = {}
            for jj, w in e:
                c = carrier(jj)
                acc[c] = acc.get(c, 0) + w
            merged.append(tuple(sorted(acc.items())))
        skin = merged
    path = os.path.join(out, 'xbla', 'gun', item + '.slwa')
    try:
        n = slwa.write_skinned(path, slwa.CLASS_GUN, item, vertices, indices, mats,
                               [(t['w'], t['h'], t['rgba']) for t in textures],
                               meshes, skin, joints,
                               reflections if any(r is not None for r in reflections) else None,
                               bounds=ref)
    except slwa.SlwaError as exc:
        raise CharacterRefused(str(exc))
    return ('gun/%s: %d joints (%s), %d draws (%d under game switches%s%s), %d images, '
            '%d vertices, %d triangles, %.1f MB'
            % (source_item, len(joints),
               ' '.join('%s=%d' % (jt[0], jt[3] - 1) for jt in joints if jt[3]),
               len(draws), gated, (', %d blended' % clear if clear else '') +
               (', %d reflective' % shiny if shiny else ''),
               ', %d flash left to the game' % flash if flash else '',
               len(textures), len(vertices), len(indices) // 3,
               n / (1024.0 * 1024.0)))


#: The game's watch-hand model, whose cuffs the item's sleeves stand for.
WATCH_MODEL = 'suit_lf_hand'
WATCH_FILE = 'assets/obseg/chr/Csuit_lf_handZ.bin'


def convert_watchhand(item, source_item, tree, out, rom, log=print):
    """Convert the REMASTERED char/`source_item` (the arm the watch is worn
    on) found under `tree` into the view model <out>/xbla/gun/<item>.slwa
    (#70, THE WATCH HAND in src/sl_chr_asset.h), `item` being the game's own
    model name (Csuit_lf_handZ -> suit_lf_hand).

    WHAT IS CARRIED. The arm, the hand and the watch as one unit, its joints
    that carry vertices naming the model's matrices they ride (the wrist, the
    watch's hands - watchhand.relate, by the decomp's own joint names); and
    the source's ALTERNATIVE sleeves, every one of them, each marked
    slwa.MESH_ALT and gated on the game's cuff switch it serves, when the
    derivation (watchhand.derive, which reads the game's model from the
    user's ROM, `rom`, and the source's original generation of the same item)
    pairs it with one. A sleeve paired with no cuff is carried ungated: the
    runtime draws it only in the source's own composite. The derivation's
    evidence, a line per cuff and per sleeve, goes to `log`. Returns a
    one-line summary; raises CharacterRefused for anything it will not
    convert."""
    need('numpy', 'texture decoding')
    import xblachar                                          # noqa: E402
    import xblaworld                                         # noqa: E402
    import watchhand                                         # noqa: E402

    def read(gen):
        d = find_item(tree, '%s/char/%s' % (gen, source_item))
        files = sorted(n for n in os.listdir(d) if n.lower().endswith('.bin')) \
            if d is not None and os.path.isdir(d) else []
        if not files:
            raise CharacterRefused('this source has no %s char/%s' % (
                'remastered' if gen == REMASTERED else 'original-generation', source_item))
        try:
            return xblachar.read_character(os.path.join(d, files[0]))
        except xblachar.CharacterError as exc:
            raise CharacterRefused(str(exc))

    if not rom or not os.path.isfile(rom):
        raise CharacterRefused('no ROM to read the game\'s watch hand from; its cuffs cannot '
                               'be matched')
    new, original = read(REMASTERED), read(ORIGINAL)
    try:
        nsw, _nmtx, ntex = watchhand.model_header(WATCH_MODEL)
        arm = watchhand.read_game_arm(watchhand.rom_file(rom, WATCH_FILE), nsw, ntex)
        rel, mapping, sleeves, lines = watchhand.derive(
            new, original, arm, watchhand.matrix_names(WATCH_MODEL))
    except watchhand.WatchHandError as exc:
        raise CharacterRefused(str(exc))
    for ln in lines:
        log('  watch hand: %s' % ln)
    serves = {g: s for s, g in mapping.items()}

    vertices, skin, draws, textures = new['vertices'], new['skin'], new['draws'], new['textures']
    mats = [(i, slwa.BLEND_OPAQUE) for i in range(len(textures))]
    blended = {}
    indices, meshes, alts, clear = [], [], 0, 0
    for dr in draws:
        pm = dr['pass_mask'] or 0
        if dr['conditions']:
            raise CharacterRefused('char/%s gates a draw; the watch hand carries none'
                                   % source_item)
        if pm & xblaworld.PASS_OPAQUE:
            mat = dr['image']
        elif pm & xblaworld.PASS_TRANSLUCENT:
            # translucent_base's rule: what a blended draw consumes is an
            # alpha channel, so when exactly one of the images its material
            # names carries one, that image is its base (the watch glass: an
            # opaque image and a black one at about a third alpha)
            named = [i for i in (dr['image'], dr.get('reflect')) if i is not None]
            with_alpha = [i for i in named if min(textures[i]['rgba'][3::4]) < 255]
            img = with_alpha[0] if len(with_alpha) == 1 else dr['image']
            if img not in blended:
                blended[img] = len(mats)
                mats.append((img, slwa.BLEND_ALPHA))
            mat = blended[img]
            clear += 1
        else:
            raise CharacterRefused('char/%s draws outside its opaque and translucent passes'
                                   % source_item)
        fl = 0
        if dr['image'] in sleeves:
            s = serves.get(dr['image'])
            fl = slwa.MESH_ALT | (slwa.mesh_switch_flags([s]) if s is not None else 0)
            alts += 1
        meshes.append((mat, len(indices), len(dr['indices']), fl))
        indices.extend(dr['indices'])
    joints = []
    for j, jt in enumerate(new['joints']):
        part = slwa.part_matrix(rel['pairs'][j]) if j in rel['pairs'] else 0
        joints.append((jt['name'], jt['parent'], jt['bind'], part))
    path = os.path.join(out, 'xbla', 'gun', item + '.slwa')
    try:
        n = slwa.write_skinned(path, slwa.CLASS_GUN, item, vertices, indices, mats,
                               [(t['w'], t['h'], t['rgba']) for t in textures],
                               meshes, skin, joints)
    except slwa.SlwaError as exc:
        raise CharacterRefused(str(exc))
    return ('char/%s: %d joints (%s), %d draws (%d sleeve draws of %d sleeves; %s%s), '
            '%d images, %d vertices, %d triangles, %.1f MB'
            % (source_item, len(joints),
               ' '.join('%s=m%d' % (new['joints'][j]['name'], m) for j, m in sorted(rel['pairs'].items())),
               len(draws), alts, len(sleeves),
               ', '.join('Switches[%d] %s <- sleeve %d' % (s, watchhand.CUFFS[s - watchhand.CUFF_FIRST], g)
                         for s, g in sorted(mapping.items())) or 'no cuff paired',
               ', %d blended' % clear if clear else '',
               len(textures), len(vertices), len(indices) // 3, n / (1024.0 * 1024.0)))


def prop_joints(joints, matrix_names):
    """A moving prop item's joints as the bundle carries them:
    [(name, parent, bind, part)]. `matrix_names` are the game's names for the
    model's matrices, in matrix order (stageprops.matrix_names). A joint that
    bears one of them rides that matrix (slwa.part_matrix); a joint that bears
    none rides its nearest ancestor's - a joint moves with its parent - and a
    root that bears none rides nothing (part 0: the model's own frame).
    CharacterRefused when no joint bears a matrix's name."""
    lower = [n.lower() for n in matrix_names]
    parts = []
    for j, jt in enumerate(joints):
        nm = jt['name'].lower()
        if nm in lower:
            parts.append(slwa.part_matrix(lower.index(nm)))
        elif jt['parent'] >= 0:
            parts.append(parts[jt['parent']])
        else:
            parts.append(0)
    if not any(jt['name'].lower() in lower for jt in joints):
        raise CharacterRefused("no joint of the item bears a name of the model's matrices (%s)"
                               % ', '.join(matrix_names))
    return [(jt['name'], jt['parent'], jt['bind'], pt) for jt, pt in zip(joints, parts)]


# A PANE: a glass prop whose reference box is at most this fraction of its
# largest extent thick along one axis (the Dam's windows: one quad, 0).
PANE_THICKNESS = 0.01


#: THE GAME'S COORDINATES FOR A STATIC ITEM (#96): the static items whose
#: texture scale no rule of xblachar.texcoord_scale decides and the game's own
#: model does (gameshade.game_coords_scale, the moving prop's THE GAME'S
#: COORDINATES below): every textured draw's every coordinate, read at exactly
#: one power of two, lies on the coordinates of one game texture, a different
#: one for each image. MEASURED on Aztec's desk doors: door_azt_desk at 1/2048
#: (images 0 and 1 on the game's textures 294 and 1418), door_azt_desk_top at
#: 1/4096 (images 0 .. 2 on 1031, 294 and 1418); each refused before ('the
#: texture coordinates reach no atlas edge'), so the game's model drew. Named
#: rather than general: of the static items refused the same way on the
#: stages accepted before Aztec, hatchsevx qualifies too (1/8192, images on
#: the game's 619 and 710) and is left as its stage was accepted. Egyptian's
#: cryptdoor4 (one translucent draw, 12 triangles, coordinates 0 .. 0.25 by
#: -0.75 .. 0.75 at the whole image, in both generations) reads at 1/8192,
#: its image on the game's texture 410.
GAME_COORDS_STATIC = ('door_azt_desk', 'door_azt_desk_top', 'cryptdoor4')


def _static_game_coords(path, item, rom, why):
    """(read_static's dict at the game's coordinates' scale, a summary note)
    for a GAME_COORDS_STATIC item, or CharacterRefused naming `why` and what
    the game's model said."""
    import xblachar                                          # noqa: E402
    if not rom:
        raise CharacterRefused("%s (the game's coordinates not read: no ROM)" % why)
    import gameshade                                         # noqa: E402
    raw = xblachar.read_static(path, images=False)
    try:
        cf, cm = gameshade.game_coords_scale(rom, item, raw['vertices'], raw['draws'],
                                             xblachar.TEXCOORD_DOUBLINGS)
    except gameshade.ShadeError as exc:
        raise CharacterRefused("%s (the game's coordinates not read: %s)" % (why, exc))
    if cf is None:
        raise CharacterRefused("%s (and the game's coordinates decide nothing: %s)" % (why, cm))
    got = xblachar.read_static(path, uv_scale=cf)
    return got, (" (the game's coordinates: texture scale 1/%d, images %s on the game's "
                 "textures %s)" % (int(round(32768 / cf)), ' '.join(str(i) for i in sorted(cm)),
                                   ' '.join(str(cm[i]) for i in sorted(cm))))


#: D-054 THE GAME'S OPAQUE DOOR (Egyptian). Five of Egyptian's crypt-door
#: models - cryptdoor1a, 2a, 2b, 3 and 4, twenty doors: the secret walls into
#: room 10 (rooms 42 and 43), the upper leaf of the door between rooms 19 and
#: 20, the doors of room 20's far end and the ten alcove covers round the
#: Golden Gun's room - are, in the game, opaque stone: every triangle of
#: P<name>Z at corner alpha 255 under no blended render mode, the doors'
#: flags without DOORFLAG_WINDOWED, so the game draws them under no opacity
#: of its own (propobj.c). The source's items, in both generations, put every
#: draw in the translucent pass (gate 0x4702) at vertex alphas 0 (2a, 2b),
#: 0 .. 63 (4) and 0 .. 255 (1a, 3), and the remaster draws them so: the
#: doorway reads open, room 10's pillar showing through the secret wall and
#: room 20's through the door to 19 (private emulator, free camera), while
#: the door is shut and blocks.
#:
#: So for the items DIVERGENCE_ITEMS['D-054'] names, an item every draw of
#: which is in the translucent pass alone over a game model with no blended
#: triangle (gameshade.prop_blends) is drawn OPAQUE, at its own vertex
#: colours with alpha 255, and each draw whose game texture the remaster's
#: background owns an image for draws that image (door_face_art): the image
#: the background (DOOR_BG) draws over at least BG_IMAGE_SHARE of the rooms'
#: triangles in that texture (gameshade.bg_texture_images). MEASURED on
#: Egyptian: texture 406, the doors' grey relief face, is drawn by the
#: background's 512x512 papyrus-capital image on all 24 room triangles where
#: the game draws it; 1174, the lower leaves' face, by its 1024x512 relief
#: frieze on 475 of 592; 410 and 404, the doors' frames, by no image on more
#: than 39% - so the faces draw the remaster's art and the frames their own
#: 32x32 stone. The source's own colours, darker than the game's, keep the
#: door in the room's tone. An item none of whose draws takes an image
#: (cryptdoor4, all 410) is not prepared, and the game's door draws.
#: cryptdoor1b, the door's lower leaf, is opaque in the source (two 512x512
#: images) and is taken as it is. Without the ROM the check cannot be made
#: and the item is prepared as authored.
#:
#: A DOOR'S FACE CONTINUES ITS WALL. Where the setup places the door
#: (DOOR_STAGE, gameshade.door_placements) and the wall round it faces the
#: way the door model's +z face does, each triangle of that face whose draw
#: took no image level-wide draws what the wall draws at its height: the
#: background image and the wall's own texture map (u and v as planes over
#: the door's face, gameshade.wall_maps), when every wall triangle at that
#: height draws the one image on one map and the game draws the wall there
#: in the very texture it draws the door's triangle in (gameshade.
#: wall_textures, prop_triangle_textures) - the game's own sign that the
#: face continues the wall rather than being trim of its own. MEASURED on
#: Egyptian: the secret wall into room 10 (cryptdoor3, bound pad 4, one
#: placement) closes a gap in room 10's west wall, flush with it (the wall
#: 11.9 room units from the door's centre plane, its half-thickness). The game
#: draws that face and the wall either side alike at every height: 410 from
#: the floor to 94 world units, 404 to 277, 410 above. The remaster draws the
#: wall's 410 and 404 bands with the papyrus-capital image as ONE upright
#: copy on one map (u 0.0132 per room unit, the same either side of the gap
#: to 0.0007; v from 1.0 at the floor to 0.0 at 277), and its 410 above 277
#: with the carved upper wall (image 9). Under the level-wide rule alone the
#: face's 404 panel (no image over the share level-wide: six images, none on
#: 945 of 1511) and its two 410 strips (the frame draw, which wraps the box
#: and faces every way) kept the source's 16x64 and 32x32 stone - a plain
#: band below and above the art. Continued, the face's six triangles draw
#: the wall: the panel and the lower strip the capitals, the upper strip the
#: carved wall, each at the wall's map; the strips' vertices are copied off
#: the frame's sides, which keep their stone. The door's other face (room
#: 42's, a passage's end) has no wall facing its way and keeps the level-wide
#: rule: its 406 panel the capitals, its strips stone. A door placed more
#: than once takes a triangle only when every placement agrees; a placement
#: whose thin axis is not the pad's side (the model's z) takes none.
DOOR_BG = {'cryptdoor1a': 'temple', 'cryptdoor2a': 'temple', 'cryptdoor2b': 'temple',
           'cryptdoor3': 'temple', 'cryptdoor4': 'temple'}
#: The stage whose setup places each DOOR_BG door (gameshade.door_placements).
DOOR_STAGE = {'cryptdoor1a': 'egypt', 'cryptdoor2a': 'egypt', 'cryptdoor2b': 'egypt',
              'cryptdoor3': 'egypt', 'cryptdoor4': 'egypt'}
#: continue_wall: the cosine within which a triangle faces the door's +z.
FACE_COS = 0.99
#: continue_wall: how far apart (in images) the texture coordinates the wall's
#: triangles at one height give a door triangle may lie and still agree.
WALL_MAP_TOL = 0.01


def level_images(draw_tex, corr, share):
    """D-054: {draw: (image, triangles, total)} for each draw whose game
    texture (`draw_tex` {draw: texture}) has an image in `corr` ({texture:
    (image, triangles, total)}, gameshade.bg_texture_images) over at least
    `share` of its room triangles. Pure."""
    out = {}
    for di, tex in draw_tex.items():
        hit = corr.get(tex) if tex is not None else None
        if hit is not None and hit[2] and float(hit[1]) / hit[2] >= share:
            out[di] = hit
    return out


def _wall_at(pl, maps, texs, P, c, h, tex):
    """continue_wall at one placement: (image, three (u, v)) or None."""
    hs, ht = pl['half']
    st = [((p[0] - c[0]) / h[0] * hs, (p[1] - c[1]) / h[1] * ht) for p in P]
    tc = sum(t for _s, t in st) / 3.0
    band = [m for m in maps if m[1][0] < tc < m[1][1]]
    if not band or len({m[0] for m in band}) != 1:
        return None
    if {tx for tx, (lo, hi) in texs if lo < tc < hi} != {tex}:
        return None
    uvs = [[(m[2][0] * s + m[2][1] * t + m[2][2], m[3][0] * s + m[3][1] * t + m[3][2]) for s, t in st]
           for m in band]
    if max(abs(a[k][j] - uvs[0][k][j]) for a in uvs for k in range(3) for j in range(2)) > WALL_MAP_TOL:
        return None
    return band[0][0], tuple(tuple(sum(a[k][j] for a in uvs) / len(uvs) for j in range(2)) for k in range(3))


def continue_wall(vertices, draws, tri_tex, ref, walls, skip=()):
    """D-054 (a door's face continues its wall): {(draw, triangle): (image,
    ((u, v) x3))} for the triangles of `draws` not in `skip` lying on the
    item's +z face (facing within FACE_COS of +z, every corner on the
    reference box `ref`'s +z side) whose game texture (`tri_tex`,
    gameshade.prop_triangle_textures) the game draws the wall round the door
    in at the triangle's height, and which the remaster's wall draws there
    with one image on one map (within WALL_MAP_TOL) - at every placement of
    `walls` ([(placement, gameshade.wall_maps, gameshade.wall_textures)])
    alike. The item's box is laid onto each placement's face: x along its
    first axis (the pad's up), y along its second (the pad's look), z along
    the normal (the pad's side, which the pad box's x carries) - a right-
    handed frame, as up x look = side; a placement with another thin axis
    takes nothing. Pure."""
    lo, hi = ref
    c = [(lo[a] + hi[a]) / 2.0 for a in range(3)]
    h = [(hi[a] - lo[a]) / 2.0 for a in range(3)]
    if not walls or not min(h) > 0.0 or any(pl.get('thin') != 0 for pl, _m, _t in walls):
        return {}
    eps = 1e-3 * max(h)
    out = {}
    for di, d in enumerate(draws):
        if di in skip:
            continue
        ind = d.get('indices') or []
        for q in range(0, len(ind) - 2, 3):
            tex = tri_tex.get((di, q // 3))
            if tex is None:
                continue
            P = [tuple(float(x) for x in vertices[i][:3]) for i in ind[q:q + 3]]
            a = [P[1][k] - P[0][k] for k in range(3)]
            b = [P[2][k] - P[0][k] for k in range(3)]
            n = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
            ln = sum(x * x for x in n) ** 0.5
            if not ln > 0.0 or n[2] / ln < FACE_COS or any(abs(p[2] - hi[2]) > eps for p in P):
                continue
            got = None
            for pl, maps, texs in walls:
                one = _wall_at(pl, maps, texs, P, c, h, tex)
                if one is None or (got is not None and (
                        one[0] != got[0] or max(abs(one[1][k][j] - got[1][k][j])
                                                for k in range(3) for j in range(2)) > WALL_MAP_TOL)):
                    got = None
                    break
                got = got or one
            if got is not None:
                out[(di, q // 3)] = got
    return out


def door_face_art(got, draw_tex, corr, bg_textures, share, walled=None):
    """D-054: `got` (read_static's dict, every draw translucent) drawn opaque
    with each draw in level_images drawing that image of `bg_textures`
    instead of its own, and each triangle of `walled` (continue_wall) moved
    to a draw of its image at its texture coordinates - on copies of its
    vertices, so the draws it leaves keep theirs; within one image the
    coordinates are moved by whole images to start in the first. Draws left
    empty and the images and vertices nothing names any more are left out.
    Returns (the new dict, [(draw, texture, image name, triangles, total)],
    [(image name, triangles, (draws), (textures))]), or (None, [], []) when
    nothing takes an image. Pure: the inputs are not changed."""
    import math                                              # noqa: E402
    import xblaworld                                         # noqa: E402
    gate = xblaworld.PASS_OPAQUE | xblaworld.PASS_TRANSLUCENT
    walled = walled or {}
    textures = list(got['textures'])
    added, taken = {}, []

    def image_of(im):
        if im not in added:
            added[im] = len(textures)
            textures.append(bg_textures[im])
        return added[im]

    level = level_images(draw_tex, corr, share)
    draws = []
    for di, d in enumerate(got['draws']):
        nd = dict(d, pass_mask=((d.get('pass_mask') or 0) & ~gate) | xblaworld.PASS_OPAQUE)
        if di in level:
            im, n, total = level[di]
            nd['image'] = image_of(im)
            taken.append((di, draw_tex[di], bg_textures[im]['name'], n, total))
        draws.append(nd)
    if not taken and not walled:
        return None, [], []
    vertices = [tuple(v[:8]) + (255,) + tuple(v[9:]) for v in got['vertices']]
    skin = list(got['skin']) if got.get('skin') is not None else None
    walls = []
    groups = {}
    for (di, q), (im, uvs) in sorted(walled.items()):
        groups.setdefault(im, []).append((di, q, uvs))
    for im, tris in sorted(groups.items()):
        du = math.floor(min(uv[0] for _d, _q, uvs in tris for uv in uvs))
        dv = math.floor(min(uv[1] for _d, _q, uvs in tris for uv in uvs))
        copies, ind = {}, []
        for di, q, uvs in tris:
            for k in range(3):
                old = got['draws'][di]['indices'][3 * q + k]
                uv = (uvs[k][0] - du, uvs[k][1] - dv)
                key = (old, round(uv[0], 6), round(uv[1], 6))
                if key not in copies:
                    copies[key] = len(vertices)
                    vertices.append(tuple(vertices[old][:3]) + uv + tuple(vertices[old][5:]))
                    if skin is not None:
                        skin.append(skin[old])
                ind.append(copies[key])
        src = sorted({di for di, _q, _u in tris})
        draws.append(dict(draws[src[0]], image=image_of(im), indices=ind))
        walls.append((bg_textures[im]['name'], len(tris), tuple(src),
                      tuple(sorted({draw_tex.get(di) for di in src if draw_tex.get(di) is not None}))))
    gone = {}
    for di, q in walled:
        gone.setdefault(di, set()).add(q)
    for di, qs in gone.items():
        ind = draws[di].get('indices') or []
        draws[di] = dict(draws[di], indices=[i for k, i in enumerate(ind) if k // 3 not in qs])
    draws = [d for d in draws if d.get('indices')]
    # the images no draw names any more (a face's own legacy image) are left
    # out, the draws' indices renumbered in order
    used = sorted({d['image'] for d in draws if isinstance(d.get('image'), int)})
    remap = {im: k for k, im in enumerate(used)}
    draws = [dict(d, image=remap[d['image']]) if isinstance(d.get('image'), int) else d for d in draws]
    textures = [textures[im] for im in used]
    # and the vertices no draw names any more
    if walled:
        keep = sorted({i for d in draws for i in d['indices']})
        vre = {i: k for k, i in enumerate(keep)}
        vertices = [vertices[i] for i in keep]
        if skin is not None:
            skin = [skin[i] for i in keep]
        draws = [dict(d, indices=[vre[i] for i in d['indices']]) for d in draws]
    out = dict(got, draws=draws, textures=textures, vertices=vertices)
    if skin is not None:
        out['skin'] = skin
    return out, taken, walls


_BG_LEVELS = {}


def _bg_level(out, item):
    """The prepared background bundle <out>/xbla/background/<item>.slwa read
    once (xblasheet.read_slwa) as gameshade.texture_images takes a level:
    one pool of (x, y, z, u, v), a draw per mesh naming its material's
    image, the images as raw RGBA. CharacterRefused when the bundle is not
    beside the props."""
    path = os.path.join(out, 'xbla', 'background', '%s.slwa' % item)
    if path not in _BG_LEVELS:
        import numpy as np                                   # noqa: E402
        import xblasheet                                     # noqa: E402
        if not os.path.isfile(path):
            raise CharacterRefused("D-054: no background/%s bundle beside the props (prepare the "
                                   "background first) - the game's door draws" % item)
        d = xblasheet.read_slwa(path)
        P = [tuple(float(x) for x in p) + tuple(float(x) for x in uv)
             for p, uv in zip(d['V']['p'], d['V']['uv'])]
        I = d['I']
        draws = []
        for (mat, first, count, _fl) in d['H']:
            img = d['M'][mat][0]
            draws.append(dict(image=img, pool=0,
                              indices=[int(v) for tri in I[first // 3:(first + count) // 3] for v in tri]))
        textures = []
        for k, t in enumerate(d['tex']):
            if t is None:
                textures.append(None)
                continue
            a = np.asarray(t, dtype=np.uint8)
            textures.append(dict(name='background/%s image %d' % (item, k), w=int(a.shape[1]),
                                 h=int(a.shape[0]), rgba=a.tobytes()))
        _BG_LEVELS[path] = dict(pools=[dict(vertices=P)], draws=draws, textures=textures)
    return _BG_LEVELS[path]


def translucent_only(draws):
    """D-054: whether every draw of `draws` stands in the translucent pass
    alone (its pass gate's low bits). Pure."""
    import xblaworld                                         # noqa: E402
    gate = xblaworld.PASS_OPAQUE | xblaworld.PASS_TRANSLUCENT
    return bool(draws) and all(((d.get('pass_mask') or 0) & gate) == xblaworld.PASS_TRANSLUCENT
                               for d in draws)


#: A PROP'S REFLECTION: the props whose opaque draws carry the reflection
#: image their material names (slwa reflections; sl_gfx_dl.c chr_reflection
#: draws it as a view model's is drawn). Egyptian's Golden Gun pickup:
#: every draw's base is a near-white 512x512 image (mean 248 and 254) and its
#: reflection a 256x256 one (mean 143 136 113), with the pixel constants of a
#: reflection blend (c12 1.77, c14 -0.77) - the same three images the in-hand
#: gun/goldengun draws with, gold under the view model's reflection. Without
#: it the pickup read silver in its case beside the gold in-hand gun; the
#: game's own model, held instead, draws near black under the XBLA texture
#: set. Named: the other props whose draws name a reflection (the gas tank,
#: the oil drums, the missile - static_base's sphere pair) draw as accepted.
PROP_REFLECTIONS = ('chrgolden',)


#: THE CHART SHORT OF ITS UNIT (xblaworld.chart_short): the background items
#: whose pools no other rule reads are asked whether their charts stop just
#: short of a divisor. Egyptian's `temple` alone: its palm fronds and trunks
#: (pools 4 and 3) are refused without it, and with them the whole item (its
#: declared bounds are the palms'). Named, as GAME_COORDS_STATIC is: over the
#: source's backgrounds the rule would also read Runway's pools 3 and 8 (two
#: draws) and the multiplayer Caves' pool 2 (three), which stages accepted
#: before Egyptian draw without.
CHART_SHORT_ITEMS = ('temple',)


#: THE ATLAS TRIANGLES FOR A STATIC ITEM (#96): the static items whose texture
#: scale no rule of xblachar.texcoord_scale decides and
#: xblachar.atlas_triangles_scale does - a picture atlas over the body beside
#: tiling draws. MEASURED on Aztec's shuttle at 1/8192 (368 of the atlas
#: draws' 370 triangles inside, reaching 0.943; 12 at 1/4096); refused before,
#: so the game's model drew. Named rather than general, as GAME_COORDS_STATIC
#: is, so no item of a stage accepted before Aztec is read differently.
ATLAS_TRIANGLES_STATIC = ('shuttle',)


#: D-052 THE DOOR TAKES ITS WALL (#96). The game's secret stone door is its
#: wall's stone: at Aztec's door (bound pad 34, room 6/7) the game's model
#: lays (106.8, 106.8, 82.5) over its texture and the room's wall round it
#: (textures 143 .. 146) (81.1, 81.1, 62.7) - one hue, the door 1.32 times
#: the wall - and ORIGINAL draws the door 1.25 times its wall at playtest
#: run 20261012-195917 mark 1, in every channel. The remaster draws its door
#: as a pale block in a dark, vine-hung wall; the game's shade alone (the
#: rule's first form) left it 1.5 times the wall and olive where the wall is
#: warm grey ("secret stone doors are still pretty pale", run
#: 20261012-230527). So for the doors DOOR_WALL names, the door's vertex
#: colours take the shade that puts its tone at the remaster wall's tone
#: times the game's door-to-wall ratio, per channel:
#:
#:     shade = 255 x T_wall x (S_door / S_wall) / T_door
#:
#: T_wall the area-weighted tone (image mean x vertex colour / 255) of the
#: prepared background's surfaces in the wall round each placement of the
#: door (gameshade.door_placements, in_door_wall; room units x the stage's
#: scale), S_wall the game's room shade there (gameshade.door_wall_shade),
#: S_door the game model's shade (prop_game_shade) and T_door the item's own
#: tone on its faces. Without the background bundle beside the props the
#: game's shade alone applies, as before, and the summary says so.
DOOR_WALL = {'door_aztec': 'aztec'}


def item_face_tone(vertices, draws, textures):
    """The area-weighted tone (image mean x vertex colour / 255, per
    channel) of an item's triangles facing its principal axis - the axis
    of its largest projected area. Pure."""
    import numpy as np                                       # noqa: E402
    tris = []
    for dr in draws:
        im = dr.get('image')
        if not isinstance(im, int) or not (0 <= im < len(textures)):
            continue
        t = textures[im]
        mean = np.frombuffer(bytes(t['rgba']), np.uint8).reshape(-1, 4)[:, :3].astype(float).mean(0) \
            if not hasattr(t['rgba'], 'shape') else np.asarray(t['rgba'])[..., :3].reshape(-1, 3).mean(0)
        ix = dr.get('indices') or []
        for k in range(0, len(ix) - 2, 3):
            p = [np.array(vertices[i][:3], float) for i in ix[k:k + 3]]
            n = np.cross(p[1] - p[0], p[2] - p[0])
            col = np.mean([vertices[i][5:8] for i in ix[k:k + 3]], axis=0)
            tris.append((n, mean * col / 255.0))
    if not tris:
        return None
    axis = int(np.argmax(sum(np.abs(n) for n, _ in tris)))
    acc, wsum = np.zeros(3), 0.0
    for n, tone in tris:
        a = 0.5 * float(np.linalg.norm(n))
        if a > 0 and abs(n[axis]) >= 0.9 * 2 * a:
            acc += a * tone
            wsum += a
    return tuple(float(x) for x in acc / wsum) if wsum else None


def bundle_wall_tone(path, places, scale):
    """The area-weighted tone of a prepared background bundle's triangles in
    the walls round `places` (gameshade.in_door_wall), and their area."""
    import numpy as np                                       # noqa: E402
    import xblasheet                                         # noqa: E402
    import gameshade                                         # noqa: E402
    d = xblasheet.read_slwa(path)
    P, C, I = d['V']['p'].astype(float), d['V']['c'].astype(float), d['I']
    means = [t.reshape(-1, 4)[:, :3].astype(float).mean(0) if t is not None else None for t in d['tex']]
    acc, wsum = np.zeros(3), 0.0
    for (mat, first, count, _fl) in d['H']:
        img = d['M'][mat][0]
        mean = means[img] if 0 <= img < len(means) else None
        if mean is None:
            continue
        for tri in I[first // 3:(first + count) // 3]:
            a, b, c = P[tri[0]], P[tri[1]], P[tri[2]]
            n = np.cross(b - a, c - a)
            cen = (a + b + c) / 3.0
            if not any(gameshade.in_door_wall(cen.tolist(), n.tolist(), pl, scale) for pl in places):
                continue
            area = 0.5 * float(np.linalg.norm(n))
            acc += area * mean * C[tri, :3].mean(0) / 255.0
            wsum += area
    return (tuple(float(x) for x in acc / wsum), wsum) if wsum else (None, 0.0)


def door_wall_tone(rom, item, out, shade, vertices, draws, textures):
    """D-052 (DOOR_WALL): (the shade that gives the door its wall's tone, a
    summary note), or (None, the reason the game's shade alone applies)."""
    import gameshade                                         # noqa: E402
    level = DOOR_WALL.get(item)
    if level is None:
        return None, ''
    path = os.path.join(out, 'xbla', 'background', '%s.slwa' % level)
    if not os.path.isfile(path):
        return None, '; the wall not read: no background/%s bundle beside the props' % level
    try:
        places, scale, s_wall, g_area = gameshade.door_wall_shade(rom, item, level)
    except gameshade.ShadeError as exc:
        return None, '; the wall not read: %s' % exc
    t_wall, r_area = bundle_wall_tone(path, places, scale)
    t_door = item_face_tone(vertices, draws, textures)
    if t_wall is None or t_door is None or not all(x > 0 for x in s_wall + t_door):
        return None, '; the wall not read: no remaster wall or door face measured'
    out_shade = tuple(min(255.0, 255.0 * t_wall[k] * (shade[k] / s_wall[k]) / t_door[k])
                      for k in range(3))
    return out_shade, ('; its wall: the game\'s %.1f %.1f %.1f over %.0f room units^2, the '
                       'remaster\'s tone %.1f %.1f %.1f over %.0f, the door\'s %.1f %.1f %.1f '
                       '-> shade %.1f %.1f %.1f' % (s_wall + (g_area,) + t_wall + (r_area,)
                                                    + t_door + out_shade))


def _static_atlas_triangles(path, why):
    """(read_static's dict at xblachar.atlas_triangles_scale's factor, a
    summary note) for an ATLAS_TRIANGLES_STATIC item, or CharacterRefused
    naming `why`."""
    import xblachar                                          # noqa: E402
    raw = xblachar.read_static(path, images=False)
    f = xblachar.atlas_triangles_scale(raw['vertices'], [d['indices'] for d in raw['draws']])
    if f is None:
        raise CharacterRefused('%s (and the atlas triangles decide nothing)' % why)
    got = xblachar.read_static(path, uv_scale=f)
    return got, ' (the atlas triangles: texture scale 1/%d)' % int(round(32768 / f))


def convert_prop(item, source_item, tree, out, matrix_names=None, pane=False, rom=None):
    """Convert the REMASTERED prop/`source_item` found under `tree` into
    <out>/xbla/prop/<item>.slwa (#71), `item` being the game's own model name
    (Pmainframe1Z -> mainframe1; stageprops.py derives the pair).

    A prop item is a STATIC item (xblachar.read_static): no skeleton, one
    rigid piece. Nothing in it says where the game's model sits in its space,
    so the bundle carries the REFERENCE box - the box of the source's ORIGINAL
    generation of the same item, which is the game's own model in that space
    (measured: the two generations of the Dam's desk, keyboard, monitor and
    mainframe share one box) - and the runtime relates it to the game model's
    own box. The opaque pass is carried as it is and the translucent pass
    blended, and a draw of either pass under the source's decal style bit is
    marked SL_WA_MAT_DECAL; a draw in neither pass is refused. A blended draw
    whose program takes its coverage from the image's blue channel
    (xblaworld.coverage_channel) is marked SL_WA_MAT_COVERAGE_BLUE. Returns a
    one-line summary; raises CharacterRefused, with the reason, for anything it
    will not convert.

    A MOVING prop - a vehicle, whose model moves its wheels under matrices of
    their own - has a SKINNED item: a joint per moving part, named as the game
    names that part's matrix (`matrix_names`, stageprops.matrix_names). Its
    joints are carried with the matrix each rides (prop_joints) and the
    reference box as for any prop; the runtime places the item by the box and
    each part at the game's own node for its matrix (sl_prop_derive).

    GLASS (`pane` True: every object the model stands for is breakable glass)
    is taken as a one-piece prop - the lab glassware - unless the item is a
    PANE: its reference box at most PANE_THICKNESS of its largest extent
    thick. A pane is a window, the room's glazing, and stays the game's on
    every stage (refused here, so no bundle can stand in for it).

    D-032, THE GAME'S TINT ON ITS BARS: for an item DIVERGENCE_ITEMS names,
    and with the user's ROM (`rom`), the image texels the game draws tinted by
    a vertex colour the remaster whitened are multiplied by that colour
    (gameshade.restore_prop_tint). Without the ROM, or where the game's data
    does not relate, the item is prepared as authored and the summary says
    why."""
    need('numpy', 'texture decoding')
    import xblachar                                          # noqa: E402
    import xblaworld                                         # noqa: E402

    def item_file(gen):
        d = find_item(tree, '%s/prop/%s' % (gen, source_item))
        if d is None or not os.path.isdir(d):
            raise CharacterRefused('this source has no %s prop/%s' % (gen, source_item))
        files = sorted(n for n in os.listdir(d) if n.lower().endswith('.bin'))
        if not files:
            raise CharacterRefused('%s prop/%s holds no bundle file' % (gen, source_item))
        return os.path.join(d, files[0])
    coord_note = ''
    # D-053 THE ORIGINAL SHUTTLE (ORIGINAL_GENERATION): the item is converted
    # from the source's original generation, the game's model in its space.
    original = item in ORIGINAL_GENERATION and divergence_applies('D-053', item)
    try:
        new = item_file(ORIGINAL if original else REMASTERED)
        if matrix_names and not xblachar.is_static(new):
            try:
                got = xblachar.read_character(new, table_names=True, plain_sphere=True,
                                              layers=True)
            except xblachar.CharacterError as exc:
                # THE GAME'S COORDINATES (#96, gameshade.coords_scale): a
                # moving prop every texture-scale rule refused, whose
                # coordinates are the game model's own at exactly one power
                # of two, is read at that one. The remaster's Caverns eyelid
                # door (new/prop/dooreyelid, the same as its original
                # generation) samples two clamped steel images and runs one
                # draw to 1.289 of its image - so no rule reads it - and at
                # 1/16384 its coordinates ARE the game's Pdoor_eyelidZ's:
                # texture 1161 s -0.109 .. 1.073, t -0.289 .. 1.026 against the
                # item's u -0.109 .. 1.073, v -0.026 .. 1.289 (v = 1 - t), and
                # texture 197 likewise. Without the ROM the item stays refused.
                if 'reach no atlas edge' not in str(exc) or not rom:
                    raise
                import gameshade                             # noqa: E402
                raw = xblachar.read_character(new, images=False)
                try:
                    cf, cm = gameshade.game_coords_scale(rom, item, raw['vertices'], raw['draws'],
                                                         xblachar.TEXCOORD_DOUBLINGS)
                except gameshade.ShadeError as exc2:
                    raise CharacterRefused('%s (the game\'s coordinates not read: %s)' % (exc, exc2))
                if cf is None:
                    raise CharacterRefused('%s (and the game\'s coordinates decide nothing: %s)'
                                           % (exc, cm))
                got = xblachar.read_character(new, table_names=True, plain_sphere=True,
                                              uv_scale=cf, layers=True)
                coord_note = (" (the game's coordinates: texture scale 1/%d, images %s on the "
                              "game's textures %s)"
                              % (int(round(32768 / cf)), ' '.join(str(i) for i in sorted(cm)),
                                 ' '.join(str(cm[i]) for i in sorted(cm))))
            joints = prop_joints(got['joints'], matrix_names)
        else:
            hat = False
            try:
                got = xblachar.read_static(new)
            except xblachar.CharacterError as exc:
                # THE GAME'S COORDINATES for a static item (#96, below)
                if 'reach no atlas edge' in str(exc) and item in GAME_COORDS_STATIC:
                    got, coord_note = _static_game_coords(new, item, rom, str(exc))
                elif 'reach no atlas edge' in str(exc) and item in ATLAS_TRIANGLES_STATIC:
                    got, coord_note = _static_atlas_triangles(new, str(exc))
                elif 'has a skeleton' not in str(exc):
                    raise
                else:
                    hat = True
            if hat:
                # ONE RIGID PIECE ON A SKELETON: a hat is authored on the
                # character's skeleton with every vertex on one joint. It is
                # one rigid piece in the item's space, as a static item is.
                got = xblachar.read_character(new, table_names=True)
                on = {j for s in got['skin'] for j, w in s if w}
                if len(on) != 1:
                    raise CharacterRefused('prop/%s is skinned over %d joints and the game '
                                           'model is one piece' % (source_item, len(on)))
                got = dict(got, skin=[((0, 255),)] * len(got['vertices']))
            joints = [('SKEL_ROOT', -1, (0.0, 0.0, 0.0))]
        ref = xblachar.static_bounds(item_file(ORIGINAL))
    except xblachar.CharacterError as exc:
        raise CharacterRefused(str(exc))
    # D-054 THE GAME'S OPAQUE DOOR (translucent_only, gameshade.prop_blends,
    # door_face_art): opaque, its faces in the remaster's own art
    door_note = ''
    if divergence_applies('D-054', item) and rom and translucent_only(got['draws']):
        import gameshade                                     # noqa: E402
        try:
            nb, nt = gameshade.prop_blends(rom, item)
        except gameshade.ShadeError:
            nb = None
        if nb == 0:
            lv = _bg_level(out, DOOR_BG[item])
            try:
                dtex = gameshade.prop_draw_textures(rom, item, got['vertices'], got['draws'], ref)
                corr = gameshade.bg_texture_images(rom, DOOR_BG[item], lv)
            except gameshade.ShadeError as exc:
                raise CharacterRefused("D-054: prop/%s - the game's door draws (its textures not "
                                       "read: %s)" % (source_item, exc))
            # a door's face continues its wall, when the setup places the
            # door; without the wall the level-wide rule stands alone
            walled, wall_note = {}, ''
            if item in DOOR_STAGE:
                try:
                    walls = gameshade.door_walls(rom, item, DOOR_STAGE[item], DOOR_BG[item], lv)
                    ttex = gameshade.prop_triangle_textures(rom, item, got['vertices'], got['draws'], ref)
                    walled = continue_wall(got['vertices'], got['draws'], ttex, ref, walls,
                                           set(level_images(dtex, corr, gameshade.BG_IMAGE_SHARE)))
                except gameshade.ShadeError as exc:
                    wall_note = '; the wall round it not read: %s' % exc
            new_got, taken, walls_taken = door_face_art(got, dtex, corr, lv['textures'],
                                                        gameshade.BG_IMAGE_SHARE, walled)
            if new_got is None:
                raise CharacterRefused("D-054: prop/%s draws every draw in the translucent pass, "
                                       "the game's model (%d triangles) blends none and the "
                                       "background owns no image for its textures - the game's "
                                       "door draws" % (source_item, nt))
            got = new_got
            door_note = (" (D-054: opaque, the game's model blending none; %s%s)"
                         % ('; '.join(['draw %d on the game\'s texture %d draws the background\'s %s '
                                       '(%d of %d room triangles)' % t for t in taken]
                                      + ['%d triangle(s) of draw(s) %s on the game\'s texture(s) %s '
                                         'continue the wall round the door: the background\'s %s at '
                                         'the wall\'s own texture map'
                                         % (w[1], ', '.join(str(d) for d in w[2]),
                                            ', '.join(str(x) for x in w[3]), w[0])
                                         for w in walls_taken]), wall_note))
    if pane:
        ext = [ref[1][k] - ref[0][k] for k in range(3)]
        if min(ext) <= PANE_THICKNESS * max(ext):
            raise CharacterRefused('prop/%s is a pane (%.1f x %.1f x %.1f): a window is the '
                                   'game\'s own glazing' % (source_item, ext[0], ext[1], ext[2]))
    vertices, skin, draws, textures = got['vertices'], got['skin'], got['draws'], got['textures']
    # THE GAME'S REPEATS (#95, gameshade.game_repeats): a static item whose
    # texture scale only THE WHOLE IMAGE decided is checked against the
    # game's own model, and read a power of two over when the game's faces
    # say so clearly. Without the ROM the answer stands and the summary says
    # so; nothing another rule decided is asked.
    rep_note = ''
    if got.get('texcoord_rule') == 'whole':
        if not rom:
            rep_note = " (the game's repeats not checked: no ROM)"
        else:
            import gameshade                                 # noqa: E402
            try:
                rf, rmed, rshare, rn = gameshade.game_repeats(rom, item, vertices, draws, ref)
            except gameshade.ShadeError as exc:
                rep_note = " (the game's repeats not checked: %s)" % exc
            else:
                if rf != 1.0:
                    vertices = [(x, y, z, u * rf, v * rf) + tuple(rest)
                                for (x, y, z, u, v, *rest) in vertices]
                    rep_note = (" (the game's repeats: texture scale x%g over the whole image's - "
                                "%d triangles on the game's, per-axis log2 %+.3f, %.0f%% agreeing)"
                                % (rf, rn, rmed, 100.0 * rshare))
                else:
                    rep_note = (" (the game's repeats keep the whole image's scale: %d triangles, "
                                "per-axis log2 %s)" % (rn, 'n/a' if rmed is None else '%+.3f' % rmed))
    # D-053 THE GAME'S DECAL ALPHA (gameshade.prop_decal_alpha): the original
    # generation's decals are drawn at the alpha the game blends them at.
    decal_note = ''
    if original:
        if not rom:
            decal_note = " (D-053: the original generation; the game's decal alpha not read: no ROM)"
        else:
            import gameshade                                 # noqa: E402
            try:
                vertices, skin, draws, drep = gameshade.prop_decal_alpha(rom, item, vertices, skin,
                                                                         draws, ref)
            except gameshade.ShadeError as exc:
                decal_note = (" (D-053: the original generation; the game's decal alpha not "
                              "read: %s)" % exc)
            else:
                decal_note = (" (D-053: the original generation; %s)"
                              % gameshade.describe_decal_alpha(drep))
    tint_note = ''
    if divergence_applies('D-032', item):
        if not rom:
            tint_note = " (D-032 not applied: no ROM to read the game's tint from)"
        else:
            import gameshade                                 # noqa: E402
            try:
                textures, tint = gameshade.restore_prop_tint(rom, item, vertices, draws,
                                                             textures, ref)
            except gameshade.ShadeError as exc:
                tint, tint_note = None, ' (D-032 not applied: %s)' % exc
            if tint:
                tint_note = ' (D-032: %s)' % '; '.join(gameshade.describe_tint(tint))
            elif not tint_note:
                tint_note = ' (D-032: the game tints nothing on this item)'
    # D-052 THE GAME'S SHADE ON A PALE ITEM (gameshade.prop_game_shade): the
    # item's vertex colours are multiplied by the shade the game lays over
    # its own model, so the remaster's detail is kept at the game's tone.
    if divergence_applies('D-052', item):
        if not rom:
            tint_note += " (D-052 not applied: no ROM to read the game's shade from)"
        else:
            import gameshade                                 # noqa: E402
            try:
                shade, nsh, seen = gameshade.prop_game_shade(rom, item)
            except gameshade.ShadeError as exc:
                tint_note += ' (D-052 not applied: %s)' % exc
            else:
                if shade is None:
                    tint_note += ' (D-052: the game shades nothing on this item)'
                else:
                    wall, wall_note = door_wall_tone(rom, item, out, shade, vertices, draws,
                                                     textures)
                    vertices = gameshade.shade_vertices(vertices, wall or shade)
                    tint_note += (" (D-052: the game's shade %.1f %.1f %.1f over %d triangles, "
                                  "corners %s%s)" % (shade + (nsh, ' '.join(
                                      '%02x%02x%02x' % c for c in seen), wall_note)))
    # THE UNSEEN IMAGE (xblachar.lift_unseen): an opaque image every draw of
    # which carries a wholly black vertex colour is drawn as authored
    vertices, draws, skin, lifted = xblachar.lift_unseen(vertices, draws, skin,
                                                         xblaworld.PASS_OPAQUE)
    mats = [(i, slwa.BLEND_OPAQUE) for i in range(len(textures))]
    # A PROP'S REFLECTION (PROP_REFLECTIONS): the reflection image an opaque
    # draw names rides its material, for the props named there alone.
    reflections = [None] * len(mats)
    shiny_ok = item in PROP_REFLECTIONS
    extra = {}
    indices, meshes, clear, decals, blue, shiny = [], [], 0, 0, 0, 0
    for dr in draws:
        pm = dr['pass_mask'] or 0
        # THE DECAL PASS, read from the same style word as the level item's
        # (xblaworld.STYLE_DECAL): a prop's graffiti and labels are authored
        # exactly on the face they mark, so they carry the depth priority the
        # renderer gives SL_WA_MAT_DECAL. Part of the material key, so a decal
        # never shares a material with the ordinary draw of the same image.
        decal = slwa.MAT_DECAL if (pm & xblaworld.STYLE_DECAL) else 0
        if dr.get('layer'):
            # THE LAYER OF A TWO-LAYER DRAW (xblachar.STRIDE_RIGID_UV2): the
            # program lerps the base toward it by the layer colour's alpha
            # times one channel of the layer image - the base drawn first,
            # then this over it on the same triangles, blended by source
            # alpha with the decal pass's depth priority, its alpha the
            # image's blue where the program weighs by blue.
            blend = slwa.BLEND_ALPHA
            decal |= slwa.MAT_DECAL
            clear += 1
            if dr['layer']['channel'] == 2:
                decal |= slwa.MAT_COVERAGE_BLUE
                blue += 1
        elif pm & xblaworld.PASS_OPAQUE:
            blend = slwa.BLEND_OPAQUE
        elif pm & xblaworld.PASS_TRANSLUCENT:
            blend = slwa.BLEND_ALPHA
            clear += 1
        else:
            raise CharacterRefused('prop/%s draws outside its opaque and translucent passes'
                                   % source_item)
        # THE COVERAGE CHANNEL, read from the draw's own pixel program: where
        # it multiplies the image's BLUE into the exported alpha and never
        # fetches the image's alpha (a crate's grey graffiti on black), black
        # is transparent there and the material says so. Part of the key, so
        # an image drawn both ways is two materials.
        if blend == slwa.BLEND_ALPHA and dr.get('coverage') == 'z':
            decal |= slwa.MAT_COVERAGE_BLUE
            blue += 1
        refl = dr.get('reflect') if (shiny_ok and blend == slwa.BLEND_OPAQUE and not decal) \
            else None
        if blend == slwa.BLEND_OPAQUE and not decal and refl is None:
            mat = dr['image']
        else:
            key = (dr['image'], blend, decal, refl)
            if key not in extra:
                extra[key] = len(mats)
                mats.append((dr['image'], blend, decal))
                reflections.append(refl)
            mat = extra[key]
        decals += 1 if decal & slwa.MAT_DECAL else 0
        shiny += 1 if refl is not None else 0
        meshes.append((mat, len(indices), len(dr['indices']), 0))
        indices.extend(dr['indices'])
    path = os.path.join(out, 'xbla', 'prop', item + '.slwa')
    try:
        n = slwa.write_skinned(path, slwa.CLASS_PROP, item, vertices, indices, mats,
                               [(t['w'], t['h'], t['rgba']) for t in textures],
                               meshes, skin, joints,
                               reflections if any(r is not None for r in reflections) else None,
                               bounds=ref)
    except slwa.SlwaError as exc:
        raise CharacterRefused(str(exc))
    moving = sorted({jt[3] - slwa.PART_MATRIX - 1 for jt in joints if len(jt) == 4 and jt[3]})
    return ('prop/%s: %d draws%s%s%s%s, %d images, %d vertices, %d triangles, %sreference box '
            '%.1f %.1f %.1f .. %.1f %.1f %.1f, %.1f MB%s'
            % (source_item, len(draws), ' (%d blended)' % clear if clear else '',
               (' (%d decal)' % decals if decals else '')
               + (' (%d reflective)' % shiny if shiny else ''),
               ' (%d blue coverage)' % blue if blue else '',
               ' (lane lifted on unseen image%s %s)' % ('s' if len(lifted) > 1 else '',
                                                       ' '.join(str(i) for i in lifted))
               if lifted else '',
               len(textures), len(vertices), len(indices) // 3,
               '%d joints on matrices %s, ' % (len(joints), ' '.join(str(m) for m in moving))
               if moving else '',
               ref[0][0], ref[0][1], ref[0][2], ref[1][0], ref[1][1], ref[1][2],
               n / (1024.0 * 1024.0), coord_note + rep_note + tint_note + decal_note + door_note))


def prepare_character(cls, item, tree, out):
    try:
        summary = convert_character(cls, item, tree, out)
    except CharacterRefused as exc:
        print('prepare-xbla-assets: %s' % exc)
        print('  It is refused rather than guessed at.')
        return 1
    print('  %s/%s: %s' % (cls, item, summary))
    print('  wrote %s' % os.path.join(out, 'xbla', cls, item + '.slwa'))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cls', choices=sorted(CLASSES), metavar='class')
    ap.add_argument('--item', required=True, help='the canonical item path, e.g. dam')
    ap.add_argument('--tree', required=True, help='an extracted source tree')
    ap.add_argument('--out', required=True, help='the world-asset root to write under')
    ap.add_argument('--rom', help="the user's ROM: the game's own shades (D-033)")
    a = ap.parse_args()
    if a.cls == 'background':
        return prepare_background(a.cls, a.item, a.tree, a.out, rom=a.rom)
    if a.cls in ('char', 'head'):
        return prepare_character(a.cls, a.item, a.tree, a.out)
    return prepare(a.cls, a.item, a.tree, a.out)


if __name__ == '__main__':
    sys.exit(main())
