"""SLWA writer - the Sightline-owned runtime world-asset bundle (#68).

The format is specified once, in src/gfx/sl_gfx_worldasset.h, which is what
the runtime parses; this writer exists so that exactly one tool produces it
and the two cannot drift. Little-endian throughout, 128-byte header, every
section 4-byte aligned, and the declared file size is the real one.

THIS IS NOT A UNIVERSAL MODEL FORMAT. It carries what the renderer consumes -
positions, one texture coordinate set, vertex colour, indexed triangles, flat
materials with one image each, (version 2) the per-vertex fields and the
records of the source's own vertex animation, and (version 3) the per-vertex
tangent frame and the per-material record of the source's own multi-image
pixel program - and nothing more, so that what it does carry can be
validated exhaustively at load.

It carries no source bytes, no identifier of any particular source and no way
to obtain one.
"""
import math
import os
import struct

MAGIC = b'SLWA'
# VERSION 1 is the layout every bundle before material animation used; the
# three reserved header words are zero. VERSION 2 (#69) gives those words a
# meaning - the per-vertex AUXILIARY section and the ANIMATION table below -
# and is written ONLY when a bundle carries them: a bundle with nothing to
# animate is still a version-1 file, byte for byte, so a skydome prepared
# before and after this change is identical. VERSION 3 (#69 material) widens
# the auxiliary entry to carry the tangent frame and gives the header word at
# +44 a meaning - the MATERIAL EXTENSION table below - and is written ONLY
# when a bundle carries a material record: a bundle that only moves is still
# the version-2 file it was, byte for byte. The runtime reads all three.
VERSION = 3
VERSION_ANIMATED = 2
VERSION_STATIC = 1
HEADER = 128
PATH_MAX = 32

CLASS_SKYDOME = 1
CLASS_BACKGROUND = 2
CLASSES = (CLASS_SKYDOME, CLASS_BACKGROUND)

VERTEX_SIZE = 24
INDEX_SIZE = 4
MATERIAL_SIZE = 16
TEXTURE_SIZE = 16
MESH_SIZE = 16

# MATERIAL ANIMATION (#69). Two optional sections, present together or not at
# all, mirrored in src/gfx/sl_gfx_worldasset.h:
#
#   aux   (12 per vertex, one per vertex of the bundle)
#         s16 wave_a, s16 wave_b     the wave pair the program reads, RAW - an
#                                    unnormalised integer; the record's
#                                    coord_scale is what it is read at
#         s16 nx, ny, nz             the displacement direction, signed-
#                                    normalised (32767 = 1.0)
#         u16 anim                   0 = this vertex never moves; else the
#                                    1-based animation record that moves it
#   anim  (128 per record, 1 .. MAX_ANIMS)
#         u32 mode (ANIM_SINEWAVES)  f32 coord_scale  u32 wave_count  u32 0
#         f32 amplitude[4] phase[4] speed[4] freq_a[4] freq_b[4]
#         f32 turn_scale  turn_bias  turn  f32 0
#         f32 poly[4]
#
# The record is the vertex program's own contract, every number the item's
# (see xblaworld.wave_record): per wave, theta = frac(-t * speed + phase +
# wave_a * coord_scale * freq_a + wave_b * coord_scale * freq_b); angle =
# (theta * turn_scale - turn_bias) * turn; s = poly[0] * angle + poly[1] *
# angle^3 + poly[2] * angle^5 + poly[3] * angle^7; the vertex moves along its
# direction by the sum over waves of amplitude * s. `t` is seconds of the
# game's own frame clock, which the runtime supplies.
AUX_SIZE = 12
ANIM_SIZE = 128
MAX_ANIMS = 64
ANIM_SINEWAVES = 1
ANIM_WAVES = 4

# MATERIAL EXTENSION (#69 material). Version 3 only, mirrored in
# src/gfx/sl_gfx_worldasset.h:
#
#   aux   (24 per vertex)  the 12 bytes above, then
#         s16 xx, xy, xz             the tangent axis the normal map's first
#                                    component scales (32767 = 1.0)
#         s16 yx, yy, yz             ... and its second component's
#   matx  (96 per material, exactly material_count of them; header +44)
#         u32 mode (0 none, MATX_NORMAL_SPHERE)
#         u32 normal_tex  u32 sphere_tex  u32 offset_tex   (< texture_count)
#         u32 flags (MATX_SPHERE_CLAMP)
#         f32 base_weight[3]  f32 reflect_weight[3]  f32 warp_scale
#         f32 normal_scale  f32 normal_bias
#         f32 sphere_scale[2]  f32 sphere_bias[2]
#         f32 mean_weight  f32 alpha_floor   u32 0 x4
#
# The record is the pixel program's own contract, every number the item's
# (see xblaworld.material_record):
#
#   n      = normal(uv) * normal_scale + normal_bias        tangent space
#   N      = normalize(n.x * X + n.y * Y + n.z * normal)   view space, per
#                                                          vertex frame rotated
#                                                          by the view
#   base   = base(uv + warp_scale * offset(uv).xy)
#   refl   = sphere(N.x * sphere_scale.x + sphere_bias.x,
#                   N.y * sphere_scale.y + sphere_bias.y)
#   rgb    = base_weight * colour * base + reflect_weight * refl
#   a      = max(dot(rgb, mean_weight), alpha_floor) * base.a * colour.a
#
# then the stage fog the material already consumes. The material's own
# `texture` stays the base image.
AUX3_SIZE = 24
MATX_SIZE = 96
MATX_NONE = 0
MATX_NORMAL_SPHERE = 1
MATX_SPHERE_CLAMP = 1

