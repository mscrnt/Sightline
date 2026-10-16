"""Read ONE world/item asset out of a user-supplied CAFF item bundle (#68).

A separate module from the texture pipeline on purpose. `tools/texpack/caff.py`
already reads this CONTAINER and `tools/texpack/xdecode.py` already decodes its
TEXTURE records - both abstractions apply here unchanged and are reused rather
than copied - but a texture tool has no business describing geometry, so the
mesh reading lives here and nothing in tools/texpack was bent to accommodate it.

WHAT A BUNDLE HOLDS, measured on the first item this reads:

  header | .gpu | .data | .stream

  * the resource table names a `.data` record and a `.gpu` blob under the
    bundle's own resource name - the MODEL - plus one `.data` record and one
    `.gpu` blob per texture;
  * the `.data` model record is a node graph (its first field is the literal
    "rendergraph") carrying the asset's bounding sphere and AABB, the vertex
    stride, the buffer byte lengths and the shader constant tables. The AABB it
    declares agrees to the float with the one computed from the vertices below,
    which is the cross-check that says these are the right bytes;
  * the `.gpu` model blob is the vertex and index data itself, laid out as
    alternating runs - a vertex buffer, then the index buffer that reads it -
    followed by shader microcode.

THE VERTEX RECORD, 24 bytes, big-endian, measured:

  +0   f32 x, f32 y, f32 z     absolute position, in the ORIGINAL game's own
                               world units and axes (measured: the remastered
                               world bundle for the same item spans the same
                               coordinate range as the original level, so there
                               is no scale and no axis change to apply)
  +12  u32                     a packed field. NOT a texture coordinate: read
                               as a coordinate pair it jumps by tens of
                               thousands between the corners of one triangle,
                               where a real one varies smoothly. Carried by
                               neither this reader nor the runtime format.
  +16  u16 u, u16 v            the TEXTURE COORDINATE, as 16-bit FIXED POINT
                               with 14 fractional bits: the value is the raw
                               integer times 1/16384.
  +20  u32                     the vertex colour, BYTE-REVERSED like every
                               other field in this record: a, b, g, r on disk.
                               See `vertex_colour` for the endian contract and
                               for the measurement the game itself supplied.

WHY FIXED POINT AND NOT HALF FLOAT. This field was first read as two IEEE
binary16 floats, on the evidence that the result was continuous across
triangles. That test does not discriminate: binary16 is monotone in the
unsigned bit pattern over the positive range, so ANY smooth fixed-point sweep
also reads as a smooth half-float sweep. Three measurements settle it, and
all three are arithmetic on the bytes rather than a reading of code:

  * RANGE. On the first item's dome mesh the raw u16 span is exactly 16384
    units on BOTH axes (0x1ea8..0x5ea8 and 0x004f..0x4050). Times 1/16384
    that is exactly 1.0 of texture on each axis - what a dome that wraps once
    must have. Read as binary16 the same bytes give a U of 0.0065 .. 426.0,
    which would wrap the image 426 times around the sky.
  * ANCHOR. On the second mesh the raw maxima are 0x3fff and 0x4000. Only a
    divisor of 16384 makes 0x4000 the exact 1.0 those coordinates are laid
    out against; 32767 and 65535 both leave it a fraction.
  * GEOMETRY. Taking 1/16384, the dome's U is a linear function of the
    azimuth about its own axis with a residual spread of 0.00005 of a turn
    over the 5th..95th percentile, and its V tracks elevation. A parameter
    that reproduces the model's own angles to five decimals is the parameter
    the model was authored with.

The half-float reading is what produced the radial streaks, the smeared bands
and the moire contour fan the first attempt at this asset drew: a texture
coordinate two orders of magnitude too large is minification far past what any
filter can carry.

The shader constant tables in the same bundle name only c_ambient, c_constant0,
c_fog, c_fogcolour, c_fogtype, c_light0colour, c_light0dir and s_sampler0 for
the pixel stage and c_modelview / c_modelviewprojection for the vertex stage.
There is NO uv scale, offset or matrix constant, so the coordinate above is the
coordinate the sampler receives, and no material transform is missing.

Indices are unsigned 16-bit big-endian, a triangle LIST.

HOW THE MESHES ARE FOUND. From the bundle's own records, not from the order
things happen to appear in:

  * the item's '.stream' record is a COMMAND LIST, and it is consumed by its
    own record framing rather than searched for a byte pattern - see
    `command_records`. A draw is opcode 0x01 carrying a primitive TYPE, an
    element COUNT and an index buffer HANDLE. Their order in the stream is the
    order the item draws in;
  * the '.data' rendergraph carries the buffer table. An index buffer is
    (handle, byte offset, byte length, kind) and a vertex buffer is (stride,
    handle, byte offset, byte length), both into the model '.gpu' blob.

For a ONE-ITEM bundle (the skydome) each draw is resolved through that table by
`buffer_ranges`, and the result is required to agree with what the blob itself
decomposes into (`split_meshes`, kept as the cross-check it always was). A
bundle whose records and whose bytes disagree is REFUSED, not guessed at.

A LEVEL bundle breaks both of the one-item reader's assumptions, and
`read_level` below is the reader for it. See its docstring: many primitive
types, a SHARED vertex pool with many index buffers into it, 16- and 32-bit
index buffers, and a per-draw material.

This module parses a file the USER supplies. It contains no source bytes, no
identifier of any particular source, and no way to obtain one.
"""
import math
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'texpack'))

import caff        # noqa: E402  the container reader, reused unchanged
import xdecode     # noqa: E402  the texture decoder, reused unchanged

VERTEX_STRIDE = 24
MIN_RUN = 16          # a run shorter than this is noise, not a mesh
MAX_COORD = 1.0e6

# The texture coordinate's fixed-point scale: 16-bit with 14 fractional bits,
# so 0x4000 is exactly 1.0 of texture. Measured - see the module docstring.
TEXCOORD_SHIFT = 14
TEXCOORD_ONE = 1 << TEXCOORD_SHIFT
TEXCOORD_SCALE = 1.0 / TEXCOORD_ONE

# The draw command in an item's '.stream' command list, as the ONE-ITEM reader
# below matches it: this word, then a 4, then the index count and the index
# buffer's handle. Structurally it is (length 0x10) << 16 | (opcode 1) << 8,
# followed by the primitive type - see `command_records`.
DRAW_OP = 0x00100100

# ---- the command list, read by its own framing ---------------------------
#
# MEASURED on both a one-item bundle and a level bundle: a '.stream' record is
#
#     word0 = (byte length of the WHOLE record) << 16 | opcode << 8 | low
#     then (length/4 - 1) argument words
#
# The record area begins at the offset the stream's own first header word
# names, and ends at the smallest other header word past it - the next section
# the stream carries. Walked that way the records consume the area EXACTLY on
# every bundle this has been run against, which is the check that says the rule
# is the format rather than a pattern that happens to fit.
OP_DRAW = 0x01        # primitive type, element count, index-buffer handle
OP_DRAW_GATED = 0x30  # the same three, then a condition word: see below
OP_STYLE = 0x15       # continuation offset, style word: see STYLE_DECAL
OP_MATERIAL = 0x2d    # a resource table: see `material_slots`

# THE VERTEX PROGRAM'S CONSTANTS, three records that scope together (#69).
#
# A material block binds its vertex program, then hands that program its
# constants, then draws. MEASURED on the Dam level item (1481 records):
#
#   op 0x2e  (next, vertex-buffer record, ?, ?, VERTEX PROGRAM record, ?)
#            binds the program - 46 records, one per variant of every block.
#   op 0x20  (0, 0, 0, RENDERGRAPH OFFSET, 0)  names the program's constant
#            DEFAULT block: at that offset + 4, float4 rows, one per register
#            from WAVE_MISC_REG up. 2 records in the item, both in the two
#            blocks whose program is the animated-water one.
#   op 0x03  (0, (register << 16) | count, count x 4 floats)  uploads vertex
#            constants. 6 records in the item - register 32, count 7, one per
#            variant of those same two blocks - and no other. (The 189 op-0x06
#            records are the PIXEL program's uploads, the c12/c16 pairs the
#            water round read; they are not consumed here.)
#
# THE SCOPE IS THE BINDING, not "standing until overwritten": the two small
# draws that follow the water in the stream (+0x668c, +0x6838) bind another
# program at op 0x2e with no upload of their own, and carrying the water's
# constants over to them would animate a surface the source never animates.
# So the state below is CLEARED at every op 0x2e and read at the draw.
OP_VS_BIND = 0x2e
OP_VS_DEFAULTS = 0x20
OP_VS_CONSTANTS = 0x03

# THE PIXEL PROGRAM'S CONSTANTS (#69 material): op 0x06 has the same framing
# as op 0x03 - (0, (register << 16) | count, count x 4 floats) - and sits in
# the same binding scope, after the material record and before the draw: on
# the Dam item every water variant uploads register 12, count 3 (its
# reflection weight, bump-environment scale and base weight - the names the
# program's own constant table gives c12..c14) and register 16, count 1. It
# is cleared at op 0x2e exactly as the vertex uploads are, and read at the
# draw. See material_record.
OP_PS_CONSTANTS = 0x06

# THE INSTANCE MATRIX (#80). A level item may draw one mesh many times, each
# time under a transform of its own - Surface's item draws its one tree (a
# 264-vertex canopy of alpha cutouts and a 23-vertex trunk) sixty-four times.
# MEASURED on that item (3537 records): every repeat but the first is
# bracketed
#
#   op 0x23  (0)  or  (0, 0, 0)
#   op 0x21  (rendergraph offset, 0, 0, 16 floats)   the matrix
#   op 0x01  the draw
#   op 0x25  (19 zero words)                          the same shape, cleared
#
# 126 of each, 63 per mesh, and the sixteen floats read as a row-vector
# affine matrix - rows 0..2 a rotation about y (24 distinct, every one
# orthonormal to four places), row 3 the translation with 1 in its last
# place (every record), the translations spanning the whole level. The
# first instance of each mesh is drawn with no record before it, at the
# pool's own coordinates. The Dam's, Facility's and Runway's items carry
# none of the three. A draw standing after an op-0x21 record carries its
# matrix (`xform`) until the op-0x25 record clears it; the preparation bakes
# the instance by transforming the pool's positions (prepare.py). The op-0x23
# record is not read.
OP_INSTANCE = 0x21
OP_INSTANCE_END = 0x25
INSTANCE_WORDS = 19

# THE LEVEL BUNDLE HAS TWO DRAW RECORDS, and only the first was being read.
#
#   op 0x01  (prim, count, index-buffer handle)
#   op 0x30  (prim, count, index-buffer handle, condition)
#   op 0x17  (condition, skip target)   the same condition over a BLOCK of
#                                       op-0x01 draws
#
# MEASURED on the Dam's level item, which carries two op-0x30 records (0x3d58:
# quad list, 112 indices; 0x4c54: quad list, 56) and two op-0x17 blocks, every
# condition word 0:
#   - their index-buffer handles sit in the item's own sequential handle chain
#     between their op-0x01 neighbours, and their indices address exactly the
#     vertices no op-0x01 draw references (pool 0's 45, pool 3's 30);
#   - that geometry is the Dam's EAST SHORE, and it is vertex-exact with the
#     op-0x17 blocks: the east-bank terrain's lower edge is the first op-0x30
#     strip's top row, its bottom row is the second strip's top row, and the
#     second strip's bottom row is the op-0x17 lake-bed pieces' edge;
#   - in two frames captured from the running remaster at the level start,
#     far from the reservoir, it withholds the two op-0x17 blocks and the two
#     op-0x30 draws and issues the item's other draws (one of the frames also
#     omits two small unrelated op-0x01 draws): the two forms are conditional
#     in the same way.
# This pipeline draws the op-0x17 blocks unconditionally (there is no such
# visibility here), so an op-0x30 draw is carried the same way. Leaving it out
# is what left the east shore open between the terrain and the water.
OP_BLOCK_GATED = 0x17

# THE STYLE WORD IS A BITFIELD, and only its low byte was being read.
#
# A draw stands under the last op-0x15 record before it, whose second argument
# is a style word. The Dam's level bundle uses eight of them:
#
#     0x2701 0x2702 0x2712   0x4701 0x4702 0x4711   0x4b01   0x6701
#
# The LOW byte is the blend the preparation already reads (1 opaque, 2
# translucent, plus a 0x10 bit). The HIGH half is a group, and it is a
# BITFIELD rather than an enumeration: 0x2000 and 0x4000 occur alone and
# together (0x6701 is exactly 0x2000 | 0x4701), which an enumeration could
# not do.
#
# WHAT 0x2000 SELECTS, measured rather than named - the source carries no
# string for it:
#
#   - by SIZE. The draws carrying it are 17 of 389, and 254 triangles of
#     90360. Everything else is 71066 triangles of world geometry under
#     0x4000 and 39044 triangles of foliage under 0x4000 | 0x0800.
#   - by SHAPE. Every one of them is a small quad-list patch, 2 to 198
#     triangles, and the ones that lie flat do so at a constant world y.
#   - by POSITION, which is the test that can fail. Cast the eight
#     playtest marks against the prepared geometry and collect every ray that
#     crosses two surfaces within 0.05 world units: 652 of 655 such rays have
#     EXACTLY ONE of the two in this group. The three that do not are two
#     ordinary world meshes that happen to touch (2 rays) and a foliage mesh
#     grazing a wall (1 ray).
#   - and the meshes it names are the ones earlier rounds had already found
#     by hand, one at a time, as coplanar overlays: mesh 58 over meshes 26
#     and 27, mesh 66 over mesh 2, and now mesh 70 over meshes 2 and 22.
#
# So this bit is the source's own DECAL PASS, and it is read per draw rather
# than derived from geometry, which is what keeps it a fact about the bundle
# instead of a heuristic about a picture.
STYLE_DECAL = 0x2000

# THE STYLE WORD IS A PASS GATE, and the list has two forms of it.
#
#   op 0x15  (skip target, mask)  a gate over a BLOCK: a walk whose pass does
#                                 not match `mask` continues at `skip target`
#   op 0x1a  (mask)               a gate over the REST of the list
#
# The second form is what stands at the head of a list and before a list's
# LAST block, which has nothing after it to skip to. MEASURED on both items:
# the skydome's head gate 0x4703 is exactly the union of its two blocks (0x4701
# and 0x4702), the level bundle's head gate 0x6f13 is exactly the union of every
# op-0x15 mask it carries, and the blocks nest properly (0x6711 wraps its 0x4701,
# 0x4711, 0x6701 and 0x2701 blocks). The LOW byte is the pass the gate admits:
# 1 opaque, 2 translucent.
OP_PASS_REST = 0x1a
PASS_OPAQUE = 0x01
PASS_TRANSLUCENT = 0x02

# THE FACE CULL (#96). Ops 0x0c and 0x0d write one render-state register each:
# (register, value). The registers met are 0x48 / 0x4c / 0x50 (blend 6 / 7 / 0,
# the translucent pass's SRC_ALPHA / ONE_MINUS_SRC_ALPHA), 0x60 / 0x64 / 0x68
# (alpha test on, reference 0x7f, function 4 - the cutout group), 0x148, 0x150
# / 0x154 - and 0x38, written before almost every draw. Over the sixteen
# accepted backgrounds and Caverns' every write of 0x38 is 0, except Control's:
# 102 writes of 0x10 standing over 9661 of its 15868 triangles (the rest under
# 0). MEASURED in the private emulator: the remaster's Control intro swirl
# shows the lift's inside where the game's camera stands behind the lift's back
# wall (that wall's draw stands under 0x10), while its Caverns intro, under 0,
# shows the wall's back face at the same moment - and the Dam's console frames
# carry PA_SU_SC_MODE_CNTL 0x4 (no cull) on every background draw under 0. So a
# nonzero 0x38 is read as the draw culling the face that turns away from the
# eye; which register value means which face is not decoded (one value, 0x10,
# occurs). level_draw_records carries the value standing over each draw.
OP_RENDER_STATE = (0x0c, 0x0d)
RS_CULL = 0x38


def draw_passes(recs):
    """{draw offset: mask of the innermost pass gate governing it} for every
    draw in a walked command list, from `command_records`.

    The innermost gate is the LAST gate record before the draw whose extent
    still contains it - an op-0x15 block ends at its skip target, an op-0x1a
    gate runs to the end of the list. Blocks nest, so the last containing one
    is the innermost. A draw no gate governs is absent from the answer."""
    out = {}
    gates = []                                   # (end offset or None, mask)
    for (o, op, _low, a) in recs:
        if op == OP_STYLE and len(a) >= 2:
            gates.append((a[0], a[1]))
        elif op == OP_PASS_REST and len(a) >= 1:
            gates.append((None, a[0]))
        elif op == OP_DRAW:
            for end, mask in reversed(gates):
                if end is None or o < end:
                    out[o] = mask
                    break
    return out

# Primitive types. All three occur in a level bundle; a one-item bundle uses
# only the first.
#
# ONE ENUMERATION, NOT TWO, AND THAT IS WHAT WAS WRONG HERE. Type 13 is a QUAD
# LIST, and a quad list exists only in the XBOX 360 extension of
# D3DPRIMITIVETYPE - desktop Direct3D 9 stops at 6. In that SAME table 5 is a
# TRIANGLE FAN and a triangle strip is 6, which this item never uses. The
# earlier revision took 13 from the Xbox table and 5 from the desktop one,
# where 5 is a strip, and expanded every type-5 draw as a ribbon. Taking two
# meanings from two tables is the defect; the item's own set of types (4, 5,
# 13 and never 6) says which table governs.
#
# WHY NO COUNT COULD CATCH IT. A fan of n elements and a strip of n elements
# both make n - 2 triangles, so `primitive_triangles` is identical either way
# and so is everything downstream of it: the index buffer still resolves by
# exact consumed length, every index still falls inside its pool, and the
# bounds computed from the vertices read still agree with the bounds the bundle
# declares for itself. The Dam bundle carries 90360 triangles under BOTH
# readings. Only the topology differs, so only the picture can see it.
#
# MEASURED, at playtest mark 1 (2026-09-23), casting the prepared
# geometry at the rays of a hole photographed in the playtest:
#
#   prim 5 as a strip   0 of 87 gap rays covered
#   prim 5 as a FAN    87 of 87 gap rays covered
#
# and the control that makes it a repair rather than a bigger blanket: of the
# 493 rays that already hit a surface, 493 still hit the SAME surface at the
# SAME distance, none moved, and none was lost. The source's ORIGINAL-
# generation model of the same item - a second, independent model - reproduces
# the identical result, 0 of 87 against 87 of 87 at the same triangle count.
PRIM_TRILIST = 4
PRIM_TRIFAN = 5
PRIM_QUADLIST = 13

# Index-buffer record kinds, measured: 1 is 16-bit, 6 is 32-bit. A pool of more
# than 65536 vertices necessarily uses the second, and the level bundle has one.
IB_16 = 1
IB_32 = 6

# The vertex strides a bundle may declare. Bounded so no length computed from
# one can be large, and so an accidental match is unlikely.
VERTEX_STRIDES = (12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 64)

# Hard ceilings, applied before any count is multiplied or any slice taken.
MAX_RECORDS = 1 << 20
MAX_ELEMENTS = 1 << 22
MAX_SLOTS = 16


