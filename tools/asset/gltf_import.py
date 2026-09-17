#!/usr/bin/env python3
"""Sightline - install an ordinary glTF 2.0 model as a native asset override.

This is the OFFLINE half of the asset-override seam. The game never parses
glTF, JSON, PNG or JPEG: this tool converts a .gltf/.glb into one compact,
versioned, bounds-checked binary ("SLM1") and writes it where the runtime
looks. See src/sl_asset_override.h for the format and the C loader, and
docs/asset-overrides.md for the whole picture.

Nothing this tool reads or writes belongs in the repository. Custom models are
the player's own content and live outside the tree, beside the EEPROM.

Usage is normally through the PowerShell wrapper:

    .\\tools\\windows\\asset-import.ps1 -Asset boot.nintendo_logo -Input model.glb
    .\\tools\\windows\\asset-import.ps1 -Remove boot.nintendo_logo
    .\\tools\\windows\\asset-import.ps1 -Help

Standard library only, by design: the converter must run on a bare Python with
no wheels installed, because it is the one step between the owner's Blender
export and a working override.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import os
import re
import struct
import sys
import zlib
from pathlib import Path

# --------------------------------------------------------------------------
# The asset id table. Kept in step with src/sl_asset_override.h by hand; adding
# an id is a deliberate act in both files. The test suite checks that every
# relative path here appears verbatim in the C loader.
# --------------------------------------------------------------------------

ASSET_IDS = {
    "boot.nintendo_logo": "boot/nintendo_logo.slmodel",
    "boot.rareware_logo": "boot/rareware_logo.slmodel",
    "boot.goldeneye_logo": "boot/goldeneye_logo.slmodel",
    "boot.legal_page": "boot/legal_page.slmodel",
}

# --------------------------------------------------------------------------
# Limits. THESE ARE THE SAME NUMBERS THE RUNTIME ENFORCES
# (src/sl_asset_override.h, SL_AMDL_MAX_*). Checking here means the owner gets
# a useful message at import time instead of a rejection at boot; checking
# there means a hand-edited or corrupt file still cannot make the game
# allocate a gigabyte.
#
# The ceilings are MEASURED rather than guessed: a survey of 27 real
# Blender/trimesh logo exports ran from 22k to 461k vertices and from 132k to
# 2.76M indices, so a smaller cap would reject the very files this feature
# exists to install. At the ceiling a model costs about 32 MB of attributes
# plus 12 MB of indices - bounded, and nowhere near a gigabyte.
# --------------------------------------------------------------------------

MAX_VERTS = 1000000
MAX_INDICES = 3000000
MAX_PRIMS = 256
MAX_MATS = 256
MAX_TEXS = 16
MAX_TEXDIM = 2048
MAX_TEXBYTES = 64 * 1024 * 1024
MAX_FILE = 96 * 1024 * 1024

MAGIC = b"SLM1"
# Version 2: a texture slot is EITHER embedded pixels OR a reference to a
# game texture, and the slot grew from 16 to 32 bytes to say which. This
# number and SL_AMDL_VERSION in src/sl_asset_override.h must agree; the test
# suite asserts it.
VERSION = 2
HEADER_SIZE = 128

F_NORMALS, F_UV, F_COLOR = 1, 2, 4
TEX_EMBEDDED, TEX_REF = 0, 1
MAX_TEXNAME = 63
M_DOUBLESIDED, M_ALPHA_BLEND, M_ALPHA_MASK, M_UNLIT = 1, 2, 4, 8
# Generated texture coordinates, per material. See the long note in
# src/sl_asset_override.h; the short version is that a reflection sampled
# through BAKED UVs is welded to the surface and turns with the model, while
# the original logos set G_TEXTURE_GEN and get a coordinate manufactured per
# frame from the vertex normal - which is what makes the highlight SWEEP.
#
# The DEFAULT is decided at runtime, not here: a material whose texture is a
# REFERENCE to a game reflection map inherits generation automatically. These
# two bits exist only to OVERRIDE that default, from the glTF material's
#     "extras": { "sl_texgen": true }     force on
#     "extras": { "sl_texgen": false }    force off
# and the importer never sets both, which the loader also refuses.
#
# NO VERSION BUMP. These are unused bits of a material flags word that has
# been a u32 since version 1 - no offset, size or count changes - so an older
# build reads such a file and ignores them, landing on exactly the baked-UV
# behaviour it had before. The version field is for changes an old reader
# could get WRONG, and this is not one.
M_TEXGEN, M_TEXGEN_OFF = 16, 32

# glTF extensions this converter understands well enough to be faithful.
# KHR_materials_unlit says "render the base colour directly, do not light it",
# which the renderer honours per material.
ALLOWED_EXTENSIONS = {"KHR_materials_unlit"}

# ==========================================================================
# AUTHORED TEXTURES, and why --repo accepts them when it accepts no others
#
# --repo's original rule was categorical: a committed model carries NO
# embedded pixels, only references to game textures. That rule exists to keep
# ROM pixels out of the repository (project rule 2), and as a guard against
# ROM pixels it is exactly right and is NOT relaxed here.
#
# But it also refused pixels that were never the game's. A colour ramp drawn
# from a handful of stops, a procedurally generated mask, a gradient authored
# in a paint program - none of these are ROM-derived, and refusing them made
# "no ROM assets" and "no author's own artwork" the same rule when they are
# two different rules with two different justifications.
#
# So an embedded texture may be committed if, and ONLY if, the source file
# SAYS SO, in these two keys on the glTF image or texture object:
#
#     "extras": {
#         "sl_authored": true,
#         "sl_authored_provenance": "how these pixels were made"
#     }
#
# THREE PROPERTIES THIS MECHANISM HAS, each load-bearing:
#
#   EXPLICIT, NEVER INFERRED. Nothing about the pixels themselves can earn
#   this - not their size, not their palette, not their name. The only way in
#   is a marker an author typed. A file that does not carry it is refused
#   exactly as before, so the default is unchanged and the failure mode of
#   forgetting the marker is a rejection rather than a silent commit.
#
#   IT CANNOT LAUNDER ROM PIXELS. Content-addressed game-texture detection
#   runs FIRST, in texture_index(), and turns a recognised texture into a
#   REFERENCE before this marker is ever read. Marking a game texture as
#   authored therefore does not embed it - the pixels are already gone. The
#   marker widens what may be embedded; it does not narrow what is
#   recognised, and the registry check it would have to beat does not ask the
#   author anything.
#
#   IT CARRIES ITS REASON. The provenance string is REQUIRED and must be
#   non-empty: a marker alone would record that somebody claimed authorship
#   and not what the claim was. It is printed at import and belongs in the
#   commit, so a reviewer reading the model can see the assertion without
#   re-running the tool.
#
# What this does NOT decide is whether the author had the RIGHT to make those
# pixels - font licensing, third-party artwork, and the like. That is an
# owner's judgement about a specific asset and no check here can stand in for
# it; the provenance string is what puts the question in front of them.
# ==========================================================================

AUTHORED_KEY = "sl_authored"
AUTHORED_PROVENANCE_KEY = "sl_authored_provenance"


def authored_marker(*objs):
    """Read the authored marker off the first glTF object that carries it.

    Returns (authored, provenance). Raises when the marker is present but
    malformed, because a marker the tool cannot read is the one case where
    staying quiet would be worst: the author believes the texture is declared
    and the categorical rule would reject it later with an unrelated message.
    """
    for o in objs:
        if not isinstance(o, dict):
            continue
        ex = o.get("extras")
        if not isinstance(ex, dict) or AUTHORED_KEY not in ex:
            continue
        v = ex[AUTHORED_KEY]
        if v is not True:
            if v is False:
                return False, None
            raise ImportError_(
                "extras.%s must be true or false, not %r" % (AUTHORED_KEY, v))
        prov = ex.get(AUTHORED_PROVENANCE_KEY)
        if not isinstance(prov, str) or not prov.strip():
            raise ImportError_(
                "extras.%s is set, but extras.%s is missing or empty. An "
                "authored texture must say HOW it was authored - that string "
                "is the whole point of the marker, and it is what a reviewer "
                "reads instead of re-running this tool."
                % (AUTHORED_KEY, AUTHORED_PROVENANCE_KEY))
        return True, prov.strip()
    return False, None



# ==========================================================================
# GAME TEXTURES: the registry, and why detection is CONTENT-ADDRESSED
#
# Project rule 2 - no ROM-derived assets in the repository, ever - is the
# rule the project's whole distribution model rests on. A custom model that
# reuses one of the game's own textures therefore cannot be shipped with those
# pixels in it. Its GEOMETRY and MATERIALS are the author's own work and travel
# fine; the pixels do not.
#
# So this importer REFUSES TO EMBED a texture it can identify as the game's,
# and writes a REFERENCE - the texture's stable identifier - in its place. The
# runtime resolves it from data the player already has
# (src/native/sl_texref.c).
#
# WHY CONTENT-ADDRESSING RATHER THAN A DECLARATION. The alternative is to trust
# the author to say "this one came from the game". That makes rule 2 a matter
# of discipline, and discipline is exactly what fails at 2am on the fortieth
# import. Hashing every incoming texture and comparing it against the game
# textures this tool can reach makes the rule MECHANICAL: an author cannot
# embed those pixels even deliberately, because the check does not ask them.
#
# WHAT EACH ENTRY KNOWS. For every identifier the registry needs the canonical
# decoded RGBA's dimensions, a SHA-256 to recognise it by, and the FNV-1a the
# runtime will check its own resolution against. Where those come from differs,
# and the difference is MEASURED, not assumed:
#
#   rareware.env_field   DERIVED, at import time, from assets/rarewarelogo.c -
#   rareware.env_gold    which is committed repository source. The two u32
#                        arrays D_02004FE8 (:1549) and D_02005FF0 (:1813) are
#                        the textures, and title.c:384/:388 is the authority
#                        for reading them as 32x32 RGBA16. NOTHING IS COMMITTED
#                        FOR THESE: the digests are recomputed on every run
#                        from source that is already in the tree, so they
#                        cannot go stale and they add no bytes.
#
#   nintendo.logo_i8     Two DIGESTS, committed below. The pixels are inside
#                        the 1172-compressed ROM prop PnintendologoZ and are
#                        NOT in the tree - `git ls-files assets/obseg/prop/
#                        nintendologo/` returns a ModelFileHeader and a
#                        propFileRecord and no texture - so there is nothing
#                        here to derive them from.
#
# ON COMMITTING THOSE TWO DIGESTS. They are 32 and 4 bytes of one-way hash over
# a 32x32 image. They cannot reconstruct a pixel, cannot be decoded, and cannot
# be rendered; their ONLY function is to let this tool REFUSE the pixels and to
# let the runtime prove it resolved the right ones. They make rule 2 stronger,
# not weaker - without them the Nintendo texture can only be caught by an
# author's declaration, and a player's own local .slmodel would quietly contain
# ROM bytes. This is a judgment call and it is written down so it can be
# reversed: delete the two constants and detection for that identifier falls
# back to --texture-ref, with the repository-safety check below still absolute.
#
# There is precedent in this file: REFERENCE above carries the ROM prop's
# measured bounding box for the same reason, with the same note that three
# numbers are not asset data.
# ==========================================================================

REPO_ROOT = Path(__file__).resolve().parents[2]

# The committed source the two Rareware environment maps are read out of, and
# the symbols inside it. Cited rather than guessed: title.c binds exactly these
# two with gDPLoadTextureBlock(..., G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, ...).
RAREWARE_LOGO_C = "assets/rarewarelogo.c"

# MEASURED from the owner's own export of the ROM prop, decoded to RGBA the way
# this importer decodes any greyscale PNG. See the note above on why a digest
# is here and pixels are not.
NINTENDO_LOGO_SHA256 = (
    "5c045f46258b3659c4643ca0a66c6ac995cbfa9f2f69799a840c874ca64eb772")
NINTENDO_LOGO_FNV1A = 0x6A5F45A8


def fnv1a(data: bytes) -> int:
    """FNV-1a, 32-bit. The same function as sl_amdl_fnv1a() in
    src/sl_asset_override.h - the runtime recomputes it over what it resolved
    and compares. Defined in both places and tested to agree."""
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def tex_digest(w: int, h: int, rgba) -> str:
    """The canonical content address of a decoded texture. Dimensions are part
    of it so two different images cannot collide merely by sharing bytes at
    different shapes."""
    return hashlib.sha256(
        ("%dx%d:" % (w, h)).encode("ascii") + bytes(rgba)).hexdigest()


def _c_u32_array(path: Path, sym: str):
    """The u32 initialiser list of a named C array, as integers.

    Deliberately narrow: it matches `u32 <sym>[] = { ... };` and reads number
    tokens. It is not a C parser and is not asked to be one - it reads two
    arrays in one committed file, and if either ever stops matching, the
    identifier reports itself unavailable rather than returning wrong data."""
    src = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"\bu32\s+" + re.escape(sym) + r"\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;",
                  src, re.S)
    if not m:
        return None
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    try:
        return [int(t, 0) for t in re.findall(r"0[xX][0-9a-fA-F]+|\b\d+\b", body)]
    except ValueError:
        return None


def _x5to8(v: int) -> int:
    v &= 0x1F
    return (v << 3) | (v >> 2)


def _rgba16_words_to_rgba(words, w: int, h: int):
    """Decode N64 RGBA16 held as u32 C literals.

    The high halfword of each word is the FIRST texel - the source is
    big-endian ROM data written out as 32-bit literals - so this is correct on
    any host without knowing the host's byte order. It is the same 5/5/5/1
    expansion sl_gfx_tex.c performs (sl_put_rgba16, :260)."""
    need = w * h
    texels = []
    for word in words:
        texels.append((word >> 16) & 0xFFFF)
        texels.append(word & 0xFFFF)
        if len(texels) >= need:
            break
    if len(texels) < need:
        return None
    out = bytearray()
    for i in range(need):
        c = texels[i]
        out += bytes((_x5to8(c >> 11), _x5to8(c >> 6), _x5to8(c >> 1),
                      0xFF if (c & 1) else 0x00))
    return out


_GAME_TEX_CACHE = None


def game_textures() -> dict:
    """identifier -> {w, h, sha256, fnv1a, source} for every game texture this
    tool can recognise. Computed once per run."""
    global _GAME_TEX_CACHE
    if _GAME_TEX_CACHE is not None:
        return _GAME_TEX_CACHE

    table = {}

    logo_c = REPO_ROOT / RAREWARE_LOGO_C
    for name, sym in (("rareware.env_field", "D_02004FE8"),
                      ("rareware.env_gold", "D_02005FF0")):
        rgba = None
        if logo_c.is_file():
            words = _c_u32_array(logo_c, sym)
            if words:
                rgba = _rgba16_words_to_rgba(words, 32, 32)
        if rgba is None:
            continue
        table[name] = {
            "w": 32, "h": 32,
            "sha256": tex_digest(32, 32, rgba),
            "fnv1a": fnv1a(bytes(rgba)),
            "source": "%s, %s (32x32 RGBA16)" % (RAREWARE_LOGO_C, sym),
            # GENERATED coordinates, sourced: title.c:338 sets G_TEXTURE_GEN
            # in the geometry mode, and the gDPLoadTextureBlock calls for both
            # of these stand under it. Mirrors the runtime table's `coord`
            # column (src/native/sl_texref.c); t_texref_tables_agree checks the
            # two do not drift.
            "generated": True,
        }

    table["nintendo.logo_i8"] = {
        "w": 32, "h": 32,
        "sha256": NINTENDO_LOGO_SHA256,
        "fnv1a": NINTENDO_LOGO_FNV1A,
        "source": "the PnintendologoZ ROM prop (32x32 I8), by digest",
        # GENERATED, measured rather than sourced: the geometry mode is inside
        # the compressed prop, so the renderer's own decline line is the
        # evidence - mode=00062205, and 0x00040000 of that is G_TEXTURE_GEN.
        "generated": True,
    }

    _GAME_TEX_CACHE = table
    return table


def game_texture_for_digest(d: str):
    for name, e in game_textures().items():
        if e["sha256"] == d:
            return name
    return None


# ==========================================================================
# THE MODEL-SPACE CONTRACT, per asset.
#
# The target space is the GAME's own model space: 1 glTF unit == 1 N64 model
# unit, no conversion anywhere, the same space tools/export/logo_models.py
# writes when it exports an original. So "how big should my replacement be?"
# has a concrete answer - the box the original occupies - and it is printed at
# every import, so a scale mismatch is a number on screen rather than
# something discovered after a launch.
#
# These are the ORIGINALS' measured extents. They are REFERENCE DATA FOR THE
# PRINTOUT ONLY. Nothing here is ever applied automatically, and the game holds
# no per-logo factor of any kind.
#
#   rareware  MEASURED here, from committed source - assets/rarewarelogo.c,
#             397 Vtx records, x [-143,165] y [-207,208] z [-14,15].
#   nintendo  MEASURED from the glTF that tools/export/logo_models.py writes
#   goldeneye for PnintendologoZ / PgoldeneyelogoZ / PlegalpageZ. The ROM props
#   legal     are not in the tree and never will be; three extent numbers are
#             not asset data.
# ==========================================================================

REFERENCE = {
    "boot.nintendo_logo": {
        "size": (3706.0, 814.0, 498.0),
        "note": "PnintendologoZ, the ROM prop, via tools/export/logo_models.py",
    },
    "boot.rareware_logo": {
        "size": (308.0, 415.0, 29.0),
        "note": "assets/rarewarelogo.c, measured over its 397 Vtx records",
    },
    # MEASURED over the exported glTF's POSITION accessor min/max, both
    # meshes together: `lettering` is flat at z=0 spanning x [-1284, 1284]
    # y [-139, 106], and `flatshaded_slab` is what carries the y and z depth,
    # x [345, 903] y [-272, 272] z [-67, 67].
    "boot.goldeneye_logo": {
        "size": (2568.0, 544.0, 134.0),
        "note": "PgoldeneyelogoZ, the ROM prop, via tools/export/logo_models.py",
    },
    # MEASURED the same way over six single-quad meshes. Entirely flat: every
    # vertex is at z = 0, so the depth is 0 and only width and height can be
    # matched. ARTWORK ONLY - the wording is font-rendered at runtime and is
    # not part of this model, so a replacement cannot and need not carry text.
    "boot.legal_page": {
        "size": (5201.0, 2117.0, 0.0),
        "note": "PlegalpageZ, the ROM prop, via tools/export/logo_models.py",
    },
}


def report_fit(asset, lo, hi, scale):
    """Print the model's box beside the original's, and say what uniform scale
    would match it. NEVER applies anything."""
    size = [hi[j] - lo[j] for j in range(3)]
    print("  bounds     x [%.1f %.1f]  y [%.1f %.1f]  z [%.1f %.1f]%s"
          % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
             "   (after --scale %g)" % scale if scale != 1.0 else ""))
    print("  size       %.1f x %.1f x %.1f model units" % tuple(size))
    ref = REFERENCE.get(asset)
    if ref is None:
        return
    rs = ref["size"]
    print("  original   %.1f x %.1f x %.1f   <- the box a replacement should "
          "occupy to appear at the original's size" % rs)
    print("             (%s)" % ref["note"])
    axes = [j for j in range(3) if size[j] > 1e-6]
    if not axes:
        return
    fits = [rs[j] / size[j] for j in axes]
    lo_f, hi_f = min(fits), max(fits)
    if hi_f <= lo_f * 1.05:
        print("  fit        --scale %.4g matches the original's size "
              "(all axes agree)" % ((lo_f + hi_f) / 2.0 * scale))
    else:
        names = "XYZ"
        per = "  ".join("%s %.4g" % (names[j], rs[j] / size[j]) for j in axes)
        print("  fit        per-axis factors differ: %s" % per)
        print("             --scale %.4g matches the original's WIDTH, "
              "--scale %.4g matches its HEIGHT."
              % (rs[0] / size[0] * scale if size[0] > 1e-6 else 0.0,
                 rs[1] / size[1] * scale if size[1] > 1e-6 else 0.0))
        print("             Your model's aspect ratio is not the original's, "
              "so there is no single right answer - pick one.")
        print("             NOTHING IS APPLIED AUTOMATICALLY.")


class ImportError_(Exception):
    """A rejection with an explanation the owner can act on."""


# ==========================================================================
# Path resolution - THE SAME RULE AS THE RUNTIME
#
# sl_asset_override_dir() in src/native/sl_asset_override.c implements this
# ladder in C. If the two ever disagree the importer writes a file the game
# never looks for, and an opt-in feature silently does nothing - the worst
# failure mode available to it. The ladder itself is not invented here: it is
# the one tools/windows/play.ps1 uses for the EEPROM and the one
# tools/export/logo_models.py uses for exports.
# ==========================================================================


def override_root() -> Path:
    env = os.environ.get("SL_ASSET_OVERRIDE_DIR")
    if env:
        return Path(env)
    base = os.environ.get("LOCALAPPDATA")
    if not base:
        home = os.environ.get("USERPROFILE")
        if home:
            base = str(Path(home) / "AppData" / "Local")
    if not base:
        base = os.environ.get("TEMP")
    if not base:
        base = "."
    return Path(base) / "sightline" / "assets"


def asset_path(asset: str) -> Path:
    return override_root() / ASSET_IDS[asset]


# The committed models. Below the install directory in the runtime's search
# order, so a player's own import always wins - see the ladder comment in
# src/native/sl_asset_override.c.
REPO_OVERRIDE_DIR = "data/asset-overrides"


def repo_asset_path(asset: str) -> Path:
    return REPO_ROOT / REPO_OVERRIDE_DIR / ASSET_IDS[asset]


# ==========================================================================
# PNG - decoded with the standard library and nothing else
#
# A texture has to reach the game as RGBA8, and the game must not carry an
# image decoder. PNG is zlib plus five per-row filters, so stdlib zlib is the
# whole dependency. JPEG is not attempted and is rejected by name rather than
# mis-decoded.
# ==========================================================================

PNG_SIG = bytes([137, 80, 78, 71, 13, 10, 26, 10])


def png_decode(data: bytes):
    if data[:8] != PNG_SIG:
        raise ImportError_("texture is not a PNG")

    pos = 8
    width = height = depth = ctype = interlace = 0
    idat = bytearray()
    palette = b""
    trns = b""
    while pos + 8 <= len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        ctag = data[pos + 4: pos + 8]
        body = data[pos + 8: pos + 8 + length]
        pos += 12 + length
        if ctag == b"IHDR":
            (width, height, depth, ctype,
             _comp, _filt, interlace) = struct.unpack(">IIBBBBB", body)
        elif ctag == b"PLTE":
            palette = body
        elif ctag == b"tRNS":
            trns = body
        elif ctag == b"IDAT":
            idat += body
        elif ctag == b"IEND":
            break

    if interlace:
        raise ImportError_(
            "interlaced (Adam7) PNG is not supported - re-save the texture "
            "without interlacing")
    if depth not in (8, 16):
        raise ImportError_(
            "PNG bit depth %d is not supported - use 8 bits per channel" % depth)
    if ctype not in (0, 2, 3, 4, 6):
        raise ImportError_("PNG colour type %d is not supported" % ctype)
    if ctype == 3 and depth != 8:
        raise ImportError_("paletted PNG must be 8 bits per pixel")

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bypc = depth // 8
    stride = width * channels * bypc
    bpp = channels * bypc

    raw = zlib.decompress(bytes(idat))
    if len(raw) < (stride + 1) * height:
        raise ImportError_("PNG pixel data is truncated")

    # Undo the per-row filters (PNG spec 9.2). One previous row is all the
    # state any of the five filters needs.
    out = bytearray(stride * height)
    prev = bytearray(stride)
    src = 0
    for y in range(height):
        ftype = raw[src]
        src += 1
        row = bytearray(raw[src: src + stride])
        src += stride
        if ftype == 1:
            for i in range(bpp, stride):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = row[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                row[i] = (row[i] + pr) & 0xFF
        elif ftype != 0:
            raise ImportError_("unknown PNG row filter %d" % ftype)
        out[y * stride: (y + 1) * stride] = row
        prev = row

    # -> RGBA8
    rgba = bytearray(width * height * 4)
    for y in range(height):
        ro = y * stride
        wo = y * width * 4
        for x in range(width):
            po = ro + x * bpp
            if ctype == 3:
                idx = out[po]
                if (idx + 1) * 3 > len(palette):
                    raise ImportError_("PNG palette index out of range")
                r, g, b = palette[idx * 3: idx * 3 + 3]
                a = trns[idx] if idx < len(trns) else 255
            else:
                comp = [out[po + c * bypc] for c in range(channels)]
                if ctype == 0:
                    r = g = b = comp[0]
                    a = 255
                elif ctype == 2:
                    r, g, b = comp
                    a = 255
                elif ctype == 4:
                    r = g = b = comp[0]
                    a = comp[1]
                else:
                    r, g, b, a = comp
            o = wo + x * 4
            rgba[o] = r
            rgba[o + 1] = g
            rgba[o + 2] = b
            rgba[o + 3] = a
    return width, height, rgba


def halve(w: int, h: int, px: bytearray):
    """One box-filter halving step, used to bring an oversized texture under
    the dimension cap rather than rejecting the whole model for it."""
    nw, nh = max(1, w // 2), max(1, h // 2)
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        y0, y1 = min(2 * y, h - 1), min(2 * y + 1, h - 1)
        for x in range(nw):
            x0, x1 = min(2 * x, w - 1), min(2 * x + 1, w - 1)
            o = (y * nw + x) * 4
            for c in range(4):
                out[o + c] = (px[(y0 * w + x0) * 4 + c]
                              + px[(y0 * w + x1) * 4 + c]
                              + px[(y1 * w + x0) * 4 + c]
                              + px[(y1 * w + x1) * 4 + c]) // 4
    return nw, nh, out


def png_encode(w: int, h: int, px) -> bytes:
    """RGBA8 -> a minimal, valid, non-interlaced PNG. Used only by
    --emit-sample, so the sample model can carry a real texture."""
    raw = b"".join(bytes([0]) + bytes(px[y * w * 4:(y + 1) * w * 4])
                   for y in range(h))

    def ch(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    return (PNG_SIG
            + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + ch(b"IDAT", zlib.compress(raw))
            + ch(b"IEND", b""))


# ==========================================================================
# glTF
# ==========================================================================

COMPONENT = {
    5120: ("b", 1),
    5121: ("B", 1),
    5122: ("h", 2),
    5123: ("H", 2),
    5125: ("I", 4),
    5126: ("f", 4),
}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}

JPEG_SOI = bytes([255, 216, 255])


def uri_unquote(u: str) -> str:
    from urllib.parse import unquote
    return unquote(u)


class Gltf:
    def __init__(self, path: Path):
        self.path = path
        raw = path.read_bytes()
        self.glb_bin = None
        if raw[:4] == b"glTF":
            _magic, ver, total = struct.unpack_from("<III", raw, 0)
            if ver != 2:
                raise ImportError_("GLB container version %d; only glTF 2.0" % ver)
            off = 12
            doc = None
            while off + 8 <= min(total, len(raw)):
                clen, ctype = struct.unpack_from("<II", raw, off)
                chunk = raw[off + 8: off + 8 + clen]
                if ctype == 0x4E4F534A:
                    doc = json.loads(chunk.decode("utf-8"))
                elif ctype == 0x004E4942:
                    self.glb_bin = chunk
                off += 8 + clen
            if doc is None:
                raise ImportError_("GLB has no JSON chunk")
            self.doc = doc
        else:
            self.doc = json.loads(raw.decode("utf-8"))

        if self.doc.get("asset", {}).get("version", "").split(".")[0] != "2":
            raise ImportError_("only glTF 2.0 is supported")

        self._buffers = {}

    def buffer(self, i: int) -> bytes:
        if i in self._buffers:
            return self._buffers[i]
        b = self.doc["buffers"][i]
        uri = b.get("uri")
        if uri is None:
            if self.glb_bin is None:
                raise ImportError_(
                    "buffer %d has no uri and there is no GLB chunk" % i)
            data = self.glb_bin
        elif uri.startswith("data:"):
            data = base64.b64decode(uri.split(",", 1)[1])
        else:
            data = (self.path.parent / uri_unquote(uri)).read_bytes()
        self._buffers[i] = data
        return data

    def view(self, i: int):
        v = self.doc["bufferViews"][i]
        data = self.buffer(v["buffer"])
        off = v.get("byteOffset", 0)
        length = v["byteLength"]
        if off + length > len(data):
            raise ImportError_("bufferView %d runs past its buffer" % i)
        return data[off: off + length], v.get("byteStride", 0)

    def accessor(self, i: int) -> list:
        a = self.doc["accessors"][i]
        if "sparse" in a:
            raise ImportError_("sparse accessors are not supported")
        if a.get("bufferView") is None:
            raise ImportError_(
                "accessors without a bufferView are not supported")
        fmt, size = COMPONENT[a["componentType"]]
        n = NCOMP[a["type"]]
        count = a["count"]
        data, stride = self.view(a["bufferView"])
        base = a.get("byteOffset", 0)
        if not stride:
            stride = n * size
        out = []
        for k in range(count):
            o = base + k * stride
            if o + n * size > len(data):
                raise ImportError_("accessor %d reads past its bufferView" % i)
            out.append(struct.unpack_from("<" + fmt * n, data, o))
        if a.get("normalized"):
            div = {5120: 127.0, 5121: 255.0,
                   5122: 32767.0, 5123: 65535.0}.get(a["componentType"])
            if div:
                out = [tuple(max(-1.0, c / div) for c in v) for v in out]
        return out


# -- matrices ---------------------------------------------------------------
#
# Row-major under a row-vector convention (v' = v * M) - the same convention
# the runtime's modelview stack uses, so the feature has one convention rather
# than two that must be kept in agreement.

IDENTITY = (1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0)


def mat_mul(a, b):
    return tuple(sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4))
                 for r in range(4) for c in range(4))


def node_matrix(node: dict):
    if "matrix" in node:
        # glTF stores column-major; transposed into the row-vector convention.
        m = node["matrix"]
        return tuple(m[c * 4 + r] for r in range(4) for c in range(4))
    t = node.get("translation", [0.0, 0.0, 0.0])
    r = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    s = node.get("scale", [1.0, 1.0, 1.0])
    x, y, z, w = r
    rot = (
        1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0.0,
        2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0.0,
        2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0.0,
        0.0, 0.0, 0.0, 1.0,
    )
    scl = (s[0], 0, 0, 0, 0, s[1], 0, 0, 0, 0, s[2], 0, 0, 0, 0, 1.0)
    trn = (1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, t[0], t[1], t[2], 1.0)
    return mat_mul(mat_mul(scl, rot), trn)


def xform_point(m, p):
    x, y, z = p
    return (x * m[0] + y * m[4] + z * m[8] + m[12],
            x * m[1] + y * m[5] + z * m[9] + m[13],
            x * m[2] + y * m[6] + z * m[10] + m[14])


def normal_basis(m):
    """Inverse transpose of the upper 3x3, for transforming normals. Falls back
    to the plain basis when the matrix is singular (a degenerate scale) rather
    than producing NaNs."""
    a = [m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]]
    det = (a[0] * (a[4] * a[8] - a[5] * a[7])
           - a[1] * (a[3] * a[8] - a[5] * a[6])
           + a[2] * (a[3] * a[7] - a[4] * a[6]))
    if abs(det) < 1e-12:
        return a
    inv = [
        (a[4] * a[8] - a[5] * a[7]) / det,
        (a[2] * a[7] - a[1] * a[8]) / det,
        (a[1] * a[5] - a[2] * a[4]) / det,
        (a[5] * a[6] - a[3] * a[8]) / det,
        (a[0] * a[8] - a[2] * a[6]) / det,
        (a[2] * a[3] - a[0] * a[5]) / det,
        (a[3] * a[7] - a[4] * a[6]) / det,
        (a[1] * a[6] - a[0] * a[7]) / det,
        (a[0] * a[4] - a[1] * a[3]) / det,
    ]
    return [inv[0], inv[3], inv[6], inv[1], inv[4], inv[7], inv[2], inv[5], inv[8]]


def xform_normal(b, n):
    x, y, z = n
    nx = x * b[0] + y * b[3] + z * b[6]
    ny = x * b[1] + y * b[4] + z * b[7]
    nz = x * b[2] + y * b[5] + z * b[8]
    ln = (nx * nx + ny * ny + nz * nz) ** 0.5
    if ln < 1e-9:
        return (0.0, 0.0, 1.0)
    return (nx / ln, ny / ln, nz / ln)


# ==========================================================================
# Rejections - the unsupported glTF features, named
# ==========================================================================


def check_unsupported(g: Gltf) -> list:
    d = g.doc
    notes = []

    for key in ("extensionsRequired", "extensionsUsed"):
        for e in d.get(key, []) or []:
            if e not in ALLOWED_EXTENSIONS:
                raise ImportError_(
                    "glTF extension %r is not supported. Supported: %s. "
                    "Re-export without it."
                    % (e, ", ".join(sorted(ALLOWED_EXTENSIONS))))
    if d.get("animations"):
        raise ImportError_(
            "the file contains animation tracks. Boot logo choreography is the "
            "game's, not the asset's - export a static mesh.")
    if d.get("skins"):
        raise ImportError_(
            "skinned meshes are not supported - apply the pose and export a "
            "static mesh.")
    if d.get("cameras"):
        notes.append("cameras in the file are ignored; the boot screen owns "
                     "the camera")

    for mi, m in enumerate(d.get("meshes", []) or []):
        for p in m.get("primitives", []):
            if p.get("targets"):
                raise ImportError_(
                    "mesh %d uses morph targets, which are not supported - "
                    "apply the shape keys and export a static mesh." % mi)
            mode = p.get("mode", 4)
            if mode != 4:
                raise ImportError_(
                    "mesh %d uses primitive mode %d; only mode 4 (TRIANGLES) "
                    "is supported. Triangulate on export." % (mi, mode))
            for attr in p["attributes"]:
                if attr.startswith(("JOINTS_", "WEIGHTS_")):
                    raise ImportError_("skinning attributes are not supported")
                if attr == "TANGENT":
                    notes.append("TANGENT is ignored (no normal mapping)")
                if attr in ("TEXCOORD_1", "TEXCOORD_2"):
                    notes.append("%s is ignored; only TEXCOORD_0 is used" % attr)

    for mi, m in enumerate(d.get("materials", []) or []):
        for bad, why in (("normalTexture", "normal maps"),
                         ("occlusionTexture", "ambient occlusion maps"),
                         ("emissiveTexture", "emissive maps")):
            if bad in m:
                raise ImportError_(
                    "material %r uses %s (%s), which this renderer cannot "
                    "render and will not fake. Bake it into the base colour "
                    "texture." % (m.get("name", mi), bad, why))
        pbr = m.get("pbrMetallicRoughness", {})
        if "metallicRoughnessTexture" in pbr:
            raise ImportError_(
                "material %r uses a metallicRoughnessTexture. There is no PBR "
                "renderer here; bake the look into the base colour texture."
                % m.get("name", mi))
        if (pbr.get("metallicFactor") not in (None, 0)
                or pbr.get("roughnessFactor") not in (None, 1)):
            notes.append(
                "material %r: metallicFactor/roughnessFactor are IGNORED - the "
                "base colour is rendered directly" % m.get("name", mi))
    return notes


# ==========================================================================
# Conversion
# ==========================================================================


def convert(g: Gltf, verbose: bool = True, scale: float = 1.0,
            asset=None, texture_refs=None, repo_safe: bool = False) -> bytes:
    d = g.doc
    notes = check_unsupported(g)
    if verbose:
        for n in dict.fromkeys(notes):
            print("  note: %s" % n)

    scene = d.get("scene", 0)
    scenes = d.get("scenes") or [
        {"nodes": list(range(len(d.get("nodes", []) or [])))}]
    roots = scenes[scene].get("nodes", [])
    nodes = d.get("nodes", []) or []

    positions = []
    normals = []
    uvs = []
    colours = []
    indices = []
    prims = []     # (first, count, material)
    mats = []
    images = {}    # glTF image index -> our texture index
    textures = []  # dicts: EMBEDDED {kind,w,h,px} or REF {kind,w,h,name,fnv}
    refs = {k.lower(): v for k, v in (texture_refs or {}).items()}
    refs_used = set()

    have_nrm = [False]
    have_uv = [False]
    have_col = [False]

    def texture_index(gltf_tex_index: int) -> int:
        tex = d["textures"][gltf_tex_index]
        src = tex.get("source")
        if src is None:
            raise ImportError_("a texture has no image source")
        if src in images:
            return images[src]
        img = d["images"][src]
        mime = img.get("mimeType", "")
        if "uri" in img and not img["uri"].startswith("data:"):
            raw = (g.path.parent / uri_unquote(img["uri"])).read_bytes()
            if img["uri"].lower().endswith((".jpg", ".jpeg")):
                mime = "image/jpeg"
        elif "uri" in img:
            head, b64 = img["uri"].split(",", 1)
            raw = base64.b64decode(b64)
            if "jpeg" in head:
                mime = "image/jpeg"
        else:
            raw, _ = g.view(img["bufferView"])
        if "jpeg" in mime or bytes(raw[:3]) == JPEG_SOI:
            raise ImportError_(
                "JPEG textures are not supported (the converter carries no "
                "JPEG decoder and the game carries none either). Re-export the "
                "texture as PNG.")
        w, h, px = png_decode(bytes(raw))

        # ---- is this one of the GAME's textures? ----------------------
        #
        # Asked BEFORE any resizing, and asked of the pixels rather than of
        # the author. A game texture is never embedded, in a repository file
        # or a player's own: what leaves this function for such a slot is an
        # identifier, and the pixels are dropped on the floor here.
        label = img.get("uri") or ("image %d" % src)
        ident = game_texture_for_digest(tex_digest(w, h, px))
        how = "recognised by content"
        if ident is None:
            for key in (str(label).lower(),
                        str(label).lower().rsplit("/", 1)[-1],
                        str(label).lower().rsplit(chr(92), 1)[-1]):
                if key in refs:
                    ident = refs[key]
                    how = "declared with --texture-ref"
                    refs_used.add(key)
                    break
            if ident is not None and ident not in game_textures():
                raise ImportError_(
                    "--texture-ref names '%s', which is not a game texture "
                    "identifier this build knows. Known: %s"
                    % (ident, ", ".join(sorted(game_textures()))))

        if len(textures) >= MAX_TEXS:
            raise ImportError_("more than %d distinct textures" % MAX_TEXS)

        if ident is not None:
            e = game_textures()[ident]
            if len(ident) > MAX_TEXNAME:
                raise ImportError_("texture identifier '%s' is longer than "
                                   "%d characters" % (ident, MAX_TEXNAME))
            if verbose:
                print("  texture    %s is the game's own %s (%s)"
                      % (label, ident, how))
                print("             -> stored as a REFERENCE; its pixels are "
                      "NOT written into the model")
            # The dimensions and the hash come from the REGISTRY, not from
            # the incoming image. A declared reference may have been
            # re-saved, resized or recoloured by the author's toolchain; the
            # runtime is going to resolve the GAME's copy, so the hash it
            # will be checked against must be the game's too.
            images[src] = len(textures)
            textures.append({"kind": TEX_REF, "w": e["w"], "h": e["h"],
                             "name": ident, "fnv": e["fnv1a"],
                             "label": label})
            return images[src]

        while w > MAX_TEXDIM or h > MAX_TEXDIM:
            if verbose:
                print("  note: texture %dx%d exceeds the %d limit - halving"
                      % (w, h, MAX_TEXDIM))
            w, h, px = halve(w, h, px)
        # READ ONLY AFTER game-texture detection has already declined this
        # image. Ordering is the guarantee, not a convenience: a recognised
        # texture returned above as a REFERENCE never reaches this line, so
        # the marker cannot apply to one and cannot launder it.
        authored, provenance = authored_marker(tex, img)
        images[src] = len(textures)
        textures.append({"kind": TEX_EMBEDDED, "w": w, "h": h, "px": px,
                         "label": label, "authored": authored,
                         "provenance": provenance})
        return images[src]

    def emit_primitive(p: dict, m):
        attrs = p["attributes"]
        if "POSITION" not in attrs:
            raise ImportError_("a primitive has no POSITION attribute")
        pos = g.accessor(attrs["POSITION"])
        n = len(pos)
        nrm = g.accessor(attrs["NORMAL"]) if "NORMAL" in attrs else None
        uv = g.accessor(attrs["TEXCOORD_0"]) if "TEXCOORD_0" in attrs else None
        col = g.accessor(attrs["COLOR_0"]) if "COLOR_0" in attrs else None
        for name, arr in (("NORMAL", nrm), ("TEXCOORD_0", uv), ("COLOR_0", col)):
            if arr is not None and len(arr) != n:
                raise ImportError_(
                    "%s has a different count from POSITION" % name)

        if "indices" in p:
            idx = [v[0] for v in g.accessor(p["indices"])]
        else:
            idx = list(range(n))
        if len(idx) % 3:
            raise ImportError_("index count is not a multiple of 3")
        for v in idx:
            if v >= n:
                raise ImportError_(
                    "a triangle index (%d) is outside the primitive's %d "
                    "vertices" % (v, n))

        mat = {}
        gm = d.get("materials", [])
        gi = p.get("material")
        src = gm[gi] if (gi is not None and gi < len(gm)) else {}
        pbr = src.get("pbrMetallicRoughness", {})
        base = list(pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0]))
        while len(base) < 4:
            base.append(1.0)
        mat["texture"] = -1
        if "baseColorTexture" in pbr:
            bct = pbr["baseColorTexture"]
            if bct.get("texCoord", 0) != 0:
                raise ImportError_("baseColorTexture must use TEXCOORD_0")
            if uv is None:
                raise ImportError_(
                    "a material has a baseColorTexture but its mesh has no "
                    "TEXCOORD_0 - export UVs or drop the texture")
            mat["texture"] = texture_index(bct["index"])
        flags = 0
        if "KHR_materials_unlit" in (src.get("extensions") or {}):
            flags |= M_UNLIT
        # extras.sl_texgen - the author's explicit override of the implied
        # default. Only a real bool is accepted: a string "true" or a 1 would
        # be a guess about intent, and the whole point of the field is to be
        # unambiguous where the implication is not trusted.
        xg = (src.get("extras") or {}).get("sl_texgen")
        if xg is not None:
            if not isinstance(xg, bool):
                raise ImportError_(
                    "material %r has extras.sl_texgen = %r; it must be true or "
                    "false. true forces per-frame generated texture "
                    "coordinates on for that material, false forces them off, "
                    "and leaving it out lets the texture decide."
                    % (src.get("name", "?"), xg))
            flags |= M_TEXGEN if xg else M_TEXGEN_OFF
        if src.get("doubleSided"):
            flags |= M_DOUBLESIDED
        amode = src.get("alphaMode", "OPAQUE")
        cutoff = float(src.get("alphaCutoff", 0.5))
        if amode == "BLEND":
            flags |= M_ALPHA_BLEND
        elif amode == "MASK":
            flags |= M_ALPHA_MASK
        elif amode != "OPAQUE":
            raise ImportError_("unknown alphaMode %r" % amode)
        mat["flags"] = flags
        mat["cutoff"] = cutoff

        # COLOR_0, when present, absorbs baseColorFactor. Fixed-function GL
        # cannot apply a per-vertex colour AND a constant factor at once, and
        # glTF multiplies them - so the multiply happens here, where it is
        # exact, and the material's own factor becomes white. Every vertex
        # belongs to exactly one primitive in this format, so the fold is
        # unambiguous.
        mat["base"] = [1.0, 1.0, 1.0, 1.0] if col is not None else base

        nb = normal_basis(m)
        first = len(indices)
        vbase = len(positions)
        for k in range(n):
            positions.append(xform_point(m, pos[k][:3]))
            if nrm is not None:
                normals.append(xform_normal(nb, nrm[k][:3]))
            else:
                normals.append((0.0, 0.0, 1.0))
            uvs.append(tuple(uv[k][:2]) if uv is not None else (0.0, 0.0))
            if col is not None:
                c = list(col[k])
                while len(c) < 4:
                    c.append(1.0)
                colours.append(tuple(
                    max(0, min(255, int(round(c[j] * base[j] * 255.0))))
                    for j in range(4)))
            else:
                colours.append((255, 255, 255, 255))
        if nrm is not None:
            have_nrm[0] = True
        if uv is not None:
            have_uv[0] = True
        if col is not None:
            have_col[0] = True

        indices.extend(vbase + v for v in idx)
        if len(mats) >= MAX_MATS:
            raise ImportError_("more than %d materials" % MAX_MATS)
        mats.append(mat)
        prims.append((first, len(idx), len(mats) - 1))

    # A uniform pre-scale, baked in exactly the way a node transform is. It
    # lives here rather than in the renderer deliberately: the runtime contract
    # stays 1 glTF unit == 1 N64 model unit for every asset, with no per-logo
    # number anywhere in the game, and the owner's choice is recorded in the
    # file they installed.
    root_xform = (scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, 1.0)

    def walk(ni: int, parent):
        if ni >= len(nodes):
            return
        node = nodes[ni]
        # BAKED. The runtime holds one modelview - the boot screen's own - so a
        # node hierarchy has nowhere to live at draw time. Composing it here
        # also means the file the game reads carries no transform semantics to
        # get wrong.
        m = mat_mul(node_matrix(node), parent)
        if "mesh" in node:
            for p in d["meshes"][node["mesh"]].get("primitives", []):
                if len(prims) >= MAX_PRIMS:
                    raise ImportError_("more than %d primitives" % MAX_PRIMS)
                emit_primitive(p, m)
        for c in node.get("children", []) or []:
            walk(c, m)

    for r in roots:
        walk(r, root_xform)

    if not prims:
        raise ImportError_("the file contains no triangle geometry")
    if len(positions) > MAX_VERTS:
        raise ImportError_(
            "%d vertices exceeds the limit of %d - decimate the mesh"
            % (len(positions), MAX_VERTS))
    if len(indices) > MAX_INDICES:
        raise ImportError_(
            "%d indices exceeds the limit of %d - decimate the mesh"
            % (len(indices), MAX_INDICES))
    # A REFERENCE costs the same memory once resolved as embedded pixels
    # would, so both count against the budget. Exempting references would
    # make the cap describe the file rather than the runtime.
    total_tex = sum(t["w"] * t["h"] * 4 for t in textures)
    if total_tex > MAX_TEXBYTES:
        raise ImportError_(
            "decoded textures total %d bytes, over the %d limit"
            % (total_tex, MAX_TEXBYTES))

    flags = 0
    if have_nrm[0]:
        flags |= F_NORMALS
    if have_uv[0]:
        flags |= F_UV
    if have_col[0]:
        flags |= F_COLOR

    # ---- the repository-safety gate ----------------------------------
    #
    # For a file destined for the REPOSITORY the rule is not "no texture we
    # recognise" - it is NO EMBEDDED PIXELS AT ALL. That distinction is the
    # whole guarantee. A recognition-based rule is only as good as the
    # registry, and a texture the registry has never seen would sail
    # through it; a categorical rule cannot be evaded, because a committed
    # model simply has no pixel data in it to inspect.
    #
    # The one exception is a texture the source file EXPLICITLY declares as
    # the author's own work - see the AUTHORED TEXTURES note above. That
    # declaration is read off the glTF, never inferred from the pixels, and it
    # is read only after content-addressed game-texture detection has already
    # had its say, so it cannot apply to a texture the registry recognises.
    if repo_safe:
        bad = [t for t in textures
               if t["kind"] != TEX_REF and not t.get("authored")]
        ok_authored = [t for t in textures
                       if t["kind"] != TEX_REF and t.get("authored")]
        if bad:
            raise ImportError_(
                "this model cannot be committed: %d of its %d textures "
                "would be written into the file as pixels (%s).\n"
                "A model in the repository may REFERENCE game textures by "
                "identifier, or embed textures the file DECLARES as the "
                "author's own - see docs/project-rules.md, rule 2.\n"
                "Known identifiers: %s.\n"
                "If one of these IS a game texture that was re-saved or "
                "edited, declare it with\n"
                "    --texture-ref <image>=<identifier>\n"
                "If it is YOUR OWN artwork and no part of it came from the "
                "game, mark it in the glTF image or texture:\n"
                '    "extras": { "%s": true, "%s": "<how it was made>" }'
                % (len(bad), len(textures),
                   ", ".join(str(t["label"]) for t in bad),
                   ", ".join(sorted(game_textures())),
                   AUTHORED_KEY, AUTHORED_PROVENANCE_KEY))
        if verbose:
            # SAY WHICH, AND WHY. A blanket "repo-safe" line would hide the
            # difference between a file that embeds nothing and a file that
            # embeds pixels under a declaration - and the second is the one a
            # reviewer has to actually look at.
            nref = sum(1 for t in textures if t["kind"] == TEX_REF)
            for t in ok_authored:
                print("  authored   %s is embedded as the author's OWN "
                      "artwork" % t["label"])
                print("             provenance: %s" % t["provenance"])
                print("             (declared in extras.%s; not recognised "
                      "as any game texture)" % AUTHORED_KEY)
            if ok_authored:
                print("  repo-safe  %d reference(s), %d declared-authored "
                      "texture(s); no game pixels are in this file"
                      % (nref, len(ok_authored)))
            else:
                print("  repo-safe  every texture slot is a reference; no "
                      "game pixels are in this file")

    unused = sorted(set(refs) - refs_used)
    if unused:
        raise ImportError_(
            "--texture-ref named %s, but no image in this model has that "
            "name. Images present: %s"
            % (", ".join("'%s'" % u for u in unused),
               ", ".join(str(t["label"]) for t in textures) or "none"))

    nvert = len(positions)
    body = bytearray()
    offs = {}

    def place(name: str, blob: bytes) -> None:
        offs[name] = HEADER_SIZE + len(body)
        body.extend(blob)
        while len(body) % 4:
            body.append(0)

    place("pos", struct.pack("<%df" % (nvert * 3),
                             *[c for p in positions for c in p]))
    if flags & F_NORMALS:
        place("nrm", struct.pack("<%df" % (nvert * 3),
                                 *[c for p in normals for c in p]))
    else:
        offs["nrm"] = 0
    if flags & F_UV:
        place("uv", struct.pack("<%df" % (nvert * 2),
                                *[c for p in uvs for c in p]))
    else:
        offs["uv"] = 0
    if flags & F_COLOR:
        place("col", bytes(c for p in colours for c in p))
    else:
        offs["col"] = 0
    place("idx", struct.pack("<%dI" % len(indices), *indices))
    place("prim", b"".join(struct.pack("<IIII", f, c, mi, 0)
                           for f, c, mi in prims))
    place("mat", b"".join(
        struct.pack("<ffffiIfI",
                    m["base"][0], m["base"][1], m["base"][2], m["base"][3],
                    m["texture"], m["flags"], m["cutoff"], 0)
        for m in mats))

    # The texture table. 32 bytes per slot in version 2, laid out exactly as
    # src/sl_asset_override.h documents it:
    #   w, h, pixel offset, pixel length, kind, name offset, name length,
    #   FNV-1a of the pixels the reference stands for.
    # An embedded slot has no name and no hash; a reference has no pixels.
    if textures:
        tex_table_off = HEADER_SIZE + len(body)
        body.extend(b"\0" * (32 * len(textures)))
        entries = []
        for t in textures:
            if t["kind"] == TEX_REF:
                nm = t["name"].encode("ascii")
                noff = HEADER_SIZE + len(body)
                body.extend(nm)
                body.append(0)
                while len(body) % 4:
                    body.append(0)
                entries.append((t["w"], t["h"], 0, 0,
                                TEX_REF, noff, len(nm), t["fnv"]))
            else:
                off = HEADER_SIZE + len(body)
                body.extend(t["px"])
                while len(body) % 4:
                    body.append(0)
                entries.append((t["w"], t["h"], off, len(t["px"]),
                                TEX_EMBEDDED, 0, 0, 0))
        for i, e in enumerate(entries):
            struct.pack_into("<8I", body,
                             tex_table_off - HEADER_SIZE + i * 32, *e)
        offs["tex"] = tex_table_off
    else:
        offs["tex"] = 0

    total = HEADER_SIZE + len(body)
    if total > MAX_FILE:
        raise ImportError_("the converted model is %d bytes, over the %d limit"
                           % (total, MAX_FILE))

    header = bytearray(HEADER_SIZE)
    struct.pack_into(
        "<4sIIIIIIIIIIIIIIIII", header, 0,
        MAGIC, VERSION, HEADER_SIZE, flags,
        nvert, len(indices), len(prims), len(mats), len(textures),
        offs["pos"], offs["nrm"], offs["uv"], offs["col"], offs["idx"],
        offs["prim"], offs["mat"], offs["tex"], total)

    if verbose:
        lo = [min(p[j] for p in positions) for j in range(3)]
        hi = [max(p[j] for p in positions) for j in range(3)]
        report_fit(asset, lo, hi, scale)
        print("  vertices   %d" % nvert)
        print("  triangles  %d" % (len(indices) // 3))
        print("  primitives %d" % len(prims))
        print("  materials  %d" % len(mats))
        print("  textures   %d%s"
              % (len(textures),
                 ("  (" + ", ".join(
                     "%dx%d %s" % (t["w"], t["h"],
                                    t["name"] if t["kind"] == TEX_REF
                                    else "embedded")
                     for t in textures) + ")") if textures else ""))
        print("  attributes %s"
              % (", ".join(n for n, f in (("NORMAL", F_NORMALS),
                                          ("TEXCOORD_0", F_UV),
                                          ("COLOR_0", F_COLOR)) if flags & f)
                 or "POSITION only"))
        # WHICH MATERIALS SWEEP, said at import time. The decision is the
        # runtime's to make - it is the one that knows the texture registry -
        # so this reports the SAME rule rather than a second copy of it, and
        # says which of the two inputs decided. An author who expected a
        # reflection to sweep and sees "stored" here has the answer before
        # launching anything.
        gt = game_textures()
        for mi, mm in enumerate(mats):
            ti = mm["texture"]
            ref = (textures[ti]["name"]
                   if ti >= 0 and textures[ti]["kind"] == TEX_REF else None)
            if mm["flags"] & M_TEXGEN:
                why, on = "declared in extras.sl_texgen", True
            elif mm["flags"] & M_TEXGEN_OFF:
                why, on = "declared off in extras.sl_texgen", False
            elif ref is not None and gt.get(ref, {}).get("generated"):
                why, on = "implied by the '%s' reference" % ref, True
            else:
                continue
            print("  material %d texture coordinates: %s  (%s)"
                  % (mi, "GENERATED per frame" if on else "stored (baked)",
                     why))
        if flags & F_NORMALS == 0 and any(m["flags"] & M_TEXGEN for m in mats):
            print("  WARNING: a material asks for generated coordinates but "
                  "the mesh has no NORMAL - they are generated FROM the "
                  "normal, so it will draw its stored UVs instead")
    return bytes(header) + bytes(body)


# ==========================================================================
# A synthetic sample model
#
# Written so the whole loop - import, install, launch, see it, remove, see the
# original - can be walked without owning a single asset, and so the
# integration test is REPEATABLE: a fixed, tiny, self-describing model is
# better evidence that the seam works than a large real one, because it can be
# regenerated on any machine with no ROM, no downloads and no modelling.
#
# It is also the honest way to demonstrate the coordinate contract, because it
# is authored directly to the reference box that report_fit() prints.
#
# Two primitives and two materials, covering the supported subset in one file:
# a double-sided octagonal plate carrying COLOR_0 and no texture, and a smaller
# quad in front of it carrying TEXCOORD_0 and an embedded checkerboard.
# ==========================================================================


def emit_sample(path: Path, asset) -> None:
    ref = REFERENCE.get(asset or "", {}).get("size", (300.0, 400.0, 30.0))
    hx, hy, hz = ref[0] / 2.0, ref[1] / 2.0, max(ref[2], 1.0) / 2.0

    ring = []
    for i in range(8):
        a = (i + 0.5) * math.pi / 4.0
        ring.append((hx * math.cos(a), hy * math.sin(a)))
    pos0, col0, nrm0, idx0 = [], [], [], []
    for sign, nz in ((1.0, 1.0), (-1.0, -1.0)):
        base = len(pos0)
        pos0.append((0.0, 0.0, sign * hz))
        col0.append((1.0, 1.0, 1.0, 1.0))
        nrm0.append((0.0, 0.0, nz))
        for (x, y) in ring:
            pos0.append((x, y, sign * hz))
            col0.append((0.15, 0.35, 1.0, 1.0) if sign > 0
                        else (0.6, 0.1, 0.1, 1.0))
            nrm0.append((0.0, 0.0, nz))
        for i in range(8):
            a, b = base + 1 + i, base + 1 + (i + 1) % 8
            idx0 += [base, a, b] if sign > 0 else [base, b, a]

    qx, qy, qz = hx * 0.55, hy * 0.55, hz * 1.4
    pos1 = [(-qx, -qy, qz), (qx, -qy, qz), (qx, qy, qz), (-qx, qy, qz)]
    uv1 = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
    nrm1 = [(0.0, 0.0, 1.0)] * 4
    idx1 = [0, 1, 2, 0, 2, 3]

    n = 16
    px = bytearray(n * n * 4)
    for y in range(n):
        for x in range(n):
            o = (y * n + x) * 4
            on = ((x // 2) + (y // 2)) & 1
            px[o] = 250 if on else 20
            px[o + 1] = 200 if on else 20
            px[o + 2] = 40 if on else 90
            px[o + 3] = 255
    tex = png_encode(n, n, px)

    buf = bytearray()
    views, accessors = [], []

    def add(vals, ctype, atype):
        fmt, size = COMPONENT[ctype]
        k = NCOMP[atype]
        while len(buf) % 4:
            buf.append(0)
        off = len(buf)
        for v in vals:
            buf.extend(struct.pack(
                "<" + fmt * k,
                *(v if isinstance(v, (list, tuple)) else (v,))))
        views.append({"buffer": 0, "byteOffset": off,
                      "byteLength": len(buf) - off})
        accessors.append({"bufferView": len(views) - 1, "componentType": ctype,
                          "count": len(vals), "type": atype})
        return len(accessors) - 1

    a_p0 = add(pos0, 5126, "VEC3")
    a_n0 = add(nrm0, 5126, "VEC3")
    a_c0 = add(col0, 5126, "VEC4")
    a_i0 = add(idx0, 5125, "SCALAR")
    a_p1 = add(pos1, 5126, "VEC3")
    a_n1 = add(nrm1, 5126, "VEC3")
    a_u1 = add(uv1, 5126, "VEC2")
    a_i1 = add(idx1, 5125, "SCALAR")
    while len(buf) % 4:
        buf.append(0)
    tex_off = len(buf)
    buf.extend(tex)
    views.append({"buffer": 0, "byteOffset": tex_off, "byteLength": len(tex)})
    tex_view = len(views) - 1

    doc = {
        "asset": {"version": "2.0",
                  "generator": "sightline gltf_import.py --emit-sample"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [{"name": "plate", "mesh": 0}, {"name": "badge", "mesh": 1}],
        "meshes": [
            {"primitives": [{"attributes": {"POSITION": a_p0, "NORMAL": a_n0,
                                            "COLOR_0": a_c0},
                             "indices": a_i0, "material": 0, "mode": 4}]},
            {"primitives": [{"attributes": {"POSITION": a_p1, "NORMAL": a_n1,
                                            "TEXCOORD_0": a_u1},
                             "indices": a_i1, "material": 1, "mode": 4}]},
        ],
        "materials": [
            {"name": "plate", "doubleSided": True,
             "pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0]}},
            {"name": "badge",
             "pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                                      "baseColorTexture": {"index": 0}}},
        ],
        "images": [{"bufferView": tex_view, "mimeType": "image/png"}],
        "textures": [{"source": 0}],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(buf),
                     "uri": "data:application/octet-stream;base64,"
                            + base64.b64encode(bytes(buf)).decode()}],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc), encoding="utf-8")
    print("wrote a sample glTF to %s" % path)
    print("  %.1f x %.1f x %.1f model units - authored to %s's reference box"
          % (ref[0], ref[1], ref[2], asset or "a default"))
    print("  two primitives, two materials, vertex colour, an embedded PNG "
          "texture, one double-sided material")


# ==========================================================================
# CLI
# ==========================================================================

EPILOG = """\
ASSET IDS
    boot.nintendo_logo    the model on the Nintendo boot screen
                          3706 x 814 x 498 model units
    boot.rareware_logo    the model on the Rareware boot screen
                          308 x 415 x 29 model units
    boot.goldeneye_logo   the GOLDENEYE logo model on the logo screen. This
                          screen is the REFLECTIVE one: it uploads its own
                          gSPLookAt basis and its lettering is drawn through
                          G_TEXTURE_GEN, so a replacement that references a
                          generated-coordinate map (or declares
                          extras.sl_texgen) sweeps its highlight the way the
                          original does.  2568 x 544 x 134 model units
    boot.legal_page       the ARTWORK layer of the Legal screen - six flat
                          textured quads. The twelve lines of legal WORDING
                          are NOT part of this model: they are font-rendered
                          at runtime from the language bank, so replacing the
                          model changes no text. Flat, so it has no depth.
                          5201 x 2117 x 0 model units