BLEND_OPAQUE = 0
BLEND_ALPHA = 1
# CUTOUT: a shape carried in the alpha channel rather than a translucent
# surface. It writes depth; ALPHA does not. The distinction is read from the
# source's own style byte - see src/gfx/sl_gfx_worldasset.h and
# prepare.py's prepare_background. Widening the accepted set cannot make an
# older bundle invalid, so the VERSION does not move.
BLEND_CUTOUT = 2
BLENDS = (BLEND_OPAQUE, BLEND_ALPHA, BLEND_CUTOUT)

# Material FLAGS. DECAL says this material's draws are the source's own decal
# pass - a surface authored to lie ON another surface - which is a depth-order
# property rather than a colour one. See src/gfx/sl_gfx_worldasset.h for the
# measurement behind it. The flags word was previously required to be zero;
# widening it cannot make an older bundle invalid, so the VERSION does not move.
MAT_DECAL = 1
# FOG says this material's source pixel program consumes the stage-fog contract
# (declares c_fog and c_fogcolour), so the renderer applies the remaster's
# per-environment stage fog to it (#68). Set only when EVERY pixel program in
# the item declares both - see xblaworld.fog_programs.
MAT_FOG = 2
# GENERATED says this material is NOT the source's: the preparation made it
# (tools/worldasset/zenith.py, the D-023 zenith clouds) and the runtime draws
# it only while that divergence is on - off, the frame is the source's alone.
# Skydome class only; the runtime refuses it on any other. Same layout, a
# wider accepted set, so the VERSION does not move (the DECAL rule).
MAT_GENERATED = 4
# COVERAGE_BLUE says this blended material's coverage - its alpha - is its
# image's BLUE channel, not its alpha channel: the source's pixel program
# fetches the image without its alpha and multiplies blue into the exported
# alpha (xblaworld.coverage_channel). The runtime samples alpha from blue for
# it; a material without the bit keeps the image's own alpha, which is what
# every bundle written before it did. ALPHA-blended materials only. Same
# layout, a wider accepted set, so the VERSION does not move (the DECAL rule).
MAT_COVERAGE_BLUE = 8
# PANORAMA says this material belongs to the item's PANORAMA SHELL: the
# outermost ring of the background, which lies farther out than everything
# else in the item and closes the circle round it (prepare.panorama_shell).
# Where the stage fog covers it completely the runtime lets it fade toward the
# sky drawn behind it (D-026); a material without the bit is drawn as it always
# was. Background class only. Same layout, a wider accepted set, so the
# VERSION does not move (the DECAL rule).
MAT_PANORAMA = 16
# CLOUD_PLANE says this GENERATED skydome material is the D-023 cloud field:
# its image is one repeat of the game's cloud plane, and the runtime casts each
# vertex onto that plane with the game's own cloud phase instead of reading the
# stored coordinates. Skydome class only, and only beside GENERATED. Same
# layout, a wider accepted set, so the VERSION does not move (the DECAL rule).
MAT_CLOUD_PLANE = 32
# CULL_BACK says this material's draws stand under the source's face cull (#96,
# xblaworld.RS_CULL): the remaster draws a triangle of it only from the side its
# authored normal - cross(B - A, C - A) - faces. A material without the bit is
# drawn from both sides, as every bundle written before it was. Background
# class only. Same layout, a wider accepted set, so the VERSION does not move
# (the DECAL rule).
MAT_CULL_BACK = 64
# GAME_WATER says this material's draws redraw the game's MOVING WATER (D-048,
# gameshade.game_water): they lie on the game's texture-1508 surfaces and
# carry the game's own coordinates there, u = s / 2048 + c and v = c' - t /
# 2048. The runtime moves them by the game's own water phases (two scrolled
# tiles of the image, blended by the game's animated fraction); a material
# without the bit is drawn with its stored coordinates, as every bundle
# written before it was. Background class only. Same layout, a wider accepted
# set, so the VERSION does not move (the DECAL rule).
MAT_GAME_WATER = 128
MAT_KNOWN = 255

# The LAYOUT is unchanged by the background class, so the VERSION is too: a
# bundle written before it still parses. These limits only widen - which can
# never make an older file invalid, only stop rejecting a larger new one. They
# mirror src/gfx/sl_gfx_worldasset.h exactly and the self-test checks that.
MAX_VERTICES = 1000000
MAX_INDICES = 3000000
MAX_MATERIALS = 256
MAX_TEXTURES = 256  # Depot's background item carries 136 (#92)
MAX_MESHES = 1024
MAX_TEX_DIM = 2048
MAX_FILE = 192 * 1024 * 1024


class SlwaError(Exception):
    """The bundle being written would not be one the runtime accepts."""