def command_records(stream):
    """Walk one '.stream' command list by its record framing.

    Returns (records, end, complete) where each record is
    (offset, opcode, low, args) and `complete` says the walk landed exactly on
    the end the stream's own header names. Total: a malformed stream returns
    what was read and `complete` False rather than raising or reading past.
    """
    out = []
    n = len(stream)
    if n < 4:
        return out, 0, False
    start = struct.unpack('>I', stream[0:4])[0]
    if start < 4 or start > n or start % 4:
        return out, 0, False
    hdr = struct.unpack('>%dI' % (start // 4), stream[:start])
    end = n
    for w in hdr:
        if start < w <= n and w < end:
            end = w
    o = start
    while o + 4 <= end:
        w = struct.unpack('>I', stream[o:o + 4])[0]
        ln = (w >> 16) & 0xffff
        op = (w >> 8) & 0xff
        low = w & 0xff
        if ln < 4 or ln % 4 or o + ln > end or len(out) >= MAX_RECORDS:
            return out, o, False
        args = struct.unpack('>%dI' % (ln // 4 - 1), stream[o + 4:o + ln]) if ln > 4 else ()
        out.append((o, op, low, args))
        o += ln
    return out, o, o == end


def primitive_triangles(prim, count):
    """How many triangles a primitive of this type and element count makes, or
    None when the count cannot be one."""
    if count <= 0 or count > MAX_ELEMENTS:
        return None
    if prim == PRIM_TRILIST:
        return count // 3 if count % 3 == 0 else None
    if prim == PRIM_TRIFAN:
        return count - 2 if count >= 3 else None
    if prim == PRIM_QUADLIST:
        return (count // 4) * 2 if count % 4 == 0 else None
    return None


def expand_indices(prim, ind):
    """One primitive's element list -> a flat triangle list. Winding is the
    source's own: a fan turns about its first element, a quad is its two
    corner triangles.

    THE FAN IS A HUB, NOT A RIBBON. Element 0 is in every triangle, and the
    rest are the rim in order. Read as a ribbon instead, the same elements
    tile a zig-zag that leaves the rim's own area uncovered - which is the
    lens-shaped hole of sky playtest mark 1 photographed, bounded by the
    very draws that should have closed it. See PRIM_TRIFAN for the
    enumeration that settles it and for the numbers."""
    out = []
    n = len(ind)
    if prim == PRIM_TRILIST:
        for i in range(0, n - 2, 3):
            out.extend((ind[i], ind[i + 1], ind[i + 2]))
    elif prim == PRIM_QUADLIST:
        for i in range(0, n - 3, 4):
            out.extend((ind[i], ind[i + 1], ind[i + 2]))
            out.extend((ind[i], ind[i + 2], ind[i + 3]))
    elif prim == PRIM_TRIFAN:
        for i in range(1, n - 1):
            out.extend((ind[0], ind[i], ind[i + 1]))
    return out


def material_slots(args):
    """The op-0x2d resource record -> {sampler slot: image index}, or None.

    MEASURED shape, and it is read rather than guessed:

        ptr, hdr, [kvalue, hdr again], then (hdr >> 16) pairs of
        (image index, (sampler slot << 16) | flags)

    `hdr & 0xffff` is non-zero exactly when the K block is present. The image
    field is an INDEX into the bundle's own image list, not a resource name:
    the values run 0 .. 72 against 73 images, and a resource name would start
    at 3 and reach 75. That is what settles the two readings - the range, not
    which one looks more like an identifier.
    """
    if len(args) < 3:
        return None
    n = args[1] >> 16
    k = args[1] & 0xffff
    if n > MAX_SLOTS:
        return None
    i = 2
    if k:
        i += 2
    out = {}
    for _ in range(n):
        if i + 1 >= len(args):
            return None
        out[args[i + 1] >> 16] = args[i]
        i += 2
    return out


def material_slot_flags(args):
    """The op-0x2d resource record -> {sampler slot: flags}, the low half of
    the word beside each image index (material_slots reads the high half),
    or None. On the Dam item's water the sphere-map slot carries 0x8 and the
    other three carry 0 - see SLOT_FLAG_CLAMP."""
    if len(args) < 3:
        return None
    n = args[1] >> 16
    k = args[1] & 0xffff
    if n > MAX_SLOTS:
        return None
    i = 2
    if k:
        i += 2
    out = {}
    for _ in range(n):
        if i + 1 >= len(args):
            return None
        out[args[i + 1] >> 16] = args[i + 1] & 0xffff
        i += 2
    return out


class WorldAssetError(Exception):
    """The bundle is not one this reader understands."""


def texcoord(raw):
    """One 16-bit fixed-point texture coordinate -> float.

    Unsigned: every value measured on the first item lies in 0 .. 0x5ea8, and
    the axis that exceeds 1.0 is the dome's wrap, not a negative coordinate."""
    return raw * TEXCOORD_SCALE


def vertex_colour(b0, b1, b2, b3):
    """The four bytes of a vertex COLOUR word, in the order they lie on disk,
    -> (r, g, b, a). One place, because both readers need the same rule.

    THE WORD IS BYTE-REVERSED, like every other multi-byte field in this
    record. A Xenos vertex fetch converts memory to register order with the
    endian its fetch constant carries, and this buffer's is the one that makes
    a 32-bit float come out right: 8IN32, which reverses all four bytes
    (`GpuSwap`, Xenia canary_experimental c9c8e483). That is why the position
    reads '>fff' and the index buffers read big-endian. `k_8_8_8_8` then takes
    x, y, z, w from bits 0..7, 8..15, 16..23, 24..31 of the reversed word, so

        r, g, b, a  =  byte3, byte2, byte1, byte0

    and alpha stays where the earlier reading already put it, at byte 0 - 0xff
    on all 132860 vertices of the Dam item's four large pools.

    MEASURED, against the game rather than against a statistic. The source
    carries the level background twice under one item path, and the
    ORIGINAL-generation model is the N64 level's own geometry. Its floor decal
    (draw 0x2f10, style 0x2702, translucent) carries the colour word
    ff 00 e1 ff on 34 of its 46 vertices, and the REMASTERED model's road-line
    draw (0x478c) carries ff 00 6b 6b on 8 of its 42. Read byte1..byte3 those
    are (0, 225, 255) and (0, 107, 107) - cyan. Read reversed they are
    (255, 225, 0) and (107, 107, 0) - yellow. Rendered at the playtest
    mark-001 camera of 2026-09-24, PRESENTATION ORIGINAL draws that tunnel's
    centre line YELLOW; the replacement drew it cyan. The game is the oracle
    and it says reversed.

    The same frame carries the control: the kerb's yellow hazard stripes are a
    TEXTURE, and they are correct under both presentations - so the defect was
    never in the texture path, only in this word."""
    return b3, b2, b1, b0


def _vertex_at(blob, o):
    if o + VERTEX_STRIDE > len(blob):
        return None
    x, y, z = struct.unpack('>fff', blob[o:o + 12])
    for c in (x, y, z):
        if not math.isfinite(c) or abs(c) > MAX_COORD:
            return None
        if c != 0.0 and abs(c) < 1.0e-3:
            return None
    u, v = struct.unpack('>HH', blob[o + 16:o + 20])
    col = blob[o + 20:o + 24]
    return (x, y, z, texcoord(u), texcoord(v)) + vertex_colour(*col)


def split_meshes(blob):
    """Walk one model .gpu blob into [(vertices, indices), ...].

    Deterministic and total: it either returns runs whose indices all address
    their own vertex run, or it returns what it found and lets the caller
    refuse. Nothing here writes, and nothing here trusts a count from the file.
    """
    out = []
    pos = 0
    n = len(blob)
    while pos + VERTEX_STRIDE <= n:
        v = _vertex_at(blob, pos)
        if v is None:
            break
        verts = []
        while pos + VERTEX_STRIDE <= n:
            v = _vertex_at(blob, pos)
            if v is None:
                break
            verts.append(v)
            pos += VERTEX_STRIDE
        if len(verts) < MIN_RUN:
            break
        nv = len(verts)
        ib = pos
        count = 0
        while pos + 2 <= n:
            if _vertex_at(blob, pos) is not None:
                break
            if struct.unpack('>H', blob[pos:pos + 2])[0] >= nv:
                break
            pos += 2
            count += 1
        count -= count % 3
        pos = ib + count * 2
        if count == 0:
            break
        idx = list(struct.unpack('>%dH' % count, blob[ib:ib + count * 2]))
        out.append((verts, idx))
    return out


def draw_records(stream):
    """The TRIANGLE-LIST draw calls in an item's command list, in execution
    order: [(stream_offset, index_count, index_buffer_handle), ...].

    This is the ONE-ITEM reader's view and it is deliberately narrow - it is
    what `read_item` consumes, and a one-item bundle draws only triangle
    lists. It is now taken from the structural walk above rather than from a
    byte-pattern search, so a record that merely CARRIES those bytes is no
    longer mistaken for a draw; on the one-item bundle the two agree exactly.

    A level bundle is not read through here - see `read_level`, which carries
    every primitive type and does not assume one vertex buffer per draw."""
    out = []
    recs, _end, _ok = command_records(stream)
    if not recs:                       # a stream whose framing does not walk
        pat = struct.pack('>II', DRAW_OP, 4)
        for m in re.finditer(re.escape(pat), stream):
            o = m.start()
            if o + 16 > len(stream):
                continue
            _, _, count, ib = struct.unpack('>IIII', stream[o:o + 16])
            if count and count % 3 == 0:
                out.append((o, count, ib))
        return out
    for (o, op, _low, a) in recs:
        if op != OP_DRAW or len(a) < 3:
            continue
        prim, count, ib = a[0], a[1], a[2]
        if prim == PRIM_TRILIST and count and count % 3 == 0:
            out.append((o, count, ib))
    return out


def buffer_ranges(graph, handle, index_count, blob_len):
    """Resolve one draw through the rendergraph's own buffer table.

    The index buffer is the 16-byte record (handle, offset, length, 1) whose
    length is exactly the draw's index count in 16-bit indices; the vertex
    buffer is the 12-byte record (handle, offset, length) whose bytes END
    where that index buffer begins and whose length is a whole number of
    vertices. Both are then required to lie inside the model blob.

    Returns (vertex_offset, vertex_count, index_offset, index_count) or None.
    """
    want = index_count * 2
    pat = struct.pack('>I', handle)
    for m in re.finditer(re.escape(pat), graph):
        q = m.start()
        if q + 16 > len(graph):
            continue
        _, ioff, isize, flag = struct.unpack('>IIII', graph[q:q + 16])
        if flag != 1 or isize != want or ioff + isize > blob_len:
            continue
        for p in range(0, len(graph) - 12, 4):
            _, voff, vsize = struct.unpack('>III', graph[p:p + 12])
            if (vsize and vsize % VERTEX_STRIDE == 0 and voff + vsize == ioff
                    and voff + vsize <= blob_len):
                return (voff, vsize // VERTEX_STRIDE, ioff, index_count)
        return None
    return None


def read_item(path):
    """Read one item bundle. Returns

        dict(meshes=[(verts, idx), ...],
             textures=[dict(name, w, h, fmt, rgba, has_alpha), ...],
             declared=dict(centre, extent, radius) or None,
             draws=[dict(at, index_count, handle, pass_mask), ...])

    `pass_mask` is the innermost pass gate over the draw (see `draw_passes`),
    or None when no gate governs it.

    `meshes` is in the item's own DRAW ORDER, taken from its command list, and
    each entry's byte ranges come from the rendergraph's buffer table. What the
    blob decomposes into on its own is used as the cross-check.

    Raises WorldAssetError when the bundle is not one this reader understands.
    """
    try:
        b = caff.parse(path)
    except Exception as exc:                                  # noqa: BLE001
        raise WorldAssetError('%s: %s' % (os.path.basename(path), exc))

    texnames = {t['name'] for t in b['texs']}
    models = [e for e in b['ents']
              if e['sec'] == '.gpu' and e['kind'] == 0x05 and e['name'] not in texnames]
    if not models:
        raise WorldAssetError('%s: no model data in this bundle'
                              % os.path.basename(path))
    # The item's geometry is the model entity with a command list
    # (model_entity); the others are the bundle's shared pools.
    m = model_entity(b, models)
    blob = b['data'][b['base']['.gpu'] + m['off']:][:m['size']]
    name = os.path.basename(path)

    # The rendergraph and the command list for THIS model, by resource name.
    graph = b''
    dr = [e for e in b['ents'] if e['sec'] == '.data' and e['name'] == m['name']]
    if dr:
        g = max(dr, key=lambda e: e['size'])
        graph = b['data'][b['base']['.data'] + g['off']:][:g['size']]
    stream = b''
    sr = [e for e in b['ents'] if e['sec'] == '.stream' and e['name'] == m['name']]
    if sr:
        s = max(sr, key=lambda e: e['size'])
        stream = b['data'][b['base']['.stream'] + s['off']:][:s['size']]

    # THE BINDING, from the item's own records rather than from the order the
    # runs happen to lie in: every draw in the command list, in execution
    # order, resolved to a vertex and an index range through the buffer table.
    draws, meshes = [], []
    passes = draw_passes(command_records(stream)[0])
    for (at, count, handle) in draw_records(stream):
        rng = buffer_ranges(graph, handle, count, len(blob))
        if rng is None:
            continue
        voff, nv, ioff, ni = rng
        verts = [_vertex_at(blob, voff + i * VERTEX_STRIDE) for i in range(nv)]
        if any(v is None for v in verts):
            raise WorldAssetError('%s: the vertex buffer a draw names does not '
                                  'read as vertices' % name)
        idx = list(struct.unpack('>%dH' % ni, blob[ioff:ioff + ni * 2]))
        if max(idx) >= nv:
            raise WorldAssetError('%s: a draw indexes past its own vertex '
                                  'buffer' % name)
        draws.append(dict(at=at, index_count=ni, handle=handle,
                          pass_mask=passes.get(at)))
        meshes.append((verts, idx))

    # THE CROSS-CHECK: what the blob decomposes into on its own must be the
    # same geometry the records claim. Disagreement means one of the two is
    # being read wrongly, which is exactly what must not ship quietly.
    found = split_meshes(blob)
    if not found:
        raise WorldAssetError('%s: the model blob does not decompose into '
                              'vertex and index runs' % name)
    if not meshes:
        raise WorldAssetError('%s: no draw in this item resolves to a vertex '
                              'and an index buffer' % name)
    if sorted((len(v), len(i)) for v, i in meshes) != \
       sorted((len(v), len(i)) for v, i in found):
        raise WorldAssetError('%s: the draws this item declares (%s) are not '
                              'the geometry its blob holds (%s). Refused '
                              'rather than guessed at.'
                              % (name,
                                 ', '.join('%dv/%di' % (len(v), len(i))
                                           for v, i in meshes),
                                 ', '.join('%dv/%di' % (len(v), len(i))
                                           for v, i in found)))

    textures = []
    for t in b['texs']:
        nm = caff.tex_name(b, t)
        if t['fmt'] not in xdecode.SUPPORTED:
            raise WorldAssetError('%s: texture %s is format %d, which this '
                                  'pipeline does not decode'
                                  % (os.path.basename(path), nm, t['fmt']))
        try:
            img = xdecode.decode(caff.base_level(b, t), t['fmt'], t['w'],
                                 t['h'], mip_levels=t['mips'])
        except Exception as exc:                              # noqa: BLE001
            raise WorldAssetError('%s: texture %s: %s'
                                  % (os.path.basename(path), nm, exc))
        alpha = img[:, :, 3]
        textures.append(dict(name=nm, w=int(t['w']), h=int(t['h']), fmt=int(t['fmt']),
                             rgba=img, has_alpha=bool((alpha != 255).any())))

    # The record's own declared bounds, when the head is the shape measured on
    # the first item. Used as a CROSS-CHECK, never as the answer.
    declared = None
    if graph[:11] == b'rendergraph' and len(graph) >= 0x50:
        r, cx, cy, cz, ex, ey, ez = struct.unpack('>fffffff', graph[0x34:0x50])
        if all(math.isfinite(v) for v in (r, cx, cy, cz, ex, ey, ez)):
            declared = dict(radius=r, centre=(cx, cy, cz), extent=(ex, ey, ez))

    return dict(meshes=meshes, textures=textures, declared=declared, draws=draws)


# ---- the LEVEL bundle -----------------------------------------------------
#
# A level's world bundle is one item per level and it breaks both of the
# one-item reader's assumptions, which is why it has its own reader:
#
#   * its command list draws THREE primitive types, so a pattern requiring
#     type 4 sees 46 records where there are 389;
#   * its draws read a SHARED vertex pool - 342 of 387 index one buffer of
#     54651 vertices - so "the vertex buffer ends where the index buffer
#     begins" mis-resolves, and a pool larger than 65536 vertices forces a
#     32-bit index buffer alongside the 16-bit ones.
#
# What replaces them is structural and is checked against CONSUMED OUTPUT
# rather than against itself: every draw's index buffer must be a record whose
# length is exactly that draw's element count times its index width; every
# index run must begin exactly where some vertex buffer's bytes END, which is
# what names the pool without inventing a binding; every index must then fall
# inside that pool; and the bounds computed from the vertices actually read
# must agree with the bounds the bundle declares for itself.


def model_entity(b, models):
    """Which of a bundle's non-image .gpu model entities is the MODEL: the
    one with a .stream (a command list) of its own, the largest of those -
    and the largest of all when none has one, as before (#80).

    The other entities are program pools (_program_pools), and a pool can be
    the larger: the original generations of prop/chrgrenadelaunch and
    prop/chrskorpion hold their model as entity 1 (8288 bytes, a .stream and a
    54476-byte rendergraph) beside a 9088-byte pool (no .stream, a 546-byte
    rendergraph). Taking the largest read the pool, whose empty command list
    "does not walk to its end", and refused both weapons. On every bundle
    where the largest entity carries the command list nothing changes."""
    streamed = {e['name'] for e in b['ents'] if e['sec'] == '.stream' and e['size'] > 0}
    return max(models, key=lambda e: (e['name'] in streamed, e['size']))


def _model_parts(path):
    """(container, model entry, .gpu blob, .data rendergraph, .stream)."""
    try:
        b = caff.parse(path)
    except Exception as exc:                                  # noqa: BLE001
        raise WorldAssetError('%s: %s' % (os.path.basename(path), exc))
    texnames = {t['name'] for t in b['texs']}
    models = [e for e in b['ents']
              if e['sec'] == '.gpu' and e['kind'] == 0x05 and e['name'] not in texnames]
    if not models:
        raise WorldAssetError('%s: no model data in this bundle'
                              % os.path.basename(path))
    m = model_entity(b, models)
    blob = b['data'][b['base']['.gpu'] + m['off']:][:m['size']]
    graph = b''
    dr = [e for e in b['ents'] if e['sec'] == '.data' and e['name'] == m['name']]
    if dr:
        g = max(dr, key=lambda e: e['size'])
        graph = b['data'][b['base']['.data'] + g['off']:][:g['size']]
    stream = b''
    sr = [e for e in b['ents'] if e['sec'] == '.stream' and e['name'] == m['name']]
    if sr:
        s = max(sr, key=lambda e: e['size'])
        stream = b['data'][b['base']['.stream'] + s['off']:][:s['size']]
    return b, m, blob, graph, stream


def _program_pools(b, m):
    """The other .gpu model entities of the bundle - every one that is
    neither an image nor the model itself. On the Dam item that is one
    entity of 0x3960 bytes holding the pixel programs, each preceded by its
    literal bank (pixel_programs); the vertex programs sit in the model's
    own blob (wave_program_literal)."""
    texnames = {t['name'] for t in b['texs']}
    out = []
    for e in b['ents']:
        if e['sec'] != '.gpu' or e['kind'] != 0x05 or e['name'] in texnames:
            continue
        if e['name'] == m['name'] and e['off'] == m['off']:
            continue
        out.append(b['data'][b['base']['.gpu'] + e['off']:][:e['size']])
    return out


def _words(buf):
    return struct.unpack('>%dI' % (len(buf) // 4), buf[:(len(buf) // 4) * 4])


def index_buffer_records(graph, blob_len, wanted):
    """{handle: set((offset, length, kind))} for the handles in `wanted`.

    `wanted` maps a handle to the set of (length, kind) pairs some draw could
    accept, so a record is only kept when it is the buffer a draw asks for -
    which is a far tighter test than "looks like a record"."""
    out = {}
    W = _words(graph)
    for i in range(0, len(W) - 3):
        h = W[i]
        acc = wanted.get(h)
        if acc is None:
            continue
        off, ln, kind = W[i + 1], W[i + 2], W[i + 3]
        if (ln, kind) not in acc:
            continue
        if off > blob_len or ln > blob_len or off + ln > blob_len:
            continue
        out.setdefault(h, set()).add((off, ln, kind))
    return out


def vertex_buffer_ending_at(graph, blob_len, end):
    """The vertex buffer whose bytes end exactly at `end`, as
    (graph offset, stride, offset, length, vertex count), or None.

    The record is (stride, handle, byte offset, byte length); the length must
    be a whole number of vertices and the range must lie inside the blob. When
    several records would fit, the one describing the most vertices wins and
    the caller is told, because an ambiguous resolution is a finding."""
    best = None
    n = 0
    W = _words(graph)
    for i in range(0, len(W) - 3):
        st, off, ln = W[i], W[i + 2], W[i + 3]
        if st not in VERTEX_STRIDES or ln == 0 or ln % st:
            continue
        if off + ln != end or off + ln > blob_len:
            continue
        cand = (i * 4, st, off, ln, ln // st)
        n += 1
        if best is None or cand[4] > best[4]:
            best = cand
    return best, n


# A TRAILING PER-VERTEX STREAM (#80). A pool's index run need not begin where
# its position buffer ends: MEASURED on Surface's item, the two pools of its
# instanced tree (264 vertices of 20 bytes, 23 of 28) are each followed by a
# second buffer the rendergraph describes in the same four words - stride 4,
# its own handle, laid end to end with the first - of exactly one word per
# vertex (1056 = 264 x 4, 92 = 23 x 4), and the index run begins where THAT
# ends. Its words are the texture coordinate (s16 u, s16 v: the canopy's
# quads read 0 / 255 / 256, the trunk's 1/256 wraps), which the 20-byte
# record has no room for. The pool is therefore named through the stream,
# and the coordinate is MEASURED over both buffers at once (pool_source) by
# the same edge and pile-up rule as any other - not read from the stream
# because it is there. The Dam's, Facility's and Runway's items have no such
# stream, and read unchanged.
STREAM_STRIDES = (4, 8)


def pool_ending_at(graph, blob_len, end):
    """The vertex pool whose bytes end at `end`: (vb, n, stream) - vb and n
    as vertex_buffer_ending_at gives them and stream None; or, when no
    position buffer ends there but a per-vertex stream of STREAM_STRIDES
    does, the position buffer ending where the stream begins and carrying
    the same number of vertices, with stream = (offset, stride)."""
    vb, n = vertex_buffer_ending_at(graph, blob_len, end)
    if vb is not None:
        return vb, n, None
    W = _words(graph)
    for i in range(0, len(W) - 3):
        st, off, ln = W[i], W[i + 2], W[i + 3]
        if st not in STREAM_STRIDES or ln == 0 or ln % st:
            continue
        if off + ln != end or off + ln > blob_len:
            continue
        vb2, n2 = vertex_buffer_ending_at(graph, blob_len, off)
        if vb2 is not None and vb2[4] == ln // st:
            return vb2, n2, (off, st)
    return None, 0, None


def pool_source(p, blob):
    """(bytes, offset, stride) a pool's records are read from: the blob
    itself for an ordinary pool; for a pool with a trailing stream, a copy of
    its records with the stream's bytes laid in front of the colour word, so
    the record is stride + stream stride wide and every reader below sees one
    buffer. Built once per pool."""
    s = p.get('stream')
    if s is None:
        return blob, p['off'], p['stride']
    if p.get('_source') is None:
        st, (so, ss) = p['stride'], s
        out = bytearray()
        for i in range(p['count']):
            o = p['off'] + i * st
            out += blob[o:o + st - 4] + blob[so + i * ss:so + (i + 1) * ss] + blob[o + st - 4:o + st]
        p['_source'] = bytes(out)
    return p['_source'], 0, p['stride'] + s[1]


def declared_bounds(graph):
    """The bundle's own bounding sphere and AABB, or None. A CROSS-CHECK."""
    if graph[:11] != b'rendergraph' or len(graph) < 0x50:
        return None
    r, cx, cy, cz, ex, ey, ez = struct.unpack('>fffffff', graph[0x34:0x50])
    if not all(math.isfinite(v) for v in (r, cx, cy, cz, ex, ey, ez)):
        return None
    if ex <= 0 or ey <= 0 or ez <= 0:
        return None
    return dict(radius=r, centre=(cx, cy, cz), extent=(ex, ey, ez))


# The fixed-point scales a texture coordinate may be written at, largest
# first. Which one an item uses is MEASURED per pool - see _uv_format.
UV_DIVISORS = (16384, 8192, 4096, 2048, 1024, 512, 256, 128, 64)

# ---- vertex records whose position is NOT the first field --------------
#
# Every pool the readers above were written against starts its vertex with the
# position. A 36-byte pool does not: read from +0, its "position" is a packed
# coordinate pair (x reaches -3.16e38) and the pool fails validation. On the
# Dam item those pools carry the water - two draws, 19626 and 374 triangles.
#
# THE LAYOUT IS THE ITEM'S OWN, read from the vertex program the command list
# binds to those pools (op 0x2e names the pool's buffer record and the
# program; the program's fetch instructions carry offset, format and stride).
# Decoded with the Xenos fetch encoding, stride 9 words:
#
#   +0   16_16 signed integer    a coordinate pair the program feeds its
#                                animated wave term, not a sampler
#   +4   32_32_32_FLOAT          position, w = 1
#   +16  2_10_10_10              normal
#   +20  16_16 signed integer    the TEXTURE COORDINATE: the program passes it
#                                through its two texture matrices to the
#                                interpolators the pixel program samples with
#   +24  2_10_10_10, +28 2_10_10_10    tangent frame
#   +32  8_8_8_8                 colour, the same word as every other pool's
#                                last one (see vertex_colour)
#
# The same decoder applied to the 24-byte pools' own program reads position
# +0, normal +12, coordinate +16, colour +20 - the layout this module had
# already measured for them - which is what says it reads fetches correctly.
#
# THE COORDINATE'S SCALE is named too, because for these pools the value
# histogram _uv_format reads a scale from carries no signal. The coordinate
# is a planar projection of position (raw u = 0.2838 x + 87.6, within one raw
# unit, on all 10099 vertices of the large pool), not charts cornered on a
# texture edge, so its values pile up nowhere: 1.0% of them are multiples of
# 128 in one pool (chance alone gives 0.8%) and none are in the other. The
# scale is the program's own instead. The fetch is an UNNORMALISED integer
# with no exponent adjust, so the texture matrices carry all of it, and both
# are diag(1/128, 1/128, 1/128, 1) with no offset - as the running game hands
# them to the program (a constant capture of that draw), and as the item's
# rendergraph stores them beside the same programs' pixel constants.
#
# Only the fields that differ from the default are named. A stride with no
# entry keeps position +0 and a measured coordinate word and scale, exactly as
# before; a named layout is still VALIDATED (edge test, bounds, range).
#
# THE ANIMATION'S OWN FIELDS (#69). The +0 pair and the +16 normal are what
# the program's wave term consumes - see wave_program_literal and
# wave_record for the term itself - so a layout that names them is carried
# through as per-vertex AUXILIARY data (_pool_aux), beside the position,
# coordinate and colour every pool carries. A stride without them animates
# nothing.
#
# THE MATERIAL'S TANGENT FRAME (#69 material). The same vertex program
# fetches two more 2_10_10_10 vectors, at +24 and +28, rotates each by the
# modelview beside the normal and hands all three to the pixel program, which
# builds its shading normal as
#
#     N = n.x * (+28) + n.y * (+24) + n.z * (+16)
#
# with n the tangent-space normal map sample. So +28 is the axis the map's
# first component scales (`frame` names it first) and +24 the second; the
# normal is the third. Both vectors are stored explicitly - there is no cross
# product and no handedness sign in the program, so none is derived here.
POOL_LAYOUTS = {
    36: dict(position=4, texcoord=20, scale=128.0, wave=0, normal=16,
             frame=(28, 24)),
}


def pool_layout(stride):
    """(position offset, texture-coordinate offset, scale) for a pool's
    stride; the last two are None when _uv_format is to measure them."""
    lay = POOL_LAYOUTS.get(stride)
    if lay is None:
        return 0, None, None
    return lay['position'], lay['texcoord'], lay['scale']


def pool_aux_layout(stride):
    """(wave-coordinate offset, normal offset) for a pool's stride, or
    (None, None) for a layout that carries no animation fields."""
    lay = POOL_LAYOUTS.get(stride)
    if lay is None or 'wave' not in lay:
        return None, None
    return lay['wave'], lay['normal']


def pool_frame_layout(stride):
    """(first tangent offset, second tangent offset) for a pool's stride -
    the +28 / +24 pair above - or (None, None) for a layout without them."""
    lay = POOL_LAYOUTS.get(stride)
    if lay is None or 'frame' not in lay:
        return None, None
    return lay['frame']


def snorm10x3(word):
    """One 2_10_10_10 signed-normalised word -> (x, y, z) in -1..1.

    Ten bits per axis, two's complement, low bits first; a full-scale value of
    511 is 1.0 (so the measured +y normal 0x0007fc00 reads (0, 1, 0)). The
    two top bits are the w field and are not a normal's business."""
    out = []
    for k in range(3):
        v = (word >> (10 * k)) & 0x3ff
        if v & 0x200:
            v -= 0x400
        out.append(max(-1.0, v / 511.0))
    return tuple(out)


def _pool_aux(blob, off, stride, count, wave, normal, pos, uvo):
    """Decode one pool's per-vertex ANIMATION fields, or None when the layout
    does not validate: [(wave_a, wave_b, nx, ny, nz), ...].

    `wave` is the offset of the s16 pair the program feeds its wave term - an
    UNNORMALISED integer, so it is carried raw and the program's own scale is
    carried beside it (wave_record's `coord_scale`). `normal` is the offset of
    the 2_10_10_10 normal the displacement runs along. Both are checked to lie
    inside the record and to overlap neither the position, the coordinate nor
    the colour word, and every normal must have a length - a zero normal would
    displace nothing but says the field is not what the layout claims."""
    if stride < 24 or count <= 0 or off + count * stride > len(blob):
        return None
    co = stride - 4
    for f in (wave, normal):
        if f is None or f < 0 or f + 4 > co:
            return None
        if f < pos + 12 and f + 4 > pos:
            return None
        if f < uvo + 4 and f + 4 > uvo:
            return None
    if wave == normal:
        return None
    out = []
    for i in range(count):
        o = off + i * stride
        a, b2 = struct.unpack('>hh', blob[o + wave:o + wave + 4])
        n = snorm10x3(struct.unpack('>I', blob[o + normal:o + normal + 4])[0])
        if n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 0.25:
            return None
        out.append((a, b2, n[0], n[1], n[2]))
    return out


def _pool_frame(blob, off, stride, count, tx, ty, pos, uvo):
    """Decode one pool's per-vertex TANGENT FRAME, or None when the layout
    does not validate: [(xx, xy, xz, yx, yy, yz), ...] - the +28 vector
    (the axis the normal map's first component scales) then the +24 one.

    The same checks as _pool_aux: both words inside the record, overlapping
    neither the position, the coordinate nor the colour, distinct, and every
    vector with a length - a zero axis would collapse the shading normal onto
    the other two and says the field is not what the layout claims."""
    if stride < 24 or count <= 0 or off + count * stride > len(blob):
        return None
    co = stride - 4
    for f in (tx, ty):
        if f is None or f < 0 or f + 4 > co:
            return None
        if f < pos + 12 and f + 4 > pos:
            return None
        if f < uvo + 4 and f + 4 > uvo:
            return None
    if tx == ty:
        return None
    out = []
    for i in range(count):
        o = off + i * stride
        x = snorm10x3(struct.unpack('>I', blob[o + tx:o + tx + 4])[0])
        y = snorm10x3(struct.unpack('>I', blob[o + ty:o + ty + 4])[0])
        for v in (x, y):
            if v[0] * v[0] + v[1] * v[1] + v[2] * v[2] < 0.25:
                return None
        out.append(x + y)
    return out


# ---- the water pixel program's contract (#69 material) ---------------------
#
# WHAT THE PROGRAM DOES, read from its own microcode (the Xenos disassembly of
# the two water pixel programs) with every register named by the item's own
# constant table (the ps_3_0 CTAB in the rendergraph: c_constant0 = c12,
# c_texbem_sampler3 = c13, c_constant2 = c14, c_constant3 = c15,
# c_constant4 = c16, c_globalcolour = c7, c_ambient / c_light0colour /
# c_light0dir = c196..c198, c_fog / c_fogcolour = c0 / c1, s_sampler0..3):
#
#   n      = tex(s0, uv) * c254.x + c255.z          tangent-space normal
#   base   = tex(s3, uv + c13.x * tex(s2, uv).xy)    bump-environment warp
#   N      = normalize(n.x * T28 + n.y * T24 + n.z * normal)   view space
#   refl   = tex(s1, (N.x * c255.x + c255.x, N.y * c255.y + c255.x))
#   rgb    = c14 * (c196 + sat(N.z) * c197) * colour * base
#            + c12 * refl + spec * c197 * c16
#   a      = max(dot(rgb, c254.y), c255.w) * base.a  (* colour.a, small draw)
#   then the per-object tint (c7) and the stage fog (c0, c1).
#
# c12, c13 and c14 are uploaded per draw (OP_PS_CONSTANTS); c254 and c255
# are the program's own LITERAL BANK, stored in front of its microcode
# exactly as the vertex program's is (pixel_programs); c196..c198 and c7 are
# the running game's light and object state - measured (1,1,1,0), 0, (0,0,1)
# and (1,1,1,0) on every logged frame, so the lit term is the ambient alone
# and the specular vanishes. The record below carries the item's numbers and
# none of the game's.
PS_LITERAL_ROWS = 4            # c252..c255, sixteen bytes each, before the code
PS_REFLECT_REG = 12            # c12  reflection weight (c_constant0)
PS_WARP_REG = 13               # c13  bump-environment scale (c_texbem_sampler3)
PS_BASE_REG = 14               # c14  base weight (c_constant2)
PS_NORMAL_ROW = 2              # c254: .x normal scale, .y the mean's weight
PS_SPHERE_ROW = 3              # c255: .x .y sphere scale, .x bias, .z normal
                               #       bias, .w alpha floor
SLOT_NORMAL, SLOT_SPHERE, SLOT_OFFSET, SLOT_BASE = 0, 1, 2, 3
MATERIAL_SLOTS = (SLOT_NORMAL, SLOT_SPHERE, SLOT_OFFSET, SLOT_BASE)
SLOT_FLAG_CLAMP = 0x8          # the material record's per-slot flag word:
                               # set on the sphere slot alone, whose fetch
                               # constant the running game sets to clamp
                               # while the other three wrap (measured)

CF_EXEC_OPCODES = (1, 2, 3, 4, 5, 6, 13, 14)   # exec, exec-end, the
CF_END_OPCODES = (2, 4, 6, 14)                  # conditional forms, and
                                                # the predicated-clean forms


def _cf_pairs(buf, start):
    """The control-flow instructions of a program starting at `start`, in
    order, as (opcode, address, count, sequence) until an exec-end - or None
    when the words do not walk as control flow. Two 48-bit instructions per
    three dwords: the first is dword 0 with the low half of dword 1 above
    it, the second the high half of dword 1 with dword 2 above that."""
    out = []
    o = start
    for _ in range(64):
        if o + 12 > len(buf):
            return None
        d0, d1, d2 = struct.unpack('>III', buf[o:o + 12])
        for cf in (d0 | ((d1 & 0xffff) << 32), (d1 >> 16) | (d2 << 16)):
            op = (cf >> 44) & 15
            if op in CF_EXEC_OPCODES:
                out.append((op, cf & 0xfff, (cf >> 12) & 7, (cf >> 16) & 0xfff))
                if op in CF_END_OPCODES:
                    return out
            elif op == 0 or op == 12:
                continue                     # nop, alloc
            else:
                return None                  # loops, calls, jumps: not one of these
        o += 12
    return None


def pixel_programs(pool):
    """Every pixel program in a program pool whose texture fetches can be
    walked from its own control flow: [(start, [fetch constants in fetch
    order], [literal rows c252..c255])].

    A program is found from a texture fetch (opcode 1 in the low five bits)
    by walking back to the exec that addresses its slot - exactly as
    wave_program_literal finds the vertex programs - and its fetches are
    then enumerated from the exec sequence bits (two per instruction; the
    low one marks a fetch) rather than by pattern, because an ALU word can
    carry the same low bits. The PS_LITERAL_ROWS rows before the start are
    its literal bank."""
    W = _words(pool)
    found = {}
    for i in range(len(W) - 2):
        if (W[i] & 31) != 1:
            continue
        a = i * 4
        for k in range(1, 24):
            s = a - 12 * k
            if s < 16 * PS_LITERAL_ROWS:
                break
            c0, c1 = W[s // 4], W[s // 4 + 1]
            if ((c1 >> 12) & 15) != 1 or (c0 & 0xfff) != k:
                continue
            if s in found:
                break
            cfs = _cf_pairs(pool, s)
            if not cfs:
                break
            consts = []
            ok = True
            for (_op, addr, cnt, seq) in cfs:
                for j in range(cnt):
                    if (seq >> (2 * j)) & 1:
                        o = s + 12 * (addr + j)
                        if o + 12 > len(pool):
                            ok = False
                            break
                        w0 = struct.unpack('>I', pool[o:o + 4])[0]
                        if (w0 & 31) != 1:
                            ok = False
                            break
                        consts.append((w0 >> 20) & 31)
                if not ok:
                    break
            if ok and consts:
                rows = [struct.unpack('>ffff', pool[s - 16 * (PS_LITERAL_ROWS - r):
                                                     s - 16 * (PS_LITERAL_ROWS - r) + 16])
                        for r in range(PS_LITERAL_ROWS)]
                found[s] = (consts, rows)
            break
    return [(s, found[s][0], found[s][1]) for s in sorted(found)]


def material_record(uploads, programs, slots, slot_flags):
    """The material record one draw's pixel-program state describes, or
    (None, why): the draw names the four sampler slots (`slots`, from its
    material record) and uploads c12..c14; the program that fetches exactly
    those four constants carries the literal bank. Every number is the
    item's. `slot_flags` is the material record's flag word per slot, for
    the clamp bit."""
    if not slots or sorted(slots) != list(MATERIAL_SLOTS):
        return None, 'the draw does not name the four material slots'
    want = set(MATERIAL_SLOTS)
    banks = {}
    for (_s, consts, rows) in programs:
        if set(consts) == want:
            key = tuple(tuple(r) for r in rows)
            banks[key] = banks.get(key, 0) + 1
    if not banks:
        return None, 'no pixel program fetches the four slots'
    if len(banks) != 1:
        return None, 'the four-slot programs carry different literal banks'
    rows = list(banks)[0]
    normal_row, sphere_row = rows[PS_NORMAL_ROW], rows[PS_SPHERE_ROW]
    regs = {}
    for (reg, vals) in uploads:
        for r in range(len(vals) // 4):
            row = tuple(vals[4 * r:4 * r + 4])
            prev = regs.get(reg + r)
            if prev is not None and prev != row:
                return None, 'the program variants upload different constants'
            regs[reg + r] = row
    for reg in (PS_REFLECT_REG, PS_WARP_REG, PS_BASE_REG):
        if reg not in regs:
            return None, 'the draw uploads no c%d' % reg
    rec = dict(mode='normal_sphere',
               normal=slots[SLOT_NORMAL], sphere=slots[SLOT_SPHERE],
               offset=slots[SLOT_OFFSET], base=slots[SLOT_BASE],
               reflect=tuple(regs[PS_REFLECT_REG][:3]),
               warp=float(regs[PS_WARP_REG][0]),
               base_weight=tuple(regs[PS_BASE_REG][:3]),
               normal_scale=float(normal_row[0]),
               normal_bias=float(sphere_row[2]),
               sphere_scale=(float(sphere_row[0]), float(sphere_row[1])),
               sphere_bias=(float(sphere_row[0]), float(sphere_row[0])),
               mean_weight=float(normal_row[1]),
               alpha_floor=float(sphere_row[3]),
               sphere_clamp=1 if ((slot_flags or {}).get(SLOT_SPHERE, 0) & SLOT_FLAG_CLAMP) else 0)
    flat = (list(rec['reflect']) + list(rec['base_weight']) + list(rec['sphere_scale'])
            + list(rec['sphere_bias']) + [rec['warp'], rec['normal_scale'],
                                          rec['normal_bias'], rec['mean_weight'],
                                          rec['alpha_floor']])
    if not all(math.isfinite(v) for v in flat):
        return None, 'the material constants carry a value no evaluation can use'
    if rec['normal_scale'] == 0.0:
        return None, 'the program scales the normal map by zero'
    return rec, None


# ---- which image channel a blended draw's coverage comes from (#71) --------
#
# A blended draw's pixel program decides its output alpha, and the image's
# alpha channel is only one of the things it may use. MEASURED on the
# remastered items (the item's own microcode, cross-checked against the
# emulator's disassembly of the same programs): every blended item program
# exports
#
#     oC0.a = coverage * colour.a * max(dot(lit rgb, c255.y), c255.x)
#
# with c255 = (1, 1/3, ...) in the program's literal bank and the running
# game's light state (ambient 1, directional 0, measured) making the last
# factor exactly 1. What `coverage` is differs by program, and the item says
# which: it is the component of the base-image fetch that reaches the export
# through the multiply chain -
#
#   'w'  the image's alpha: glass, foliage, most translucent draws;
#   ''   nothing - coverage is the vertex colour's alpha alone;
#   'z'  the image's BLUE channel, the fetch never reading its alpha at all.
#
# The third is the one a picture needs: the Dam crate stack's graffiti images
# are opaque DXT1 (no block in them uses the one-bit transparent index), white
# glyphs on black, and it is their blue channel - equal to the grey, on a grey
# image - that is the coverage. A black texel is therefore a transparent one.
# The draw is blended SRC_ALPHA / ONE_MINUS_SRC_ALPHA with the alpha test
# disabled and no depth write (measured on a draw of the same program in the
# running remaster: RB_COLORCONTROL's alpha-test enable clear, the fetch
# constant's swizzle the identity, so its z IS the image's blue).
#
# HOW A DRAW NAMES ITS PROGRAM, measured. The rendergraph carries the item's
# shader objects - header magic 0x102a11 then 0x00 (pixel) or 0x41 (vertex) -
# as effect groups: a run of pixel objects, then a run of as many vertex
# objects, the i-th of one pairing the i-th of the other. Op 0x2e names a
# vertex-program record; the k-th distinct record, in ascending order, is the
# k-th vertex object - checked, not assumed, by requiring the two sets'
# spacings to agree exactly. The pool stores the pixel objects' microcode in
# object order, 32-byte aligned, a repeated object sharing its first copy's
# program or carrying its own (_pixel_program_layout); a pixel object whose
# header word 5 is non-zero carries a 64-byte literal bank before its code,
# counted in its size (word 2). A bundle any of this does not walk for gets no
# coverage answer at all, and its draws keep the image's alpha - the
# conservative reading, never a guess.
SHADER_MAGIC = b'\x10\x2a\x11'
SHADER_PIXEL = 0x00
SHADER_VERTEX = 0x41
PS_LITERAL_BYTES = 16 * PS_LITERAL_ROWS
_VECTOR_MUL = 1                 # the ALU vector opcode of `mul`
_FETCH_TEXTURE = 1              # the fetch opcode of a texture fetch
_EXEC, _EXEC_END = 1, 2         # the unconditional exec forms
# the conditional exec forms (cond_exec, cond_pred_exec, cond_exec_pred_clean)
# and their program-ending variants; same address / count / sequence fields
_EXEC_CONDITIONAL = (3, 5, 13)
_EXEC_CONDITIONAL_END = (4, 6, 14)


def shader_objects(graph):
    """[(offset, 'P' or 'V', header words)] of every shader object in a
    rendergraph, in graph order."""
    out = []
    o = graph.find(SHADER_MAGIC)
    while o >= 0:
        if o % 4 == 0 and o + 48 <= len(graph) and graph[o + 3] in (SHADER_PIXEL, SHADER_VERTEX):
            out.append((o, 'P' if graph[o + 3] == SHADER_PIXEL else 'V',
                        struct.unpack('>12I', graph[o:o + 48])))
        o = graph.find(SHADER_MAGIC, o + 1)
    return out


def _exec_instructions(code, conditional=False):
    """The instructions a pixel program executes, in order, as ('F' or 'A',
    three dwords) - or None when its control flow is anything but plain execs
    (a conditional exec would make 'the last write' ambiguous).

    With `conditional`, the conditional and predicated execs
    (_EXEC_CONDITIONAL) are walked as well, their instructions in program
    order as if every one ran: the instructions that MAY execute. That is the
    answer for a question about what may have written a register before an
    instruction (computed_fetches), never for one about the last write."""
    out = []
    o = 0
    plain = (_EXEC, _EXEC_END)
    ends = (_EXEC_END,) + (_EXEC_CONDITIONAL_END if conditional else ())
    walked = plain + ((_EXEC_CONDITIONAL + _EXEC_CONDITIONAL_END) if conditional else ())
    for _ in range(64):
        if o + 12 > len(code):
            return None
        d0, d1, d2 = struct.unpack('>III', code[o:o + 12])
        for cf in (d0 | ((d1 & 0xffff) << 32), (d1 >> 16) | (d2 << 16)):
            op = (cf >> 44) & 15
            if op in walked:
                addr, cnt, seq = cf & 0xfff, (cf >> 12) & 7, (cf >> 16) & 0xfff
                for j in range(cnt):
                    q = 12 * (addr + j)
                    if q + 12 > len(code):
                        return None
                    out.append(('F' if (seq >> (2 * j)) & 1 else 'A',
                                struct.unpack('>III', code[q:q + 12])))
                if op in ends:
                    return out
            elif op not in (0, 12):              # nop, alloc
                return None
        o += 12
    return None


def coverage_channel(code, base_fetch=0):
    """Which component of fetch constant `base_fetch` is a MULTIPLICATIVE
    factor of the program's exported alpha (oC0.w): 'x', 'y', 'z' or 'w';
    '' when no component of it is; None when the program cannot be read so.

    Walks back from the export through vector `mul` operands only - the
    factors - to the instruction that last wrote each operand lane. A fetch
    lane answers with the component its destination swizzle routed there;
    anything else (a max, a dot product, a scalar op, an interpolator, a
    constant) is not a texture factor and ends that branch."""
    ins = _exec_instructions(code)
    if ins is None:
        return None
    writes = []                    # (instruction, (kind, reg), lane, op, info)
    for n, (k, d) in enumerate(ins):
        if k == 'F':
            if (d[0] & 31) != _FETCH_TEXTURE:
                continue
            dst, const, swz = (d[0] >> 12) & 63, (d[0] >> 20) & 31, d[1] & 0xfff
            for lane in range(4):
                s = (swz >> (3 * lane)) & 7
                if s < 4:
                    writes.append((n, ('r', dst), lane, 'fetch', (const, s)))
                elif s != 7:
                    writes.append((n, ('r', dst), lane, 'alu', None))
            continue
        if (d[1] >> 28) & 1:
            return None            # predicated: the last write is ambiguous
        w0 = d[0]
        vdst, sdst, export = w0 & 63, (w0 >> 8) & 63, (w0 >> 15) & 1
        vmask, smask = (w0 >> 16) & 15, (w0 >> 20) & 15
        vop = (d[2] >> 24) & 31
        regs = ((d[2] >> 16) & 0xff, (d[2] >> 8) & 0xff)
        temp = ((d[2] >> 31) & 1, (d[2] >> 30) & 1)
        swz = ((d[1] >> 16) & 0xff, (d[1] >> 8) & 0xff)
        kind = 'o' if export else 'r'
        for lane in range(4):
            if vmask >> lane & 1:
                srcs = [(('r', regs[a] & 63) if temp[a] else ('c', regs[a]),
                         ((swz[a] >> (2 * lane)) + lane) & 3) for a in range(2)]
                writes.append((n, (kind, vdst), lane,
                               'mul' if vop == _VECTOR_MUL else 'alu', srcs))
            if smask >> lane & 1:
                writes.append((n, (kind, sdst), lane, 'alu', None))

    def factors(reg, lane, before, depth):
        if depth > 16:
            return None
        for (n, r, ln, op, info) in reversed(writes):
            if n < before and r == reg and ln == lane:
                if op == 'fetch':
                    return {'xyzw'[info[1]]} if info[0] == base_fetch else set()
                if op != 'mul':
                    return set()
                got = set()
                for (src, comp) in info:
                    if src[0] == 'r':
                        f = factors(src, comp, n, depth + 1)
                        if f is None:
                            return None
                        got |= f
                return got
        return set()               # an interpolator: never written here

    last = [w for w in writes if w[1] == ('o', 0) and w[2] == 3]
    if not last:
        return None
    got = factors(('o', 0), 3, last[-1][0] + 1, 0)
    if got is None or len(got) > 1:
        return None
    return got.pop() if got else ''


def computed_fetches(code):
    """The fetch constants a pixel program samples at a coordinate it COMPUTED
    itself - one whose x or y lane an earlier instruction of the program wrote
    - rather than at an interpolator as the vertex program handed it over; or
    None when the program cannot be read so.

    (#82) MEASURED on Silo's item: the walkway rails' draw names a tangent-
    space normal map (slot 0), a 128x128 landscape (slot 1) and a 512x256
    grey metal (slot 2). Its program fetches slots 0 and 2 at interpolator
    r0.xy, builds the shading normal from slot 0 and the tangent frame, and
    fetches slot 1 at 0.5 N + 0.5 - a reflection, added over the lit slot 2.
    A texture fetch's source swizzle is three absolute two-bit components
    (bits 26..31 of its first word); the first two are the 2D coordinate.

    (#95) The program is walked with its conditional and predicated execs
    (_exec_instructions' `conditional`): a write that may not run can only
    add to what may have computed a coordinate, and a fetch does not change
    what it samples when an instruction after it is skipped. MEASURED on
    Control's item: the floors' draws (pool 0, 1068 triangles) name a
    tangent-space normal map (slot 0), a 256x256 pre-rendered view of the
    control room (slot 1) and a 512x512 marble tile (slot 2); both placed
    variants of their program end in a predicated exec (control-flow op 5),
    so the strict walk read neither, and slot 1 - fetched at a coordinate the
    program computes - was drawn as the floor's colour."""
    ins = _exec_instructions(code, conditional=True)
    if ins is None:
        return None
    written = {}                   # register -> lanes written so far
    out = set()
    for (k, d) in ins:
        if k == 'F':
            if (d[0] & 31) != _FETCH_TEXTURE:
                continue
            src, dst, const = (d[0] >> 5) & 63, (d[0] >> 12) & 63, (d[0] >> 20) & 31
            swz = (d[0] >> 26) & 63
            lanes = {swz & 3, (swz >> 2) & 3}
            if (d[0] >> 11) & 1:
                return None            # a relatively addressed source
            if lanes & written.get(src, set()):
                out.add(const)
            for lane in range(4):
                if ((d[1] >> (3 * lane)) & 7) != 7:
                    written.setdefault(dst, set()).add(lane)
            continue
        w0 = d[0]
        if (w0 >> 15) & 1:
            continue                   # an export writes no register
        vdst, sdst = w0 & 63, (w0 >> 8) & 63
        vmask, smask = (w0 >> 16) & 15, (w0 >> 20) & 15
        for lane in range(4):
            if vmask >> lane & 1:
                written.setdefault(vdst, set()).add(lane)
            if smask >> lane & 1:
                written.setdefault(sdst, set()).add(lane)
    return out


# #81 WHERE EACH PIXEL OBJECT'S PROGRAM LIES, measured on the five stage
# items. Two things the first walk assumed are not so:
#
#   - a program can OPEN with an alloc whose low word is zero (Bunker 1's
#     fourth object: 00000000 6002c400 ...), so a zero first word is not the
#     sign of a gap - twelve zero bytes are (no control-flow pair is);
#   - an object identical to an earlier one USUALLY shares its program, but
#     not always: Bunker 1's objects 30..32 repeat 24..26 and carry their own
#     copies while 33..35 repeat 15..17 and share (the Dam: 21..23 share;
#     Runway: 15..17 share and 25 does not). Nothing in the object says which.
#
# So the walk tries both for every repeated object and keeps the layout only
# when EXACTLY ONE way decodes every object and leaves nothing but zeros after
# the last program - the whole pool accounted for. Measured: one such walk on
# each of Bunker 1, the Dam, Facility, Runway and Surface; the share-always
# walk placed none of the first four and, on Runway, stopped short of a
# non-zero tail. More than one, or none, refuses as before.
LAYOUT_WALKS = 4096             # repeated objects branch; the search is capped


def _pixel_program_layout(pool, pixel, graph):
    """(start, length) of each pixel object's microcode in the pool, in object
    order, or None (above)."""
    objs = []
    for (o, _k, w) in pixel:
        lit = PS_LITERAL_BYTES if w[5] else 0
        objs.append((bytes(graph[o:o + w[1]]), lit, w[2] - lit))
    found, budget = [], [LAYOUT_WALKS]

    def fits(s, n):
        return n > 0 and s + n <= len(pool) and pool[s:s + 12] != bytes(12) \
            and _exec_instructions(pool[s:s + n]) is not None

    def walk(i, p, seen, out):
        budget[0] -= 1
        if len(found) > 1 or budget[0] < 0:
            return
        if i == len(objs):
            if not any(pool[p:]):
                found.append(list(out))
            return
        key, lit, n = objs[i]
        if key in seen:
            out.append(seen[key])
            walk(i + 1, p, seen, out)
            out.pop()
        s = (p + 31) // 32 * 32 + lit
        if fits(s, n):
            had = key in seen
            if not had:
                seen[key] = (s, n)
            out.append((s, n))
            walk(i + 1, s + n, seen, out)
            out.pop()
            if not had:
                del seen[key]

    walk(0, 0, {}, [])
    return found[0] if len(found) == 1 and budget[0] >= 0 else None


def draw_coverage(graph, pools, recs):
    """{draw offset: coverage channel} for every op-0x01 draw of a walked
    command list: the channel common to every program variant the draw may
    run under (see coverage_channel), or None where the variants disagree.
    Empty when the bundle's programs cannot be placed at all."""
    try:
        return _draw_coverage(graph, pools, recs)
    except (struct.error, IndexError, KeyError):
        return {}


def _bound_programs(graph, pools, recs):
    """{draw offset: [the pixel-program microcode of every vertex-program
    record bound for it]} for every op-0x01 draw of a walked command list, or
    {} when the bundle's programs cannot be placed or paired."""
    objs = shader_objects(graph)
    pixel = [x for x in objs if x[1] == 'P']
    vertex = [x[0] for x in objs if x[1] == 'V']
    if not pools or not pixel:
        return {}
    layout = _pixel_program_layout(pools[0], pixel, graph)
    records = sorted({a[4] for (_o, op, _l, a) in recs if op == OP_VS_BIND and len(a) >= 5})
    if layout is None or not records or len(records) != len(vertex) or             [r - records[0] for r in records] != [v - vertex[0] for v in vertex]:
        return {}
    vs_pixel = _paired_pixel_objects(objs, graph)
    if vs_pixel is None:
        return {}                  # a group whose halves do not pair
    code = {}
    for r, v in zip(records, vertex):
        s, n = layout[vs_pixel[v]]
        code[r] = pools[0][s:s + n]
    out, pending, bound = {}, [], []
    for (o, op, _l, a) in recs:
        if op == OP_VS_BIND and len(a) >= 5:
            pending.append(a[4])
        elif op == OP_DRAW:
            if pending:
                bound, pending = pending, []
            out[o] = [code[r] for r in bound if r in code]
    return out


def _draw_coverage(graph, pools, recs):
    out = {}
    for o, codes in _bound_programs(graph, pools, recs).items():
        got = {coverage_channel(c) for c in codes} if codes else {None}
        out[o] = got.pop() if len(got) == 1 else None
    return out


def bound_programs(graph, pools, recs):
    """_bound_programs, or {} where the bundle's programs cannot be walked."""
    try:
        return _bound_programs(graph, pools, recs)
    except (struct.error, IndexError, KeyError):
        return {}


# ---- a TWO-IMAGE draw whose coverage is one image and colour the other (#92)
#
# MEASURED on Depot's level item, the graffiti on the container in its yard
# (draw at 0x80e8, playtest run 20261009-122825-lvl30 mark 1): two sampler
# slots, a 256x128 black-on-white silhouette (slot 0) and a 256x128 coloured
# tag on black (slot 1). Its pixel program fetches both at the same
# interpolator and exports
#
#     rgb = light x colour x slot1.rgb        a = slot0.b x colour.a x 1
#
# (coverage_channel: slot 0 'z', slot 1 ''), so neither slot alone is the
# draw: drawn as slot 0 with slot 0's alpha (255), the bundle painted the
# white silhouette image as an opaque rectangle. The answer is the slot that
# multiplies its channel into the exported alpha and the OTHER slot as the
# colour - only for a draw whose two fetches both sample at an interpolator
# (computed_fetches empty: a reflection is no colour image) and whose
# programs agree. Of the source's 24 background items this is the only such
# draw; Frigate's one candidate samples its colour slot at a computed
# coordinate and is not one.
def two_image_coverage(codes, slots):
    """(coverage slot, channel, colour slot) of a two-slot draw whose bound
    programs `codes` all take the exported alpha from exactly one slot's
    channel and nothing from the other, both slots sampled at an
    interpolator (a program variant may sample a third, unnamed fetch at a
    computed coordinate - one of Depot's three does); or None."""
    if not codes or not slots or len(slots) != 2:
        return None
    for c in codes:
        got = computed_fetches(c)
        if got is None or got & set(slots):
            return None
    ch = {}
    for s in slots:
        got = {coverage_channel(c, s) for c in codes}
        if len(got) != 1:
            return None
        ch[s] = got.pop()
    cov = [s for s in sorted(slots) if ch[s] and ch[s] in 'xyzw']
    if len(cov) != 1:
        return None
    other = [s for s in sorted(slots) if s != cov[0]][0]
    if ch[other] != '':
        return None
    return (cov[0], ch[cov[0]], other)


# ---- a TWO-LAYER draw: two images blended by the program, not by a pass (#72)
#
# A handful of the Dam item's draws name TWO sampler slots of ordinary images.
# Neither slot alone is the draw: what it looks like is decided by its pixel
# program, so it is read from that program. MEASURED on
# the Dam item (the Xenos disassembly of the programs these draws bind, every
# register named by the item's constant table):
#
#   t0     = tex(s0, interpolator 1)        t1 = tex(s1, interpolator 2)
#   base   = light x i5.rgb x t1.rgb
#   layer  = light x i4.rgb x t0.rgb
#   rgb    = lerp(base, layer, i4.a x t0.a)          (the tower wall, 1 draw)
#   rgb    = lerp(base, layer, i4.a)                 (7 draws, opaque images)
#   then the object tint (c7) and the stage fog (c0, c1); alpha is a constant.
#
# with `light` = c196 + sat(N . c198) x c197, the running game's light state
# (see the water program's contract below for the measurement: c196..c198 =
# (1,1,1,0), 0, (0,0,1); c7 = (1,1,1,0)), so light is one and the tint is off.
#
# And the vertex program of a 32-byte pool feeds those interpolators straight
# from the record (every 32-byte-pool vertex program of the item, decoded with
# the Xenos fetch encoding): +16 -> 1 and +20 -> 2, the two 16_16 coordinates,
# both scaled by the same literal; +24 -> 4, an 8_8_8_8 colour with alpha;
# +28 -> 5, an 8_8_8_8 colour whose alpha the program never reads. The pool's
# own measured coordinate (_uv_format) is +20 and its colour the last word -
# the BASE's - so drawing slot 0 as a single surface paints the base's
# geometry with the LAYER's image. On the tower wall, whose layer is the
# ladder's shadow (black, alpha 0x8b, the ladder image's alpha as its shape),
# that loses the wall wherever the image is transparent.
#
# A blend of two coplanar surfaces is exactly what the decal pass already
# draws: the base as an ordinary surface, then the layer over it blended by
# source alpha - t0 x i4 in colour, t0.a x i4.a in alpha, which is the
# program's weight. So such a draw is carried as TWO draws of the same
# triangles, each with its own coordinate and colour.
#
# WHICH draws, is read from the program itself rather than from its shape: the
# program is EVALUATED on numbers (run_pixel_program) with random inputs and
# the game's light state, and a draw is two-layer only when its colour equals
# that lerp for exactly one assignment of images, interpolators and weight.
# The program evaluated is the FIRST one the draw's binding names; the others
# extend it (the second lerps the result toward one more interpolator by its
# alpha, the third adds a fetch of a third slot) and renumber its colour
# interpolators, so the field table below is the first program's.
LAYER_INPUTS = {
    32: {1: 16, 2: 20, 4: 24, 5: 28},
}

#: The running game's light and object state, as measured (see above).
LAYER_GAME_STATE = {196: (1.0, 1.0, 1.0, 0.0), 197: (0.0, 0.0, 0.0, 0.0),
                    198: (0.0, 0.0, 1.0, 0.0), 7: (1.0, 1.0, 1.0, 0.0)}
#: A fog factor of one: the stage fog is the renderer's (SL_WA_MAT_FOG), so
#: the colour is read before it.
LAYER_NO_FOG = {0: (0.0, 1.0, 0.0, 0.0)}


# ---- the untextured draw's material colour (#90) ---------------------------
#
# A draw naming no image binds a pixel program that FETCHES nothing. Streets'
# backdrop behind the start alley (draw 150, 76 triangles) is such a draw,
# and the private emulator's draw log names its program (a quad list of 152
# indices, no texture binding); the program's own disassembly reads
#
#   rgb = (c196 + sat(N . c198) x c197) x colour x c13
#   then the object tint (c7) and the stage fog (c0, c1)
#
# with c13 the item's own per-draw upload (op 0x06, `c_constant1` in the
# program's constant table) and the light the running game's state, one
# (LAYER_GAME_STATE). The draw uploads c13 = (0.5, 0.5, 0.5, 1): the remaster
# draws the backdrop at half its vertex colour, where a white image under the
# vertex colour drew it at the whole of it.
#
# Which item carries that program is read from the item, not named: its
# program pool is walked for every FETCH-LESS program (plain execs, no fetch)
# and each is evaluated on random inputs (run_pixel_program, the game's light
# state, no fog); an item carries the rule when one of them exports exactly
# an interpolator times c13 on every trial. Of the twelve stage items
# measured (Streets and the eleven stages before it) that is Streets,
# Bunker 1, Surface, Silo and Facility (one such program each); the Dam,
# Runway, Frigate, Bunker 2, Statue and Archives carry none.
UNTEX_COLOUR_REG = 13           # c13, the draw's material colour (c_constant1)
UNTEX_TRIALS = 3
UNTEX_TOL = 1e-4


def colour_times_c13_program(pools):
    """True when one of the item's program pools carries a fetch-less pixel
    program whose exported colour is an interpolator times c13 under the
    game's light state and no fog (above), on every one of UNTEX_TRIALS
    random trials. Pure."""
    import random
    for pool in pools:
        for o in range(0, len(pool) - 24, 4):
            code = pool[o:o + 4096]
            ins = _exec_instructions(code)
            if not ins or len(ins) < 4 or any(k == 'F' for k, _d in ins):
                continue
            good = True
            for trial in range(UNTEX_TRIALS):
                rnd = random.Random(0x90 + trial)
                inputs = {i: tuple(rnd.uniform(0.1, 0.9) for _k in range(4)) for i in range(16)}
                c13 = tuple(rnd.uniform(0.1, 0.9) for _k in range(4))

                def const(r, c13=c13):
                    if r == UNTEX_COLOUR_REG:
                        return c13
                    if r in LAYER_GAME_STATE:
                        return LAYER_GAME_STATE[r]
                    return LAYER_NO_FOG.get(r, (1.0, 1.0, 1.0, 1.0))
                got = run_pixel_program(code, inputs, lambda _ci: (1.0, 1.0, 1.0, 1.0), const)
                if got is None or not any(
                        all(abs(got[0][c] - inputs[i][c] * c13[c]) < UNTEX_TOL for c in range(3))
                        for i in range(16)):
                    good = False
                    break
            if good:
                return True
    return False


def untextured_colour(ps_uploads):
    """The material colour (r, g, b, 0..1) a draw's own c13 upload gives it,
    or None when the draw uploads none. The last upload standing wins, as the
    register would."""
    col = None
    for reg, vals in ps_uploads or []:
        q = UNTEX_COLOUR_REG - reg
        if 0 <= q and 4 * q + 3 < len(vals):
            col = tuple(float(v) for v in vals[4 * q:4 * q + 3])
    return col

_VOPS ={0: 'add', 1: 'mul', 2: 'max', 3: 'min', 11: 'mad', 15: 'dp4', 16: 'dp3'}
_SOPS = {0: 'adds', 2: 'muls', 5: 'maxs', 6: 'mins', 14: 'exp', 16: 'log',
         19: 'rcp', 22: 'rsq', 40: 'sqrt'}


def _alu_operand(regs, const, reg, temp, swz, neg, abs_const):
    """One ALU source after swizzle, absolute and negate, or None when it is
    addressed relatively."""
    if temp:
        if reg & 0x40:
            return None
        v = regs.get(reg & 63, (0.0, 0.0, 0.0, 0.0))
        absolute = bool(reg & 0x80)
    else:
        v = const(reg)
        absolute = abs_const
    out = [v[((swz >> (2 * lane)) + lane) & 3] for lane in range(4)]
    if absolute:
        out = [abs(x) for x in out]
    if neg:
        out = [-x for x in out]
    return out


def run_pixel_program(code, inputs, texel, const):
    """Evaluate a pixel program on numbers. `inputs` {interpolator: 4 floats}
    are its starting registers, `texel(fetch constant)` the 4 floats a fetch
    returns and `const(register)` a float constant. Returns (oC0, {fetch
    constant: the interpolator its coordinate is, or None when the coordinate
    was computed}), or None when the program uses anything this does not
    evaluate: control flow other than plain execs, a predicate, relative
    addressing, or an operation outside _VOPS / _SOPS."""
    ins = _exec_instructions(code)
    if ins is None:
        return None
    regs = {k: tuple(v) for k, v in inputs.items()}
    written = set()
    out = {}
    src = {}
    try:
        for (k, d) in ins:
            if k == 'F':
                if (d[0] & 31) != _FETCH_TEXTURE or (d[1] >> 31) & 1 \
                        or (d[0] >> 11) & 1 or (d[0] >> 18) & 1:
                    return None
                s, dst, ci = (d[0] >> 5) & 63, (d[0] >> 12) & 63, (d[0] >> 20) & 31
                src[ci] = None if s in written else s
                t = texel(ci)
                old = list(regs.get(dst, (0.0, 0.0, 0.0, 0.0)))
                for lane in range(4):
                    sel = (d[1] >> (3 * lane)) & 7
                    if sel < 4:
                        old[lane] = t[sel]
                    elif sel == 4:
                        old[lane] = 0.0
                    elif sel == 5:
                        old[lane] = 1.0
                    elif sel != 7:
                        return None
                regs[dst] = tuple(old)
                written.add(dst)
                continue
            w0, w1, w2 = d
            if (w1 >> 28) & 1 or (w1 >> 29) & 7 or (w0 >> 6) & 1:
                return None                    # predicated / relative
            vdst, sdst, export = w0 & 63, (w0 >> 8) & 63, (w0 >> 15) & 1
            vm, sm = (w0 >> 16) & 15, (w0 >> 20) & 15
            vclamp, sclamp, sop = (w0 >> 24) & 1, (w0 >> 25) & 1, (w0 >> 26) & 63
            vop = (w2 >> 24) & 31
            absc = bool((w0 >> 7) & 1)
            ops = []
            for (r, t, sw, ng) in (((w2 >> 16) & 0xff, (w2 >> 31) & 1, (w1 >> 16) & 0xff, (w1 >> 26) & 1),
                                   ((w2 >> 8) & 0xff, (w2 >> 30) & 1, (w1 >> 8) & 0xff, (w1 >> 25) & 1),
                                   (w2 & 0xff, (w2 >> 29) & 1, w1 & 0xff, (w1 >> 24) & 1)):
                ops.append(_alu_operand(regs, const, r, t, sw, ng, absc))
            vres = sres = None
            if vm:
                name = _VOPS.get(vop)
                a, b, c = ops
                if name is None or a is None or b is None:
                    return None
                if name == 'add':
                    vres = [a[i] + b[i] for i in range(4)]
                elif name == 'mul':
                    vres = [a[i] * b[i] for i in range(4)]
                elif name == 'max':
                    vres = [a[i] if a[i] >= b[i] else b[i] for i in range(4)]
                elif name == 'min':
                    vres = [a[i] if a[i] < b[i] else b[i] for i in range(4)]
                elif name == 'mad':
                    if c is None:
                        return None
                    vres = [a[i] * b[i] + c[i] for i in range(4)]
                else:
                    n = 4 if name == 'dp4' else 3
                    vres = [sum(a[i] * b[i] for i in range(n))] * 4
                if vclamp:
                    vres = [min(1.0, max(0.0, x)) for x in vres]
            if sm:
                name = _SOPS.get(sop)
                # the scalar operands are the third source's components `a`
                # (selected by its swizzle's W lane) and `b` (by its X lane)
                raw = _alu_operand(regs, const, w2 & 0xff, (w2 >> 29) & 1, 0,
                                   (w1 >> 24) & 1, absc)
                if name is None or raw is None:
                    return None
                sw = w1 & 0xff
                a = raw[(3 + ((sw >> 6) & 3)) & 3]
                b = raw[(0 + (sw & 3)) & 3]
                if name == 'adds':
                    x = a + b
                elif name == 'muls':
                    x = a * b
                elif name == 'maxs':
                    x = a if a >= b else b
                elif name == 'mins':
                    x = a if a < b else b
                elif name == 'exp':
                    x = 2.0 ** a
                elif name == 'log':
                    x = math.log(a, 2) if a > 0 else float('-inf')
                elif name == 'rcp':
                    x = 1.0 / a
                elif name == 'rsq':
                    x = 1.0 / math.sqrt(a)
                else:
                    x = math.sqrt(a)
                if sclamp:
                    x = min(1.0, max(0.0, x))
                sres = [x] * 4
            if export:
                cur = list(out.get(vdst, (0.0, 0.0, 0.0, 0.0)))
                for lane in range(4):
                    v_on, s_on = vm >> lane & 1, sm >> lane & 1
                    if v_on and s_on:
                        cur[lane] = 1.0
                    elif v_on:
                        cur[lane] = vres[lane]
                    elif s_on:
                        cur[lane] = sres[lane]
                out[vdst] = tuple(cur)
                continue
            if vm:
                cur = list(regs.get(vdst, (0.0, 0.0, 0.0, 0.0)))
                for lane in range(4):
                    if vm >> lane & 1:
                        cur[lane] = vres[lane]
                regs[vdst] = tuple(cur)
                written.add(vdst)
            if sm:
                cur = list(regs.get(sdst, (0.0, 0.0, 0.0, 0.0)))
                for lane in range(4):
                    if sm >> lane & 1:
                        cur[lane] = sres[lane]
                regs[sdst] = tuple(cur)
                written.add(sdst)
    except (ValueError, ZeroDivisionError, OverflowError):
        return None
    if 0 not in out:
        return None
    return out[0], src


def layer_blend(code, trials=3, channels=None):
    """The TWO-LAYER contract of one pixel program, or None.

    dict(base, layer, base_colour, layer_colour, base_coord, layer_coord,
    texel_alpha): the fetch constants (sampler slots) of the base and the
    layer image, the interpolators carrying each one's colour and coordinate,
    and whether the layer's weight includes the layer image's alpha - read by
    evaluating the program (run_pixel_program) with random inputs and the
    game's light state, and accepted only when exactly one assignment makes

        rgb == lerp(base_colour x t_base, layer_colour x t_layer, weight)

    hold on every trial, weight = layer_colour.a (x t_layer.a).

    `channels`, when given, are the layer texel's channels (0..3, None for
    none) the weight may take in place of its alpha, and the record names
    the one found as `texel_channel` (xblachar's two-layer record, whose
    tail number weighs by its image's blue). Without it the test is the
    alpha-or-nothing one above, unchanged."""
    import random
    rnd = random.Random(0x72)
    runs = []
    for _ in range(trials):
        inputs = {i: tuple(rnd.uniform(0.1, 0.9) for _k in range(4)) for i in range(16)}
        tex = {}
        cst = {}

        def texel(ci, tex=tex):
            if ci not in tex:
                tex[ci] = tuple(rnd.uniform(0.1, 0.9) for _k in range(4))
            return tex[ci]

        def const(r, cst=cst):
            if r in LAYER_GAME_STATE:
                return LAYER_GAME_STATE[r]
            if r in LAYER_NO_FOG:
                return LAYER_NO_FOG[r]
            if r not in cst:
                cst[r] = tuple(rnd.uniform(0.1, 0.9) for _k in range(4))
            return cst[r]
        got = run_pixel_program(code, inputs, texel, const)
        if got is None:
            return None
        runs.append((inputs, tex, got[0], got[1]))
    src = runs[0][3]
    if len(src) != 2 or any(v is None for v in src.values()) \
            or len(set(src.values())) != 2 or any(r[3] != src for r in runs):
        return None
    coords = set(src.values())
    found = []
    for base, layer in (tuple(sorted(src)), tuple(sorted(src))[::-1]):
        for cb in range(16):
            for cl in range(16):
                if cb in coords or cl in coords:
                    continue
                for ch in (channels if channels is not None else (3, None)):
                    ta = ch == 3
                    ok = True
                    for (inp, tex, o, _s) in runs:
                        w = inp[cl][3] * (tex[layer][ch] if ch is not None else 1.0)
                        for i in range(3):
                            b_ = inp[cb][i] * tex[base][i]
                            e = b_ + (inp[cl][i] * tex[layer][i] - b_) * w
                            if abs(o[i] - e) > 1e-5:
                                ok = False
                                break
                        if not ok:
                            break
                    if ok:
                        rec = dict(base=base, layer=layer, base_colour=cb,
                                   layer_colour=cl, base_coord=src[base],
                                   layer_coord=src[layer], texel_alpha=ta)
                        if channels is not None:
                            rec['texel_channel'] = ch
                        found.append(rec)
    return found[0] if len(found) == 1 else None


def _paired_pixel_objects(objs, graph=None):
    """{vertex object offset: pixel object index} over an effect list whose
    groups are N pixel objects followed by their N vertex objects, or None
    when a group's halves do not pair.

    #92 A GROUP MAY CARRY ITS VERTEX HALF MORE THAN ONCE: N pixel objects
    followed by k x N vertex objects, every further run of N a byte-for-byte
    copy of the first, pairs the n-th vertex object with pixel object n mod N.
    MEASURED on Depot's level item, the only one of the source's 24 background
    items with such a group: its first group is 3 pixel objects then 6 vertex
    objects, the last three identical to the first three, and the item's 33
    vertex-program records name all 33 vertex objects (one draw binds the
    copies). The other 23 items pair exactly as before. A further run that is
    not a copy - or a graph not given to compare it in - leaves the list
    unpaired, as before."""
    pixel = [x for x in objs if x[1] == 'P']
    index = {o: i for i, (o, _k, _w) in enumerate(pixel)}
    vs_pixel = {}
    i = 0
    while i < len(objs):
        j = i
        while j < len(objs) and objs[j][1] == 'P':
            j += 1
        k = j
        while k < len(objs) and objs[k][1] == 'V':
            k += 1
        n_p, n_v = j - i, k - j
        if n_v != n_p:
            if n_p == 0 or n_v % n_p or graph is None:
                return None
            for n in range(n_p, n_v):
                a, b = objs[j + n - n_p], objs[j + n]
                if bytes(graph[a[0]:a[0] + a[2][1]]) != bytes(graph[b[0]:b[0] + b[2][1]]):
                    return None
        for n in range(n_v):
            vs_pixel[objs[j + n][0]] = index[objs[i + n % n_p][0]]
        i = max(k, i + 1)
    return vs_pixel


def record_pixel_programs(graph, pool, recs):
    """{vertex-program record: its paired pixel program's microcode} for the
    records whose program can be placed in `pool`.

    Placed the way _pixel_program_layout places them - distinct objects in
    order of first use, 32-byte aligned, a flagged literal bank in front -
    but a program is KEPT only where pixel_programs, which finds programs
    from their own texture fetches and control flow independently of any
    layout, finds one starting at the same byte. On the Dam level item the
    running layout drifts from that walk on its last three objects, which is
    why _pixel_program_layout refuses the item as a whole; the cross-check
    keeps what both agree on and nothing else."""
    objs = shader_objects(graph)
    pixel = [x for x in objs if x[1] == 'P']
    vertex = [x[0] for x in objs if x[1] == 'V']
    records = sorted({a[4] for (_o, op, _l, a) in recs if op == OP_VS_BIND and len(a) >= 5})
    if not pixel or not records or len(records) != len(vertex) or \
            [r - records[0] for r in records] != [v - vertex[0] for v in vertex]:
        return {}
    vs_pixel = _paired_pixel_objects(objs, graph)
    if vs_pixel is None:
        return {}
    starts, p, seen = [], 0, {}
    for (o, _k, w) in pixel:
        key = bytes(graph[o:o + w[1]])
        if key in seen:
            starts.append(seen[key])
            continue
        lit = PS_LITERAL_BYTES if w[5] else 0
        s = (p + 31) // 32 * 32 + lit
        seen[key] = (s, w[2] - lit)
        starts.append(seen[key])
        p = s + w[2] - lit
    confirmed = {s for (s, _c, _l) in pixel_programs(pool)}
    out = {}
    for r, v in zip(records, vertex):
        s, n = starts[vs_pixel[v]]
        if s in confirmed and n > 0 and s + n <= len(pool):
            out[r] = bytes(pool[s:s + n])
    return out


# ---- the animated-water vertex program's contract (#69) --------------------
#
# WHAT THE PROGRAM DOES, read from its own microcode (the Xenos disassembly of
# the two water programs, and the vertex fetches decoded above), with every
# register named by the item's own constant table:
#
#   w      = wave pair (+0, s16, integer) * LITERAL.z
#   theta  = frac( -t * c34 + c33 + w.b * c35 + w.a * c36 )      per wave, x4
#   angle  = (theta * c29.x - c28.y) * c29.w
#   s      = angle * c30.x + angle^3 * c30.y + angle^5 * c30.z + angle^7 * c30.w
#   pos   += normal(+16) * dot(s, c32)
#
# c28.x is TIME, in seconds: measured on the console it starts near zero when
# gameplay begins and advances by exactly the frame's period every rendered
# frame, pause menu included; c33 (phase) is zero and no pixel-program
# constant moves, so this displacement is the whole of the material's motion.
# Everything else is data the item carries:
#
#   c32..c38  the WAVE TABLE, uploaded per draw by op 0x03 (amplitude, phase,
#             speed, the two frequency rows, two unused rows);
#   c28..c31  the EVALUATION constants, in the program's default block named
#             by op 0x20 (0.5 / 0.98 / 2 pi / the sine polynomial), whose rows
#             32..38 repeat the upload and are checked against it;
#   LITERAL   the program's own literal bank, the sixteen bytes in front of its
#             microcode in the blob: (0, 1, 1/128, 0) on both water programs,
#             .z being the scale the wave pair is read at.
#
# The item's own numbers go into the bundle; the runtime evaluates the shape
# above and holds none of them.
WAVE_MISC_REG = 28          # c28..c31: time slot and evaluation constants
WAVE_TABLE_REG = 32         # c32..c38: the per-draw wave table
WAVE_TABLE_ROWS = 7
WAVE_COUNT = 4              # one wave per register lane
WAVE_STRIDE_WORDS = 9       # the 36-byte pool, in fetch units
WAVE_FETCH_FMT = 25         # FMT_16_16


def wave_program_literal(blob, stride_words=WAVE_STRIDE_WORDS, wave_off=0):
    """The literal bank of every vertex program that fetches a `stride_words`
    record's wave pair at `wave_off`: {(x, y, z, w): programs}.

    A program's microcode begins with its control-flow words; the first of
    them is an `exec` whose address field says which instruction slot the
    program's first fetch sits in (both water programs: slot 7, six control-
    flow pairs and a seventh before it). So a wave fetch found at byte A
    belongs to the program starting at S = A - 12 * k for the k whose control-
    flow word at S is an exec with address k - which is checked, not assumed.
    The sixteen bytes before S are that program's literal bank.

    The fetch itself is the Xenos vertex-fetch encoding vfetch decodes:
    opcode 0, a FULL fetch, format 16_16, signed, integer, stride
    `stride_words`, offset `wave_off`, no exponent adjust."""
    W = _words(blob)
    found = {}
    for i in range(len(W) - 2):
        w0, w1, w2 = W[i], W[i + 1], W[i + 2]
        if (w0 & 31) != 0 or ((w0 >> 19) & 1) != 1:
            continue                       # not a full vertex fetch
        if (w1 >> 30) & 1:
            continue                       # a mini fetch
        if ((w1 >> 16) & 63) != WAVE_FETCH_FMT or not ((w1 >> 12) & 1):
            continue                       # not a signed 16_16
        if not ((w1 >> 13) & 1):
            continue                       # normalised, not integer
        if (w2 & 255) != stride_words or ((w2 >> 8) & 0x7fffff) != wave_off:
            continue
        a = i * 4
        for k in range(1, 24):
            s = a - 12 * k
            if s < 16:
                break
            c0, c1 = W[s // 4], W[s // 4 + 1]
            if ((c1 >> 12) & 15) == 1 and (c0 & 0xfff) == k:
                lit = struct.unpack('>ffff', blob[s - 16:s])
                found[lit] = found.get(lit, 0) + 1
                break
    return found


def wave_record(uploads, block_off, graph, literal):
    """The animation record one draw's program state describes, or
    (None, why).

    `uploads` is [(register, [float, ...]), ...] - every op-0x03 upload seen
    since the program was bound; `block_off` the op-0x20 default block;
    `literal` the program's literal bank. Every number in the record is the
    item's: the wave table from the upload, the evaluation constants from the
    default block, the coordinate scale from the literal. The block's copy of
    the table has to agree with the upload row for row, which is what says
    the block is laid out from WAVE_MISC_REG as this reader reads it."""
    if literal is None:
        return None, 'the program\'s literal bank was not found'
    if not (len(literal) == 4 and math.isfinite(literal[2]) and literal[2] > 0.0):
        return None, 'the literal bank carries no coordinate scale'
    rows = {}
    for (reg, vals) in uploads:
        for r in range(len(vals) // 4):
            row = tuple(vals[4 * r:4 * r + 4])
            prev = rows.get(reg + r)
            if prev is not None and prev != row:
                return None, 'the program variants upload different tables'
            rows[reg + r] = row
    table = [rows.get(WAVE_TABLE_REG + r) for r in range(WAVE_TABLE_ROWS)]
    if any(t is None for t in table):
        return None, 'no complete wave table was uploaded'
    if block_off is None:
        return None, 'the program names no default block'
    need = block_off + 4 + 16 * (WAVE_TABLE_REG + WAVE_TABLE_ROWS - WAVE_MISC_REG)
    if block_off < 0 or block_off % 4 or need > len(graph):
        return None, 'the default block lies outside the rendergraph'

    def brow(reg):
        o = block_off + 4 + 16 * (reg - WAVE_MISC_REG)
        return struct.unpack('>ffff', graph[o:o + 16])
    for r in range(WAVE_TABLE_ROWS):
        if brow(WAVE_TABLE_REG + r) != table[r]:
            return None, 'the default block does not repeat the uploaded table'
    misc, turn, poly = brow(WAVE_MISC_REG), brow(WAVE_MISC_REG + 1), brow(WAVE_MISC_REG + 2)
    rec = dict(mode='sinewaves',
               coord_scale=float(literal[2]),
               amplitude=tuple(table[0]), phase=tuple(table[1]),
               speed=tuple(table[2]),
               freq_b=tuple(table[3]),      # multiplies the pair's second value
               freq_a=tuple(table[4]),      # multiplies the pair's first value
               turn_scale=float(turn[0]), turn_bias=float(misc[1]),
               turn=float(turn[3]), poly=tuple(poly))
    flat = (list(rec['amplitude']) + list(rec['phase']) + list(rec['speed'])
            + list(rec['freq_a']) + list(rec['freq_b']) + list(rec['poly'])
            + [rec['turn_scale'], rec['turn_bias'], rec['turn'], rec['coord_scale']])
    if not all(math.isfinite(v) for v in flat) or rec['turn'] == 0.0:
        return None, 'the wave table carries a value no evaluation can use'
    if not any(v != 0.0 for v in rec['amplitude']):
        return None, 'every wave has zero amplitude'
    return rec, None


def _uv_format(blob, off, stride, count, tris, pos=0, texcoord=None, scale=None, short=False):
    """WHICH word of the vertex is the texture coordinate, and at WHAT SCALE.
    Both measured, neither decoded from the vertex declaration - except for a
    pool listed in POOL_LAYOUTS, whose position offset (`pos`), coordinate
    word (`texcoord`) and `scale` come from the item's own vertex program.
    There the edge test below still has to pass on that word.

    The bundle names a vertex DECLARATION per draw (opcode 0x0c's first
    argument), and a level bundle uses several. This reader does not decode
    them - it measures the two consequences that matter, and each measurement
    is arithmetic on the bytes.

    WHICH WORD, from the geometry. Along every triangle EDGE, take
    |world change| / |raw coordinate change|. A texture coordinate gives a
    consistent positive number, because a coordinate is a linear function of
    position over a face. A packed NORMAL - which is what the other word is -
    is CONSTANT over a face, so the only edges where it changes at all are the
    ones between faces, where the position barely changes and the value jumps:
    its ratio collapses to about zero. On the Dam's main pool, over 113972
    edges, the coordinate word reads 0.072 and the normal word reads 0.000.

    This is measured per EDGE and not per draw, and that is the correction that
    matters: a draw is a material BATCH and every face in it carries its own
    chart, so the batch's total coordinate span says nothing about the mapping.
    Measured per draw, the normal word wins - which is exactly the wrong answer,
    and it is what the first attempt at this item drew.

    AT WHAT SCALE, from the values themselves. A chart mapped across one whole
    texture puts its corners on exact multiples of the scale, so the raw values
    pile up there. On the Dam item the three most common absolute values are 0
    (86365 vertices), 128 (45200) and 127 (42974), and 25.8% of the non-zero
    values are exact multiples of 128 against a 0.78% chance - while only 0.47%
    are multiples of 256. So one texture is 128 raw units here. The skydome
    item's own declaration uses 16384, measured independently in the earlier
    round, which is why this is read per item and not fixed.

    Returns (offset, divisor, edge_median, evidence_fraction) or None when the
    words cannot be told apart or no scale stands out - refused, not guessed.
    With `short`, a pool whose values land on no divisor is asked
    chart_short's question before it is refused."""
    if texcoord is not None:
        if (texcoord < 0 or texcoord + 4 > stride - 4
                or (texcoord < pos + 12 and texcoord + 4 > pos)):
            return None
        cands = [texcoord]
    else:
        cands = [c for c in range(12, stride - 4, 4)]
        # A WORD THAT IS A UNIT NORMAL ON EVERY VERTEX IS NOT THE COORDINATE
        # (#81). The edge test below separates a normal from a coordinate
        # only where the normal changes between faces; a pool of flat,
        # axis-aligned quads carries one normal for many faces, and there it
        # measured the normal's word as the coordinate. Bunker 1's pool 5
        # (24-byte records): +12 reads 2_10_10_10 (0, 1, 0)-style unit normals
        # on all 398 vertices, +16 the s16 pair - the shaft's grate drew one
        # texel, the whole quad transparent. The test is _pool_sphere's.
        if len(cands) > 1:
            kept = [c for c in cands if _pool_normals(blob, off, stride, count, c) is None]
            if kept:
                cands = kept
    if not cands or not tris or pos < 0 or pos + 12 > stride - 4:
        return None
    best = None
    for cand in cands:
        ratios = []
        for (a, b2, c) in tris:
            for (p, q) in ((a, b2), (b2, c), (c, a)):
                if p >= count or q >= count:
                    return None
                oa, ob = off + p * stride, off + q * stride
                pa = struct.unpack('>fff', blob[oa + pos:oa + pos + 12])
                pb = struct.unpack('>fff', blob[ob + pos:ob + pos + 12])
                ta = struct.unpack('>hh', blob[oa + cand:oa + cand + 4])
                tb = struct.unpack('>hh', blob[ob + cand:ob + cand + 4])
                dl = math.sqrt(sum((pa[k] - pb[k]) ** 2 for k in range(3)))
                dt = math.hypot(ta[0] - tb[0], ta[1] - tb[1])
                if dl > 1.0 and dt > 0.5:
                    ratios.append(dl / dt)
        if not ratios:
            continue
        ratios.sort()
        med = ratios[len(ratios) // 2]
        if best is None or med > best[1]:
            best = (cand, med)
    if best is None or best[1] <= 0.0:
        return None
    cand, med = best

    raw = []
    for i in range(count):
        o = off + i * stride
        u, v = struct.unpack('>hh', blob[o + cand:o + cand + 4])
        raw.append(u)
        raw.append(v)
    nz = [x for x in raw if x]
    if not nz:
        return None
    if scale is not None:
        k = sum(1 for x in nz if x % int(scale) == 0)
        return (cand, float(scale), med, float(k) / len(nz))
    # A divisor is a candidate when at least 1% of the values land on it. The
    # scale is the candidate whose values pile up most ABOVE CHANCE - hits
    # times the divisor, since 1/d of evenly spread values are multiples of d -
    # not simply the largest candidate: a multiple of the true unit is also a
    # multiple of each of its divisors' halves, and in a small pool a handful
    # of corners at 4x the unit clear 1% on their own. Measured on the Dam's
    # 205-vertex ladder-and-rail pool: 23.6% of its values are multiples of
    # 128 and 1.75% of 512, so the largest candidate read it at 1/512 and drew
    # a quarter of each ladder image across a whole ladder - while the same
    # image drawn from two other pools reads at 1/128, as every pool of the
    # item does. Where the largest candidate IS the most enriched (every other
    # pool of the Dam), the answer is unchanged; ties keep the larger.
    #
    # THE UNIT ITSELF (#82). A chart laid across one whole image has a corner
    # AT the unit, so a candidate with no value at exactly +-d yields to the
    # candidates that have one: its hits are corners several images out, and
    # those land on every divisor of them too. MEASURED on Silo's cut-out pool
    # (pool 5, 2570 vertices: the stairs' and walkways' frames, gratings,
    # hatches and ladders): its quad decals run raw 0 .. 512 exactly and 44%
    # of its values are on 512, but 1/2048 won through 576 values of +-6144
    # (one draw's frames, twelve images along), none of them at 2048 - so
    # every decal drew a quarter of its image. Over the six stage items this
    # moves that pool, and lets one 4-vertex Silo pool that was refused at its
    # old scale read (a wall quad); every other pool reads as before.
    # Where no candidate with a value at its unit clears 1%, the choice is
    # the one above.
    def pick(only_units):
        best = None
        for d in UV_DIVISORS:
            if only_units and d not in nz and -d not in nz:
                continue
            k = sum(1 for x in nz if x % d == 0)
            if k * 100 >= len(nz):             # at least 1% land on the scale
                if best is None or k * d > best[1] * best[0]:
                    best = (d, k)
        return best
    best_d = pick(True)
    if best_d is not None:
        best_d = unit_edge(nz, best_d)
    else:
        best_d = pick(False)
    if best_d is not None:
        return (cand, float(best_d[0]), med, float(best_d[1]) / len(nz))
    return chart_short(nz, cand, med) if short else None


#: THE CHART SHORT OF ITS UNIT: how far below a divisor a pool's largest
#: value may stop, as a fraction of the divisor (chart_short).
CHART_SHORT = 0.02


def chart_short(nz, cand, med):
    """THE CHART SHORT OF ITS UNIT (Egyptian, `background/temple`). A pool
    whose values land on NO divisor - no candidate holds 1% of them - is read
    at the smallest divisor d that bounds every value (|x| <= d) when its
    largest value stops within CHART_SHORT of d: charts laid across one whole
    image, drawn a few raw units short of its edge. Asked last (read_level):
    only of a pool every other rule refused - its own pile-up, the item's
    scale and the layout's. MEASURED on the temple's
    palm fronds (pool 4, 1292 vertices, 323 quads on one 64x512 frond image):
    the coordinate word holds four pairs, (1021, -1), (1, -2), (1020, 1020)
    and (0, 1019) - each quad the whole image, a 1024-unit chart drawn three
    to five units short of both edges - so no value is a multiple of any
    divisor and the pool was refused; with it the item's bounds, which the
    palms set, disagreed with the geometry read and the whole background was
    refused. The private emulator draws each frond as one whole image (start
    courtyard, room 31). The trunks (pool 3, 703 vertices) reach 1010, 1.4%
    short of 1024. A pool any divisor already reads is not asked. Returns
    (offset, divisor, edge_median, evidence) - the evidence the share of
    values within CHART_SHORT of 0 or of +-d - or None."""
    top = max(abs(x) for x in nz)
    for d in sorted(UV_DIVISORS):
        if top <= d:
            if d - top > CHART_SHORT * d:
                return None
            near = sum(1 for x in nz if abs(x) <= CHART_SHORT * d or d - abs(x) <= CHART_SHORT * d)
            return (cand, float(d), med, float(near) / len(nz))
    return None


def _edge_values(nz, d):
    """How many of the values `nz` lie at a unit `d`'s chart edge: at +-d, and
    at +-(d - 1), the remaster's chart drawn to one raw unit short of it."""
    return sum(1 for x in nz if x in (d, -d, d - 1, 1 - d))


def unit_edge(nz, best):
    """THE UNIT'S EDGE (#93). `best` = (divisor, hits) is the enrichment pick
    of _uv_format among the divisors with a value at their unit. A smaller
    such divisor whose enrichment (hits x divisor) stands within
    UNIT_EDGE_NEAR of the pick's, and that holds at least UNIT_EDGE_FACTOR
    times as many values at its own chart edge (_edge_values: +-d and
    +-(d - 1)), is the unit instead - the one carrying the most edge values.
    Enrichment alone cannot tell two such divisors apart; where the charts
    END can. MEASURED on Train's item, pool 2 (1028 vertices: the
    compartments' luggage racks - rails, brackets and the net): 1.4% of its
    values on 1024 (enrichment 14.3, 24 values at its unit, none at 1023)
    against 10.2% on 128 (13.1, with 144 values at 128 and 242 at 127 - the
    pile-up the Dam's own pools show at their unit). Read at 1024, then 512
    (#81's item repeat), the net drew four times too coarse: five crosses
    along a rack where the game draws about twenty cells and the remaster's
    own frame (private emulator, debug camera, compartment room 19) draws a
    fine brass net. At 128 its texel size is the item's. Over the twelve
    stage items prepared before Train no other pool meets both conditions,
    and every pool reads as before. Returns (divisor, hits)."""
    d0, k0 = best
    e0 = _edge_values(nz, d0)
    got = best
    top = e0
    for d in UV_DIVISORS:
        if d >= d0 or (d not in nz and -d not in nz):
            continue
        k = sum(1 for x in nz if x % d == 0)
        if k * 100 < len(nz) or k * d * UNIT_EDGE_NEAR < k0 * d0:
            continue
        e = _edge_values(nz, d)
        if e >= UNIT_EDGE_FACTOR * max(e0, 1) and e > top:
            got, top = (d, k), e
    return got


#: #93 how near a smaller divisor's enrichment must stand to the pick's (a
#: factor) before unit_edge compares their chart edges ...
UNIT_EDGE_NEAR = 1.25
#: ... and how many times as many values it must hold at its own chart edge.
UNIT_EDGE_FACTOR = 4.0


def tied_divisors(blob, off, stride, count, uvo, chosen):
    """The candidate divisors SMALLER than `chosen` that the pool's own values
    cannot tell from it (#81): those on which the values on `chosen` land and
    next to no others - at most REPEAT_TIE_EXTRA of them more, or one. A
    value on a divisor is on every divisor of it, so such a pile-up says the
    coordinate is on `chosen`'s grid AND on each of these; which one is the
    image's repeat it does not say. (Bunker 1's floor: 41 values on 2048, 42
    on 1024 and on 512, 82 on 256.)"""
    if uvo is None or chosen is None or off + count * stride > len(blob):
        return []
    nz = []
    for i in range(count):
        o = off + i * stride + uvo
        nz.extend(x for x in struct.unpack('>hh', blob[o:o + 4]) if x)
    if not nz:
        return []
    hits = sum(1 for x in nz if x % int(chosen) == 0)
    slack = max(1, int(hits * REPEAT_TIE_EXTRA))
    return [d for d in UV_DIVISORS if d < chosen
            and sum(1 for x in nz if x % d == 0) - hits <= slack]


#: #86 how far apart a pool's repeat of an image and the item's other pools'
#: repeat of the same image must stand (a factor, on every image the pool
#: draws that another pool draws too) before image_repeat asks the pool again.
IMAGE_REPEAT_APART = 4.0


def _draw_repeats(V, ind):
    """Item units per one whole repeat of the image along each triangle edge
    of a draw: |position change| / |coordinate change|, for the edges where
    both change."""
    out = []
    for t in range(0, len(ind) - 2, 3):
        for a, b in ((ind[t], ind[t + 1]), (ind[t + 1], ind[t + 2]), (ind[t + 2], ind[t])):
            dl = math.sqrt(sum((V[a][k] - V[b][k]) ** 2 for k in range(3)))
            dt = math.hypot(V[a][3] - V[b][3], V[a][4] - V[b][4])
            if dl > 1.0 and dt > 1.0e-4:
                out.append(dl / dt)
    return out


def _median(xs):
    xs = sorted(xs)
    return xs[len(xs) // 2] if xs else None


def image_repeat(pools, bound, blob, lo, hi, rep):
    """THE IMAGE'S OWN REPEAT (#86): a pool that draws the item's images at a
    repeat IMAGE_REPEAT_APART or more away from the repeat the item's other
    pools draw the SAME images at - on every image it shares with them, all
    on the same side - is read again at the candidate divisor (at least 1% of
    its values on it) that brings every one of them within REPEAT_TIE_FACTOR
    of the others, when one does. An image is laid at one density across an
    item; a small pool's pile-up can name a multiple of its true unit.

    MEASURED on Statue's item (`background/statuepark`): pool 4 (16
    vertices, 8 triangles - a container's wall, roof and floor and one quad
    of path) holds 2 of its 28 values on 4096 and 7 on 256 and reads at
    1/4096, where its galvanised panel image (512x512) repeats every 1331
    world units; pool 1 draws the same image (two draws, 122 triangles) every
    95 and 202. At 1/4096 the container's 275-unit wall showed a sixth of the
    image - two flat bands, light over dark - where 1/256 lays 83 units a
    repeat. Its values say the same: 255, 2559, 2304, 4096, 247 and 253 sit
    on or one raw unit off 256's grid. A pool whose images no other pool
    draws, a pool within the factor, a stream's, a measured layout's, a
    sphere pool and one whose repeats disagree in direction read as before."""
    by_pool = {}
    for (d, q, ind) in bound:
        if d['image'] is None or pools[q].get('vertices') is None \
                or pools[q].get('sphere') is not None or not pools[q].get('uv_divisor'):
            continue
        ex = expand_indices(d['prim'], ind)
        by_pool.setdefault(q, []).append((d['image'], ex))
    ratios = {}                                   # (pool, image) -> edge repeats
    for q, ds in by_pool.items():
        V = pools[q]['vertices']
        for img, ex in ds:
            ratios.setdefault((q, img), []).extend(_draw_repeats(V, ex))
    for pi, p in enumerate(pools):
        if pi not in by_pool or p.get('stream') is not None \
                or pool_layout_stride(p) in POOL_LAYOUTS or p.get('repeat_from') is not None:
            continue
        mine = {}
        for img, _ex in by_pool[pi]:
            own = _median(ratios.get((pi, img), []))
            ref = _median([r for (q, im), rs in ratios.items() if im == img and q != pi for r in rs])
            if own and ref:
                mine[img] = own / ref
        if not mine:
            continue
        logs = [math.log(r) for r in mine.values()]
        if not (all(x >= math.log(IMAGE_REPEAT_APART) for x in logs)
                or all(x <= -math.log(IMAGE_REPEAT_APART) for x in logs)):
            continue
        nz = []
        for i in range(p['count']):
            o = p['off'] + i * p['stride'] + p['uv_offset']
            nz.extend(x for x in struct.unpack('>hh', blob[o:o + 4]) if x)
        best = None
        for d in UV_DIVISORS:
            if d == p['uv_divisor'] or sum(1 for x in nz if x % d == 0) * 100 < len(nz):
                continue
            worst = max(abs(x + math.log(d / p['uv_divisor'])) for x in logs)
            if worst <= math.log(REPEAT_TIE_FACTOR) and (best is None or worst < best[1]):
                best = (d, worst)
        if best is None:
            continue
        was = (p['uv_offset'], p['uv_divisor'], p['uv_edge'], p['uv_evidence'],
               p['vertices'], p.get('aux'), p.get('frame'))
        _pool_read(p, blob, lo, hi, (p['uv_offset'], float(best[0]), p['uv_edge'],
                                     p['uv_evidence']), rep)
        if p['vertices'] is None:
            (p['uv_offset'], p['uv_divisor'], p['uv_edge'], p['uv_evidence'],
             p['vertices'], p['aux'], p['frame']) = was
            continue
        p['image_repeat_from'] = was[1]
        rep['image_repeat_pools'] = rep.get('image_repeat_pools', 0) + 1


#: #81 how far a pool's repeat may stand from the item's dominant pool's before
#: the tie is left as measured (a factor; 1.5 accepts only the nearer power).
REPEAT_TIE_FACTOR = 1.5
#: ... and how many more values a smaller divisor may hold and still be tied.
REPEAT_TIE_EXTRA = 0.05
#: ... and only a pool whose measured divisor holds fewer than this share of its
#: values is asked: a pile-up as thin as the floor's (1.2%) places a grid, not
#: a repeat; one as thick as the gratings' (34.3% on 1/256, pool 5, its
#: coordinate word read past the normal) names its repeat and is left alone.
REPEAT_WEAK = 0.05


def _pool_vertices(blob, off, stride, count, lo, hi, uvo, uvdiv, pos=0, co=None):
    """Decode one vertex pool, or None when its layout does not validate.

    MEASURED layout:

        `pos`         f32 x, y, z       position: +0, or what POOL_LAYOUTS
                                        names for the stride
        `uvo`         s16 u, s16 v      the texture coordinate, SIGNED 16-bit
                                        fixed point over `uvdiv` - both found
                                        by _uv_format, neither assumed
        stride - 4    u8 a, b, g, r     the vertex colour, BYTE-REVERSED like
                                        every other field here - see
                                        `vertex_colour`; `co` names another
                                        colour word of the record instead

    Alpha is the FIRST byte: 0xff on all 132860 vertices of the Dam item's
    four large pools, and 0x80, 0x8b or 0x99 on the small ones. Read as alpha
    that is "opaque everywhere except a few translucent decals", which is what
    a level background is; read as red it would be "the red channel is
    saturated on every vertex of the level", which nothing else in the bundle
    supports. The three bytes after it are B, G, R, not R, G, B, and the
    measurement that says so is in `vertex_colour`.

    VALIDATED, not trusted: every position must be finite and inside the
    bundle's own declared bounds, and every texture coordinate must be inside
    a range a sampler can carry. A pool that fails either is refused and
    REPORTED - never drawn from a layout that happens not to crash."""
    if stride < 24 or count <= 0:
        return None
    if off + count * stride > len(blob):
        return None
    if co is None:
        co = stride - 4
        if uvo + 4 > co or pos < 0 or pos + 12 > co:
            return None
    elif co < 0 or co + 4 > stride or pos < 0 or pos + 12 > stride \
            or uvo < 0 or uvo + 4 > stride \
            or (uvo < co + 4 and uvo + 4 > co) or (co < pos + 12 and co + 4 > pos):
        return None
    if uvo < pos + 12 and uvo + 4 > pos:
        return None                        # the coordinate cannot overlap it
    out = []
    for i in range(count):
        o = off + i * stride
        x, y, z = struct.unpack('>fff', blob[o + pos:o + pos + 12])
        for j, c in enumerate((x, y, z)):
            if not math.isfinite(c) or c < lo[j] or c > hi[j]:
                return None
        u, v = struct.unpack('>hh', blob[o + uvo:o + uvo + 4])
        if abs(u) > uvdiv * 1024 or abs(v) > uvdiv * 1024:
            return None
        r, g, bb, a = vertex_colour(blob[o + co], blob[o + co + 1],
                                    blob[o + co + 2], blob[o + co + 3])
        out.append((x, y, z, u / uvdiv, v / uvdiv, r, g, bb, a))
    return out



# ---- a pool that carries a NORMAL where a coordinate would be (#81) -------
#
# MEASURED on Bunker 1's item: the railings - every handrail, stair rail and
# post of the control room, the vent room and the stairs, 3216 triangles in
# two draws - stand in one 2990-vertex pool of 20-byte records whose single
# word between the position and the colour is not a texture coordinate. Read
# as a 2_10_10_10 signed normal (snorm10x3) it has length 0.997 .. 1.000 on
# all 2990 vertices; read as an s16 pair it measures 0.0009 units per raw
# unit along an edge and fails validation, so the pool was refused and the
# rails were never drawn. The colour word carries the rails' baked shading
# (greys 0x1a .. 0xb3). The draws' material binds a chrome sphere image to
# slot 0 (a 128x128 reflective bubble) and the room's own fisheye panorama
# to slot 1, and the program fetches both at coordinates its vertex program
# COMPUTES - the source sphere-maps the rails by the vertex normal.
#
# Such a pool is read with the sphere coordinate of its normal in the place
# of the coordinate it does not have: u = 0.5 + 0.5 n.x, v = 0.5 - 0.5 n.y -
# the slot-0 image looked up by the normal, taken in the item's own frame
# (the world's up to a uniform scale). That is the reflection held still:
# the source's turns with the eye, this one does not, and the panorama term
# is not drawn. Taken only when EVERY vertex's word is a unit normal (within
# SPHERE_NORMAL_TOL) and the record has exactly that one word between the
# position and the colour; any other pool is refused as before.
#
# The same record carries the item's UNTEXTURED surfaces: draws that name no
# image at all, drawn by their vertex colour - on Bunker 1 a 154-vertex pool
# holding the control room's black screen backing (one quad, colour
# ff000000, standing in the wall's opening behind the game's screens), the
# ceiling lamps' warm faces and a few frames. Its words are unit normals
# too (0.998 .. 1.000 on all 154). Such a pool is read the same way; the
# coordinate it is given is never sampled (prepare.UNTEXTURED).
SPHERE_NORMAL_TOL = 0.01


def _pool_sphere(blob, off, stride, count, lo, hi, pos=0):
    """Decode one pool whose record carries a unit normal and no texture
    coordinate (above), or None when it is not one: the same tuples as
    _pool_vertices, the coordinate the normal's sphere coordinate."""
    co = stride - 4
    nw = pos + 12
    if stride < 20 or count <= 0 or pos != 0 or nw + 4 != co:
        return None
    if off + count * stride > len(blob):
        return None
    out = []
    for i in range(count):
        o = off + i * stride
        x, y, z = struct.unpack('>fff', blob[o + pos:o + pos + 12])
        for j, c in enumerate((x, y, z)):
            if not math.isfinite(c) or c < lo[j] or c > hi[j]:
                return None
        n = snorm10x3(struct.unpack('>I', blob[o + nw:o + nw + 4])[0])
        ln = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
        if abs(ln - 1.0) > SPHERE_NORMAL_TOL:
            return None
        r, g, bb, a = vertex_colour(blob[o + co], blob[o + co + 1],
                                    blob[o + co + 2], blob[o + co + 3])
        out.append((x, y, z, n[0] / ln, n[1] / ln, n[2] / ln, r, g, bb, a))
    uvs = sphere_uvs([v[3:6] for v in out])
    return [v[:3] + uv + v[6:] for v, uv in zip(out, uvs)]


#: #81 a pool whose normals average to at least this length FACES one way (a
#: dome, a porthole), and its sphere coordinate is taken as seen face-on.
SPHERE_FACING = 0.5


def sphere_uvs(normals):
    """The held-still sphere coordinate of each unit normal (#81): u = 0.5 +
    0.5 n.x, v = 0.5 - 0.5 n.y in a frame that stands for the eye. For a pool
    whose normals point every way (the rails: mean length 0.02) the frame is
    the item's own axes. A pool that FACES one way (mean length at least
    SPHERE_FACING - the porthole's dome, 0.81, facing -x) is taken as seen
    face-on: the frame's depth axis is its mean normal and its up axis the
    item's up, so the dome's middle samples the image's middle. Taken in the
    item's axes, a dome facing along x put its middle on the image's edge
    and drew a dark slit between two blue rims."""
    n = len(normals)
    if n == 0:
        return []
    m = [sum(v[k] for v in normals) / n for k in range(3)]
    ml = math.sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2])
    xa, ya = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)
    if ml >= SPHERE_FACING:
        w = (m[0] / ml, m[1] / ml, m[2] / ml)
        ref = (0.0, 1.0, 0.0) if abs(w[1]) < 0.9 else (1.0, 0.0, 0.0)
        x = (ref[1] * w[2] - ref[2] * w[1], ref[2] * w[0] - ref[0] * w[2],
             ref[0] * w[1] - ref[1] * w[0])
        xl = math.sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2])
        xa = (x[0] / xl, x[1] / xl, x[2] / xl)
        ya = (w[1] * xa[2] - w[2] * xa[1], w[2] * xa[0] - w[0] * xa[2],
              w[0] * xa[1] - w[1] * xa[0])
    return [(0.5 + 0.5 * (v[0] * xa[0] + v[1] * xa[1] + v[2] * xa[2]),
             0.5 - 0.5 * (v[0] * ya[0] + v[1] * ya[1] + v[2] * ya[2])) for v in normals]



def _pool_normals(blob, off, stride, count, nw):
    """Every vertex's 2_10_10_10 word at +nw read as a normal, or None unless
    each is a unit normal (within SPHERE_NORMAL_TOL) - the test _pool_sphere
    makes, for a pool that also carries a coordinate (#81)."""
    if nw < 12 or nw + 4 > stride - 4 or off + count * stride > len(blob):
        return None
    out = []
    for i in range(count):
        o = off + i * stride + nw
        n = snorm10x3(struct.unpack('>I', blob[o:o + 4])[0])
        ln = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
        if abs(ln - 1.0) > SPHERE_NORMAL_TOL:
            return None
        out.append((n[0] / ln, n[1] / ln, n[2] / ln))
    return out


# ---- which shader programs consume the stage-fog contract (#68) -----------
#
# The rendergraph carries each shader program's CONSTANT TABLE: the constant
# names it declares, followed by its target profile string. Measured on the Dam
# items: every table ends in exactly one of `ps_3_0` / `vs_3_0`, the names
# between one profile string and the next are the next program's, and 42 of 42
# pixel programs of the level item declare `c_fog` and `c_fogcolour` - the pair
# its pixel programs end in `lerp(c_fogcolour, colour, saturate(d * c_fog.x +
# c_fog.y))` with. So a pixel program's table is SEGMENTED by those strings
# and asked for both names.
#
# The draw -> program binding is NOT decoded here, so the answer is per ITEM
# and deliberately conservative: only when every pixel program declares the
# pair is every material marked as consuming it. A split item is reported and
# left unmarked rather than guessed at per draw.
PROFILE_PS = b'ps_3_0\x00'
PROFILE_VS = b'vs_3_0\x00'
FOG_NAMES = (b'c_fog\x00', b'c_fogcolour\x00')


def fog_programs(graph):
    """(pixel programs, pixel programs declaring both stage-fog constants)
    over the constant tables in a rendergraph."""
    marks = sorted([(m.start(), 'ps') for m in re.finditer(re.escape(PROFILE_PS), graph)]
                   + [(m.start(), 'vs') for m in re.finditer(re.escape(PROFILE_VS), graph)])
    nps = nfog = 0
    prev = 0
    for (at, kind) in marks:
        seg = graph[prev:at]
        if kind == 'ps':
            nps += 1
            if all(n in seg for n in FOG_NAMES):
                nfog += 1
        prev = at + len(PROFILE_PS)
    return (nps, nfog)


def level_draw_records(recs):
    """Every draw of a walked LEVEL command list, in command-list order:
    [dict(at, prim, count, handle, image, slots, style)], each carrying the
    material and style standing where it is recorded, and the vertex-program
    records its binding names (`vs_records`, in the order op 0x2e names them).

    Both draw records count - op 0x01 and the conditional op 0x30 (see
    OP_DRAW_GATED); the condition word is not a property of what is drawn and
    is not kept. A record too short to name its index buffer is not a draw."""
    raw = []
    mat = None
    flags = None
    style = None
    uploads = []                 # the vertex program's op-0x03 uploads (#69)
    block = None                 # ... and its op-0x20 default block
    ps_uploads = []              # the pixel program's op-0x06 uploads
    binds = []                   # the vertex-program records bound (#72)
    xform = None                 # the standing instance matrix (#80)
    cull = 0                     # the standing RS_CULL value (#96)
    drew = True
    for (o, op, _low, a) in recs:
        if op in OP_RENDER_STATE and len(a) >= 2 and a[0] == RS_CULL:
            cull = a[1]
        elif op == OP_MATERIAL:
            mat = material_slots(a)
            flags = material_slot_flags(a)
        elif op == OP_STYLE and len(a) >= 2:
            style = a[1]
        elif op == OP_INSTANCE and len(a) == INSTANCE_WORDS:
            xform = instance_matrix(a)
        elif op == OP_INSTANCE_END and len(a) == INSTANCE_WORDS:
            xform = None
        elif op == OP_VS_BIND:
            uploads = []         # a new program: nothing carries over
            block = None
            ps_uploads = []
            if drew:
                binds, drew = [], False
            if len(a) >= 5:
                binds.append(a[4])
        elif op == OP_VS_DEFAULTS and len(a) >= 4:
            block = a[3]
        elif op in (OP_VS_CONSTANTS, OP_PS_CONSTANTS) and len(a) >= 2:
            reg, cnt = (a[1] >> 16) & 0xffff, a[1] & 0xffff
            if 0 < cnt <= 64 and len(a) >= 2 + 4 * cnt:
                vals = struct.unpack('>%df' % (4 * cnt),
                                     struct.pack('>%dI' % (4 * cnt), *a[2:2 + 4 * cnt]))
                (uploads if op == OP_VS_CONSTANTS else ps_uploads).append((reg, list(vals)))
        elif op in (OP_DRAW, OP_DRAW_GATED) and len(a) >= 3:
            raw.append(dict(at=o, prim=a[0], count=a[1], handle=a[2],
                            image=(mat or {}).get(0), slots=dict(mat or {}),
                            slot_flags=dict(flags or {}),
                            style=style, vs_uploads=list(uploads),
                            vs_block=block, ps_uploads=list(ps_uploads),
                            vs_records=list(binds), xform=xform, cull=cull))
            drew = True
    return raw


def instance_matrix(args):
    """The 4x4 row-vector matrix of an op-0x21 record (OP_INSTANCE), as 16
    floats in row order, or None for a record that is not one: a finite
    matrix whose last column is (0, 0, 0, 1) and whose upper 3x3 has rows
    pairwise perpendicular (to INSTANCE_EPS, after normalising) and each of
    some length - a placement: a rotation, with a scale per axis. Of
    Surface's 126 records 124 are rotations about y with unit rows and two
    stretch the tree by 1.2113 along y."""
    if len(args) != INSTANCE_WORDS:
        return None
    f = struct.unpack('>16f', struct.pack('>16I', *args[3:19]))
    if not all(math.isfinite(v) for v in f):
        return None
    if any(abs(f[k] - (1.0 if k == 15 else 0.0)) > INSTANCE_EPS for k in (3, 7, 11, 15)):
        return None
    rows = [f[0:3], f[4:7], f[8:11]]
    lens = [math.sqrt(sum(v * v for v in r)) for r in rows]
    if any(ln <= INSTANCE_EPS for ln in lens):
        return None
    for i in range(3):
        for j in range(i + 1, 3):
            if abs(sum(rows[i][k] * rows[j][k] for k in range(3))) > INSTANCE_EPS * lens[i] * lens[j]:
                return None
    return tuple(f)


def instance_apply(xform, x, y, z):
    """(x, y, z) under a row-vector matrix from instance_matrix."""
    return (x * xform[0] + y * xform[4] + z * xform[8] + xform[12],
            x * xform[1] + y * xform[5] + z * xform[9] + xform[13],
            x * xform[2] + y * xform[6] + z * xform[10] + xform[14])


#: How far an instance matrix may stray from a rigid placement (instance_matrix).
INSTANCE_EPS = 1e-3


def draw_layers(code, fields, slots, pool, blob, lo, hi):
    """(layer record, None) for a draw whose program is the two-layer blend
    (layer_blend) and whose pool carries the fields it reads, or (None, why)
    - why is empty when the program is simply not that blend.

    The record: dict(base, layer) of (slot, vertices), each `vertices` the
    pool read again with that surface's own coordinate and colour words
    (LAYER_INPUTS), and `texel_alpha` as layer_blend says. Both coordinates
    are read over the pool's measured divisor: the vertex program scales
    them by the same literal."""
    lb = layer_blend(code)
    if lb is None:
        return None, ''
    if sorted(slots) != sorted((lb['base'], lb['layer'])):
        return None, 'its program samples slots %s, the draw names %s' % (
            sorted((lb['base'], lb['layer'])), sorted(slots))
    return _layer_record(lb, fields, slots, pool, blob, lo, hi)


#: #85 THE TWO-LAYER LAYOUT'S OWN ASSIGNMENT. Every draw the two-layer test
#: (layer_blend) confirms on a 32-byte pool assigns the same way: 42 of 42
#: over the Dam, Facility, Runway, Surface, Silo, Frigate and Bunker 2 items -
#: the base is slot 1 at +20 with the colour at +28, the layer slot 0 at +16
#: with the colour (and its alpha, the weight) at +24. In the interpolators
#: LAYER_INPUTS names: base coordinate 2, base colour 5, layer coordinate 1,
#: layer colour 4. layout_layers carries a draw its program does not decide
#: by this assignment.
LAYER_CONTRACT = dict(base=1, layer=0, base_coord=2, base_colour=5,
                      layer_coord=1, layer_colour=4)


def layout_layers(codes, fields, slots, pool, blob, lo, hi):
    """(record, None) for a draw on a two-layer pool that names exactly
    slots 0 and 1 and whose program does not decide it - none of its program
    variants can be placed, or those that can sample no slot but at the
    coordinate LAYER_CONTRACT gives it - carried by LAYER_CONTRACT; or
    (None, why). `codes` are the draw's placed program variants. The
    record's texel_alpha is left None: the caller sets it from the layer
    image (its alpha is the layer's shape where it has one).

    MEASURED on Bunker 2's item (#85): the door threshold's quad (pool 15)
    and the helipad's rim (pool 9) are 32-byte pools whose draws name two
    plain images, the threshold's a hazard-stripe layer (slot 0, its +24
    colour yellow at alpha 0x80) over the corridor's floor image (slot 1,
    +28 an opaque grey), the rim's a snow-drift layer whose image alpha is
    the drift's edge, over a cracked-concrete base. No program variant of
    the rim's draw can be placed; the threshold's one placed variant samples
    slot 0 alone, at interpolator 1, the layer's coordinate. Drawn as one
    surface the rim painted the snow image at the base's coordinate,
    transparent below the drift edge, and the threshold drew the stripe
    image opaque in the base's colour. A program that samples a slot at
    another coordinate (the Dam's and Runway's two-image terrain, both slots
    at interpolator 0) is not this blend, and its draw is left as it was."""
    import random
    if sorted(slots) != sorted((LAYER_CONTRACT['layer'], LAYER_CONTRACT['base'])):
        return None, 'it names slots %s' % sorted(slots)
    want = {LAYER_CONTRACT['layer']: LAYER_CONTRACT['layer_coord'],
            LAYER_CONTRACT['base']: LAYER_CONTRACT['base_coord']}
    for code in codes:
        rnd = random.Random(0x85)
        inputs = {i: tuple(rnd.uniform(0.1, 0.9) for _k in range(4)) for i in range(16)}
        got = run_pixel_program(
            code, inputs, lambda _ci: (0.5, 0.5, 0.5, 0.5),
            lambda r: LAYER_GAME_STATE.get(r, LAYER_NO_FOG.get(r, (0.5, 0.5, 0.5, 0.5))))
        if got is None:
            continue
        for ci, interp in got[1].items():
            if want.get(ci) != interp:
                return None, 'its program samples slot %d at %s' % (ci, interp)
    return _layer_record(dict(LAYER_CONTRACT, texel_alpha=None),
                         fields, slots, pool, blob, lo, hi)


def _layer_record(lb, fields, slots, pool, blob, lo, hi):
    """The record draw_layers returns, for the assignment `lb`."""
    words = {}
    for role in ('base', 'layer'):
        uvo = fields.get(lb[role + '_coord'])
        co = fields.get(lb[role + '_colour'])
        if uvo is None or co is None:
            return None, 'the %s reads an interpolator this pool layout does not name' % role
        if pool.get('vertices') is None or pool.get('uv_divisor') is None:
            return None, 'its pool was not read'
        pos = pool_layout(pool['stride'])[0]
        vs = _pool_vertices(blob, pool['off'], pool['stride'], pool['count'], lo, hi,
                            uvo, pool['uv_divisor'], pos=pos, co=co)
        if vs is None:
            return None, 'the %s words (+%d, +%d) do not validate' % (role, uvo, co)
        words[role] = (slots[lb[role]], (uvo, co), vs)
    return dict(base=words['base'], layer=words['layer'],
                texel_alpha=lb['texel_alpha']), None


def item_scale(pools):
    """The coordinate scale every pool that validated agrees on, or None
    when none validated or they disagree (read_level, THE ITEM'S SCALE FOR A
    POOL TOO SMALL TO SHOW ITS OWN)."""
    got = {p['uv_divisor'] for p in pools if p.get('vertices') is not None}
    return got.pop() if len(got) == 1 else None


def layout_scale(pools, stride):
    """The coordinate scale every validated pool of `stride` (no trailing
    stream) agrees on, or None when none validated or they disagree
    (read_level, THE LAYOUT'S SCALE WHERE THE ITEM'S POOLS DISAGREE)."""
    got = {p['uv_divisor'] for p in pools
           if p.get('vertices') is not None and p.get('stream') is None
           and p['stride'] == stride and p.get('uv_divisor')}
    return got.pop() if len(got) == 1 else None


def pool_layout_stride(p):
    """The stride POOL_LAYOUTS is asked about for a pool: its own, or none
    for a pool with a trailing stream - a measured layout names one buffer's
    words, and a record widened by a stream (pool_source) is not that
    buffer, so such a pool is measured like any unlisted one."""
    return None if p.get('stream') is not None else p['stride']


def _pool_read(p, blob, lo, hi, got, rep):
    """Decode and validate one pool at the coordinate word and scale
    `got` (_uv_format), with its animation fields and tangent frame."""
    pos = pool_layout(pool_layout_stride(p))[0]
    p['uv_offset'], p['uv_divisor'], p['uv_edge'], p['uv_evidence'] = got
    src, soff, sstride = pool_source(p, blob)
    p['vertices'] = _pool_vertices(src, soff, sstride, p['count'],
                                   lo, hi, got[0], got[1], pos=pos)
    # THE ANIMATION FIELDS (#69), for a layout that names them. Refused
    # separately from the pool: a pool whose wave pair or normal does not
    # validate still draws, it just does not move.
    wave, normal = pool_aux_layout(pool_layout_stride(p))
    p['aux'] = None
    if p['vertices'] is not None and wave is not None:
        p['aux'] = _pool_aux(blob, p['off'], p['stride'], p['count'],
                             wave, normal, pos, got[0])
        if p['aux'] is None:
            rep['bad_aux'] = rep.get('bad_aux', 0) + 1
    # THE TANGENT FRAME (#69 material), the same way: a pool whose frame
    # does not validate still draws and still moves, its material just
    # has no frame to shade with.
    tx, ty = pool_frame_layout(pool_layout_stride(p))
    p['frame'] = None
    if p['vertices'] is not None and tx is not None:
        p['frame'] = _pool_frame(blob, p['off'], p['stride'], p['count'],
                                 tx, ty, pos, got[0])
        if p['frame'] is None:
            rep['bad_frame'] = rep.get('bad_frame', 0) + 1


def read_level(path, short=False):
    """Read one LEVEL world bundle. With `short`, a pool every other
    rule refuses is asked chart_short's question. Returns

        dict(pools   = [dict(off, stride, count, vertices)],
             draws   = [dict(at, prim, count, pool, indices, image, style,
                             pass_mask)],
             textures= [dict(name, w, h, fmt, rgba, has_alpha)],
             declared= dict(radius, centre, extent) or None,
             report  = dict(...)   what was read, what was refused and why)

    Every draw comes back with the triangle list its primitive type expands
    into and the index of its base image, or None when the bundle names none.
    Nothing is invented: a draw whose buffers do not resolve, whose indices
    leave its pool, or whose pool does not validate is DROPPED and counted.

    Raises WorldAssetError only when the bundle is not one this reader
    understands at all."""
    b, m, blob, graph, stream = _model_parts(path)
    name = os.path.basename(path)
    blob_len = len(blob)

    recs, end, complete = command_records(stream)
    if not recs:
        raise WorldAssetError('%s: this item\'s command list does not walk as '
                              'a record stream' % name)

    declared = declared_bounds(graph)
    if declared is None:
        raise WorldAssetError('%s: this item declares no bounds, so nothing '
                              'read from it could be checked' % name)
    c, e = declared['centre'], declared['extent']
    pad = [abs(e[j]) * 0.01 + 1.0 for j in range(3)]
    lo = [c[j] - e[j] - pad[j] for j in range(3)]
    hi = [c[j] + e[j] + pad[j] for j in range(3)]

    # 1. the draws, in command-list order, with the standing material and style
    raw = level_draw_records(recs)
    if not raw:
        raise WorldAssetError('%s: this item\'s command list carries no draw' % name)
    # ... and the pass gate governing each (draw_passes: the innermost op-0x15
    # / op-0x1a mask, which may carry the decal bit where the style word does
    # not - Runway's dock numbers stand under a 0x2702 gate as style 0x4701)
    passes = draw_passes(recs)

    # 2. every draw's index buffer, by handle AND by exact consumed length
    wanted = {}
    for d in raw:
        t = primitive_triangles(d['prim'], d['count'])
        if t is None:
            continue
        wanted.setdefault(d['handle'], set()).update(
            ((d['count'] * 2, IB_16), (d['count'] * 4, IB_32)))
    ibs = index_buffer_records(graph, blob_len, wanted)

    rep = dict(records=len(recs), consumed=end, complete=complete,
               draws=len(raw), bad_primitive=0, no_index_buffer=0,
               ambiguous_index_buffer=0, no_pool=0, out_of_range=0,
               bad_pool=0, ambiguous_pool=0, no_image=0)
    res = []
    for d in raw:
        t = primitive_triangles(d['prim'], d['count'])
        if t is None:
            rep['bad_primitive'] += 1
            continue
        cand = sorted(c2 for c2 in ibs.get(d['handle'], ())
                      if c2[1] in (d['count'] * 2, d['count'] * 4))
        if not cand:
            rep['no_index_buffer'] += 1
            continue
        if len(cand) > 1:
            rep['ambiguous_index_buffer'] += 1
            continue
        off, ln, kind = cand[0]
        d = dict(d, ioff=off, ilen=ln, iw=(2 if kind == IB_16 else 4), tris=t)
        res.append(d)
    if not res:
        raise WorldAssetError('%s: no draw in this item resolves to an index '
                              'buffer' % name)

    # 3. the index RUNS, and the vertex pool each one begins at the end of
    runs = []
    for off, ln in sorted(set((d['ioff'], d['ilen']) for d in res)):
        if runs and off <= runs[-1][1] + 4:
            runs[-1][1] = max(runs[-1][1], off + ln)
        else:
            runs.append([off, off + ln])
    pools = []
    seen = {}
    for (r0, _r1) in runs:
        vb, n, strm = pool_ending_at(graph, blob_len, r0)
        if vb is None:
            continue                       # a run that continues the last pool
        if n > 1:
            rep['ambiguous_pool'] += 1
        key = (vb[2], vb[1], vb[4])
        if key in seen:
            continue
        seen[key] = len(pools)
        pools.append(dict(off=vb[2], stride=vb[1], count=vb[4], vertices=None,
                          uv_offset=None, uv_divisor=None, uv_edge=None,
                          uv_evidence=None, stream=strm))
    # A pool's END, for binding a draw's index run to it, is where its LAST
    # buffer ends: the stream's, when it has one (pool_ending_at).
    if any(p['stream'] is not None for p in pools):
        rep['streamed_pools'] = sum(1 for p in pools if p['stream'] is not None)
    if not pools:
        raise WorldAssetError('%s: no index run in this item begins at a '
                              'vertex buffer, so no pool can be named' % name)
    def pool_end(p):
        if p['stream'] is not None:
            return p['stream'][0] + p['count'] * p['stream'][1]
        return p['off'] + p['count'] * p['stride']

    ends = sorted((pool_end(p), i) for i, p in enumerate(pools))

    def pool_for(ioff):
        best = None
        for (e2, i) in ends:
            if e2 <= ioff:
                best = i
            else:
                break
        return best

    # 4. bind each draw to a pool, read its indices, and range-check them
    bound = []
    for d in res:
        pi = pool_for(d['ioff'])
        if pi is None:
            rep['no_pool'] += 1
            continue
        p = pools[pi]
        ind = struct.unpack('>%d%s' % (d['count'], 'H' if d['iw'] == 2 else 'I'),
                            blob[d['ioff']:d['ioff'] + d['ilen']])
        if max(ind) >= p['count']:
            rep['out_of_range'] += 1
            continue
        bound.append((d, pi, ind))

    # 5. per pool: WHERE the texture coordinate is, measured from its own
    #    draws, then decode and validate the pool once.
    #
    #    THE ITEM'S SCALE FOR A POOL TOO SMALL TO SHOW ITS OWN (#77). The
    #    scale is read from where a pool's values pile up, and a pool of a
    #    few vertices may pile up nowhere: Facility's 20-vertex two-layer
    #    pool (a door frame's hazard stripes over its concrete) has no value
    #    on any candidate divisor, so it was refused and the frame drew
    #    without its stripes. Its vertex program scales its coordinates by
    #    the same literal as every other pool's (draw_layers), so such a pool
    #    takes the scale every pool of the item that did measure one agrees
    #    on - only when they all agree, and still through the edge test and
    #    the validation below. A pool that measures its own is unchanged.
    #
    #    THE LAYOUT'S SCALE WHERE THE ITEM'S POOLS DISAGREE (#85). A
    #    two-layer pool (LAYER_INPUTS) whose item's pools measure several
    #    scales takes the scale every two-layer pool of the item that did
    #    measure one agrees on (layout_scale): the 32-byte record is read by
    #    its own family of vertex programs, and MEASURED on Bunker 2's item
    #    every vertex program that fetches a 32-byte record carries the one
    #    coordinate scale 1/256 in its literal bank, the scale its read
    #    two-layer pool (pool 1, 174 vertices) measures - while its 24-byte
    #    pools measure 1/64 .. 1/512, so the item has no scale. Its two
    #    refused two-layer pools are a door threshold's hazard stripes over
    #    the floor (pool 15, 4 vertices) and the helipad's rim (pool 9, 56
    #    vertices); refused, the floor under the doorway and round the pad
    #    was missing and the clear colour showed (playtest run
    #    20261007-085655, marks 1 and 4). Still through the edge test and the
    #    validation; an item with no such measured pool is unchanged.
    retry = []
    for pi, p in enumerate(pools):
        tris = []
        for (d, q, ind) in bound:
            if q != pi:
                continue
            ex = expand_indices(d['prim'], ind)
            for t in range(0, len(ex) - 2, 3):
                tris.append((ex[t], ex[t + 1], ex[t + 2]))
                if len(tris) >= 3000:
                    break
            if len(tris) >= 3000:
                break
        if not tris:
            continue
        pos, tco, tsc = pool_layout(pool_layout_stride(p))
        src, soff, sstride = pool_source(p, blob)
        got = _uv_format(src, soff, sstride, p['count'], tris,
                         pos=pos, texcoord=tco, scale=tsc)
        if got is None:
            retry.append((pi, tris))
            continue
        _pool_read(p, blob, lo, hi, got, rep)
    scale = item_scale(pools)
    for pi, tris in retry:
        p = pools[pi]
        use, key = scale, 'item_scale_pools'
        if use is None and p.get('stream') is None and p['stride'] in LAYER_INPUTS:
            use, key = layout_scale(pools, p['stride']), 'layout_scale_pools'
        pos, tco, _tsc = pool_layout(pool_layout_stride(p))
        src, soff, sstride = pool_source(p, blob)
        if use is None:
            if not short:
                continue
            # THE CHART SHORT OF ITS UNIT (chart_short): asked only here,
            # of a pool no other rule reads, of an item that asks for it.
            got = _uv_format(src, soff, sstride, p['count'], tris,
                             pos=pos, texcoord=tco, short=True)
            key = 'chart_short_pools'
        else:
            got = _uv_format(src, soff, sstride, p['count'], tris,
                             pos=pos, texcoord=tco, scale=use)
        if got is not None:
            _pool_read(p, blob, lo, hi, got, rep)
            if p['vertices'] is not None:
                rep[key] = rep.get(key, 0) + 1
    #    THE ITEM'S REPEAT (#81): a pool whose measured divisor is TIED - the
    #    smaller candidates carry exactly the same values (tied_divisors), so
    #    its pile-up cannot tell them apart - is read at the tied divisor
    #    whose repeat (item units per image) stands nearest the repeat of the
    #    item's LARGEST read pool, when one stands within REPEAT_TIE_FACTOR of
    #    it - asked only of a pool whose pile-up is thin (REPEAT_WEAK). MEASURED on Bunker 1's item: the control room's floor (pool 2,
    #    1755 vertices, one 256x256 tile image) piles up on 2048, 1024 and 512
    #    alike (1.2% of its values each, the same values), was read at 1/2048
    #    - 800 item units, 343 world units a tile - and drew each tile four
    #    times too large; the item's largest pool (15676 vertices, every wall
    #    and corridor floor) repeats every 200 units, which 1/512 gives the
    #    floor exactly, and the remaster draws that floor with the cartridge's
    #    tile count. A pool measured without a tie, a layout's, a stream's and
    #    the largest pool itself read as before.
    read = [q for q in pools if q.get('vertices') is not None and q['uv_divisor']]
    if read:
        big = max(read, key=lambda q: q['count'])
        rep_big = big['uv_edge'] * big['uv_divisor']
        for p in read:
            if p is big or p.get('stream') is not None \
                    or pool_layout_stride(p) in POOL_LAYOUTS or rep_big <= 0 \
                    or (p['uv_evidence'] or 0.0) >= REPEAT_WEAK:
                continue
            ties = tied_divisors(blob, p['off'], p['stride'], p['count'],
                                 p['uv_offset'], p['uv_divisor'])
            if not ties:
                continue
            best = min([p['uv_divisor']] + ties,
                       key=lambda d: abs(math.log(p['uv_edge'] * d / rep_big)))
            if best == p['uv_divisor'] or \
                    abs(math.log(p['uv_edge'] * best / rep_big)) > math.log(REPEAT_TIE_FACTOR):
                continue
            was = (p['uv_offset'], p['uv_divisor'], p['uv_edge'], p['uv_evidence'],
                   p['vertices'], p.get('aux'), p.get('frame'))
            _pool_read(p, blob, lo, hi, (p['uv_offset'], float(best), p['uv_edge'],
                                         p['uv_evidence']), rep)
            if p['vertices'] is None:
                (p['uv_offset'], p['uv_divisor'], p['uv_edge'], p['uv_evidence'],
                 p['vertices'], p['aux'], p['frame']) = was
                continue
            p['repeat_from'] = was[1]
            rep['repeat_pools'] = rep.get('repeat_pools', 0) + 1

    #    A POOL WITH A NORMAL WHERE THE COORDINATE WOULD BE (#81, _pool_sphere):
    #    a pool still refused, with no stream and no measured layout, that
    #    some draw binds, is read once more as one. Its draws either name an
    #    image (the sphere-mapped rails) or name none (UNTEXTURED - the
    #    preparation draws those by their colour alone).
    for pi, p in enumerate(pools):
        if p['vertices'] is not None or p.get('stream') is not None                 or pool_layout_stride(p) in POOL_LAYOUTS:
            continue
        if not any(q == pi for (_d, q, _i) in bound):
            continue
        vs = _pool_sphere(blob, p['off'], p['stride'], p['count'], lo, hi)
        if vs is not None:
            p['vertices'] = vs
            p['uv_offset'] = p['uv_divisor'] = p['uv_edge'] = p['uv_evidence'] = None
            p['sphere'] = 12
            rep['sphere_pools'] = rep.get('sphere_pools', 0) + 1

    #    THE SPHERE IMAGE (#81): an image a sphere-mapped pool's draws sample
    #    in slot 0 is a sphere image, and a pool that carries a coordinate
    #    AND a unit normal at +12, whose every draw names such an image in
    #    slot 0, is sphere-mapped by that normal as well: its coordinate is
    #    not what the program samples. MEASURED on Bunker 1's item: the
    #    control room's porthole (pool 15, 223 vertices, unit normals 0.997 ..
    #    1.000) names the rails' chrome bubble in slot 0 with the room's
    #    fisheye and a flare image beside it; read by its coordinate the
    #    bubble drew four times over across the glass, where the remaster
    #    (private emulator, debug camera, playtest mark 2) draws one
    #    reflecting dome. A pool whose draws name any other image reads as
    #    before.
    sphere_imgs = {d['image'] for (d, q, _i) in bound
                   if pools[q].get('sphere') is not None and d['image'] is not None
                   and (d.get('slots') or {}).get(0) == d['image']}
    for pi, p in enumerate(pools):
        if not sphere_imgs or p['vertices'] is None or p.get('sphere') is not None \
                or p.get('stream') is not None or pool_layout_stride(p) in POOL_LAYOUTS:
            continue
        mine = [d for (d, q, _i) in bound if q == pi]
        if not mine or any(d['image'] not in sphere_imgs
                           or (d.get('slots') or {}).get(0) != d['image'] for d in mine):
            continue
        ns = _pool_normals(blob, p['off'], p['stride'], p['count'], 12)
        if ns is None or (p['uv_offset'] is not None and p['uv_offset'] < 16):
            continue
        p['vertices'] = [v[:3] + uv + tuple(v[5:])
                         for v, uv in zip(p['vertices'], sphere_uvs(ns))]
        p['sphere'] = 12
        p['sphere_image'] = True
        rep['sphere_pools'] = rep.get('sphere_pools', 0) + 1

    image_repeat(pools, bound, blob, lo, hi, rep)

    # 6. the wave program's literal bank, read once per item (#69): the
    #    programs that fetch a 36-byte record's wave pair, and what they
    #    carry in front of their microcode. One distinct bank, or none.
    lits = wave_program_literal(blob)
    literal = list(lits)[0] if len(lits) == 1 else None
    rep['wave_programs'] = sum(lits.values())
    rep['wave_literal'] = literal
    # 7. the pixel programs (#69 material): every program pool the item
    #    carries beside its model data, walked once for the programs' fetch
    #    constants and literal banks. The record is matched per draw.
    programs = []
    ppools = _program_pools(b, m)
    for pool_bytes in ppools:
        programs.extend(pixel_programs(pool_bytes))
    rep['pixel_programs'] = len(programs)
    # 8. the two-layer draws (#72): the program each binding names first,
    #    where it can be placed; see LAYER_INPUTS for the contract.
    placed = record_pixel_programs(graph, ppools[0], recs) if ppools else {}
    # 9. which channel each draw's program takes its coverage from (#81, the
    #    prop reader's #71 rule read for the level item too): None where the
    #    item's programs cannot be placed.
    coverage = draw_coverage(graph, ppools, recs) if ppools else {}
    #    ... and, for a two-slot draw, which slot is its coverage and which
    #    its colour (#92, two_image_coverage)
    progs = bound_programs(graph, ppools, recs) if ppools else {}
    # 10. the untextured draws' material colour (#90, colour_times_c13_program):
    #     asked only of an item with a draw naming no image.
    untex_prog = any(d['image'] is None for (d, _pi, _ind) in bound) \
        and colour_times_c13_program(ppools)
    rep['untextured_program'] = bool(untex_prog)

    out = []
    for (d, pi, ind) in bound:
        if pools[pi]['vertices'] is None:
            rep['bad_pool'] += 1
            continue
        if d['image'] is None:
            rep['no_image'] += 1
        # THE DRAW'S ANIMATION RECORD (#69): only a draw whose pool carries
        # the fields AND whose program state carries the table animates; the
        # reason a candidate does not is reported rather than lost.
        wave = None
        if pools[pi].get('aux') is not None:
            wave, why = wave_record(d.get('vs_uploads', []), d.get('vs_block'),
                                    graph, literal)
            if wave is None:
                rep.setdefault('wave_refused', []).append((d['at'], why))
        # THE DRAW'S MATERIAL RECORD (#69 material): only a draw whose pool
        # carries the tangent frame AND which names the four sampler slots
        # is a candidate; what its program state then lacks is reported.
        material = None
        if pools[pi].get('frame') is not None and d.get('slots') \
                and sorted(d['slots']) == list(MATERIAL_SLOTS):
            material, why = material_record(d.get('ps_uploads', []), programs,
                                            d['slots'], d.get('slot_flags'))
            if material is None:
                rep.setdefault('material_refused', []).append((d['at'], why))
        layer = None
        fields = LAYER_INPUTS.get(pool_layout_stride(pools[pi]))
        if fields is not None and d.get('slots') and len(d['slots']) == 2 \
                and d.get('vs_records') and d['vs_records'][0] in placed:
            layer, why = draw_layers(placed[d['vs_records'][0]], fields,
                                     d['slots'], pools[pi], blob, lo, hi)
            if layer is None and why:
                rep.setdefault('layer_refused', []).append((d['at'], why))
        # #85 a draw on a two-layer pool its program does not decide is
        # carried by the layout's own assignment (layout_layers); its weight
        # takes the layer image's alpha, set below once the images are read
        if layer is None and fields is not None and d.get('slots') \
                and pools[pi].get('uv_divisor'):
            layer, _why = layout_layers([placed[r] for r in d.get('vs_records') or []
                                         if r in placed],
                                        fields, d['slots'], pools[pi], blob, lo, hi)
            if layer is not None:
                rep['contract_layered'] = rep.get('contract_layered', 0) + 1
        # THE SLOTS SAMPLED AT A COMPUTED COORDINATE (#82, computed_fetches):
        # over every program variant of the draw that can be placed; None
        # when none can be, or one that is cannot be read.
        reflect = None
        for r in d.get('vs_records') or []:
            code = placed.get(r)
            if code is None:
                continue
            got_c = computed_fetches(code)
            if got_c is None:
                reflect = None
                break
            reflect = (reflect or set()) | got_c
        out.append(dict(at=d['at'], prim=d['prim'], count=d['count'],
                        pool=pi, indices=expand_indices(d['prim'], ind),
                        image=d['image'], slots=d['slots'],
                        style=d['style'], pass_mask=passes.get(d['at']),
                        tris=d['tris'], wave=wave,
                        material=material, layer=layer,
                        xform=d.get('xform'), coverage=coverage.get(d['at']),
                        cull=d.get('cull', 0),
                        cover2=two_image_coverage(progs.get(d['at']), d['slots']),
                        reflect=reflect,
                        untex=(untextured_colour(d.get('ps_uploads'))
                               if d['image'] is None and untex_prog else None)))
    if not out:
        raise WorldAssetError('%s: no draw in this item survives validation' % name)
    rep['kept'] = len(out)
    rep['pools'] = len(pools)
    rep['triangles'] = sum(d['tris'] for d in out)
    rep['animated'] = sum(1 for d in out if d['wave'] is not None)
    rep['materials'] = sum(1 for d in out if d['material'] is not None)
    rep['layered'] = sum(1 for d in out if d['layer'] is not None)

    textures = []
    for t in sorted(b['texs'], key=lambda x: x['name']):
        nm = caff.tex_name(b, t)
        if t['fmt'] not in xdecode.SUPPORTED:
            raise WorldAssetError('%s: texture %s is format %d, which this '
                                  'pipeline does not decode' % (name, nm, t['fmt']))
        try:
            img = xdecode.decode(caff.base_level(b, t), t['fmt'], t['w'],
                                 t['h'], mip_levels=t['mips'])
        except Exception as exc:                              # noqa: BLE001
            raise WorldAssetError('%s: texture %s: %s' % (name, nm, exc))
        textures.append(dict(name=nm, w=int(t['w']), h=int(t['h']), fmt=int(t['fmt']),
                             rgba=img, has_alpha=bool((img[:, :, 3] != 255).any())))

    for d in out:                          # #85 layout_layers' weight
        ly = d.get('layer')
        if ly is not None and ly['texel_alpha'] is None:
            li = ly['layer'][0]
            ly['texel_alpha'] = bool(0 <= li < len(textures) and textures[li]['has_alpha'])

    return dict(pools=pools, draws=out, textures=textures, declared=declared,
                report=rep, fog=fog_programs(graph))