WHERE FILES GO
    $SL_ASSET_OVERRIDE_DIR, else %LOCALAPPDATA%\\sightline\\assets, else
    %USERPROFILE%\\AppData\\Local\\sightline\\assets, else %TEMP%\\sightline\\assets.
    The game resolves the SAME ladder; --where prints the resolved path.
    Delete the file (or use --remove) and the original renders again on the
    next launch. No rebuild is ever needed, and no source is ever edited.

SUPPORTED glTF 2.0 SUBSET
    Static triangle meshes (mode 4), several primitives and materials,
    node transforms (BAKED into the mesh), POSITION, NORMAL, TEXCOORD_0,
    COLOR_0, indexed or non-indexed, baseColorFactor, baseColorTexture (PNG),
    alphaMode OPAQUE/MASK/BLEND with alphaCutoff, and doubleSided.
    KHR_materials_unlit is accepted and honoured per material.

    REJECTED, with a message rather than a wrong picture: skinning, morph
    targets, animation tracks, non-triangle modes, sparse accessors, normal /
    occlusion / emissive / metallicRoughness textures, JPEG textures,
    interlaced PNG, and any extension not named above. metallicFactor and
    roughnessFactor are ignored - there is no PBR renderer.

    Lighting: a model with NORMALs gets one fixed directional light from the
    camera; one without, or one whose material declares KHR_materials_unlit, is
    drawn unlit from its base colour. Either way the boot screen's own fade is
    applied on top, so a custom logo fades in on the original schedule.