def _pad4(n):
    return (4 - (n & 3)) & 3


def pack_anim(rec):
    """One animation record (the dict xblaworld.wave_record returns) -> its
    ANIM_SIZE bytes, or SlwaError when it is not one the runtime evaluates."""
    if not isinstance(rec, dict) or rec.get('mode') != 'sinewaves':
        raise SlwaError('animation record %r is not a sine-wave record' % (rec,))
    lanes = []
    for k in ('amplitude', 'phase', 'speed', 'freq_a', 'freq_b', 'poly'):
        v = rec.get(k)
        if not isinstance(v, (tuple, list)) or len(v) != ANIM_WAVES:
            raise SlwaError('animation record field %r is not %d lanes' % (k, ANIM_WAVES))
        lanes.append([float(x) for x in v])
    scal = [float(rec.get(k, float('nan'))) for k in ('coord_scale', 'turn_scale', 'turn_bias', 'turn')]
    for v in scal + [x for lane in lanes for x in lane]:
        if not math.isfinite(v):
            raise SlwaError('animation record carries a value that is not finite')
    if not (scal[0] > 0.0) or scal[3] == 0.0:
        raise SlwaError('animation record has no coordinate scale or no turn')
    return (struct.pack('<IfII', ANIM_SINEWAVES, scal[0], ANIM_WAVES, 0)
            + b''.join(struct.pack('<4f', *lane) for lane in lanes[:5])
            + struct.pack('<ffff', scal[1], scal[2], scal[3], 0.0)
            + struct.pack('<4f', *lanes[5]))


def pack_matx(rec, ntex):
    """One material-extension record (the dict xblaworld.material_record
    returns, or None) -> its MATX_SIZE bytes, or SlwaError when it is not one
    the runtime evaluates. `ntex` bounds the image references."""
    if rec is None:
        return b'\0' * MATX_SIZE
    if not isinstance(rec, dict) or rec.get('mode') != 'normal_sphere':
        raise SlwaError('material record %r is not a normal-sphere record' % (rec,))
    texs = []
    for k in ('normal', 'sphere', 'offset'):
        t = rec.get(k)
        if not isinstance(t, int) or not (0 <= t < ntex):
            raise SlwaError('material record names %s image %r, of %d' % (k, t, ntex))
        texs.append(t)
    vecs = []
    for k, n in (('base_weight', 3), ('reflect', 3), ('sphere_scale', 2), ('sphere_bias', 2)):
        v = rec.get(k)
        if not isinstance(v, (tuple, list)) or len(v) != n:
            raise SlwaError('material record field %r is not %d wide' % (k, n))
        vecs.append([float(x) for x in v])
    scal = [float(rec.get(k, float('nan'))) for k in
            ('warp', 'normal_scale', 'normal_bias', 'mean_weight', 'alpha_floor')]
    for v in scal + [x for vec in vecs for x in vec]:
        if not math.isfinite(v):
            raise SlwaError('material record carries a value that is not finite')
    if scal[1] == 0.0:
        raise SlwaError('material record scales the normal map by zero')
    flags = MATX_SPHERE_CLAMP if rec.get('sphere_clamp') else 0
    return (struct.pack('<IIIII', MATX_NORMAL_SPHERE, texs[0], texs[1], texs[2], flags)
            + struct.pack('<3f', *vecs[0]) + struct.pack('<3f', *vecs[1])
            + struct.pack('<fff', scal[0], scal[1], scal[2])
            + struct.pack('<2f', *vecs[2]) + struct.pack('<2f', *vecs[3])
            + struct.pack('<ff', scal[3], scal[4]) + b'\0' * 16)


