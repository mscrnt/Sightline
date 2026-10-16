"""Read ONE skinned character or head item out of a user-supplied CAFF bundle
(#70), for the character-asset provider.

A character item is the same container and the same command-list framing as
the world items `xblaworld` reads, and those readers are reused unchanged -
`command_records`, `expand_indices`, `material_slots`, the container itself and
the texture decoder. What a character adds is a SKELETON, and that is all this
module describes.

WHAT A CHARACTER ITEM HOLDS, measured on a body item and a head item of both
generations:

  * a POSE block in the '.data' rendergraph: the literal "pose", a version
    string, then (at +0x18) the joint count, and from +0x38 one 52-byte record
    per joint,

        +0   f32 x, y, z     the joint's offset from its parent
        +12  f32 x, y, z     the joint's bind position in item space
        +24  f32 x, y, z, w  the bind rotation, a unit quaternion
        +40  u16 parent, first child, next sibling, self, 0xffff, (unread)

    with every bind position equal to its parent's plus its own offset - the
    cross-check that says the records are framed right. Offsets add without
    any rotation, and the vertices are stored in item space, not in a
    joint's frame, so the bind ROTATION places nothing and is not read: the
    characters carry identity, a rifle's muzzle leaf a fraction of a degree,
    a watch gadget's wrist joint some forty degrees, and the runtime relates
    joints to the game's parts by position alone. The joints are named
    by a table after the records: triples (u32 name offset, u32 0, u32 joint)
    whose name is a NUL-terminated "SKEL_..." string inside the same block;
  * a BONE TABLE, command-list opcode 0x12: (count << 16) and then one pair
    per bone whose high half-word is the joint that bone is. The two
    generations of one character list the same joints in DIFFERENT orders, so
    a vertex's bone is only meaningful through this table;
  * a PALETTE before each group of draws, opcode 0x13: (count << 16) and then
    `count` bytes, each a bone. A vertex names a palette slot, not a bone;
  * VERTEX BUFFERS of two strides, big-endian:

        stride 36 (blended)            stride 32 (rigid)
        +0   f32 x, y, z               +0   f32 x, y, z
        +12  u16 slot0, slot1          +12  u16 slot0, slot1 (always none)
        +16  u16 slot2, slot3          +16  u16 slot2, slot3 (always none)
        +20  u8 w3, w2, w1, w0         +20  packed normal (unread)
        +24  packed normal (unread)    +24  s16 u, v   (item scale)
        +28  s16 u, v   (item scale)   +28  colour  a, b, g, r
        +32  colour  a, b, g, r

    and a third, stride 28 (plain): stride 32 without the texture
    coordinate - position, slots, packed normal, colour. Its draws sample no
    image (their materials name none; weapon hands and trims are made this
    way) and are carried as their vertex colour on a small white image.
    A slot is a palette index times three (one 4x3 matrix is three constant
    registers); 0xf000 is "no slot". The four weight bytes are in REVERSE slot
    order and sum to 255 on every vertex read; a rigid vertex takes all 255 on
    slot 0;
  * the TEXTURE COORDINATE's scale is the item's own. Its vertex programs
    fetch the pair as a raw 16_16 SIGNED integer (no exponent adjust) and
    scale it in code this reader does not decode, and items differ: most
    reach their atlases' far edge at 1/32768, some at 1/16384, and weapon
    items as low as 1/1024. A coordinate may run a little below zero (a
    pistol's grip wraps to -2920 at 1/16384), which read unsigned would pass
    for one far past the edge. So the scale is MEASURED, as the world reader
    measures its pools' (xblaworld _uv_format), over the same range of
    scales: an unwrapped atlas reaches its far edge, and the scale is the
    power of two that brings the item's largest coordinate into
    (TEXCOORD_REACH, 1]. An item that reaches no such edge is refused;
  * the VERTEX BUFFER a draw reads is the one its vertex-program binding
    names (`vertex_buffer_at`);
  * GATED blocks, opcode 0x17 (skip-to offset), hold an item's own HEAD
    geometry - drawn on the face image, weighted to the neck. On a body item
    that is the body's BUILT-IN head, which a character wearing a separate
    head item does not draw; a head item may gate its whole neck and face the
    same way, and there the gated block IS the worn head. `builtin_head`
    below reports the gate; which of the two it is, is the item class's.

Positions are the item's own bind space, unscaled; the runtime relates that
space to the game's skeleton (see src/gfx/sl_gfx_charskin.h). Nothing here is
a transform of the geometry.

This module parses a file the USER supplies. It contains no source bytes, no
identifier of any particular source, and no way to obtain one.
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'texpack'))

import caff        # noqa: E402  the container reader, reused unchanged
import xdecode     # noqa: E402  the texture decoder, reused unchanged
import xblaworld   # noqa: E402  the command-list and primitive readers

OP_BONES = 0x12
OP_PALETTE = 0x13
OP_GATE = 0x17
OP_DRAW = xblaworld.OP_DRAW
OP_MATERIAL = xblaworld.OP_MATERIAL

POSE_TAG = b'pose\x00'
POSE_COUNT_AT = 0x18
POSE_FIRST = 0x38
POSE_RECORD = 52
MAX_JOINTS = 32
NAME_MAX = 23                  # the runtime field is 24 bytes with its NUL

SLOT_NONE = 0xf000
SLOT_REGS = 3
STRIDE_BLENDED = 36
STRIDE_RIGID = 32
STRIDE_PLAIN = 28              # rigid, and no texture coordinate
STRIDES = (STRIDE_BLENDED, STRIDE_RIGID, STRIDE_PLAIN)
# A RIGID TWO-LAYER RECORD: 40 bytes. The item's own vertex programs declare
# it (static_fetches, following the second full fetch that starts the
# colours): position 32_32_32_FLOAT at +0, the four 16-bit slots at +12, a
# 2_10_10_10 normal at +20, two 16_16 coordinates at +24 and +28, two
# 8_8_8_8 colours at +32 and +36 - the rigid record carrying both surfaces of
# a two-layer draw, as the level item's 32-byte two-layer pool carries them
# (xblaworld.LAYER_INPUTS: coordinates, then colours). MEASURED on the
# remaster's Cradle helicopter (new/prop/helicopter), the tail boom's draw
# (52 of 53, 40 vertices): its pixel program is the two-layer blend with
# the base, slot 1 (the 512x512 body atlas), at interpolator 2 coloured by
# 5, and the layer, slot 0 (the 256x128 "LB-5577" number, white on black),
# at interpolator 1 coloured by 4, weighed by the layer colour's alpha
# times the number image's BLUE (xblaworld.layer_blend, `channels`).
# Interpolators 1, 2, 4, 5 are the record's +24, +28, +32, +36: the +24
# coordinate spans the whole number image (0 .. 1) and the +28 one a patch
# of the atlas (u 0.515 .. 0.629), and every +32 colour carries alpha 0xbf
# (the weight, 0.75) over the grey its +36 twin carries at an alpha of 0xff
# the program never reads. Read only where the caller carries two-layer
# draws (read_character's `layers`): the base as an ordinary draw, then the
# layer as a blended draw of the same triangles (`layer` in its record).
STRIDE_RIGID_UV2 = 40
#: The layout a 40-byte record must be declared with to be read: {offset:
#: Xenos fetch format} (26 the 16-bit slots, 7 the packed normal).
RIGID_UV2_FETCHES = {0: 57, 12: 26, 20: 7, 24: 25, 28: 25, 32: 6, 36: 6}
#: Its two-layer program's interpolators -> the record's byte offsets.
RIGID_UV2_FIELDS = {1: 24, 2: 28, 4: 32, 5: 36}
#: The layer weights the renderer realises: the image's blue (a blended
#: SL_WA_MAT_COVERAGE_BLUE material) and its alpha (a blended one).
LAYER_CHANNELS = (2, 3)
# The image an UNTEXTURED draw is given: white, so the draw is its vertex
# colour alone, as the source's programs for those draws sample no image.
PLAIN_IMAGE = '(vertex colour)'
PLAIN_SIZE = 4
TEXCOORD_SCALE = 1.0 / 32768.0
TEXCOORD_REACH = 0.9           # an unwrapped atlas reaches its far edge
# 1/32768 .. the coarsest scale the world reader measures (1/64)
TEXCOORD_DOUBLINGS = int(round(math.log2(32768.0 / min(xblaworld.UV_DIVISORS))))
# An edge is reached to within a fraction of a texel either side: the weapon
# items (#70 view models) run to 16394/16384 - ten units past the far edge -
# so the accepted reach extends that far past one, and no further.
TEXCOORD_SLACK = 1.0 / 64.0
# THE WRAPPED OVERRUN (texcoord_scale's last rule, overrun_scale): how far past
# its image's far edge a face of a WRAPPING draw may run and still be read as
# one repeat of it. Measured: the remaster's Train brake unit runs its back
# face to 17066/16384 (0.042 past the edge) and Surface's jerry can its cap's
# side to 16719/16384 (0.020 past it); the stool's legs run 0.672 past and stay
# refused.
TEXCOORD_OVERRUN = 1.0 / 16.0
# THE WHOLE IMAGE (texcoord_scale's last rule, whole_image_scale): how far a
# draw laid over its whole image may stop short of an edge or run past one.
# Measured: the remaster's Jungle drone gun lays its muzzle face, a disc
# polygon, over its five-hole image at 0.031 .. 0.957 by 0.010 .. 1.017 (at
# 1/4096), and the Jungle tree its leaf clump at 0.004 .. 0.945 by -0.042 ..
# 0.984 (at 1/8192).
WHOLE_IMAGE_MARGIN = 1.0 / 16.0
# The rules texcoord_scale decides by, as its `why` names them, in the order it
# asks them: the reach, the wrapped lip, the flat draw, the face, the wrapped
# overrun and THE WHOLE IMAGE (the rounded zero asks them again and reports the
# one that answered).
TEXCOORD_RULES = ('reach', 'lip', 'flat', 'face', 'overrun', 'whole')
# A FLAT image (texcoord_scale's flat draw): every channel within this many
# levels across the image - one step of a 5-bit channel, the spread block
# compression leaves on a tint authored as one colour (the lab glassware's
# 32x32 tint reads blue 148..150).
FLAT_SPREAD = 8
BIND_TOLERANCE = 0.05          # item units; the records agree to ~1e-3


class CharacterError(Exception):
    """The bundle is not a character item this reader understands."""


def _cstr(buf, at):
    end = buf.find(b'\x00', at)
    if end < 0 or end - at > 64:
        return None
    try:
        return buf[at:end].decode('ascii')
    except UnicodeDecodeError:
        return None


def read_pose(graph, name='item', table_names=False):
    """The skeleton: [dict(name, parent, bind=(x, y, z)), ...] in joint order.
    `parent` is -1 for the root. Raises CharacterError when the block is not
    framed the way the module docstring says."""
    at = graph.find(POSE_TAG)
    if at < 0:
        raise CharacterError('%s: no pose block' % name)
    if at + POSE_FIRST > len(graph):
        raise CharacterError('%s: truncated pose block' % name)
    n = struct.unpack('>I', graph[at + POSE_COUNT_AT:at + POSE_COUNT_AT + 4])[0]
    if not (1 <= n <= MAX_JOINTS):
        raise CharacterError('%s: %d joints is outside 1..%d' % (name, n, MAX_JOINTS))
    first = at + POSE_FIRST
    if first + n * POSE_RECORD > len(graph):
        raise CharacterError('%s: the joint records run past the block' % name)
    joints = []
    for j in range(n):
        o = first + j * POSE_RECORD
        rel = struct.unpack('>3f', graph[o:o + 12])
        ab = struct.unpack('>3f', graph[o + 12:o + 24])
        rot = struct.unpack('>4f', graph[o + 24:o + 40])
        parent, _child, _sib, me = struct.unpack('>4H', graph[o + 40:o + 48])
        if me != j:
            raise CharacterError('%s: joint record %d names itself %d' % (name, j, me))
        if not all(math.isfinite(v) for v in rel + ab + rot):
            raise CharacterError('%s: joint %d carries a non-finite number' % (name, j))
        if abs(sum(c * c for c in rot) - 1.0) > 1e-3:
            raise CharacterError('%s: joint %d has a bind rotation that is no unit '
                                 'quaternion - the pose records are not framed as read'
                                 % (name, j))
        if parent == 0xffff:
            parent = -1
        elif parent >= j:
            raise CharacterError('%s: joint %d names parent %d, not an earlier joint'
                                 % (name, j, parent))
        joints.append(dict(name=None, parent=parent, bind=ab, rel=rel))
    for j, jt in enumerate(joints):
        base = (0.0, 0.0, 0.0) if jt['parent'] < 0 else joints[jt['parent']]['bind']
        if any(abs(base[k] + jt['rel'][k] - jt['bind'][k]) > BIND_TOLERANCE for k in range(3)):
            raise CharacterError('%s: joint %d is not its parent plus its offset - '
                                 'the pose records are not framed as read' % (name, j))
    # The name table: (name offset, 0, joint) triples whose target reads as
    # a "SKEL_" string, the first for each joint. A joint the game's skeleton
    # does not name - the LEAF markers a weapon item ends its chains with
    # (BEAN_END and the like), which no vertex is bound to - is named by the
    # same table in a second pass that takes any upper-case identifier, so it
    # can never displace a SKEL_ name the first pass found.
    def _name_from_table(accept):
        for o in range(first + n * POSE_RECORD, len(graph) - 11, 4):
            off, zero, j = struct.unpack('>III', graph[o:o + 12])
            if zero != 0 or j >= n or joints[j]['name'] is not None or off >= len(graph):
                continue
            s = _cstr(graph, off)
            if s and accept(s) and len(s) <= NAME_MAX \
                    and all(c.isupper() or c.isdigit() or c == '_' for c in s):
                joints[j]['name'] = s
    if table_names:
        # THE NAME TABLE ITSELF: the run of consecutive triples that starts
        # right after the joint records (measured on a vehicle's items of
        # both generations: 16 bytes after the last record, one triple per
        # joint in joint order). A PROP item's joints carry the game's own
        # names there, lower-case where the game's skeleton table has them so
        # - a vehicle's base and wheel1..wheel4 - which the passes below do
        # not take and would instead name from strings further on (a style
        # name). Read first, each name upper-cased for the bundle's field.
        o = first + n * POSE_RECORD
        start = None
        for q in range(o, min(o + 64, len(graph) - 11), 4):
            off, zero, j = struct.unpack('>III', graph[q:q + 12])
            if zero == 0 and j < n and off < len(graph) and _cstr(graph, off):
                start = q
                break
        q = start if start is not None else len(graph)
        while q + 12 <= len(graph):
            off, zero, j = struct.unpack('>III', graph[q:q + 12])
            s = _cstr(graph, off) if zero == 0 and j < n and off < len(graph) else None
            if not s or len(s) > NAME_MAX or not s[0].isalpha() \
                    or not all(c.isalnum() or c == '_' for c in s):
                break
            if joints[j]['name'] is None:
                joints[j]['name'] = s.upper()
            q += 12
    _name_from_table(lambda s: s.startswith('SKEL_'))
    _name_from_table(lambda s: any(c.isupper() for c in s))
    missing = [j for j, jt in enumerate(joints) if jt['name'] is None]
    if missing:
        raise CharacterError('%s: joints %s have no name' % (name, missing))
    if len({jt['name'] for jt in joints}) != n:
        raise CharacterError('%s: two joints share a name' % name)
    return [dict(name=jt['name'], parent=jt['parent'], bind=jt['bind']) for jt in joints]


#: How far a mended joint's bone may differ in length from its mirror's
#: (mend_stray_roots): the snow guard's left shin is 2237.6 units against the
#: right's 2174.2, 2.9% apart.
MIRROR_BONE_TOLERANCE = 0.10


def _mirror_name(s):
    for a, b in (('SKEL_LF_', 'SKEL_RT_'), ('SKEL_RT_', 'SKEL_LF_')):
        if s.startswith(a):
            return b + s[len(a):]
    return None


def mend_stray_roots(joints, name='item'):
    """A remastered body joint authored as a SECOND ROOT, mended from its
    mirror. Returns (joints, perm): the joints in an order where every parent
    comes first, and perm[old index] = new index.

    Measured on the snow guard's remastered body (new/char/snowguard): its
    SKEL_LF_ANKLE is joint 0 with parent 0xffff, its offset equal to its bind
    position, standing exactly where the knee chain puts it - while the
    original generation of the same item (original/char/snowguard) and the
    remastered commando (new/char/commguard) list the same joint, at the same
    bind, under SKEL_LF_KNEE. 164 vertices are weighted to it. The game's
    skeleton has the ankle under the knee, so the runtime's match (which
    walks the tree from its hub) never reached the stray root and refused
    the whole item.

    The rule: a root that is not the root of the skeleton (it has no
    children, and another root has), whose name is a side's SKEL_LF_/SKEL_RT_
    joint, takes as its parent the mirror of its mirror's parent - provided
    both exist, the mirror is not itself a root, and the bone that results
    is within MIRROR_BONE_TOLERANCE of the mirror's in length. Anything else
    is left as read, and the runtime decides as it did before."""
    n = len(joints)
    roots = [j for j in range(n) if joints[j]['parent'] < 0]
    if len(roots) <= 1:
        return joints, list(range(n))
    has_kids = {jt['parent'] for jt in joints if jt['parent'] >= 0}
    by_name = {jt['name']: j for j, jt in enumerate(joints)}
    parents = [jt['parent'] for jt in joints]
    for r in roots:
        if r in has_kids:
            continue
        mn = _mirror_name(joints[r]['name'])
        m = by_name.get(mn) if mn else None
        if m is None or joints[m]['parent'] < 0:
            continue
        pm = joints[m]['parent']
        pn = _mirror_name(joints[pm]['name'])
        q = by_name.get(pn) if pn else None
        if q is None or q == r or joints[q]['parent'] < 0 and q not in has_kids:
            continue
        la = math.dist(joints[r]['bind'], joints[q]['bind'])
        lm = math.dist(joints[m]['bind'], joints[pm]['bind'])
        if not (lm > 0.0) or abs(la / lm - 1.0) > MIRROR_BONE_TOLERANCE:
            continue
        parents[r] = q
    if parents == [jt['parent'] for jt in joints]:
        return joints, list(range(n))
    # Every parent first, otherwise the item's own order (the bundle's joint
    # table is its own topological order).
    order, placed = [], set()
    while len(order) < n:
        for j in range(n):
            if j not in placed and (parents[j] < 0 or parents[j] in placed):
                order.append(j)
                placed.add(j)
                break
        else:
            raise CharacterError('%s: the mended skeleton has a cycle' % name)
    perm = [0] * n
    for new, old in enumerate(order):
        perm[old] = new
    out = [dict(name=joints[old]['name'],
                parent=-1 if parents[old] < 0 else perm[parents[old]],
                bind=joints[old]['bind']) for old in order]
    return out, perm


def read_conditions(graph, name='item'):
    """The rendergraph's CONDITION table: [leaf name, ...] by condition index.

    MEASURED on character, head and weapon items of both generations: the
    rendergraph header word at +0x28 is the offset of a table - u32 count,
    u32 offset of the first record - of 0x50-byte records

        +0   u32 offset of the node's path, "|TOP|wppk|gun|...|SW_HANDOFF4"
        +4   u32 offset of the path's last component (the leaf)
        +8   u32 the record's own index
        +12  u32 kind

    and a gate (opcode 0x17) names a record by that index. The leaf is the
    source's name for the switch the gated block stands under: CP_HEADCHILD
    on a body (its built-in head), the game's own gun switch names on a
    weapon (SW_FLASH, SW_HANDOFF4, ...). A header word of 0 is an item with
    no conditions. Raises CharacterError when a table is present and not
    framed as read."""
    if len(graph) < 0x2c:
        return []
    at = struct.unpack('>I', graph[0x28:0x2c])[0]
    if at == 0:
        return []
    if at + 8 > len(graph):
        raise CharacterError('%s: the condition table runs past the rendergraph' % name)
    n, first = struct.unpack('>II', graph[at:at + 8])
    if n > 64 or first + n * 0x50 > len(graph):
        raise CharacterError('%s: condition table of %d records' % (name, n))
    out = []
    for k in range(n):
        o = first + 0x50 * k
        _path, leaf, idx = struct.unpack('>III', graph[o:o + 12])
        s = _cstr(graph, leaf) if leaf < len(graph) else None
        if idx != k or not s:
            raise CharacterError('%s: condition record %d is not framed as read' % (name, k))
        out.append(s)
    return out


def _bytes_of(words, n):
    raw = b''.join(struct.pack('>I', w) for w in words)
    return raw[:n]


def _index_buffer(W, blob_len, handle, count):
    for i in range(len(W) - 3):
        if W[i] == handle and W[i + 2] == count * 2 and W[i + 3] == 1 \
                and W[i + 1] + W[i + 2] <= blob_len:
            return W[i + 1]
    return None


def vertex_buffer_at(graph, blob_len, at, name='item', strides=STRIDES):
    """The vertex buffer a draw reads: (offset, stride, count).

    MEASURED on character, head and weapon items: the vertex-program binding
    record (opcode 0x2e) names, in its second word, the rendergraph offset of
    the vertex buffer its draws read - a 16-byte descriptor

        +0   u32 stride
        +4   u32 the buffer's own handle
        +8   u32 byte offset into the model blob
        +12  u32 byte length, a whole number of vertices

    which need NOT sit on a four-byte boundary of the rendergraph (a knife
    item's first buffer is described at an offset of 2 mod 4, after a joint
    name). Every draw after a binding reads that buffer until the next
    binding. Raises CharacterError for a descriptor this reader does not
    take: a stride `strides` does not name (the character strides, unless a
    caller says otherwise), or a range outside the blob."""
    if at is None:
        raise CharacterError('%s: a draw before any vertex program binding' % name)
    if at + 16 > len(graph):
        raise CharacterError('%s: a vertex buffer descriptor at %#x runs past the '
                             'rendergraph' % (name, at))
    st, _handle, off, ln = struct.unpack('>IIII', graph[at:at + 16])
    if st not in strides:
        raise CharacterError('%s: the vertex buffer at %#x has stride %d; only %s are read'
                             % (name, at, st, ', '.join(str(s) for s in strides)))
    if ln == 0 or ln % st or off + ln > blob_len:
        raise CharacterError('%s: the vertex buffer at %#x is not framed as read'
                             % (name, at))
    return off, st, ln // st


def _vertex(blob, off, stride, i, palette, bones, name, fields=None):
    o = off + i * stride
    x, y, z = struct.unpack('>3f', blob[o:o + 12])
    slots = struct.unpack('>4H', blob[o + 12:o + 20])
    if stride == STRIDE_BLENDED:
        w = blob[o + 20:o + 24]
        weights = (w[3], w[2], w[1], w[0])
        uvo, co = o + 28, o + 32
    elif stride == STRIDE_PLAIN:
        weights = (255, 0, 0, 0)
        uvo, co = None, o + 24
    elif stride == STRIDE_RIGID_UV2:
        # one surface of the two-layer record: (coordinate, colour) offsets
        if fields is None:
            raise CharacterError('%s: a two-layer record read with no surface' % name)
        weights = (255, 0, 0, 0)
        uvo, co = o + fields[0], o + fields[1]
    else:
        weights = (255, 0, 0, 0)
        uvo, co = o + 24, o + 28
    if sum(weights) != 255:
        raise CharacterError('%s: vertex weights sum to %d, not 255' % (name, sum(weights)))
    infl = []
    for s, wt in zip(slots, weights):
        if s == SLOT_NONE:
            if wt:
                raise CharacterError('%s: an empty slot carries weight' % name)
            continue
        if s % SLOT_REGS or s // SLOT_REGS >= len(palette):
            raise CharacterError('%s: slot %d is outside the palette of %d'
                                 % (name, s, len(palette)))
        b = palette[s // SLOT_REGS]
        if b >= len(bones):
            raise CharacterError('%s: palette names bone %d of %d' % (name, b, len(bones)))
        if wt:
            infl.append((bones[b], wt))
    if not infl:
        raise CharacterError('%s: a vertex is bound to nothing' % name)
    u, v = struct.unpack('>hh', blob[uvo:uvo + 4]) if uvo is not None else (0, 0)
    a, bb, g, r = blob[co:co + 4]
    if not all(math.isfinite(c) and abs(c) < 1.0e6 for c in (x, y, z)):
        raise CharacterError('%s: a vertex position is not a number' % name)
    return (x, y, z, u * TEXCOORD_SCALE, v * TEXCOORD_SCALE, r, g, bb, a), infl


def own_slots(slots, texels):
    """A material record's slots that name the ITEM'S OWN images (#80) - the
    record as it was when every slot does, None when none does.

    The remastered bookshelf (new/prop/bookshelf1) follows each draw's material
    with a second record binding slots 1 and 2 to images 25 and 26 of an item
    that carries ten: the engine's own resources, not the item's, bound for the
    same draw. Read as the draw's image they replaced the true one (slot 0, the
    record before) and refused the item - "a draw names image 26 of 10". A
    record that names none of the item's images leaves the draw's image as the
    record before it set it. On every item whose records name only its own
    images nothing changes."""
    if not slots:
        return slots
    own = {k: v for k, v in slots.items() if 0 <= v < len(texels)}
    if len(own) == len(slots):
        return slots
    return own or None


def base_image(slots, texels):
    """The image a material's draws are drawn with: {sampler slot: image}
    from the material record, `texels` the size of every image of the item.

    A material names ONE image on almost every item - slot 0. Where it names
    several, MEASURED over every character, head and weapon item of the
    source: a metal weapon (the gold and silver pistols, the golden gun, the
    laser, the knives) pairs its 512x512 atlas with a 256x256 environment
    image, in either slot order; a pair of glasses or a watch glass pairs a
    54x54 lens with a 32x32 highlight. In every case the draw's own texture
    is the LARGEST image the material names (the lowest slot on a tie), and
    the smaller one is a reflection laid over it: the draw's `reflect`, which
    only a view model carries (prepare.convert_viewmodel)."""
    best = None
    for sl in sorted(slots):
        im = slots[sl]
        size = texels[im] if 0 <= im < len(texels) else -1
        if best is None or size > best[0]:
            best = (size, im)
    return best[1] if best else None


def coverage_base(slots, opaque):
    """THE BASE OF A TRANSLUCENT STATIC ITEM'S MATERIAL (#77): {sampler slot:
    image} from the material record, `opaque` the images of the item whose
    every texel has alpha 255.

    `base_image` takes the LARGEST image, which holds for the items it was
    measured on. It does not hold for glass. MEASURED over every prop item of
    the source whose translucent draws name more than one image (the glazed
    door and the window of the gas plant, the lab glassware, the Severnaya
    and train doors, the aircraft's and the jeep's glass): the pair is always
    a fully opaque environment image - the 256x256 sphere image or the 54x54
    one every lens and glass in the source shares - and a small image whose
    alpha is the pane's coverage (flat at 102 or 119 on most). Taking the
    larger draws the environment image as the pane's colour: an opaque grey
    sphere where the cartridge has tinted glass (the gas plant's glazed door
    drew black with highlights). The colour of a translucent pane is the
    image that carries its coverage; the opaque one is a reflection laid over
    it, which a prop does not carry.

    Returns that image when EXACTLY ONE image the material names carries
    alpha below 255, else None (the largest stands): two carriers (the train
    door's) or none say nothing about which one is the colour."""
    carriers = sorted({im for im in slots.values() if im not in opaque})
    return carriers[0] if len(carriers) == 1 else None


#: THE ENVIRONMENT PAIR's two sizes (static_base): the sphere image, and the
#: sky image bound in the sampler slot after it.
ENV_SPHERE = (256, 256)
ENV_SKY = (256, 128)


def static_base(slots, texels, dims):
    """THE IMAGE A STATIC ITEM'S MATERIAL DRAWS WITH (#82): {sampler slot:
    image} from the material record, `texels` and `dims` ((w, h)) of every
    image of the item.

    `base_image` takes the LARGEST image. MEASURED over every prop item of
    the source whose material names more than one image (37 records on 315
    items): wherever a material names a 256x128 image, the slot just before
    it names a 256x256 one, and the two are the same pair of images on every
    item that binds them - the opaque grey sphere image every reflective
    surface shares (mean 104) and its sky (mean 81 91 90): the gas tank, the
    oil drum, the destroyed Sea Wolf, the missile and its nose cone. They are
    the reflection laid over the draw, never its colour. The largest rule
    took the sphere image as the colour twice: on the missile's and the nose
    cone's lettering, an 85x324 decal smaller than the sphere image (the
    strip drew a grey smear where the cartridge paints its letters), and on
    the oil drum's lid, whose own 256x256 image ties the sphere image and
    lost on slot order. So the pair is set aside first and the largest of
    what remains is the draw's image; a material naming nothing else keeps
    the largest of all, as before. Every other material - one image, or no
    pair - reads exactly as base_image reads it."""
    pair = set()
    for sl, im in slots.items():
        if not (0 <= im < len(dims)) or tuple(dims[im]) != ENV_SKY:
            continue
        before = slots.get(sl - 1)
        if before is not None and 0 <= before < len(dims) and tuple(dims[before]) == ENV_SPHERE:
            pair.update((im, before))
    rest = {k: v for k, v in slots.items() if v not in pair}
    return base_image(rest or slots, texels)


FACE_PLANE_NORMAL = 0.01      # a face's plane: unit normal to this ...
FACE_PLANE_OFFSET = 1.0       # ... and offset along it to this, in item units


def _face_plane(vertices, t):
    """The plane of triangle `t` (a vertex-index triple) as a hashable key -
    its unit normal and its offset along it, each rounded to the tolerances
    above - or None for a triangle with no area."""
    a, b, c = (vertices[q] for q in t)
    e = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    g = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    n = (e[1] * g[2] - e[2] * g[1], e[2] * g[0] - e[0] * g[2], e[0] * g[1] - e[1] * g[0])
    m = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
    if not m > 0.0:
        return None
    n = (n[0] / m, n[1] / m, n[2] / m)
    d = n[0] * a[0] + n[1] * a[1] + n[2] * a[2]
    return (round(n[0] / FACE_PLANE_NORMAL), round(n[1] / FACE_PLANE_NORMAL),
            round(n[2] / FACE_PLANE_NORMAL), round(d / FACE_PLANE_OFFSET))


def overrun_scale(vertices, tris, draws, wraps=None):
    """THE WRAPPED OVERRUN rule of texcoord_scale (#93): the factor, or None.

    Asked only where every other rule refused. A draw on a WRAPPING image may
    run one face a little past the image's far edge, into the next repeat,
    while the rest of the item lies inside it. The remaster's Train brake unit
    is ONE draw on one 512x512 image: at 1/16384 its front panel, side strips
    and the two pipes land on the panel, the gauges and the yellow and red
    strips of the image, and only its back face (two of 62 triangles) runs to
    1.042; at 1/32768 every triangle is inside but none passes 0.521. No
    draw-level rule decides it (the draw exceeds 1 + TEXCOORD_SLACK), and the
    face rule does not either (no face spans the image's height: the bottom
    strip holds the pipes). Surface's jerry can is the same case (four of 94
    triangles to 1.020).

    The scale is the LARGEST power of two at which more than half of the
    item's triangles lie inside the image (within TEXCOORD_SLACK on either
    side), at least one of them reaching past TEXCOORD_REACH, and EVERY other
    triangle lies on wrapping draws only, at or above -TEXCOORD_SLACK, and at
    most TEXCOORD_OVERRUN past the far edge. One triangle that tiles further,
    runs below zero, or samples a clamping image refuses that scale. The
    source's other refused props (the stool, the server rack, the flight
    recorder, the Dam's chain door, Surface's hatch) meet it at no scale."""
    clamped = set()
    if wraps is not None:
        for g, w in zip(draws, wraps):
            if not w:
                clamped.update(g)
    S, O = TEXCOORD_SLACK, TEXCOORD_OVERRUN
    for k in range(TEXCOORD_DOUBLINGS, -1, -1):
        f = float(1 << k)
        inside, reach, ok = 0, False, True
        for t in tris:
            cs = [vertices[q][c] * f for q in t for c in (3, 4)]
            if min(cs) >= -S and max(cs) <= 1.0 + S:
                inside += 1
                reach = reach or max(cs) > TEXCOORD_REACH
            elif min(cs) < -S or max(cs) > 1.0 + O or clamped.intersection(t):
                ok = False
                break
        if ok and reach and 2 * inside > len(tris):
            return f
    return None


def whole_image_scale(vertices, draws, wraps=None):
    """THE WHOLE IMAGE rule of texcoord_scale (#94): the factor, or None.

    Asked only where every other rule refused. An item whose draws mostly
    TILE - bark wrapped around a trunk, camouflage over a gun's housing, a
    barrel's metal wrapped around it - says nothing about the scale through
    them, and every earlier rule counts vertices or triangles inside the
    image, so such an item is refused however plainly one of its draws lays
    its image whole. The remaster's Jungle tree (new/prop/jungle3tree) is five
    draws, each on its own wrapping image: at 1/8192 its vine card spans its
    image exactly (0 .. 8192 on both axes) and its leaf clump covers its own,
    while its buttress roots wrap their image six times around the trunk
    (exactly 0 .. 8192 along it) and the bark and a second clump tile; 13 of
    its 60 triangles lie inside. The Jungle drone gun (new/prop/groundgun,
    skinned) lays its muzzle face over its five-hole disc image at 1/4096,
    wraps its barrel's metal once around and tiles its camouflage.

    A draw LIES ON ITS WHOLE IMAGE at a scale when every coordinate is within
    WHOLE_IMAGE_MARGIN of the image on both axes and it comes within that
    margin of all four edges. A draw TILES when it spans more than one repeat
    plus the margin on an axis, on a wrapping image. The scale is the power of
    two at which at least one draw lies on its whole image, at least one
    other draw tiles, and every other draw either lies inside its image (to
    the margin) or tiles; a draw that runs partly past its image without
    completing a repeat refuses that scale. An item with no tiling draw is
    the earlier rules' to decide, at their own tolerances (TEXCOORD_SLACK,
    TEXCOORD_OVERRUN). When more than one power of two qualifies, the item is
    refused: a draw over a quarter of its image reads whole at twice the
    scale, and nothing then says which is meant."""
    M = WHOLE_IMAGE_MARGIN
    found = []
    for k in range(TEXCOORD_DOUBLINGS, -1, -1):
        f = float(1 << k)
        whole, tiles, ok = False, False, True
        for i, g in enumerate(draws):
            if not g:
                continue
            us = [vertices[q][3] * f for q in g]
            vs = [vertices[q][4] * f for q in g]
            lo_u, hi_u, lo_v, hi_v = min(us), max(us), min(vs), max(vs)
            if lo_u >= -M and lo_v >= -M and hi_u <= 1.0 + M and hi_v <= 1.0 + M:
                if lo_u <= M and lo_v <= M and hi_u >= 1.0 - M and hi_v >= 1.0 - M:
                    whole = True
            elif (hi_u - lo_u > 1.0 + M or hi_v - lo_v > 1.0 + M) \
                    and (wraps is None or wraps[i]):
                tiles = True
            else:
                ok = False
                break
        if ok and whole and tiles:
            found.append(f)
    return found[0] if len(found) == 1 else None


def atlas_triangles_scale(vertices, draws):
    """THE ATLAS TRIANGLES (#96): the factor, or None. Not one of
    texcoord_scale's rules - prepare.convert_prop asks it for the items it
    names (prepare.ATLAS_TRIANGLES_STATIC) once texcoord_scale has refused.

    An item may lay a picture atlas over its body while other draws tile
    small images over the rest, and one stray vertex of the atlas draws may
    pass the far edge. The remaster's Aztec shuttle is four draws: a hull
    panel (512x512) and a louvre (256x256), both tiling below zero, and two
    draws on one 512x1024 photograph of the shuttle from above and the side.
    At 1/8192 the two atlas draws trace that photograph - the wing's leading
    edge along the wing, the engines on the nozzles - with 368 of their 370
    triangles inside it and reaching 0.943; one vertex of a small flat cap
    runs to 1.274. So the reach rule's draw majority fails (the larger atlas
    draw exceeds the image), and every per-triangle rule counts the tiling
    draws' triangles, 668 of the item's 1038.

    Measured over the triangles of the draws that never run below zero (the
    draws texcoord_scale counts for its reach rule), the scale is the LARGEST
    power of two at which more than half of them lie inside the image
    (within TEXCOORD_SLACK on either side), at least one of them reaching
    past TEXCOORD_REACH. `draws` groups the vertex indices of each draw as
    its triangles' index list."""
    tris = []
    for g in draws:
        if g and min(min(vertices[q][3], vertices[q][4]) for q in g) >= 0.0:
            tris.extend(g[t:t + 3] for t in range(0, len(g) - 2, 3))
    if not tris:
        return None
    S = TEXCOORD_SLACK
    for k in range(TEXCOORD_DOUBLINGS, -1, -1):
        f = float(1 << k)
        inside, reach = 0, False
        for t in tris:
            cs = [vertices[q][c] * f for q in t for c in (3, 4)]
            if min(cs) >= -S and max(cs) <= 1.0 + S:
                inside += 1
                reach = reach or max(cs) > TEXCOORD_REACH
        if reach and 2 * inside > len(tris):
            return f
    return None


def face_scale(vertices, tris):
    """THE FACE rule of texcoord_scale (#81): the factor, or None.

    A face laid over the whole image is first sought as ONE triangle on all
    four edges. When no scale has one, it is sought as one PLANE: the inside
    triangles that share a plane (FACE_PLANE_NORMAL, FACE_PLANE_OFFSET),
    taken together, on all four edges. The remaster's Archives steel door
    (new/prop/steeldoor3) is one draw whose front and back faces each lay its
    512x1024 image whole (0 .. 16384 on both axes) as a grid of eight quads,
    no one triangle reaching more than half of either axis, while its two
    edge faces run a strip of 0.073 of the image past either side - refused
    by every rule until the face was measured as its plane. The plane form is
    asked only where the triangle form found no scale, so no item that form
    decides is read differently; the source's refused props of the earlier
    stages (the flight recorder, the Dam's chain door, Surface's hatch, the
    jerry can, the stool, the server rack) meet it at no scale either."""
    S = TEXCOORD_SLACK
    for planes in (False, True):
        for k in range(TEXCOORD_DOUBLINGS, -1, -1):
            f = float(1 << k)
            inside = reach = face = 0
            spans = {}
            for t in tris:
                us = [vertices[q][3] * f for q in t]
                vs = [vertices[q][4] * f for q in t]
                if min(us + vs) < -S or max(us + vs) > 1.0 + S:
                    continue
                inside += 1
                if max(us + vs) > TEXCOORD_REACH:
                    reach = 1
                if planes:
                    key = _face_plane(vertices, t)
                    if key is None:
                        continue
                    lo_u, lo_v, hi_u, hi_v = spans.get(key, (min(us), min(vs), max(us), max(vs)))
                    spans[key] = (min(lo_u, min(us)), min(lo_v, min(vs)),
                                  max(hi_u, max(us)), max(hi_v, max(vs)))
                elif min(us) <= S and min(vs) <= S and max(us) >= 1.0 - S and max(vs) >= 1.0 - S:
                    face = 1
            if planes:
                face = any(lo_u <= S and lo_v <= S and hi_u >= 1.0 - S and hi_v >= 1.0 - S
                           for lo_u, lo_v, hi_u, hi_v in spans.values())
            if 2 * inside > len(tris) and reach and face:
                return f
    return None


def texcoord_scale(vertices, name='item', draws=None, wraps=None, one_colour=None,
                   tris=None, why=None):
    """The factor that takes the coordinates read at TEXCOORD_SCALE to the
    item's own scale. `draws` groups the vertex indices by the draw that
    reads them; without it the vertices are one draw. `wraps`, when given,
    says per draw whether the image it samples WRAPS (its material slot's
    flag word lacks xblaworld.SLOT_FLAG_CLAMP); without it every draw is
    taken to wrap.

    An unwrapped atlas is reached to its far edge, so the scale is the power
    of two that brings the item's draws into (TEXCOORD_REACH, 1 +
    TEXCOORD_SLACK]. But a draw may TILE its image instead - a grip's
    checkering wraps below zero, a launcher's barrel runs to 1.8 of its
    image while every other draw of the same program stops at its edge - and
    a tiling draw says nothing about where the edge is. So:

      * a draw with a coordinate below zero tiles at every scale and is not
        counted;
      * of the rest, the scale is the LARGEST power of two (1/32768 ..
        1/64) at which MORE THAN HALF of their vertices lie on draws that
        stay inside the image, at least one of which reaches its far edge.

    An item whose draws all stay inside their image at one scale keeps the
    scale that takes its largest coordinate to the edge.

    THE WRAPPED LIP. A draw whose image wraps may legitimately run a little
    below zero without tiling: the remaster's Dam gate samples one 512x512
    image with coordinates on exact quarters of 16384 (0, 4096 .. 16384) and
    a lip at -1219 and -62 - the same far edge, 16384, its sibling door
    items reach with no lip at all. When the rule above finds no scale, the
    scale is the LARGEST power of two at which more than half of ALL the
    item's vertices lie on wrapping draws that stay within one repeat of the
    image - smallest coordinate at or above -1, largest at most
    1 + TEXCOORD_SLACK - at least one of which reaches its far edge. A draw
    that starts exactly one repeat below zero shows the image twice, end to
    end (the remaster's Facility lockers: v -16384 .. 16384 at 1/16384 on a
    wrapping image, u reaching 16384). A draw that reaches past a whole
    repeat below zero, or past the far edge, still tiles and counts
    for nothing; a draw whose image clamps is never read as a lip. The first
    rule is tried first and decides every item it can, so no item it
    accepts is read differently.

    THE FACE (#81), asked only with `tris` (the item's
    triangles, as vertex-index triples) and only when every rule above and
    below refused. A draw may lay its image whole over one face and wrap a
    strip of it along another: the remaster's Severnaya arrow door
    (new/prop/sevdoorv1) is ONE draw whose two faces span its 256x512 image
    exactly (0 .. 16384 on both axes) while its four side faces run the
    image's bottom strip 1.456 times along their height - so the draw
    exceeds its image at 1/16384, stays short of the far edge (0.728) at
    1/32768, and no draw-level rule decides it. Measured per TRIANGLE
    instead, the scale is the LARGEST power of two at which more than half
    of the item's triangles lie inside the image (within TEXCOORD_SLACK on
    either side), at least one of them reaching past TEXCOORD_REACH, AND
    one of them lies on all four edges of the image - a face laid over the
    whole of it. Both must hold at the same scale: the source's other
    refused props (the jerry can, the stool, a server rack) meet one and
    not the other at every scale and are not read by it.

    THE WRAPPED OVERRUN (#93, overrun_scale), also asked only with `tris`
    and only when the face rule refused: the same triangle majority, with
    every other triangle a wrapping face running at most TEXCOORD_OVERRUN
    past the far edge (the Train brake unit's back face, the jerry can's
    cap).

    THE WHOLE IMAGE (#94, whole_image_scale), the last rule: one draw laid over
    its whole image while every other draw lies inside its own or tiles it,
    at exactly one power of two (the Jungle tree, the drone gun). Raises
    CharacterError when no rule finds a scale.

    `why`, when a dict, is given the deciding rule under 'rule' - one of
    TEXCOORD_RULES - so a caller can weigh the answer (prepare.convert_prop's
    THE GAME'S REPEATS checks the whole image's alone)."""
    if why is None:
        why = {}
    if draws is None:
        draws = [list(range(len(vertices)))]
    spans = []                                    # (largest, smallest, vertices, wraps)
    for i, g in enumerate(draws):
        cs = [c for k in g for c in (vertices[k][3], vertices[k][4])]
        if cs:
            spans.append((max(cs), min(cs), len(g),
                          True if wraps is None else bool(wraps[i])))
    flat = [s for s in spans if s[1] >= 0.0]
    total = sum(s[2] for s in flat)
    for k in range(TEXCOORD_DOUBLINGS, -1, -1):
        f = float(1 << k)
        inside = [s for s in flat if s[0] * f <= 1.0 + TEXCOORD_SLACK]
        if 2 * sum(s[2] for s in inside) > total > 0 \
                and any(s[0] * f > TEXCOORD_REACH for s in inside):
            why['rule'] = 'reach'
            return f
    every = sum(s[2] for s in spans)
    for k in range(TEXCOORD_DOUBLINGS, -1, -1):
        f = float(1 << k)
        within = [s for s in spans if s[3] and s[1] * f >= -1.0
                  and s[0] * f <= 1.0 + TEXCOORD_SLACK]
        if 2 * sum(s[2] for s in within) > every > 0 \
                and any(s[0] * f > TEXCOORD_REACH for s in within):
            why['rule'] = 'lip'
            return f
    # THE FLAT DRAW (#77). A draw whose image is one colour in every texel
    # (to within FLAT_SPREAD) draws the same at any scale, so its coordinates
    # say nothing about the scale: when neither rule finds one, they are
    # measured without such draws, and an item whose every draw is flat keeps
    # its coordinates as read. Only reached where both rules refused, so no
    # item they decide is read differently.
    if one_colour is not None and len(one_colour) == len(draws) and any(one_colour):
        rest = [g for g, fl in zip(draws, one_colour) if not fl]
        if not rest:
            why['rule'] = 'flat'
            return 1.0
        return texcoord_scale(vertices, name, rest,
                              None if wraps is None else
                              [w for w, fl in zip(wraps, one_colour) if not fl], why=why)
    # THE ROUNDED ZERO (#78). A draw whose smallest coordinate is one raw unit
    # below zero (-1 at TEXCOORD_SCALE) starts at the image's edge: the
    # remaster's wooden table lays one quad over the whole of its second
    # image, raw 0 .. 4100 on u and -1 .. 4094 on v, while its top tiles the
    # first image from -12272 to 22333 - so no draw stayed inside zero and
    # the item was refused. When every rule above refused, such a draw is
    # measured as starting at zero and the rules are asked again; no item
    # they decide is read differently, and a draw further below zero still
    # tiles.
    mv = set()
    for g in draws:
        cs = [c for k in g for c in (vertices[k][3], vertices[k][4])]
        if cs and -TEXCOORD_SCALE * 1.5 <= min(cs) < 0.0:
            mv.update(g)
    if mv:
        lifted = [(x, y, z, max(u, 0.0) if k in mv else u, max(v, 0.0) if k in mv else v,
                   r, g_, b_, a) for k, (x, y, z, u, v, r, g_, b_, a) in enumerate(vertices)]
        return texcoord_scale(lifted, name, draws, wraps, one_colour, tris, why)
    if tris:
        f = face_scale(vertices, tris)
        if f is not None:
            why['rule'] = 'face'
            return f
        f = overrun_scale(vertices, tris, draws, wraps)
        if f is not None:
            why['rule'] = 'overrun'
            return f
    f = whole_image_scale(vertices, draws, wraps)
    if f is not None:
        why['rule'] = 'whole'
        return f
    top = max([s[0] for s in spans] or [0.0])
    raise CharacterError('%s: the texture coordinates reach no atlas edge at any '
                         'power-of-two scale (%d of %d draws stay inside zero, and no wrapped lip '
                         'qualifies; the largest coordinate is %.3f at 1/32768)'
                         % (name, len(flat), len(spans), top))


def plain_sphere_coords(blob, vb):
    """The sphere coordinate (xblaworld.sphere_uvs) of each vertex of a plain
    buffer `vb` (offset, stride, count) whose word at +20 is a unit normal on
    every vertex, or None: another stride, a word that is not a unit normal
    somewhere, or one word on every vertex (which says nothing either way, as
    read_static's 20-byte records)."""
    if vb[1] != STRIDE_PLAIN:
        return None
    ns = xblaworld._pool_normals(blob, vb[0], vb[1], vb[2], 20)
    return xblaworld.sphere_uvs(ns) if ns is not None and len(set(ns)) > 1 else None


def two_layer_roles(declared, codes, slots, name='item', at=0):
    """The two surfaces of a draw reading the 40-byte two-layer record
    (STRIDE_RIGID_UV2), as [(image, (coordinate offset, colour offset),
    None), (image, (...), dict(channel))] - the base, then the layer and the
    layer image's channel that weighs it. Read from the item itself:
    `declared` is static_fetches' list for the stride (every declaration must
    be RIGID_UV2_FETCHES), `codes` the draw's bound pixel programs (the first
    must be the two-layer blend, xblaworld.layer_blend, weighed by a channel
    of LAYER_CHANNELS), `slots` the draw's material {sampler slot: image}
    (exactly the program's two). Raises CharacterError for anything else."""
    if not declared or any(d != RIGID_UV2_FETCHES for d in declared):
        raise CharacterError('%s: draw at %#x reads a 40-byte record its programs declare '
                             'as %s, not the two-layer record' % (name, at, declared))
    lb = xblaworld.layer_blend(codes[0], channels=(0, 1, 2, 3, None)) if codes else None
    if lb is None:
        raise CharacterError('%s: draw at %#x reads the two-layer record and its program '
                             'is not the two-layer blend' % (name, at))
    if lb['texel_channel'] not in LAYER_CHANNELS:
        raise CharacterError('%s: draw at %#x weighs its layer by texel channel %s, which '
                             'no material realises' % (name, at, lb['texel_channel']))
    if not slots or sorted(slots) != sorted((lb['base'], lb['layer'])):
        raise CharacterError('%s: draw at %#x names slots %s; its program samples %s'
                             % (name, at, sorted(slots or ()), sorted((lb['base'], lb['layer']))))
    out = []
    for role in ('base', 'layer'):
        uvo = RIGID_UV2_FIELDS.get(lb[role + '_coord'])
        co = RIGID_UV2_FIELDS.get(lb[role + '_colour'])
        if uvo is None or co is None:
            raise CharacterError('%s: draw at %#x: its %s reads an interpolator the '
                                 'two-layer record does not carry' % (name, at, role))
        out.append((slots[lb[role]], (uvo, co),
                    dict(channel=lb['texel_channel']) if role == 'layer' else None))
    return out


def read_character(path, images=True, table_names=False, plain_sphere=False, uv_scale=None,
                   layers=False):
    """Read one character or head item. `uv_scale`, when given, is the
    texture-coordinate factor to use in place of texcoord_scale's measurement
    (the caller has measured it another way - prepare.convert_prop's THE
    GAME'S COORDINATES). `layers` carries a two-layer draw (STRIDE_RIGID_UV2,
    two_layer_roles) as its base draw followed by its layer draw, the layer's
    record naming `layer=dict(channel)`; without it such a draw is refused.
    Returns

        dict(joints=[dict(name, parent, bind), ...],
             vertices=[(x, y, z, u, v, r, g, b, a), ...],
             skin=[((joint, weight), ...), ...]      one per vertex, 1..4 pairs
             draws=[dict(image, builtin_head, indices=[...],
                         conditions=[leaf name, ...], pass_mask,
                         reflect=reflection image or None), ...],
             textures=[dict(name, w, h, rgba), ...],
             texcoord_scale=the factor texcoord_scale measured)

    in the item's own draw order. Vertices are merged across the item's
    vertex buffers into one array; each draw's indices point into it.
    Raises CharacterError on anything this reader does not understand."""
    name = os.path.basename(os.path.dirname(path)) or os.path.basename(path)
    try:
        b, m, blob, graph, stream = xblaworld._model_parts(path)
    except xblaworld.WorldAssetError as exc:
        raise CharacterError(str(exc))
    joints, perm = mend_stray_roots(read_pose(graph, name, table_names), name)
    coord_only = coord_only_strides(blob)          # #90 records with no colour
    texels =[int(t['w']) * int(t['h']) for t in b['texs']]

    recs, _end, complete = xblaworld.command_records(stream)
    if not recs or not complete:
        raise CharacterError('%s: the command list does not walk to its end' % name)
    W = xblaworld._words(graph)
    passes = xblaworld.draw_passes(recs)
    conditions = read_conditions(graph, name)

    bones = None
    palette = None
    image = None
    reflect = None
    mslots = None
    vbind = None
    gates = []
    raw_draws = []
    programs = None                  # the draws' bound programs, when one is two-layer
    for (o, op, _low, a) in recs:
        if op == xblaworld.OP_VS_BIND and len(a) >= 2:
            vbind = a[1]
        elif op == OP_BONES:
            if bones is not None or len(a) < 2:
                raise CharacterError('%s: a second or short bone table' % name)
            n = a[1] >> 16
            if not (1 <= n <= MAX_JOINTS) or len(a) < 2 + 2 * n:
                raise CharacterError('%s: bone table of %d entries' % (name, n))
            bones = []
            for k in range(n):
                j = a[2 + 2 * k] >> 16
                if j >= len(joints):
                    raise CharacterError('%s: bone %d names joint %d of %d'
                                         % (name, k, j, len(joints)))
                bones.append(perm[j])
        elif op == OP_PALETTE:
            if len(a) < 2:
                raise CharacterError('%s: a short palette record' % name)
            n = a[1] >> 16
            if not (1 <= n <= 64) or len(a) < 2 + (n + 3) // 4:
                raise CharacterError('%s: palette of %d entries' % (name, n))
            palette = list(_bytes_of(a[2:], n))
        elif op == OP_MATERIAL:
            s = own_slots(xblaworld.material_slots(a), texels)
            if s:
                image = base_image(s, texels)
                rest = {k: v for k, v in s.items() if v != image}
                reflect = base_image(rest, texels) if rest else None
                mslots = s
        elif op == OP_GATE and len(a) >= 2:
            gates.append((o, a[1], a[0]))
        elif op == OP_DRAW:
            if len(a) < 3:
                raise CharacterError('%s: a short draw record' % name)
            prim, count, handle = a[0], a[1], a[2]
            if xblaworld.primitive_triangles(prim, count) is None:
                raise CharacterError('%s: primitive %d of %d elements' % (name, prim, count))
            vb = vertex_buffer_at(graph, len(blob), vbind, name,
                                  STRIDES + tuple(STATIC_LAYOUT) + (STRIDE_RIGID_UV2,))
            if vb[1] == STRIDE_RIGID_UV2:
                if not layers:
                    raise CharacterError('%s: draw at %#x reads a two-layer 40-byte record; '
                                         'only a moving prop carries one' % (name, o))
                if programs is None:
                    programs = xblaworld.bound_programs(graph, xblaworld._program_pools(b, m),
                                                        recs)
                if palette is None or bones is None:
                    raise CharacterError('%s: a draw before its bone table or palette' % name)
                roles = two_layer_roles(static_fetches(blob, follow_full=True).get(vb[1]),
                                        programs.get(o), mslots, name, o)
                ioff = _index_buffer(W, len(blob), handle, count)
                if ioff is None:
                    raise CharacterError('%s: draw at %#x has no index buffer' % (name, o))
                conds = [c for g0, g1, c in gates if g0 < o < g1]
                for im, fields, layer in roles:
                    raw_draws.append(dict(at=o, prim=prim, count=count, ioff=ioff, vb=vb,
                                          palette=list(palette), image=im, reflect=None,
                                          sphere=None, gated=bool(conds), conds=conds,
                                          pass_mask=passes.get(o), fields=fields,
                                          layer=layer))
                continue
            plain = vb[1] == STRIDE_PLAIN
            # #83 A PLAIN RECORD (no coordinate) WHOSE WORD AT +20 IS A UNIT
            # NORMAL on every vertex of its buffer, drawn under a material
            # that names an image, is mapped by that normal - the static
            # reader's rule for its 20-byte records (#81, read_static) and the
            # level reader's (xblaworld._pool_sphere, sphere_uvs). MEASURED on
            # Frigate's helicopter (prop/tiger): its canopy is one such draw
            # (25 vertices, 32 triangles, normals 0.997 .. 1.000) under a
            # material naming the 76x76 reflection image, which no other draw
            # names; drawn by its vertex colour alone it stood as a flat grey
            # slab over the cockpit (playtest run 20261006-185746, mark 5).
            # Taken where the caller asks (`plain_sphere`: a moving prop,
            # prepare.convert_prop). Read across the source's new
            # generation, the items with such a draw are the helicopter, the
            # military helicopter and the security camera among props, and
            # three civilian bodies, the FN P90 and rocket launcher view
            # models and the ammunition cartridges among the rest; those keep
            # their vertex-colour reading until they are measured.
            sph = (plain_sphere_coords(blob, vb)
                   if plain and plain_sphere and image is not None else None)
            if vb[1] in STATIC_LAYOUT:
                # A STATIC draw inside a skinned item (a vehicle's): the
                # world items' layout, no slots and no weights, read by a
                # program that skins nothing - its vertices are the item's
                # own frame, the root joint's.
                if image is None and images:   # the geometry alone needs none
                    raise CharacterError('%s: a draw before its image' % name)
            elif palette is None or bones is None or (image is None and not plain):
                raise CharacterError('%s: a draw before its bone table, palette or image'
                                     % name)
            ioff = _index_buffer(W, len(blob), handle, count)
            if ioff is None:
                raise CharacterError('%s: draw at %#x has no index buffer' % (name, o))
            conds = [c for g0, g1, c in gates if g0 < o < g1]
            raw_draws.append(dict(at=o, prim=prim, count=count, ioff=ioff, vb=vb,
                                  palette=list(palette or ()),
                                  image=None if plain and sph is None else image,
                                  reflect=None if plain else reflect, sphere=sph,
                                  gated=bool(conds), conds=conds, pass_mask=passes.get(o)))
    if not raw_draws:
        raise CharacterError('%s: no draws' % name)

    vertices, skin = [], []
    remap = {}
    draws = []
    textured = set()
    sphered = {}                     # #83 vertex -> its sphere coordinate
    root_joint = next(j for j, jt in enumerate(joints) if jt['parent'] < 0)
    for d in raw_draws:
        voff, stride, nv = d['vb']
        ind = list(struct.unpack('>%dH' % d['count'],
                                 blob[d['ioff']:d['ioff'] + d['count'] * 2]))
        if max(ind) >= nv:
            raise CharacterError('%s: draw at %#x indexes past its vertex buffer'
                                 % (name, d['at']))
        tris = xblaworld.expand_indices(d['prim'], ind)
        out = []
        for i in tris:
            # The same buffer vertex under a DIFFERENT palette is a
            # different skinned vertex, so the palette joins the key.
            # A two-layer record's surfaces are different vertices: the
            # surface (its offsets) joins the key.
            key = (voff, i, tuple(d['palette'])) + ((d['fields'],) if d.get('fields') else ())
            k = remap.get(key)
            if k is None:
                if stride in STATIC_LAYOUT:
                    vert, infl = _static_vertex(blob, voff, stride, i, name, coord_only), \
                        [(root_joint, 255)]
                else:
                    vert, infl = _vertex(blob, voff, stride, i, d['palette'], bones, name,
                                         d.get('fields'))
                k = len(vertices)
                remap[key] = k
                vertices.append(vert)
                skin.append(tuple(infl))
                if d['sphere'] is not None:
                    sphered[k] = d['sphere'][i]
            out.append(k)
        if d['image'] is not None and d['sphere'] is None:
            textured.update(out)
        draws.append(dict(image=d['image'], builtin_head=d['gated'], indices=out,
                          conditions=[conditions[c] if c < len(conditions) else None
                                      for c in d['conds']],
                          pass_mask=d['pass_mask'], reflect=d['reflect']))
        if d.get('layer'):
            draws[-1]['layer'] = d['layer']

    if not images:              # the geometry alone (static_bounds)
        return dict(joints=joints, vertices=vertices, skin=skin, draws=draws)
    # The scale is measured over the vertices that HAVE a coordinate, draw by
    # draw.
    # The measured draws' TRIANGLES go with them, as read_static's do, so the
    # last rule of texcoord_scale (THE FACE, #81) is asked of a skinned item
    # too. MEASURED on the remaster's ceiling gun (prop/roofgun, Bunker 2's
    # autogun, a skinned prop): three of its seven draws lay their images whole
    # (0 .. 16384 on both axes) while two tile theirs along the barrel to 1.6
    # repeats, so no draw-level rule found a scale and the item was refused;
    # the face rule reads it at 1/16384. The rule is asked only where every
    # other rule refused, so no item they decide is read differently.
    measured = [d for d, rd in zip(draws, raw_draws)
                if d['image'] is not None and rd['sphere'] is None]
    if not textured:
        uv = 1.0
    elif uv_scale is not None:
        uv = float(uv_scale)
    else:
        uv = texcoord_scale(vertices, name, [sorted(set(d['indices'])) for d in measured],
                            tris=[tuple(d['indices'][t:t + 3]) for d in measured
                                  for t in range(0, len(d['indices']) - 2, 3)])
    if uv != 1.0:
        vertices = [(x, y, z, u * uv, v * uv, r, g, b_, a)
                    for (x, y, z, u, v, r, g, b_, a) in vertices]
    for k, (su, sv) in sphered.items():
        x, y, z, _u, _v, r, g, b_, a = vertices[k]
        vertices[k] = (x, y, z, su, sv, r, g, b_, a)
    return dict(joints=joints, vertices=vertices, skin=skin, draws=draws,
                textures=_images(b, name, draws), texcoord_scale=uv,
                conditions=conditions)


def _images(b, name, draws):
    """The item's images decoded, and the white image an untextured draw is
    given; every draw's image checked against them."""
    textures = []
    for t in b['texs']:
        nm = caff.tex_name(b, t)
        if t['fmt'] not in xdecode.SUPPORTED:
            raise CharacterError('%s: image %s is format %d, which this pipeline '
                                 'does not decode' % (name, nm, t['fmt']))
        img = xdecode.decode(caff.base_level(b, t), t['fmt'], t['w'], t['h'],
                             mip_levels=t['mips'])
        textures.append(dict(name=nm, w=int(t['w']), h=int(t['h']),
                             rgba=img.tobytes()))
    if any(d['image'] is None for d in draws):
        textures.append(dict(name=PLAIN_IMAGE, w=PLAIN_SIZE, h=PLAIN_SIZE,
                             rgba=b'\xff' * (4 * PLAIN_SIZE * PLAIN_SIZE)))
        for d in draws:
            if d['image'] is None:
                d['image'] = len(textures) - 1
    for d in draws:
        if not (0 <= d['image'] < len(textures)):
            raise CharacterError('%s: a draw names image %d of %d'
                                 % (name, d['image'], len(textures)))
    return textures


# THE STATIC ITEM (#70 props): an item with no skeleton - a pose block of
# zero joints, no bone table, no palette - whose draws read vertex buffers of
# the world items' strides (xblaworld): 24 bytes, position +0, packed normal
# +12, s16 texture coordinate +16, colour +20; or 20, a decal's, without the
# normal. Every vertex is the item's one rigid piece.
STATIC_LAYOUT = {24: (16, 20), 20: (12, 16)}       # stride: (coordinate, colour)
# #81 and, in a STATIC item only, a 32-byte record: position +0, packed
# normal +12, s16 texture coordinate +16, a frame word, colour +28 (the
# GoldenEye key, prop/chrgoldeneyekey). A character's 32-byte buffer is the
# rigid layout (_vertex) and never this.
STATIC_ONLY_LAYOUT = {32: (16, 28)}
# A 20-BYTE RECORD WHOSE PROGRAMS FETCH NO COLOUR (#90). The item's own vertex
# programs declare what a record holds: a full fetch of the position
# (32_32_32_FLOAT at +0) followed by mini fetches of the record's other words
# (static_fetches). MEASURED over the source's 628 new-generation props: 59
# draw from 20-byte records, and every one of their programs fetches +12 as a
# 2_10_10_10 normal; 55 fetch +16 as an 8_8_8_8 colour (the layout above:
# a unit normal at +12 maps the image by it, #81) and four - carbmw,
# carescort, carzil and the landmine - fetch +16 as a 16_16 texture
# coordinate and no colour at all. Read as a colour, the landmine's
# coordinates (0.015 .. 0.985 of its 256x256 image) drew the mine in blue,
# red and purple noise and sphere-mapped its picture (playtest run
# 20261008-214033-lvl29, mark 2). Such a record is read with its coordinate
# at +16 and white for its colour, the identity of the program's multiply.
STATIC_COORD_ONLY = {20: 16}                       # stride: coordinate
FETCH_FLOAT3 = 57                                  # FMT_32_32_32_FLOAT
FETCH_COLOUR = 6                                   # FMT_8_8_8_8
FETCH_COORD = 25                                   # FMT_16_16


def static_fetches(blob, follow_full=False):
    """The record layouts the item's vertex programs declare: {stride in
    bytes: [{byte offset: Xenos fetch format}, ...]}, one dict per program
    whose first fetch is a full fetch of a 32_32_32_FLOAT position at +0,
    holding that fetch and the mini fetches after it - and, with
    `follow_full`, any later FULL fetch of the same stride and the mini
    fetches after that (the 40-byte two-layer record declares its colours
    so: a second full fetch at +32, STRIDE_RIGID_UV2)."""
    W = xblaworld._words(blob)
    out = {}
    i = 0
    while i < len(W) - 2:
        w0, w1, w2 = W[i], W[i + 1], W[i + 2]
        if ((w0 & 31) == 0 and (w0 >> 19) & 1 and not (w1 >> 30) & 1
                and (w1 >> 16) & 63 == FETCH_FLOAT3 and (w2 >> 8) & 0x7fffff == 0
                and 0 < (w2 & 255) <= 32):
            stride = (w2 & 255) * 4
            got = {0: FETCH_FLOAT3}
            j = i + 3
            while j < len(W) - 2 and (W[j] & 31) == 0 and (
                    (W[j + 1] >> 30) & 1
                    or (follow_full and (W[j] >> 19) & 1 and (W[j + 2] & 255) * 4 == stride
                        and (W[j + 2] >> 8) & 0x7fffff)):
                got[((W[j + 2] >> 8) & 0x7fffff) * 4] = (W[j + 1] >> 16) & 63
                j += 3
            if got not in out.setdefault(stride, []):
                out[stride].append(got)
            i = j
            continue
        i += 1
    return out


def coord_only_strides(blob):
    """The strides of STATIC_COORD_ONLY this item's programs read as a
    coordinate with no colour: every program declaring the stride fetches
    the coordinate word as 16_16 and none fetches an 8_8_8_8 colour."""
    f = static_fetches(blob)
    return {s for s, at in STATIC_COORD_ONLY.items()
            if f.get(s) and all(g.get(at) == FETCH_COORD
                                and FETCH_COLOUR not in g.values() for g in f[s])}


# A buffer of POSITIONS alone (12 bytes, no coordinate, no colour): the
# original generation of the remaster's Sea Wolf launcher draws one. Such a
# buffer is read only for the reference box (static_bounds, images=False),
# which needs nothing but the positions; an item that DRAWS from one is still
# refused, since it carries nothing to texture it with.
STATIC_POSITIONS = 12

# A static item's vertex position is a number of at most this size - the
# guard against a misframed buffer read as floats (1e30s and NaNs) - OR it
# lies inside the item's own declared box (xblaworld.declared_bounds) widened
# by DECLARED_MARGIN of its largest extent. MEASURED: the remaster authors
# Statue's park gate (prop/doorstatgate, both generations) in the stage
# background's own space, at the gate's place in the park - x -2600800 ..
# -2554810, 1248.44 item units per room unit (levelinfotable's 0.0801) - so
# every corner is past 1e6, and every one lies inside the box the item
# declares for itself (centre -2577805 152404 697875, extent 22995 36303 290).
# The reference box places the item on the game's model whatever its origin.
# An item whose positions are all under 1e6 reads exactly as before.
MAX_POSITION = 1.0e6
DECLARED_MARGIN = 0.01


def declared_box(graph):
    """The item's own declared box, widened by DECLARED_MARGIN of its largest
    extent, as ((x0, y0, z0), (x1, y1, z1)), or None when it declares none."""
    d = xblaworld.declared_bounds(graph)
    if d is None:
        return None
    c, e = d['centre'], d['extent']
    pad = DECLARED_MARGIN * max(e)
    return (tuple(c[k] - e[k] - pad for k in range(3)),
            tuple(c[k] + e[k] + pad for k in range(3)))


def position_ok(p, box=None):
    """Is the position `p` (x, y, z) one a static item's draw can read - every
    coordinate finite, and either all under MAX_POSITION or all inside the
    item's declared `box` (declared_box)?"""
    if not all(math.isfinite(c) for c in p):
        return False
    if all(abs(c) < MAX_POSITION for c in p):
        return True
    return box is not None and all(box[0][k] <= p[k] <= box[1][k] for k in range(3))


def _static_vertex(blob, off, stride, i, name, coord_only=()):
    """One vertex of a STATIC-layout buffer, as _vertex returns it; a stride in
    `coord_only` (coord_only_strides) carries no colour and reads white."""
    uvo, co = STATIC_LAYOUT[stride]
    if stride in coord_only:
        uvo, co = STATIC_COORD_ONLY[stride], None
    q = off + i * stride
    x, y, z = struct.unpack('>3f', blob[q:q + 12])
    if not all(math.isfinite(c) and abs(c) < 1.0e6 for c in (x, y, z)):
        raise CharacterError('%s: a vertex position is not a number' % name)
    u, v = struct.unpack('>hh', blob[q + uvo:q + uvo + 4])
    al, bb, g, r = (255, 255, 255, 255) if co is None else blob[q + co:q + co + 4]
    return (x, y, z, u * TEXCOORD_SCALE, v * TEXCOORD_SCALE, r, g, bb, al)


# THE UNSEEN IMAGE (#78). The source's programs draw an image multiplied by
# the vertex colour, so an opaque image whose EVERY draw carries (0, 0, 0) on
# all of its vertices is black on every pixel the source could ever draw it
# on - an image nothing shows. An authored image was meant to be seen: the
# sentry gun's barrel cylinder and its five-hole muzzle face (prop/gun_runway1,
# 256x128 and 256x256 of shaded metal) are drawn by nothing but such draws,
# and the gun stands with a black barrel - a defect of the item's data, not a
# shade. MEASURED over every prepared bundle (112): those two images are the
# only ones of any prop, character or weapon drawn that way. Those draws are
# read with the lane LIFTED - their colour white, the identity of the
# multiply, alpha kept - so the image draws as authored. A wholly black draw
# of an image some other draw shows lit is a shade of that surface and is
# left alone (a chair's base, a rifle's muzzle recess); a translucent black
# draw is a shadow or a tint and never counts as showing nothing.
LIFTED_COLOUR = (255, 255, 255)


def unseen_images(vertices, draws, opaque_mask=None):
    """The images no draw of the item shows, sorted: every draw naming the
    image lies in the opaque pass and every vertex it reads is (0, 0, 0).
    `opaque_mask` is the pass bit an opaque draw's pass_mask carries
    (xblaworld.PASS_OPAQUE); None takes every draw as opaque."""
    seen, black = set(), set()
    for d in draws:
        im = d.get('image')
        if im is None or not d.get('indices'):
            continue
        opaque = opaque_mask is None or ((d.get('pass_mask') or 0) & opaque_mask) != 0
        if opaque and all(vertices[i][5] == 0 and vertices[i][6] == 0 and vertices[i][7] == 0
                          for i in d['indices']):
            black.add(im)
        else:
            seen.add(im)
    return sorted(black - seen)


def lift_unseen(vertices, draws, skin=None, opaque_mask=None):
    """Lift the lane on every draw of an unseen image: (vertices, draws, skin,
    images lifted). A vertex such a draw shares with a draw that is not
    lifted is copied, the copy white, so the other draw keeps its shade;
    `skin` (one entry per vertex, or None) follows the copies."""
    unseen = unseen_images(vertices, draws, opaque_mask)
    if not unseen:
        return vertices, draws, skin, []
    lifted = {k for k, d in enumerate(draws) if d.get('image') in unseen}
    kept = {i for k, d in enumerate(draws) if k not in lifted for i in d.get('indices') or ()}
    vertices = list(vertices)
    skin = list(skin) if skin is not None else None
    draws = [dict(d) for d in draws]
    copies = {}
    for k in sorted(lifted):
        out = []
        for i in draws[k]['indices']:
            v = vertices[i]
            white = tuple(v[:5]) + LIFTED_COLOUR + tuple(v[8:])
            if i in kept:
                j = copies.get(i)
                if j is None:
                    j = copies[i] = len(vertices)
                    vertices.append(white)
                    if skin is not None:
                        skin.append(skin[i])
                out.append(j)
            else:
                vertices[i] = white
                out.append(i)
        draws[k]['indices'] = out
    return vertices, draws, skin, unseen


def static_bounds(path):
    """The axis-aligned box of every vertex an item's draws read, as
    ((x0, y0, z0), (x1, y1, z1)): the REFERENCE a static replacement is
    related to the game's model by, when `path` is the source's ORIGINAL
    generation of the item - the game's own model in the item's space. That
    generation may itself be static (a prop) or skinned (a weapon whose
    remastered item lost its skeleton); either is read."""
    try:
        vs = read_static(path, images=False)['vertices']
    except CharacterError as exc:
        if 'has a skeleton' not in str(exc):
            raise
        try:
            vs = read_character(path, images=False)['vertices']
        except CharacterError:
            # a vehicle's original generation names its joints only in its
            # name table, several of them lower-case; the box needs no name
            vs = read_character(path, images=False, table_names=True)['vertices']
    return (tuple(min(v[k] for v in vs) for k in range(3)),
            tuple(max(v[k] for v in vs) for k in range(3)))


def is_static(path):
    """Does the item carry no skeleton (a pose block of zero joints)?"""
    try:
        b, m, blob, graph, stream = xblaworld._model_parts(path)
    except xblaworld.WorldAssetError as exc:
        raise CharacterError(str(exc))
    at = graph.find(POSE_TAG)
    return at >= 0 and at + POSE_COUNT_AT + 4 <= len(graph) and \
        struct.unpack('>I', graph[at + POSE_COUNT_AT:at + POSE_COUNT_AT + 4])[0] == 0


def read_static(path, images=True, uv_scale=None):
    """Read one STATIC item (above). Returns the dict read_character returns,
    with one joint, the root, at the item's origin, and every vertex on it.
    `uv_scale`, when given, is the texture-coordinate factor to use in place
    of texcoord_scale's measurement (prepare.convert_prop's THE GAME'S
    COORDINATES), its rule then 'game'."""
    name = os.path.basename(os.path.dirname(path)) or os.path.basename(path)
    try:
        b, m, blob, graph, stream = xblaworld._model_parts(path)
    except xblaworld.WorldAssetError as exc:
        raise CharacterError(str(exc))
    at = graph.find(POSE_TAG)
    if at >= 0 and at + POSE_COUNT_AT + 4 <= len(graph) and \
            struct.unpack('>I', graph[at + POSE_COUNT_AT:at + POSE_COUNT_AT + 4])[0] != 0:
        raise CharacterError('%s: the item has a skeleton; it is no static item' % name)
    recs, _end, complete = xblaworld.command_records(stream)
    if not recs or not complete:
        raise CharacterError('%s: the command list does not walk to its end' % name)
    W = xblaworld._words(graph)
    box = declared_box(graph)
    coord_only = coord_only_strides(blob)          # #90 records with no colour
    passes = xblaworld.draw_passes(recs)
    # Which image channel each draw's program takes its coverage from
    # (xblaworld.draw_coverage); read for a single-image material only, the
    # base image being the program's fetch 0 exactly then.
    coverage = xblaworld.draw_coverage(graph, xblaworld._program_pools(b, m), recs)
    texels = [int(t['w']) * int(t['h']) for t in b['texs']]
    dims = [(int(t['w']), int(t['h'])) for t in b['texs']]
    image = reflect = vbind = None
    clamps = False
    wraps = []                             # per draw: its image wraps (texcoord_scale)
    single = False
    mslots, mflags, dslots = {}, {}, []    # the standing material; per draw (coverage_base)
    vertices, draws, remap, textured = [], [], {}, set()
    sphere_at = {}                         # #81 per vertex buffer: its sphere coordinates
    sphered = set()                        # ... and the vertices that carry them
    for (o, op, _low, a) in recs:
        if op == xblaworld.OP_VS_BIND and len(a) >= 2:
            vbind = a[1]
        elif op == OP_MATERIAL:
            s = own_slots(xblaworld.material_slots(a), texels)
            if s:
                image = static_base(s, texels, dims)
                rest = {k: v for k, v in s.items() if v != image}
                reflect = base_image(rest, texels) if rest else None
                single = len(s) == 1 and 0 in s
                fl = xblaworld.material_slot_flags(a) or {}
                mslots, mflags = dict(s), dict(fl)
                clamps = any(fl.get(k, 0) & xblaworld.SLOT_FLAG_CLAMP
                             for k, v in s.items() if v == image)
        elif op == OP_DRAW:
            if len(a) < 3 or xblaworld.primitive_triangles(a[0], a[1]) is None:
                raise CharacterError('%s: a draw record this reader does not take' % name)
            voff, stride, nv = vertex_buffer_at(graph, len(blob), vbind, name,
                                                tuple(STATIC_LAYOUT) + tuple(STATIC_ONLY_LAYOUT) +
                                                (() if images else (STATIC_POSITIONS,)))
            # #82 A DRAW BEFORE ANY IMAGE - its material record names no
            # sampler at all - is UNTEXTURED: its vertex colour over the
            # white image (_images), the level reader's rule (#81
            # prepare.UNTEXTURED) and the character reader's plain draws.
            # MEASURED on the Silo tuning console and the satellite circuit
            # board: the first material record binds no slot and its one
            # draw follows it; every later draw names an image of the item.
            ioff = _index_buffer(W, len(blob), a[2], a[1])
            if ioff is None:
                raise CharacterError('%s: draw at %#x has no index buffer' % (name, o))
            ind = list(struct.unpack('>%dH' % a[1], blob[ioff:ioff + a[1] * 2]))
            if max(ind) >= nv:
                raise CharacterError('%s: draw at %#x indexes past its vertex buffer'
                                     % (name, o))
            uvo, co = STATIC_LAYOUT.get(stride, STATIC_ONLY_LAYOUT.get(stride, (None, None)))
            if stride in coord_only:          # #90 no colour: the coordinate at +16
                uvo, co = STATIC_COORD_ONLY[stride], None
            # #81 A 20-BYTE RECORD WHOSE WORD AT +12 IS A UNIT NORMAL on every
            # vertex of its buffer carries no coordinate: the source maps its
            # image by that normal (the level reader's rule, xblaworld.
            # _pool_sphere, and its held-still sphere coordinate, sphere_uvs).
            # MEASURED on the GoldenEye key: its gold dome (one draw, 80
            # vertices, 96 triangles, a gold sphere picture) has unit normals
            # 0.998 .. 1.000 there, averaging (0, 0.86, 0) - a dome facing up.
            # Read as an s16 pair the word spans 7.75 x 6.0 repeats and laid
            # the picture's dark rim over the dome. A buffer whose word is the
            # SAME on every vertex says nothing either way - one coordinate
            # and one normal both sample a single texel (a console's quad and
            # a chair's stem carry such a constant that happens to decode to
            # an axis) - and reads as before, as does any other buffer.
            if stride == 20 and uvo == 12 and voff not in sphere_at:
                ns = xblaworld._pool_normals(blob, voff, stride, nv, 12)
                sphere_at[voff] = xblaworld.sphere_uvs(ns) \
                    if ns is not None and len(set(ns)) > 1 else None
            sph = sphere_at.get(voff) if stride == 20 else None
            out = []
            for i in xblaworld.expand_indices(a[0], ind):
                k = remap.get((voff, i))
                if k is None:
                    q = voff + i * stride
                    x, y, z = struct.unpack('>3f', blob[q:q + 12])
                    if not position_ok((x, y, z), box):
                        raise CharacterError('%s: a vertex position is not a number' % name)
                    if uvo is None:                   # positions alone
                        u = v = 0
                        al = bb = g = r = 255
                    else:
                        u, v = struct.unpack('>hh', blob[q + uvo:q + uvo + 4])
                        if co is None:                # the program fetches no colour
                            al = bb = g = r = 255
                        else:
                            al, bb, g, r = blob[q + co:q + co + 4]
                    k = remap[(voff, i)] = len(vertices)
                    if sph is not None:
                        sphered.add(len(vertices))
                        vertices.append((x, y, z, sph[i][0], sph[i][1], r, g, bb, al))
                    else:
                        vertices.append((x, y, z, u * TEXCOORD_SCALE, v * TEXCOORD_SCALE,
                                         r, g, bb, al))
                out.append(k)
            textured.update(out)
            wraps.append(not clamps)
            dslots.append((mslots, mflags))
            draws.append(dict(image=image, builtin_head=False, indices=out, conditions=[],
                              pass_mask=passes.get(o), reflect=reflect,
                              coverage=coverage.get(o) if single else None))
    if not draws:
        raise CharacterError('%s: no draws' % name)
    # A TRANSLUCENT draw whose material names several images is coloured by
    # the one that carries its coverage (coverage_base); its wrap follows it.
    alpha_full = {}
    single_colour = {}

    def flat_image(im):
        if im not in single_colour:
            t = b['texs'][im] if 0 <= im < len(b['texs']) else None
            if t is None or t['fmt'] not in xdecode.SUPPORTED:
                single_colour[im] = False             # unread: never taken as flat
            else:
                px = bytes(xdecode.decode(caff.base_level(b, t), t['fmt'], t['w'], t['h'],
                                          mip_levels=t['mips']).tobytes())
                single_colour[im] = len(px) >= 4 and all(
                    max(px[c::4]) - min(px[c::4]) <= FLAT_SPREAD for c in range(4))
        return single_colour[im]

    def opaque_image(im):
        if im not in alpha_full:
            t = b['texs'][im] if 0 <= im < len(b['texs']) else None
            if t is None or t['fmt'] not in xdecode.SUPPORTED:
                alpha_full[im] = True          # unread: never taken as the coverage
            else:
                px = xdecode.decode(caff.base_level(b, t), t['fmt'], t['w'], t['h'],
                                    mip_levels=t['mips'])
                al = bytes(px.tobytes())[3::4]
                alpha_full[im] = min(al) == 255 if al else True
        return alpha_full[im]
    for k, d in enumerate(draws):
        s, fl = dslots[k]
        pm = d['pass_mask'] or 0
        if len(set(s.values())) < 2 or (pm & xblaworld.PASS_OPAQUE) \
                or not (pm & xblaworld.PASS_TRANSLUCENT):
            continue
        base = coverage_base(s, {im for im in set(s.values()) if opaque_image(im)})
        if base is None or base == d['image']:
            continue
        d['reflect'], d['image'], d['coverage'] = d['image'], base, None
        wraps[k] = not any(fl.get(sl, 0) & xblaworld.SLOT_FLAG_CLAMP
                           for sl, v in s.items() if v == base)
    # #81 A BASE THAT IS A TANGENT-SPACE NORMAL MAP gives way to the lowest
    # slot that is not one - the level reader's rule (prepare: a flat-blue
    # mean, red and green near the middle, blue near the top). The key's
    # card and disc draws name a swirl normal map in slot 0 and drew it as
    # their colour.
    nm = {}

    def normalmapish(im):
        if im not in nm:
            t = b['texs'][im] if 0 <= im < len(b['texs']) else None
            if t is None or t['fmt'] not in xdecode.SUPPORTED:
                nm[im] = False
            else:
                px = bytes(xdecode.decode(caff.base_level(b, t), t['fmt'], t['w'], t['h'],
                                          mip_levels=t['mips']).tobytes())
                n = len(px) // 4
                mr = sum(px[0::4]) / n; mg = sum(px[1::4]) / n; mb = sum(px[2::4]) / n
                nm[im] = abs(mr - 128.0) < 30.0 and abs(mg - 128.0) < 30.0 and mb > 170.0
        return nm[im]
    for k, d_ in enumerate(draws):
        s, _fl = dslots[k]
        if images and len(set(s.values())) > 1 and normalmapish(d_['image']):
            for sl in sorted(s):
                if not normalmapish(s[sl]):
                    d_['image'] = s[sl]
                    break
    joints = [dict(name='SKEL_ROOT', parent=-1, bind=(0.0, 0.0, 0.0))]
    skin = [((0, 255),)] * len(vertices)
    if not images:
        return dict(joints=joints, vertices=vertices, skin=skin, draws=draws)
    # The scale is measured over the stored coordinates alone: a sphere
    # coordinate (above) is already the image's own, and takes no part.
    keep = [k for k, d in enumerate(draws)
            if d['image'] is not None and not sphered.intersection(d['indices'])]
    why = {}
    if uv_scale is not None and keep:
        uv, why['rule'] = float(uv_scale), 'game'
    else:
        uv = 1.0 if not keep else texcoord_scale(
            vertices, name, [sorted(set(draws[k]['indices'])) for k in keep],
            [wraps[k] for k in keep], [flat_image(draws[k]['image']) for k in keep],
            [tuple(draws[k]['indices'][t:t + 3]) for k in keep
             for t in range(0, len(draws[k]['indices']) - 2, 3)], why)
    if uv != 1.0:
        vertices = [(x, y, z, u, v, r, g, b_, a) if n in sphered else
                    (x, y, z, u * uv, v * uv, r, g, b_, a)
                    for n, (x, y, z, u, v, r, g, b_, a) in enumerate(vertices)]
    return dict(joints=joints, vertices=vertices, skin=skin, draws=draws,
                textures=_images(b, name, draws), texcoord_scale=uv, conditions=[],
                texcoord_rule=why.get('rule'))