COORDINATE CONTRACT
    1 glTF unit == 1 N64 model unit, for EVERY asset. Axes are glTF's (+Y up,
    +Z toward the viewer, right-handed), which is the space
    tools/export/logo_models.py writes when it exports an original - so a model
    authored against an exported original drops straight in. Node transforms
    are BAKED into the vertices at import; the game applies only the boot
    screen's own camera, rotation and scale-in, and no rotation is ever baked
    into an asset. Front faces are counter-clockwise (glTF's rule) and back
    faces are culled unless the material is doubleSided. UV origin is glTF's -
    (0,0) is the top-left texel - and textures are uploaded in file row order,
    so no flip is applied.

    HOW BIG SHOULD IT BE? Every import prints your model's bounding box next to
    the box the ORIGINAL occupies, and the uniform --scale that would match it.
    The originals are:

        boot.nintendo_logo    3706 x  814 x 498 model units
        boot.rareware_logo     308 x  415 x  29 model units
        boot.goldeneye_logo   2568 x  544 x 134 model units
        boot.legal_page       5201 x 2117 x   0 model units  (flat)

    --scale is applied at import and baked into the installed file. It is never
    applied automatically and the game holds no per-logo factor. When your
    model's aspect ratio differs from the original's, matching width and
    matching height give different numbers and the tool prints both - that is a
    choice only you can make.

    --emit-sample writes a small model already authored to one of those boxes,
    so the whole loop can be walked before you have a model of your own.

LIMITS (enforced identically here and at load)
    vertices 1000000  indices 3000000   primitives 256   materials 256
    textures 16       texture dimension 2048 (larger is halved, not rejected)
    decoded texture bytes 64 MiB        file size 96 MiB

    Sized from a survey of 27 real logo exports (22k-461k vertices, 132k-2.76M
    indices).

    COST, MEASURED 2026-09-05 on the Windows build at 960x720 (SL_PHASE, one
    summary line per boot screen), NOT estimated:

        Nintendo   171984 triangles   0.570 s over 501 frames = 1.14 ms/frame
        Rareware   119644 triangles   0.207 s over 222 frames = 0.93 ms/frame

    Both screens held 59.9 fps with ~90% of each frame idle. For comparison,
    the SAME screens drawing the ORIGINAL logos through the display-list
    interpreter cost 14.52 ms and 4.56 ms per frame for 1018 and 268 triangles.

    So the override path is not merely affordable, it is an order of magnitude
    CHEAPER than the path it bypasses, despite drawing ~170x the geometry. That
    is consistent with the interpreter's cost being CPU-side list walking
    rather than GL submission (B-094). Extrapolating the measured 1.14 ms at
    172k triangles linearly, a 16.7 ms frame budget spent entirely on this path
    would take roughly 2.5M triangles - so the format's 1M vertex / 3M index
    ceiling is the binding limit here, not the frame time.
"""


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        prog="gltf_import.py",
        description="Install a glTF 2.0 model as a Sightline native asset "
                    "override.",
        epilog=EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--asset", choices=sorted(ASSET_IDS),
                    help="logical asset id to install into")
    ap.add_argument("--input", help="path to a .gltf or .glb file")
    ap.add_argument("--remove", choices=sorted(ASSET_IDS),
                    help="delete an installed override")
    ap.add_argument("--where", action="store_true",
                    help="print the override directory and exit")
    ap.add_argument("--list", action="store_true",
                    help="show which overrides are installed")
    ap.add_argument("--output",
                    help="write the converted model here instead of the "
                         "override path")
    ap.add_argument("--emit-sample", metavar="FILE",
                    help="write a small synthetic glTF sized to --asset's "
                         "reference box, then exit. Lets the whole loop be "
                         "walked with no model of your own.")
    ap.add_argument("--scale", type=float, default=1.0,
                    help="uniform scale baked into the model at import "
                         "(default 1.0 - see COORDINATE CONTRACT)")
    ap.add_argument("--repo", action="store_true",
                    help="write the model into the repository "
                         "(data/overrides/) instead of the install "
                         "directory, and REFUSE to write any texture that "
                         "is neither a reference to a game texture nor "
                         "declared in the glTF as the author's own "
                         "(extras.sl_authored with "
                         "extras.sl_authored_provenance)")
    ap.add_argument("--texture-ref", action="append", default=[],
                    metavar="IMAGE=IDENTIFIER",
                    help="declare that an image IS a game texture, when it "
                         "has been re-saved or edited and so is not "
                         "recognised by content. IMAGE is the image's uri "
                         "or file name. May be repeated.")
    ap.add_argument("--game-textures", action="store_true",
                    help="list the game textures a model may reference, "
                         "and exit")
    args = ap.parse_args(argv)

    texture_refs = {}
    for spec in args.texture_ref:
        if "=" not in spec:
            print("error: --texture-ref wants IMAGE=IDENTIFIER, got %r"
                  % spec, file=sys.stderr)
            return 2
        k, v = spec.split("=", 1)
        texture_refs[k.strip()] = v.strip()

    root = override_root()

    if args.where:
        print(root)
        return 0

    if args.game_textures:
        print("game textures a committed model may reference")
        print("(the importer refuses to embed these; the runtime resolves "
              "them from your own game data)")
        for name in sorted(game_textures()):
            e = game_textures()[name]
            print("  %-20s %dx%d  %s" % (name, e["w"], e["h"], e["source"]))
        return 0

    if args.emit_sample:
        emit_sample(Path(args.emit_sample), args.asset)
        return 0

    if args.list:
        # The LADDER, not one directory. Saying "not installed" while a
        # committed model is on screen would be worse than saying nothing:
        # the point of this command is to answer "which file am I actually
        # seeing?", and after data/overrides existed the old answer was
        # wrong for exactly the common case.
        print("install directory: %s" % root)
        print("committed models:  %s"
              % (REPO_ROOT / REPO_OVERRIDE_DIR))
        print("search order: install directory, then committed, then the "
              "original asset")
        print("(SL_ASSET_OVERRIDES=0 turns all of it off and restores the "
              "original logos)")
        print("")
        for a in sorted(ASSET_IDS):
            chain = [("installed", asset_path(a)),
                     ("committed", repo_asset_path(a))]
            active = None
            for kind, q in chain:
                if q.is_file():
                    active = (kind, q)
                    break
            if active is None:
                print("  %-22s the original (nothing overrides it)" % a)
            else:
                kind, q = active
                print("  %-22s %s, %d bytes" % (a, kind, q.stat().st_size))
                print("  %-22s   %s" % ("", q))
            for kind, q in chain:
                if active is not None and q == active[1]:
                    continue
                if q.is_file():
                    print("  %-22s   (%s copy present but shadowed: %s)"
                          % ("", kind, q))
        return 0

    if args.remove:
        p = asset_path(args.remove)
        if p.is_file():
            p.unlink()
            print("removed %s (%s)" % (args.remove, p))
            # SAY WHAT ACTUALLY HAPPENS NEXT. This used to promise the
            # original unconditionally, which stopped being true the moment
            # models shipped in the repository: removing your own copy
            # uncovers the committed one, it does not uncover the original.
            q = repo_asset_path(args.remove)
            if q.is_file():
                print("the COMMITTED model renders next (%s)." % q)
                print("for the ORIGINAL logo, set SL_ASSET_OVERRIDES=0.")
            else:
                print("the original renders again on the next launch; "
                      "no rebuild needed")
        else:
            print("%s is not installed (nothing at %s)" % (args.remove, p))
            q = repo_asset_path(args.remove)
            if q.is_file():
                print("a COMMITTED model is in use for it (%s);" % q)
                print("that one is repository content - do not delete it. "
                      "For the original logo, set SL_ASSET_OVERRIDES=0.")
        return 0

    if not args.asset or not args.input:
        ap.print_help()
        return 2

    src = Path(args.input)
    if not src.is_file():
        print("error: no such file: %s" % src, file=sys.stderr)
        return 1

    print("importing %s" % src)
    try:
        blob = convert(Gltf(src), scale=args.scale, asset=args.asset,
                       texture_refs=texture_refs, repo_safe=args.repo)
    except ImportError_ as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    except (KeyError, IndexError, ValueError, struct.error) as e:
        print("error: the file could not be read as glTF 2.0 (%s: %s)"
              % (type(e).__name__, e), file=sys.stderr)
        return 1

    if args.output:
        out = Path(args.output)
    elif args.repo:
        out = repo_asset_path(args.asset)
    else:
        out = asset_path(args.asset)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    print("installed %s -> %s (%d bytes)" % (args.asset, out, len(blob)))
    if args.repo:
        print("this is a COMMITTED model. A model installed with "
              "--asset/--input still wins over it.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