def write(path, asset_class, item_path, vertices, indices, materials,
          textures, meshes, aux=None, anims=None, frames=None, matx=None):
    """Write one bundle.

    vertices  [(x, y, z, u, v, r, g, b, a), ...]
    indices   [i, ...]                        a flat triangle list
    materials [(texture_index, blend) | (texture_index, blend, flags), ...]
    textures  [(w, h, rgba_bytes), ...]       rows top first, w*h*4 bytes
    meshes    [(material_index, first_index, index_count), ...]
    aux       [(wave_a, wave_b, nx, ny, nz, anim), ...]  one per vertex, or
              None; `anim` 0 or a 1-based index into `anims`
    anims     [record, ...]   see pack_anim; None or empty writes a
              VERSION_STATIC bundle with no auxiliary section
    frames    [(xx, xy, xz, yx, yy, yz), ...]  one per vertex, the tangent
              frame, signed-normalised; required with `matx`, else None
    matx      [record or None, ...]  one per material, see pack_matx; None
              or all-None writes the version-2 (or version-1) bundle the
              same call wrote before the extension existed

    Every limit the runtime enforces is enforced here too, so a bundle this
    writer produces cannot be one the parser rejects - and the self-test
    asserts exactly that by parsing what it writes.
    """
    if asset_class not in CLASSES:
        raise SlwaError('asset class %r is not one this format carries' % asset_class)
    if not item_path or len(item_path) >= PATH_MAX:
        raise SlwaError('item path %r does not fit the field' % item_path)
    for c in item_path:
        if not (c.islower() and c.isalpha()) and not c.isdigit() and c not in '_-':
            raise SlwaError('item path %r carries %r, which the format does not allow'
                            % (item_path, c))
    if not (0 < len(vertices) <= MAX_VERTICES):
        raise SlwaError('%d vertices is outside 1..%d' % (len(vertices), MAX_VERTICES))
    if not (0 < len(indices) <= MAX_INDICES) or len(indices) % 3:
        raise SlwaError('%d indices is not a non-zero multiple of 3 within %d'
                        % (len(indices), MAX_INDICES))
    if not (0 < len(materials) <= MAX_MATERIALS):
        raise SlwaError('%d materials is outside 1..%d' % (len(materials), MAX_MATERIALS))
    for m in materials:
        if len(m) not in (2, 3):
            raise SlwaError('material %r is neither (texture, blend) nor '
                            '(texture, blend, flags)' % (m,))
    if len(textures) > MAX_TEXTURES:
        raise SlwaError('%d textures is past %d' % (len(textures), MAX_TEXTURES))
    if not (0 < len(meshes) <= MAX_MESHES):
        raise SlwaError('%d meshes is outside 1..%d' % (len(meshes), MAX_MESHES))

    for i in indices:
        if not (0 <= i < len(vertices)):
            raise SlwaError('index %d is outside the vertex array' % i)
    # A material is (texture, blend) or (texture, blend, flags). The two-field
    # form is what every caller wrote before the decal flag existed and it
    # still means exactly what it did: no flags.
    mats = [(m[0], m[1], m[2] if len(m) > 2 else 0) for m in materials]
    for ti, blend, flags in mats:
        if not (0 <= ti < len(textures)):
            raise SlwaError('material names texture %d, of %d' % (ti, len(textures)))
        if blend not in BLENDS:
            raise SlwaError('blend mode %r is not one the runtime knows' % blend)
        if flags & ~MAT_KNOWN:
            raise SlwaError('material flags %#x carry a bit this format does not '
                            'define' % flags)
        if flags & MAT_GENERATED and asset_class != CLASS_SKYDOME:
            raise SlwaError('a GENERATED material belongs to a skydome only')
        if flags & MAT_COVERAGE_BLUE and blend != BLEND_ALPHA:
            raise SlwaError('a COVERAGE_BLUE material is an ALPHA-blended one only')
        if flags & MAT_PANORAMA and asset_class != CLASS_BACKGROUND:
            raise SlwaError('a PANORAMA material belongs to a background only')
        if flags & MAT_CLOUD_PLANE and (asset_class != CLASS_SKYDOME
                                        or not flags & MAT_GENERATED):
            raise SlwaError('a CLOUD_PLANE material is a GENERATED skydome one only')
        if flags & MAT_CULL_BACK and asset_class != CLASS_BACKGROUND:
            raise SlwaError('a CULL_BACK material belongs to a background only')
        if flags & MAT_GAME_WATER and asset_class != CLASS_BACKGROUND:
            raise SlwaError('a GAME_WATER material belongs to a background only')
    for w, h, rgba in textures:
        if not (0 < w <= MAX_TEX_DIM and 0 < h <= MAX_TEX_DIM):
            raise SlwaError('%dx%d is outside 1..%d a side' % (w, h, MAX_TEX_DIM))
        if len(rgba) != w * h * 4:
            raise SlwaError('%dx%d wants %d bytes, got %d' % (w, h, w * h * 4, len(rgba)))
    for mi, first, count in meshes:
        if not (0 <= mi < len(materials)):
            raise SlwaError('mesh names material %d, of %d' % (mi, len(materials)))
        if count <= 0 or count % 3 or first < 0 or first + count > len(indices):
            raise SlwaError('mesh range %d+%d is not a triangle range inside %d indices'
                            % (first, count, len(indices)))

    # THE ANIMATION SECTIONS, both or neither. A record nothing references is
    # still carried (the table is what the runtime validates against); an aux
    # entry naming a record past the table cannot be, and a bundle with
    # records but no per-vertex fields has nothing to move.
    anims = list(anims or [])
    if len(anims) > MAX_ANIMS:
        raise SlwaError('%d animation records is past %d' % (len(anims), MAX_ANIMS))
    if anims and aux is None:
        raise SlwaError('animation records need the per-vertex fields')
    if aux is not None and not anims:
        raise SlwaError('per-vertex animation fields need a record table')
    # THE MATERIAL EXTENSION (version 3): a table with exactly one entry per
    # material, present when any material carries a record, and then the
    # per-vertex tangent frame beside every auxiliary entry - a record with
    # no frame has nothing to shade with, and a frame with no record is
    # weight nobody reads.
    matx = list(matx or [])
    material = any(r is not None for r in matx)
    if material and len(matx) != len(materials):
        raise SlwaError('%d material records for %d materials' % (len(matx), len(materials)))
    if material and frames is None:
        raise SlwaError('material records need the per-vertex tangent frame')
    if frames is not None and not material:
        raise SlwaError('per-vertex tangent frames need a material record')
    if material and frames is not None and len(frames) != len(vertices):
        raise SlwaError('%d tangent frames for %d vertices' % (len(frames), len(vertices)))
    mxb = b''.join(pack_matx(r, len(textures)) for r in matx) if material else b''
    nb = b''.join(pack_anim(r) for r in anims)
    xb = b''
    if aux is not None or material:
        if aux is None:
            aux = [(0, 0, 0.0, 0.0, 0.0, 0)] * len(vertices)
        if len(aux) != len(vertices):
            raise SlwaError('%d auxiliary entries for %d vertices' % (len(aux), len(vertices)))
        rows = []
        for i, e in enumerate(aux):
            if len(e) != 6:
                raise SlwaError('auxiliary entry %r is not (wave_a, wave_b, nx, ny, nz, anim)' % (e,))
            wa, wb, nx, ny, nz, ai = e
            if not (-32768 <= int(wa) <= 32767 and -32768 <= int(wb) <= 32767):
                raise SlwaError('wave pair %r does not fit 16 bits' % ((wa, wb),))
            if not all(math.isfinite(c) and -1.0 <= c <= 1.0 for c in (nx, ny, nz)):
                raise SlwaError('direction %r is not signed-normalised' % ((nx, ny, nz),))
            if not (0 <= int(ai) <= len(anims)):
                raise SlwaError('auxiliary entry names animation %d, of %d' % (ai, len(anims)))
            row = struct.pack('<hhhhhH', int(wa), int(wb),
                              int(round(nx * 32767.0)), int(round(ny * 32767.0)),
                              int(round(nz * 32767.0)), int(ai))
            if material:
                fr = frames[i]
                if len(fr) != 6:
                    raise SlwaError('tangent frame %r is not (xx, xy, xz, yx, yy, yz)' % (fr,))
                if not all(math.isfinite(c) and -1.0 <= c <= 1.0 for c in fr):
                    raise SlwaError('tangent frame %r is not signed-normalised' % (fr,))
                row += struct.pack('<hhhhhh', *[int(round(c * 32767.0)) for c in fr])
            rows.append(row)
        xb = b''.join(rows)

    lo = [min(v[k] for v in vertices) for k in range(3)]
    hi = [max(v[k] for v in vertices) for k in range(3)]

    vb = b''.join(struct.pack('<fffffBBBB', *v) for v in vertices)
    ib = struct.pack('<%dI' % len(indices), *indices)
    mb = b''.join(struct.pack('<IIII', t, b_, fl, 0) for t, b_, fl in mats)
    hb = b''.join(struct.pack('<IIII', m, f, c, 0) for m, f, c in meshes)

    v_off = HEADER
    i_off = v_off + len(vb)
    m_off = i_off + len(ib)
    t_off = m_off + len(mb)
    h_off = t_off + len(textures) * TEXTURE_SIZE
    x_off = h_off + len(hb)              # the auxiliary section, if any
    n_off = x_off + len(xb)              # the animation table, if any
    mx_off = n_off + len(nb)             # the material extension, if any
    p_off = mx_off + len(mxb)
    p_off += _pad4(p_off)

    tb = b''
    payload = b''
    off = p_off
    for w, h, rgba in textures:
        tb += struct.pack('<IIII', w, h, off, len(rgba))
        payload += rgba
        off += len(rgba)
    total = off
    if total > MAX_FILE:
        raise SlwaError('%d bytes is past the %d-byte limit' % (total, MAX_FILE))

    hdr = bytearray(HEADER)
    hdr[0:4] = MAGIC
    struct.pack_into('<I', hdr, 4, VERSION if material
                     else (VERSION_ANIMATED if anims else VERSION_STATIC))
    struct.pack_into('<I', hdr, 8, asset_class)
    name = item_path.encode('ascii')
    hdr[12:12 + len(name)] = name
    struct.pack_into('<I', hdr, 44, mx_off if material else 0)
    struct.pack_into('<fff', hdr, 48, *lo)
    struct.pack_into('<fff', hdr, 60, *hi)
    struct.pack_into('<I', hdr, 72, len(vertices))
    struct.pack_into('<I', hdr, 76, len(indices))
    struct.pack_into('<I', hdr, 80, len(materials))
    struct.pack_into('<I', hdr, 84, len(textures))
    struct.pack_into('<I', hdr, 88, len(meshes))
    struct.pack_into('<I', hdr, 92, v_off)
    struct.pack_into('<I', hdr, 96, i_off)
    struct.pack_into('<I', hdr, 100, m_off)
    struct.pack_into('<I', hdr, 104, t_off if textures else HEADER)
    struct.pack_into('<I', hdr, 108, h_off)
    struct.pack_into('<I', hdr, 112, total)
    if anims or material:
        struct.pack_into('<I', hdr, 116, x_off)
    if anims:
        struct.pack_into('<I', hdr, 120, n_off)
        struct.pack_into('<I', hdr, 124, len(anims))

    blob = (bytes(hdr) + vb + ib + mb + tb + hb + xb + nb + mxb
            + b'\0' * (p_off - mx_off - len(mxb)) + payload)
    if len(blob) != total:
        raise SlwaError('internal: laid out %d bytes, declared %d' % (len(blob), total))

    # Written through a temporary and renamed, so a bundle is only ever
    # complete or unchanged - the same rule the texture packs follow.
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(blob)
    os.replace(tmp, path)
    return total


# ---- VERSION 4: the SKINNED character / head item (#70) --------------------
#
# A character or head item is drawn deformed by the game's own skeleton, so
# on top of the version-1 tables it carries, mirrored in
# src/gfx/sl_gfx_worldasset.h:
#
#   skin   (8 per vertex, at header +116)
#          u8 joint[4], u8 weight[4]   up to four influences; the weights sum
#                                      to 255 and an unused lane is joint 0,
#                                      weight 0
#   joints (48 each, at header +120, the count at +124, 1..MAX_JOINTS)
#          char name[24]               "SKEL_..." NUL-terminated, the joint's
#                                      own name in the source; [A-Z0-9_]
#          u32 parent                  0xFFFFFFFF for the root, else an
#                                      earlier joint
#          f32 bind[3]                 the joint's bind position in item space
#          u32 reserved[2]             0
#
# and one mesh flag, MESH_BUILTIN_HEAD: the draw is the body's own head,
# drawn only when the character wears no separate head item. It is a BODY's
# mark: a head item is the worn head, so none of its draws carries it.
# Version 4 is written for these two classes only and they are written only
# as version 4; the header word at +44 stays 0 (no material extension).
#
# CLASS_GUN (#70 view models) is the same version-4 layout for a FIRST-PERSON
# weapon item - the gun and the hand that holds it, one unit - bound to the
# game's own view-model skeleton. It gives two fields a meaning they have on
# no other class:
#
#   joint  u32 reserved[0]   the joint's GAME PART: 0 for a joint the game
#                            positions no part for (the root joint rides the
#                            model's root), else 1 + the index of the game's
#                            own view-model switch that joint is (FINGER,
#                            SLIDE, ... - the model file's Switches[] table)
#   mesh   flags bits 8..15  a SWITCH CONDITION: 1 + the index of a game
#          flags bits 16..23 switch the draw stands under, 0 for none; the
#                            draw is drawn only while every switch it names
#                            is visible
#   material u32 +12         0, or 1 + the index of the material's
#                            REFLECTION image (a metal weapon's environment
#                            image); 0 on every other class
#
# and the built-in-head mark does not apply to it.
VERSION_SKINNED = 4
CLASS_CHAR = 3
CLASS_HEAD = 4
CLASS_GUN = 5
# CLASS_PROP (#71): a replacement for one of the game's RIGID prop models,
# the same version-4 layout with one root joint and every vertex on it; the
# header's bounds carry the REFERENCE box (write_skinned `bounds`), the only
# relation a skeleton-less item has to the game's model.
CLASS_PROP = 6
SKINNED_CLASSES = (CLASS_CHAR, CLASS_HEAD, CLASS_GUN, CLASS_PROP)
MESH_SWITCH_A_SHIFT = 8
MESH_SWITCH_B_SHIFT = 16
MESH_SWITCH_MASK = 0xff
MAX_SWITCH = 64                    # game switch indices 0 .. 63
SKIN_SIZE = 8
JOINT_SIZE = 48
JOINT_NAME = 24
MAX_JOINTS = 32
SKIN_LANES = 4
MESH_BUILTIN_HEAD = 1
# MESH_OWN_HEAD: a BODY item with no place to wear a separate head, which
# draws the head it carries always - on every mesh of such an item or on
# none, never beside MESH_BUILTIN_HEAD (src/gfx/sl_gfx_worldasset.h).
MESH_OWN_HEAD = 2
# THE WATCH HAND (#70, CLASS_GUN): a joint may name the model's MATRIX its
# part is drawn under (PART_MATRIX + 1 + matrix), and a draw may be one of the
# item's ALTERNATIVE costume pieces (MESH_ALT), gated or not on the cuff it
# serves (src/gfx/sl_gfx_worldasset.h). A PROP's joint may name a matrix the
# same way (a vehicle's wheel, #71) - never a Switches[] part.
PART_MATRIX = 0x100
MAX_PART_MATRIX = 64
MESH_ALT = 0x01000000
JOINT_ROOT = 0xFFFFFFFF


def part_matrix(m):
    """A CLASS_GUN or CLASS_PROP joint's part word naming model matrix `m`."""
    if not isinstance(m, int) or not (0 <= m < MAX_PART_MATRIX):
        raise SlwaError('model matrix %r is outside 0..%d' % (m, MAX_PART_MATRIX - 1))
    return PART_MATRIX + 1 + m


def mesh_switch_flags(switches):
    """A CLASS_GUN mesh's flags word for the game switches (indices) its draw
    stands under - none, one or two. SlwaError for more, or for an index the
    format cannot carry."""
    switches = list(switches)
    if len(switches) > 2:
        raise SlwaError('a draw under %d game switches; the format carries two' % len(switches))
    f = 0
    for sh, s in zip((MESH_SWITCH_A_SHIFT, MESH_SWITCH_B_SHIFT), switches):
        if not isinstance(s, int) or not (0 <= s < MAX_SWITCH):
            raise SlwaError('game switch %r is outside 0..%d' % (s, MAX_SWITCH - 1))
        f |= (s + 1) << sh
    return f


def write_skinned(path, asset_class, item_path, vertices, indices, materials,
                  textures, meshes, skin, joints, reflections=None, bounds=None):
    """Write one version-4 character, head or view-model bundle.

    reflections  CLASS_GUN or CLASS_PROP only: one per material, None or the
                 index of the material's REFLECTION image, written as 1 +
                 index in the material record's last word (0 - no reflection
                 - everywhere else, and in every bundle written without them)
    bounds       CLASS_PROP (required) or a one-joint CLASS_GUN - a STATIC
                 view model: ((x0, y0, z0), (x1, y1, z1)), the REFERENCE box
                 written as the header's bounds - the box of the source's own
                 generation of the game's model (a static item has no
                 skeleton to say where the model sits; a prop whose joints
                 name the model's matrices is placed by it too)

    vertices, indices, materials, textures as `write`;
    meshes  [(material_index, first_index, index_count, flags), ...]
            flags 0 or MESH_BUILTIN_HEAD (a body's only); on CLASS_GUN,
            mesh_switch_flags(...), with MESH_ALT on an alternative sleeve
    skin    [((joint, weight), ...), ...]  one per vertex, 1..4 pairs,
            weights summing to 255
    joints  [(name, parent, (x, y, z)), ...]  parent -1 for the root; on
            CLASS_GUN (name, parent, (x, y, z), part), part 0, 1 + a game
            switch index below MAX_SWITCH, or part_matrix(m) (the watch hand);
            on CLASS_PROP either form, part 0 or part_matrix(m)

    Every limit the runtime enforces is enforced here, as `write` does."""
    if asset_class not in SKINNED_CLASSES:
        raise SlwaError('asset class %r is not a skinned class' % asset_class)
    if asset_class == CLASS_PROP and bounds is None:
        raise SlwaError('a prop needs its reference box')
    if bounds is not None and asset_class not in (CLASS_PROP, CLASS_GUN):
        raise SlwaError('a reference box belongs to a prop or a static view model')
    if bounds is not None:
        if len(bounds) != 2 or any(len(c) != 3 or not all(math.isfinite(v) for v in c)
                                   for c in bounds) \
                or any(bounds[0][k] > bounds[1][k] for k in range(3)):
            raise SlwaError('the reference box %r is not two ordered corners' % (bounds,))
        moving = asset_class == CLASS_PROP and any(
            len(jt) == 4 and jt[3] != 0 for jt in joints)
        if len(joints) != 1 and not moving:
            raise SlwaError('an item placed by a reference box is one rigid piece on one joint')
    if reflections is not None:
        if asset_class not in (CLASS_GUN, CLASS_PROP):
            raise SlwaError('a reflection image belongs to a view model or a prop only')
        if len(reflections) != len(materials):
            raise SlwaError('%d reflections for %d materials' % (len(reflections), len(materials)))
        for r in reflections:
            if r is not None and not (isinstance(r, int) and 0 <= r < len(textures)):
                raise SlwaError('a reflection names image %r, of %d' % (r, len(textures)))
    if not (1 <= len(joints) <= MAX_JOINTS):
        raise SlwaError('%d joints is outside 1..%d' % (len(joints), MAX_JOINTS))
    if len(skin) != len(vertices):
        raise SlwaError('%d skin entries for %d vertices' % (len(skin), len(vertices)))
    jb = b''
    for j, jt in enumerate(joints):
        if asset_class == CLASS_GUN:
            if len(jt) != 4:
                raise SlwaError('view-model joint %r is not (name, parent, bind, part)' % (jt,))
            name, parent, bind, part = jt
            if not isinstance(part, int) or not (0 <= part <= MAX_SWITCH
                                                 or PART_MATRIX < part <= PART_MATRIX + MAX_PART_MATRIX):
                raise SlwaError('joint %d names game part %r, outside 0..%d and the model '
                                'matrices' % (j, part, MAX_SWITCH))
        elif asset_class == CLASS_PROP and len(jt) == 4:
            name, parent, bind, part = jt
            if not isinstance(part, int) or not (
                    part == 0 or PART_MATRIX < part <= PART_MATRIX + MAX_PART_MATRIX):
                raise SlwaError('prop joint %d names game part %r; a prop joint names a '
                                'model matrix or nothing' % (j, part))
        else:
            if len(jt) != 3:
                raise SlwaError('joint %r is not (name, parent, bind)' % (jt,))
            name, parent, bind = jt
            part = 0
        if not name or len(name) >= JOINT_NAME or \
                not all(c.isupper() or c.isdigit() or c == '_' for c in name):
            raise SlwaError('joint name %r does not fit the field' % (name,))
        if parent != -1 and not (0 <= parent < j):
            raise SlwaError('joint %d names parent %r, not an earlier joint' % (j, parent))
        if len(bind) != 3 or not all(math.isfinite(c) for c in bind):
            raise SlwaError('joint %d bind %r is not three finite numbers' % (j, bind))
        jb += (name.encode('ascii').ljust(JOINT_NAME, b'\0')
               + struct.pack('<I3fII', JOINT_ROOT if parent == -1 else parent,
                             float(bind[0]), float(bind[1]), float(bind[2]), part, 0))
    sb = []
    for e in skin:
        if not (1 <= len(e) <= SKIN_LANES):
            raise SlwaError('a vertex has %d influences' % len(e))
        js = [0] * SKIN_LANES
        ws = [0] * SKIN_LANES
        for k, (j, w) in enumerate(e):
            if not (0 <= j < len(joints)) or not (0 < w <= 255):
                raise SlwaError('influence %r is not a joint and a weight' % ((j, w),))
            js[k], ws[k] = j, w
        if sum(ws) != 255:
            raise SlwaError('influence weights sum to %d, not 255' % sum(ws))
        sb.append(struct.pack('<8B', *(js + ws)))
    sb = b''.join(sb)

    # The shared tables through the version-1 writer's own checks: write the
    # bundle once as version 1 into memory, then lay the skin and joint
    # sections after its tables.
    base_meshes = []
    flags = []
    for m in meshes:
        if len(m) != 4:
            raise SlwaError('mesh %r is not (material, first, count, flags)' % (m,))
        if asset_class == CLASS_GUN:
            known = (MESH_SWITCH_MASK << MESH_SWITCH_A_SHIFT) | (MESH_SWITCH_MASK << MESH_SWITCH_B_SHIFT) \
                | MESH_ALT
            if m[3] & ~known:
                raise SlwaError('view-model mesh flags %#x carry a bit this format does not '
                                'define' % m[3])
            for sh in (MESH_SWITCH_A_SHIFT, MESH_SWITCH_B_SHIFT):
                if ((m[3] >> sh) & MESH_SWITCH_MASK) > MAX_SWITCH:
                    raise SlwaError('mesh flags %#x name a switch past %d' % (m[3], MAX_SWITCH))
        elif m[3] & ~(MESH_BUILTIN_HEAD | (MESH_OWN_HEAD if asset_class == CLASS_CHAR else 0)):
            raise SlwaError('mesh flags %#x carry a bit this format does not define' % m[3])
        elif m[3] == MESH_BUILTIN_HEAD | MESH_OWN_HEAD:
            raise SlwaError('a draw cannot be a built-in head of an item that carries its own')
        if (m[3] & MESH_BUILTIN_HEAD) and asset_class == CLASS_HEAD:
            raise SlwaError('a head item marks a draw as a body\'s built-in head; '
                            'the runtime would never draw it')
        base_meshes.append((m[0], m[1], m[2]))
        flags.append(m[3])
    if asset_class == CLASS_CHAR and len({f & MESH_OWN_HEAD for f in flags}) > 1:
        raise SlwaError('an item carries its own head on every mesh or on none')
    tmp = path + '.v1.tmp'
    write(tmp, CLASS_SKYDOME, item_path, vertices, indices, materials, textures,
          base_meshes)
    with open(tmp, 'rb') as f:
        blob = bytearray(f.read())
    os.remove(tmp)

    t_off = struct.unpack_from('<I', blob, 104)[0]
    h_off = struct.unpack_from('<I', blob, 108)[0]
    ntex = struct.unpack_from('<I', blob, 84)[0]
    tables_end = h_off + len(meshes) * MESH_SIZE
    payload_at = tables_end + _pad4(tables_end)
    payload = bytes(blob[payload_at:])
    head = blob[:tables_end]
    for i, fl in enumerate(flags):
        struct.pack_into('<I', head, h_off + i * MESH_SIZE + 12, fl)
    if reflections is not None:
        m_off = struct.unpack_from('<I', blob, 100)[0]
        for i, r in enumerate(reflections):
            if r is not None:
                struct.pack_into('<I', head, m_off + i * MATERIAL_SIZE + 12, r + 1)
    if bounds is not None:
        struct.pack_into('<fff', head, 48, *[float(v) for v in bounds[0]])
        struct.pack_into('<fff', head, 60, *[float(v) for v in bounds[1]])

    s_off = tables_end
    j_off = s_off + len(sb)
    p_off = j_off + len(jb)
    p_off += _pad4(p_off)
    shift = p_off - payload_at
    for i in range(ntex):
        o = t_off + i * TEXTURE_SIZE + 8
        struct.pack_into('<I', head, o, struct.unpack_from('<I', head, o)[0] + shift)
    total = p_off + len(payload)
    if total > MAX_FILE:
        raise SlwaError('%d bytes is past the %d-byte limit' % (total, MAX_FILE))
    struct.pack_into('<I', head, 4, VERSION_SKINNED)
    struct.pack_into('<I', head, 8, asset_class)
    struct.pack_into('<I', head, 112, total)
    struct.pack_into('<I', head, 116, s_off)
    struct.pack_into('<I', head, 120, j_off)
    struct.pack_into('<I', head, 124, len(joints))
    out = bytes(head) + sb + jb + b'\0' * (p_off - j_off - len(jb)) + payload
    if len(out) != total:
        raise SlwaError('internal: laid out %d bytes, declared %d' % (len(out), total))
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(out)
    os.replace(tmp, path)
    return total
