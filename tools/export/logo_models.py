#!/usr/bin/env python3
"""Export the boot-sequence Nintendo and Rareware logo models to glTF 2.0.

INSPECTION TOOLING. This tool reads only; it changes no game behaviour and
writes nothing into the repository.

WHAT IT PRODUCES, per logo:
    <logo>/<logo>.gltf          glTF 2.0 (+ .bin)
    <logo>/textures/*.png       decoded model textures, lossless
    <logo>/<logo>.metadata.json source facts, separated from glTF approximations
    <logo>/<logo>.raw.json      exact source integers (coords, cn[], s/t, indices)
    <logo>/<logo>.obj/.mtl      convenience copy only - OBJ cannot carry this material
    <logo>/README.txt

WHERE THE DATA COMES FROM (both static; no runtime probe, no emulator):

  Rareware - assets/rarewarelogo.c is committed SOURCE, and the same bytes are
    linked into the ROM as the `rarewarelogo` segment (ge007.ld:129, VMA
    0x02000000). We read the segment image from the ROM because one binary
    display-list walker then serves both logos; the C source is used as the
    cross-check oracle (see --verify output).

  Nintendo - there is NO source-level copy. The model is the prop resource
    PnintendologoZ, stored 1172-compressed in the ROM (assets/obseg/ob_seg.s
    incbin, resource table assets/obseg/file_resource_table.inc.c:469). It is
    inflated here and parsed with the repository's own model-format parser,
    scripts/generate_prop_model_c.py.

  ROM file offsets for both come from the committed scripts/filelist.u.csv, so
  nothing about a particular developer's machine is baked in.

FORMAT AUTHORITIES (project rule 7 - docs before decomp):
  ucode05.txt / ucode05_old.txt   command set, F5 settile, FC setcombine,
                                  F2 settilesize, tmem units
  "Absolute Basic Rendering Models.txt"  B1 GE_TRI4 nibble packing
  include/PR/gbi.h (plain-F3D branch this build compiles)  operand packing
  docs/doc-routing.json 'display-lists' learned notes B-021 (04 vertex v0
  field), B-046 (boot logos: G_LIGHTING|G_TEXTURE_GEN set, so Vtx cn[] are
  NORMALS not colours), B-048 (F5 shift fields), B-077 (tmem unit), B-088 (F2
  is the CLAMP RECTANGLE, not the image extent - a repeating axis wraps at
  2^mask and the whole of that span is sampled).

Usage:
    python tools/export/logo_models.py [--logo nintendo|rareware|all]
                                       [--output DIR] [--rom PATH]
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import struct
import sys
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

# ---------------------------------------------------------------------------
# Source locations. Every one of these is a citation, not a guess.
# ---------------------------------------------------------------------------

FILELIST_CSV = REPO_ROOT / "scripts" / "filelist.u.csv"

# assets/obseg/prop/PnintendologoZ.bin - 1172-compressed prop resource.
NINTENDO_FILELIST_KEY = "assets/obseg/prop/PnintendologoZ.bin"
# assets/rarewarelogo.bin - the uncompressed `rarewarelogo` segment (ge007.ld:129).
RAREWARE_FILELIST_KEY = "assets/rarewarelogo.bin"

# Model-file pointers are relative to this base (scripts/generate_prop_model_c.py).
PROP_BASE_ADDRESS = 0x05000000
# The rareware segment's VMA, from ge007.ld:129 BEGIN_SEG(rarewarelogo, ..., 0x02000000).
RAREWARE_SEG_VMA = 0x02000000

# assets/obseg/prop/nintendologo/ModelFileHeader.inc.c:5
#   MODELFILEHEADER(nintendologo, 0, &SKELETON(standard_object), 0, 0, 1,
#                   1868.335, 0, 1)
#   -> numswitches = 0, nummatrices = 1, boundingradius = 1868.335, numtextures = 1
NINTENDO_NUM_SWITCHES = 0
NINTENDO_NUM_TEXTURES = 1
# assets/obseg/prop/nintendologo/propFileRecord.inc.c:5 - PROPFILERECORD(nintendologo, 0.1)
NINTENDO_PROP_SCALE = 0.1

# Rareware entry display lists, in the order src/game/title.c:load_display_rare_logo
# submits them. Offsets are the symbol names' own encoding of the segment offset
# (D_020043E8 -> 0x43E8, D_02004758 -> 0x4758); DL_RAREWARETEXT carries no offset
# in its name and is derived by walking D_020043E8 to its G_ENDDL, which is
# asserted below to land exactly on it.
RAREWARE_DL_020043E8 = 0x43E8
RAREWARE_DL_02004758 = 0x4758

# Textures title.c binds to G_TX_RENDERTILE *before* entering each list. Both are
# gDPLoadTextureBlock(..., G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0,
#                     G_TX_NOMIRROR|G_TX_WRAP, G_TX_NOMIRROR|G_TX_WRAP, 5, 5, ...)
# at src/game/title.c - so 32x32 RGBA16 wrapping at 2^5 on both axes.
RAREWARE_ENVMAP_A = 0x4FE8  # &D_02004FE8, bound for D_020043E8 and DL_RAREWARETEXT
RAREWARE_ENVMAP_B = 0x5FF0  # &D_02005FF0, bound for D_02004758

# gDPSetPrimColor values title.c sets around those lists. `fade` is the boot
# fade-in ramp (0..255); the export uses its final, fully-faded value.
RAREWARE_PRIM_GROUP1 = (255, 255, 255, 255)          # (fade, fade, fade, 0xFF)
RAREWARE_PRIM_GROUP2 = (240, 208, 240, 255)          # (fade*0xF0/255, fade*0xD0/255, fade*0xF0/255, 0xFF)

# ---------------------------------------------------------------------------
# Generic prop-resource targets
#
# Every entry here is the SAME KIND OF ASSET as the Nintendo logo above: a
# 1172-compressed prop resource in the ROM, parsed with the repository's own
# scripts/generate_prop_model_c.py and walked with the display-list walker in
# this file. Adding a target is a table entry, not new code.
#
# `num_textures` and `num_switches` come from the committed MODELFILEHEADER;
# nothing here is measured from a running game or baked to a machine.
# `components` names the mesh groups the export separates the model into. The
# SPLIT is source data - it is one group per distinct bound texture image, read
# straight out of the display list. The LABELS are DERIVED descriptions and are
# marked as such in the metadata; they never affect geometry.
# ---------------------------------------------------------------------------

PROP_TARGETS = {
    "goldeneyelogo": {
        "filelist_key": "assets/obseg/prop/PgoldeneyelogoZ.bin",
        "num_textures": 2,
        "num_switches": 0,
        "resource": "PgoldeneyelogoZ (prop resource GOLDENEYELOGO)",
        "model_file_header":
            "MODELFILEHEADER(goldeneyelogo, rootnode=0, "
            "skeleton=standard_object, switches=0, numswitches=0, "
            "nummatrices=1, boundingradius=1287.1866, numrecords=0, "
            "numtextures=2)",
        "prop_file_record": "PROPFILERECORD(goldeneyelogo, scale 0.1)",
        "lookat_basis":
            "YES - and this is what makes it different from the Nintendo logo. "
            "constructor_menu04_goldeneyelogo allocates two lights as a LookAt "
            "(dynAllocateLights(2)), fills them with "
            "guLookAtReflect(eye 0,0,4000 -> at 0,0,0, up 0,1,0) at "
            "src/game/front.c:2039, and uploads them with gSPLookAt at "
            "front.c:2047. So the reflection basis this model's texgen runs "
            "against is the screen's OWN, not whatever the previous screen "
            "left resident. Commit d855a50c is the related runtime repair for "
            "zero-length normals in this same texgen path. The DERIVED UVs in "
            "the glTF do NOT model this basis - they are preview only.",
        "source_files": [
            "assets/obseg/file_resource_table.inc.c:419 "
            "{GOLDENEYELOGO, \"PgoldeneyelogoZ\", &PgoldeneyelogoZ}",
            "assets/obseg/prop/goldeneyelogo/ModelFileHeader.inc.c:5",
            "assets/obseg/prop/goldeneyelogo/propFileRecord.inc.c:5",
            "src/bondconstants.h PROP_GOLDENEYELOGO",
            "src/game/front.c:1957 init_menu04_goldeneyelogo",
            "src/game/front.c:2024 constructor_menu04_goldeneyelogo",
            "src/game/front.c:1978 update_menu04_goldeneye",
            "src/game/front.c:320 Lights1 gelogolight",
        ],
        "components": {
            0x0068: "lettering",
            0x0B28: "flatshaded_slab",
        },
        "component_notes": {
            "lettering": "MEASURED: 23 display-list draws, every vertex at "
                         "Z=0 (a flat plane), G_LIGHTING|G_TEXTURE_GEN set, "
                         "sampling the 32x32 RGBA16 reflectance map at file "
                         "offset 0x0068. Its pieces advance monotonically "
                         "left-to-right across the model's full X extent, and "
                         "the XY silhouette reads as the wordmark letterforms. "
                         "All apparent depth is the environment map plus "
                         "lighting, NOT geometry.",
            "flatshaded_slab":
                         "MEASURED: 6 display-list draws, G_LIGHTING and "
                         "G_TEXTURE_GEN both CLEAR, bound to the 1x1 RGBA16 "
                         "texture at 0x0B28 (a single texel, so effectively "
                         "flat colour). Z varies linearly with X from +67 to "
                         "-67, i.e. the whole group lies on ONE plane rotated "
                         "about the Y axis. Its vertex radii about its own "
                         "centroid are spread evenly from 97 to 378 with no "
                         "annular gap, so this group is NOT a ring. Its XY "
                         "silhouette is a top horizontal bar meeting a "
                         "diagonal stem that descends to the left.",
        },
    },
    "legalpage": {
        "filelist_key": "assets/obseg/prop/PlegalpageZ.bin",
        "num_textures": 5,
        "num_switches": 0,
        "resource": "PlegalpageZ (prop resource LEGALPAGE)",
        "model_file_header":
            "MODELFILEHEADER(legalpage, rootnode=0, "
            "skeleton=standard_object, switches=0, numswitches=0, "
            "nummatrices=1, boundingradius=2711.7573, numrecords=0, "
            "numtextures=5)",
        "prop_file_record": "PROPFILERECORD(legalpage, scale 0.1)",
        "lookat_basis":
            "NOT APPLICABLE. No primitive in this model sets G_TEXTURE_GEN, so "
            "no reflection basis is consulted and none is uploaded. Every UV "
            "here is source data read straight from the Vtx s/t records.",
        "source_files": [
            "assets/obseg/file_resource_table.inc.c:449 "
            "{LEGALPAGE, \"PlegalpageZ\", &PlegalpageZ}",
            "assets/obseg/prop/legalpage/ModelFileHeader.inc.c:5",
            "assets/obseg/prop/legalpage/propFileRecord.inc.c:5",
            "src/bondconstants.h:3513 PROP_LEGALPAGE",
            "src/game/front.c:1415 init_menu00_legalscreen",
            "src/game/front.c:1541 constructor_menu00_legalscreen",
            "src/game/front.c:345 legalpage_text_array (the SEPARATE font text)",
        ],
        "components": {
            None:   "rule_untextured",
            0x0090: "panel_0090",
            0x0898: "panel_0898",
            0x10A0: "panel_10A0",
            0x1468: "panel_1468",
            0x1A70: "panel_1A70",
        },
        "component_notes": {
            "rule_untextured":
                "MEASURED: 2 triangles, combiner names no texel (pure shade). "
                "A thin horizontal bar: X spans -2602..2599, Y only 240..261.",
            "panel_0090": "MEASURED: 2 triangles, one textured quad.",
            "panel_0898": "MEASURED: 2 triangles, one textured quad.",
            "panel_10A0": "MEASURED: 2 triangles, one textured quad.",
            "panel_1468": "MEASURED: 2 triangles, one textured quad.",
            "panel_1A70": "MEASURED: 2 triangles, one textured quad.",
        },
    },
}

# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

G_IM_FMT = {0: "G_IM_FMT_RGBA", 1: "G_IM_FMT_YUV", 2: "G_IM_FMT_CI",
            3: "G_IM_FMT_IA", 4: "G_IM_FMT_I"}
G_IM_SIZ = {0: "G_IM_SIZ_4b", 1: "G_IM_SIZ_8b", 2: "G_IM_SIZ_16b", 3: "G_IM_SIZ_32b"}

GEOMETRY_MODE_BITS = [
    (0x00000001, "G_ZBUFFER"), (0x00000004, "G_SHADE"),
    (0x00000200, "G_SHADING_SMOOTH"), (0x00001000, "G_CULL_FRONT"),
    (0x00002000, "G_CULL_BACK"), (0x00010000, "G_FOG"),
    (0x00020000, "G_LIGHTING"), (0x00040000, "G_TEXTURE_GEN"),
    (0x00080000, "G_TEXTURE_GEN_LINEAR"), (0x00100000, "G_LOD"),
]

# Colour-combiner mux names. ucode05_old.txt "FC rdp_setcombine"; the c mux is
# five bits while a/b/d are four and three respectively.
CC_A = ["COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", "SHADE", "ENVIRONMENT",
        "1", "NOISE"] + ["0"] * 8
CC_B = ["COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", "SHADE", "ENVIRONMENT",
        "CENTER", "K4"] + ["0"] * 8
CC_C = ["COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", "SHADE", "ENVIRONMENT",
        "SCALE", "COMBINED_ALPHA", "TEXEL0_ALPHA", "TEXEL1_ALPHA",
        "PRIMITIVE_ALPHA", "SHADE_ALPHA", "ENV_ALPHA", "LOD_FRACTION",
        "PRIM_LOD_FRAC", "K5"] + ["0"] * 16
CC_D = ["COMBINED", "TEXEL0", "TEXEL1", "PRIMITIVE", "SHADE", "ENVIRONMENT",
        "1", "0"]
AC_ABD = ["COMBINED_ALPHA", "TEXEL0_ALPHA", "TEXEL1_ALPHA", "PRIMITIVE_ALPHA",
          "SHADE_ALPHA", "ENV_ALPHA", "1", "0"]
AC_C = ["LOD_FRACTION", "TEXEL0_ALPHA", "TEXEL1_ALPHA", "PRIMITIVE_ALPHA",
        "SHADE_ALPHA", "ENV_ALPHA", "PRIM_LOD_FRAC", "0"]


def decode_combiner(w0: int, w1: int) -> dict:
    """Decode an FC setcombine pair into readable mux slots.

    Packing is include/PR/gbi.h gsDPSetCombine / GCCc0w0 .. GCCc1w1, the
    plain-F3D branch this build compiles.
    """
    a0 = (w0 >> 20) & 0x0F
    c0 = (w0 >> 15) & 0x1F
    Aa0 = (w0 >> 12) & 0x07
    Ac0 = (w0 >> 9) & 0x07
    a1 = (w0 >> 5) & 0x0F
    c1 = (w0 >> 0) & 0x1F
    b0 = (w1 >> 28) & 0x0F
    b1 = (w1 >> 24) & 0x0F
    Aa1 = (w1 >> 21) & 0x07
    Ac1 = (w1 >> 18) & 0x07
    d0 = (w1 >> 15) & 0x07
    Ab0 = (w1 >> 12) & 0x07
    Ad0 = (w1 >> 9) & 0x07
    d1 = (w1 >> 6) & 0x07
    Ab1 = (w1 >> 3) & 0x07
    Ad1 = (w1 >> 0) & 0x07

    def rgb(a, b, c, d):
        return "(%s - %s) * %s + %s" % (CC_A[a], CC_B[b], CC_C[c], CC_D[d])

    def alpha(a, b, c, d):
        return "(%s - %s) * %s + %s" % (AC_ABD[a], AC_ABD[b], AC_C[c], AC_ABD[d])

    return {
        "words": "%08X %08X" % (w0, w1),
        "cycle0_rgb": rgb(a0, b0, c0, d0),
        "cycle0_alpha": alpha(Aa0, Ab0, Ac0, Ad0),
        "cycle1_rgb": rgb(a1, b1, c1, d1),
        "cycle1_alpha": alpha(Aa1, Ab1, Ac1, Ad1),
    }


def geometry_mode_names(mode: int) -> list:
    names = [name for bit, name in GEOMETRY_MODE_BITS if mode & bit]
    known = 0
    for bit, _ in GEOMETRY_MODE_BITS:
        known |= bit
    if mode & ~known:
        names.append("0x%08X (unnamed bits)" % (mode & ~known))
    return names


def inflate_1172(blob: bytes) -> bytes:
    """Rare's '1172' container: a two-byte magic then a raw deflate stream.

    Authority: tools/1172inflate.sh, which prepends a stock gzip header to
    everything after the first two bytes and hands it to gzip.
    """
    if blob[:2] != b"\x11\x72":
        raise ValueError("not a 1172 stream (magic %s)" % blob[:2].hex())
    return zlib.decompressobj(-15).decompress(blob[2:])


def read_filelist_entry(key: str) -> tuple:
    """(rom_offset, length) for a path in the committed scripts/filelist.u.csv."""
    with FILELIST_CSV.open("r", encoding="utf-8") as handle:
        for line in handle:
            parts = line.strip().split(",")
            if len(parts) >= 3 and parts[2] == key:
                return int(parts[0]), int(parts[1])
    raise KeyError("%s not listed in %s" % (key, FILELIST_CSV))


# ---------------------------------------------------------------------------
# Minimal PNG writer (stdlib zlib only - no image dependency)
# ---------------------------------------------------------------------------

def write_png(path: Path, width: int, height: int, mode: str, rows: list) -> None:
    """Write an 8-bit PNG. `mode` is 'L' (grey) or 'RGBA'; rows are bytes."""
    colour_type = {"L": 0, "RGBA": 6}[mode]
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", header)
                     + chunk(b"IDAT", zlib.compress(raw, 9))
                     + chunk(b"IEND", b""))


def decode_rgba16(data: bytes, width: int, height: int) -> list:
    """N64 RGBA16 (5/5/5/1) -> 8-bit RGBA rows. 5-bit channels expand as
    (x << 3) | (x >> 2), the standard exact-endpoint expansion."""
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            (value,) = struct.unpack_from(">H", data, (y * width + x) * 2)
            r = (value >> 11) & 0x1F
            g = (value >> 6) & 0x1F
            b = (value >> 1) & 0x1F
            a = value & 1
            row += bytes(((r << 3) | (r >> 2), (g << 3) | (g >> 2),
                          (b << 3) | (b >> 2), 255 if a else 0))
        rows.append(row)
    return rows


def decode_i8(data: bytes, width: int, height: int) -> list:
    """N64 I8 -> 8-bit greyscale rows. The RDP replicates I to R, G, B and A;
    the PNG keeps the single channel, which is the lossless form."""
    return [bytearray(data[y * width:(y + 1) * width]) for y in range(height)]


def decode_i4(data: bytes, width: int, height: int) -> list:
    """N64 I4 -> 8-bit greyscale rows.

    Two texels per byte, high nibble first. The 4-bit value is promoted to 8
    bits by nibble replication (v * 0x11), which is the RDP's own widening and
    is exactly invertible, so the PNG stays lossless with respect to the source.
    Rows are byte-aligned at width/2; every I4 texture in these props has an
    even width, asserted by the caller.
    """
    stride = width // 2
    rows = []
    for y in range(height):
        line = data[y * stride:(y + 1) * stride]
        row = bytearray(width)
        for x in range(width):
            byte = line[x >> 1]
            value = (byte >> 4) if (x & 1) == 0 else (byte & 0x0F)
            row[x] = value * 0x11
        rows.append(row)
    return rows


# ---------------------------------------------------------------------------
# Display-list walker (ucode05 / plain F3D as this build compiles it)
# ---------------------------------------------------------------------------

class Tile:
    __slots__ = ("fmt", "siz", "line", "tmem", "palette",
                 "cmt", "maskt", "shiftt", "cms", "masks", "shifts",
                 "uls", "ult", "lrs", "lrt", "image")

    def __init__(self):
        for name in self.__slots__:
            setattr(self, name, 0)
        self.image = None

    def snapshot(self) -> dict:
        return {name: getattr(self, name) for name in self.__slots__}


class Draw:
    """One run of triangles sharing a material state."""

    def __init__(self, state: dict, source: str):
        self.state = state
        self.source = source
        self.tris = []
        self.verts = {}   # batch-slot -> source vertex record
        self.records = []


class Walker:
    """Walks a binary display list and records triangles with their state.

    Only the commands these two logos actually issue are decoded; anything else
    is counted so that an unexpected command announces itself rather than being
    silently skipped.
    """

    def __init__(self, data: bytes, base_vma: int, resolve_vertex=None):
        self.data = data
        self.base_vma = base_vma
        self.resolve_vertex = resolve_vertex or self._default_resolve
        self.unknown = {}

    def _default_resolve(self, addr: int) -> int:
        return addr - self.base_vma

    # -- vertex record ------------------------------------------------------
    def read_vertex(self, offset: int) -> dict:
        x, y, z, flag, s, t, cn0, cn1, cn2, cn3 = struct.unpack_from(
            ">hhhHhhBBBB", self.data, offset)
        return {"x": x, "y": y, "z": z, "flag": flag, "s": s, "t": t,
                "cn": [cn0, cn1, cn2, cn3]}

    def walk(self, offset: int, state: dict, source: str, out: list,
             depth: int = 0) -> int:
        """Execute from `offset` until G_ENDDL. Returns the offset just past it."""
        if depth > 8:
            return offset
        batch = {}
        draw = None
        while True:
            w0, w1 = struct.unpack_from(">II", self.data, offset)
            op = w0 >> 24
            offset += 8

            if op == 0xB8:                                    # G_ENDDL
                break

            elif op == 0x04:                                  # G_VTX
                # B-021: bits 20-23 are n-1 and bits 16-19 are v0. gbi.h:1865
                # gSPVertex -> gDma1p(G_VTX, v, sizeof(Vtx)*n, ((n)-1)<<4|(v0)).
                count = ((w0 >> 20) & 0x0F) + 1
                v0 = (w0 >> 16) & 0x0F
                addr = self.resolve_vertex(w1)
                for i in range(count):
                    batch[v0 + i] = self.read_vertex(addr + 16 * i)
                draw = None

            elif op == 0xB1:                                  # GE_TRI4
                # "Absolute Basic Rendering Models.txt", B1 4-triangle draw:
                # the four z indices are nibbles 0..3 of w0, the x/y pairs are
                # nibbles 0..7 of w1. An all-zero slot is not drawn.
                for i in range(4):
                    x = (w1 >> (8 * i)) & 0x0F
                    y = (w1 >> (8 * i + 4)) & 0x0F
                    z = (w0 >> (4 * i)) & 0x0F
                    if (x | y | z) == 0:
                        continue
                    draw = self._emit(draw, out, state, source, batch, x, y, z)

            elif op == 0xBF:                                  # G_TRI1
                a = ((w1 >> 16) & 0xFF) // 0x0A
                b = ((w1 >> 8) & 0xFF) // 0x0A
                c = (w1 & 0xFF) // 0x0A
                draw = self._emit(draw, out, state, source, batch, a, b, c)

            elif op == 0x06:                                  # G_DL
                self.walk(self.resolve_vertex(w1), state, source, out, depth + 1)
                draw = None

            elif op in (0xB6, 0xB7):                          # clear/set geom mode
                if op == 0xB7:
                    state["geometry_mode"] |= w1
                else:
                    state["geometry_mode"] &= ~w1 & 0xFFFFFFFF
                draw = None

            elif op == 0xFC:                                  # setcombine
                state["combiner"] = decode_combiner(w0 & 0x00FFFFFF, w1)
                draw = None

            elif op == 0xFA:                                  # setprimcolor
                state["prim_color"] = [(w1 >> 24) & 0xFF, (w1 >> 16) & 0xFF,
                                       (w1 >> 8) & 0xFF, w1 & 0xFF]
                draw = None

            elif op == 0xFB:                                  # setenvcolor
                state["env_color"] = [(w1 >> 24) & 0xFF, (w1 >> 16) & 0xFF,
                                      (w1 >> 8) & 0xFF, w1 & 0xFF]
                draw = None

            elif op == 0xFD:                                  # settextureimage
                state["timg"] = {
                    "fmt": (w0 >> 21) & 0x07, "siz": (w0 >> 19) & 0x03,
                    "width": (w0 & 0x0FFF) + 1, "addr": w1,
                    "offset": self.resolve_vertex(w1),
                }
                draw = None

            elif op == 0xF5:                                  # settile
                tile = state["tiles"][(w1 >> 24) & 0x07]
                tile.fmt = (w0 >> 21) & 0x07
                tile.siz = (w0 >> 19) & 0x03
                tile.line = (w0 >> 9) & 0x1FF
                tile.tmem = w0 & 0x1FF
                tile.palette = (w1 >> 20) & 0x0F
                tile.cmt = (w1 >> 18) & 0x03
                tile.maskt = (w1 >> 14) & 0x0F
                tile.shiftt = (w1 >> 10) & 0x0F
                tile.cms = (w1 >> 8) & 0x03
                tile.masks = (w1 >> 4) & 0x0F
                tile.shifts = w1 & 0x0F
                draw = None

            elif op == 0xF2:                                  # settilesize
                tile = state["tiles"][(w1 >> 24) & 0x07]
                tile.uls = (w0 >> 12) & 0xFFF
                tile.ult = w0 & 0xFFF
                tile.lrs = (w1 >> 12) & 0xFFF
                tile.lrt = w1 & 0xFFF
                draw = None

            elif op == 0xF3:                                  # loadblock
                tile = state["tiles"][(w1 >> 24) & 0x07]
                tile.image = state.get("timg")
                state["last_load"] = {
                    "tile": (w1 >> 24) & 0x07, "lrs": (w1 >> 12) & 0xFFF,
                    "dxt": w1 & 0xFFF, "timg": state.get("timg"),
                }
                # The render tile inherits the image the load tile just fetched.
                state["tiles"][0].image = state.get("timg")
                draw = None

            elif op == 0xBB:                                  # gSPTexture
                state["texture"] = {
                    "on": bool(w0 & 0xFF), "level": (w0 >> 11) & 0x07,
                    "tile": (w0 >> 8) & 0x07,
                    "scale_s": (w1 >> 16) & 0xFFFF, "scale_t": w1 & 0xFFFF,
                }
                draw = None

            elif op == 0xB9:                                  # setothermode_l
                # B-056: sft==3 && len==29 is a complete gDPSetRenderMode word.
                if ((w0 >> 8) & 0xFF) == 3 and (w0 & 0xFF) == 29:
                    state["render_mode"] = "%08X" % w1
                draw = None

            elif op == 0xBA:                                  # setothermode_h
                shift = (w0 >> 8) & 0xFF
                if shift == 20:                               # G_MDSFT_CYCLETYPE
                    state["cycle_type"] = ["1CYCLE", "2CYCLE", "COPY", "FILL"][
                        (w1 >> 20) & 3]
                state.setdefault("othermode_h", []).append("%08X %08X" % (w0, w1))
                draw = None

            elif op in (0x01, 0xE6, 0xE7, 0xE8, 0xE9, 0xBC, 0xBD, 0xE4, 0xF6):
                draw = None                                   # not material state

            else:
                self.unknown["%02X" % op] = self.unknown.get("%02X" % op, 0) + 1
                draw = None

        return offset

    def _emit(self, draw, out, state, source, batch, a, b, c):
        if draw is None:
            draw = Draw(snapshot_state(state), source)
            out.append(draw)
        for slot in (a, b, c):
            if slot not in draw.verts:
                draw.verts[slot] = batch.get(slot)
        draw.tris.append((a, b, c))
        return draw


def seed_render_tile(state: dict, offset: int, vma: int) -> None:
    """Reproduce the gDPLoadTextureBlock src/game/title.c issues before it enters
    each Rareware list:

        gDPLoadTextureBlock(gdl++, &D_020xxxxx, G_IM_FMT_RGBA, G_IM_SIZ_16b,
                            32, 32, 0, G_TX_NOMIRROR|G_TX_WRAP,
                            G_TX_NOMIRROR|G_TX_WRAP, 5, 5,
                            G_TX_NOLOD, G_TX_NOLOD)

    That state is established OUTSIDE the segment's own display lists, so a
    walker that started cold would report an unset tile and miss the fact that
    both axes wrap at 2^5 (B-088).
    """
    tile = state["tiles"][0]
    tile.fmt = 0            # G_IM_FMT_RGBA
    tile.siz = 2            # G_IM_SIZ_16b
    tile.line = 32 * 2 // 8  # 8 - 64 bytes per row, i.e. 32 texels at 16bpp
    tile.tmem = 0
    tile.cms = 0            # G_TX_NOMIRROR | G_TX_WRAP
    tile.masks = 5
    tile.shifts = 0
    tile.cmt = 0
    tile.maskt = 5
    tile.shiftt = 0
    tile.uls = tile.ult = 0
    tile.lrs = tile.lrt = (32 - 1) * 4
    tile.image = {"offset": offset, "fmt": 0, "siz": 2, "width": 32,
                  "addr": vma + offset}


def combiner_names_a_texel(combiner: dict) -> bool:
    if not combiner:
        return False
    text = " ".join(str(v) for k, v in combiner.items() if k != "words")
    return "TEXEL0" in text or "TEXEL1" in text


def new_state() -> dict:
    return {
        "geometry_mode": 0,
        "combiner": None,
        "prim_color": None,
        "env_color": None,
        "timg": None,
        "tiles": [Tile() for _ in range(8)],
        "texture": None,
        "render_mode": None,
        "cycle_type": None,
    }


def snapshot_state(state: dict) -> dict:
    render_tile = state["texture"]["tile"] if state.get("texture") else 0
    tile = state["tiles"][render_tile]
    return {
        "geometry_mode": state["geometry_mode"],
        "geometry_mode_names": geometry_mode_names(state["geometry_mode"]),
        "lighting": bool(state["geometry_mode"] & 0x20000),
        "texture_gen": bool(state["geometry_mode"] & 0x40000),
        "combiner": state["combiner"],
        "prim_color": state["prim_color"],
        "env_color": state["env_color"],
        "render_mode": state["render_mode"],
        "cycle_type": state["cycle_type"],
        "texture": dict(state["texture"]) if state.get("texture") else None,
        "render_tile": render_tile,
        "tile": tile.snapshot(),
        "tile_image": dict(tile.image) if tile.image else None,
    }


def state_key(snap: dict) -> str:
    tile = snap["tile"]
    return json.dumps([
        snap["geometry_mode"], snap["combiner"], snap["prim_color"],
        snap["env_color"], snap["render_mode"], snap["cycle_type"],
        snap["texture"], tile["fmt"], tile["siz"], tile["line"], tile["tmem"],
        tile["cms"], tile["cmt"], tile["masks"], tile["maskt"],
        tile["uls"], tile["ult"], tile["lrs"], tile["lrt"],
        (snap["tile_image"] or {}).get("offset"),
    ], sort_keys=True)


# ---------------------------------------------------------------------------
# glTF assembly
# ---------------------------------------------------------------------------

DERIVED_UV_NOTE = (
    "DERIVED, NOT SOURCE DATA. This primitive sets G_TEXTURE_GEN, so the RSP "
    "generates texture coordinates from the vertex NORMAL and ignores whatever "
    "s/t the Vtx records carry (those raw s/t bytes are still preserved "
    "verbatim in the .raw.json). These UVs approximate the generated mapping as "
    "u = 0.5 + nx/2, v = 0.5 - ny/2 from the object-space normal, which assumes "
    "an IDENTITY LookAt basis. On the cartridge the basis is whatever the "
    "previous screen left resident; Sightline seeds an identity basis as a "
    "compatibility repair. The approximation is for preview only."
)


COLOUR_SUMMARY = {
    "nintendo":
        "The environment-mapped TEXTURE, addressed by the VERTEX NORMAL through "
        "G_TEXTURE_GEN, modulated by the SHADE that G_LIGHTING computes from "
        "those same normals under the screen's single ramped white light. "
        "Vertex RGB contributes nothing - cn[] is normals here, not colour. No "
        "environment colour is set; no primitive colour is set by the model.",
    "rareware":
        "The reflection-mapped 32x32 RGBA16 environment TEXTURE, addressed by "
        "the VERTEX NORMAL through G_TEXTURE_GEN, multiplied by the PRIMITIVE "
        "COLOUR that src/game/title.c sets. SHADE is not in the combiner at "
        "all, so the lights only matter because lighting is what feeds texture "
        "generation. Vertex colour contributes nothing. The four lettering "
        "quads instead sample their own mipmapped textures with explicit UVs, "
        "and are likewise multiplied by the primitive colour.",
    "goldeneyelogo":
        "The `lettering` mesh: the 32x32 RGBA16 reflectance map at file offset "
        "0x0068, addressed by the VERTEX NORMAL through G_TEXTURE_GEN, "
        "modulated by the SHADE that G_LIGHTING computes from those same "
        "normals under gelogolight (src/game/front.c:320). The model sets no "
        "primitive and no environment colour. The geometry is entirely flat "
        "(Z=0 throughout), so all apparent relief comes from that map and the "
        "normals rather than from shape. The `flatshaded_slab` mesh clears both "
        "G_LIGHTING and G_TEXTURE_GEN, so its cn[] bytes are vertex COLOUR and "
        "its bound texture is a single 1x1 texel. This screen uploads its own "
        "reflection basis via guLookAtReflect/gSPLookAt, unlike the Nintendo "
        "logo which rides whatever basis is resident.",
    "legalpage":
        "Flat, unlit, textured quads. No primitive sets G_LIGHTING or "
        "G_TEXTURE_GEN, so the lights do not participate: each quad samples its "
        "own texture with explicit source UVs, modulated by vertex shade. One "
        "mesh names no texel in its combiner and draws pure shade. The black "
        "behind the page is NOT in this model - it is the runtime "
        "clear_framebuffer_black fill, and the twelve lines of wording are "
        "font-rendered at runtime from the language bank.",
}


def colour_summary(name: str) -> str:
    return COLOUR_SUMMARY[name]


class GltfBuilder:
    def __init__(self, name: str):
        self.name = name
        self.blob = bytearray()
        self.buffer_views = []
        self.accessors = []
        self.meshes = []
        self.nodes = []
        self.materials = []
        self.textures = []
        self.images = []
        self.samplers = []

    def _view(self, payload: bytes, target: int = None) -> int:
        while len(self.blob) % 4:
            self.blob.append(0)
        offset = len(self.blob)
        self.blob += payload
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
        if target:
            view["target"] = target
        self.buffer_views.append(view)
        return len(self.buffer_views) - 1

    def add_floats(self, values: list, kind: str) -> int:
        components = {"VEC2": 2, "VEC3": 3, "VEC4": 4}[kind]
        flat = [c for v in values for c in v]
        view = self._view(struct.pack("<%df" % len(flat), *flat), 34962)
        mins = [min(v[i] for v in values) for i in range(components)]
        maxs = [max(v[i] for v in values) for i in range(components)]
        self.accessors.append({
            "bufferView": view, "componentType": 5126, "count": len(values),
            "type": kind, "min": mins, "max": maxs,
        })
        return len(self.accessors) - 1

    def add_indices(self, indices: list) -> int:
        view = self._view(struct.pack("<%dI" % len(indices), *indices), 34963)
        self.accessors.append({
            "bufferView": view, "componentType": 5125, "count": len(indices),
            "type": "SCALAR", "min": [min(indices)], "max": [max(indices)],
        })
        return len(self.accessors) - 1

    def add_image(self, uri: str) -> int:
        self.images.append({"uri": uri})
        if not self.samplers:
            # G_TX_WRAP on both axes for every tile in both logos.
            self.samplers.append({"wrapS": 10497, "wrapT": 10497})
        self.textures.append({"sampler": 0, "source": len(self.images) - 1})
        return len(self.textures) - 1

    def add_material(self, name: str, texture_index, base_colour, extras) -> int:
        pbr = {
            "baseColorFactor": base_colour,
            "metallicFactor": 0.0,
            "roughnessFactor": 0.6,
        }
        if texture_index is not None:
            pbr["baseColorTexture"] = {"index": texture_index}
        self.materials.append({
            "name": name, "pbrMetallicRoughness": pbr,
            "doubleSided": False, "extras": extras,
        })
        return len(self.materials) - 1

    def write(self, path: Path, extras: dict) -> None:
        bin_name = path.stem + ".bin"
        (path.parent / bin_name).write_bytes(bytes(self.blob))
        doc = {
            "asset": {
                "version": "2.0",
                "generator": "sightline tools/export/logo_models.py",
                "extras": extras,
            },
            "scene": 0,
            "scenes": [{"name": self.name, "nodes": list(range(len(self.nodes)))}],
            "nodes": self.nodes,
            "meshes": self.meshes,
            "materials": self.materials,
            "accessors": self.accessors,
            "bufferViews": self.buffer_views,
            "buffers": [{"uri": bin_name, "byteLength": len(self.blob)}],
        }
        if self.images:
            doc["images"] = self.images
            doc["textures"] = self.textures
            doc["samplers"] = self.samplers
        path.write_text(json.dumps(doc, indent=1), encoding="utf-8")


def build_primitive(builder, draws, material_index, extras_base,
                    treat_cn_as_colour, has_explicit_uv, tex_dims):
    """Turn a set of same-material draws from one source list into a primitive."""
    positions, normals, colours, uvs, indices = [], [], [], [], []
    lookup = {}
    zero_normals = 0
    for draw in draws:
        for tri in draw.tris:
            for slot in tri:
                record = draw.verts.get(slot)
                if record is None:
                    continue
                key = (record["x"], record["y"], record["z"],
                       record["s"], record["t"], tuple(record["cn"]))
                if key not in lookup:
                    lookup[key] = len(positions)
                    positions.append((float(record["x"]), float(record["y"]),
                                      float(record["z"])))
                    cn = record["cn"]
                    if treat_cn_as_colour:
                        colours.append((cn[0] / 255.0, cn[1] / 255.0,
                                        cn[2] / 255.0, cn[3] / 255.0))
                        normals.append((0.0, 0.0, 1.0))
                    else:
                        nx = struct.unpack("b", bytes([cn[0]]))[0] / 127.0
                        ny = struct.unpack("b", bytes([cn[1]]))[0] / 127.0
                        nz = struct.unpack("b", bytes([cn[2]]))[0] / 127.0
                        length = (nx * nx + ny * ny + nz * nz) ** 0.5
                        if length < 1e-6:
                            zero_normals += 1
                            nx, ny, nz = 0.0, 0.0, 1.0
                        else:
                            nx, ny, nz = nx / length, ny / length, nz / length
                        normals.append((nx, ny, nz))
                    if has_explicit_uv:
                        # Vtx s/t are S10.5 texel coordinates.
                        uvs.append((record["s"] / 32.0 / tex_dims[0],
                                    record["t"] / 32.0 / tex_dims[1]))
                    else:
                        n = normals[-1]
                        uvs.append((0.5 + n[0] / 2.0, 0.5 - n[1] / 2.0))
                indices.append(lookup[key])

    if not indices:
        return None, 0, 0, 0

    attributes = {
        "POSITION": builder.add_floats(positions, "VEC3"),
        "TEXCOORD_0": builder.add_floats(uvs, "VEC2"),
    }
    if treat_cn_as_colour:
        attributes["COLOR_0"] = builder.add_floats(colours, "VEC4")
    else:
        attributes["NORMAL"] = builder.add_floats(normals, "VEC3")

    extras = dict(extras_base)
    if zero_normals:
        extras["zero_length_normals_substituted"] = zero_normals
        extras["zero_normal_note"] = (
            "DERIVED substitution: %d source normals are all-zero and cannot be "
            "normalised; glTF requires unit normals, so (0,0,1) was written. The "
            "original bytes are preserved verbatim in the .raw.json." % zero_normals)

    primitive = {"attributes": attributes,
                 "indices": builder.add_indices(indices),
                 "material": material_index, "mode": 4, "extras": extras}
    return primitive, len(positions), len(indices) // 3, zero_normals


# ---------------------------------------------------------------------------
# Rareware
# ---------------------------------------------------------------------------

def export_rareware(rom: bytes, out_dir: Path) -> dict:
    offset, length = read_filelist_entry(RAREWARE_FILELIST_KEY)
    seg = rom[offset:offset + length]

    walker = Walker(seg, RAREWARE_SEG_VMA)
    draws = []
    notes = []

    # --- D_020043E8, then DL_RAREWARETEXT which directly follows it ---------
    state = new_state()
    seed_render_tile(state, RAREWARE_ENVMAP_A, RAREWARE_SEG_VMA)
    state["prim_color"] = list(RAREWARE_PRIM_GROUP1)
    state["texture"] = {"on": True, "level": 0, "tile": 0,
                        "scale_s": 0x0800, "scale_t": 0x0800}
    end = walker.walk(RAREWARE_DL_020043E8, state, "D_020043E8", draws)
    rareware_text_offset = end
    notes.append("DL_RAREWARETEXT derived at segment offset 0x%04X by walking "
                 "D_020043E8 to its G_ENDDL." % rareware_text_offset)
    walker.walk(rareware_text_offset, state, "DL_RAREWARETEXT", draws)

    # --- D_02004758 --------------------------------------------------------
    state2 = new_state()
    seed_render_tile(state2, RAREWARE_ENVMAP_B, RAREWARE_SEG_VMA)
    state2["prim_color"] = list(RAREWARE_PRIM_GROUP2)
    state2["texture"] = {"on": True, "level": 0, "tile": 0,
                         "scale_s": 0x1C81, "scale_t": 0x1426}
    walker.walk(RAREWARE_DL_02004758, state2, "D_02004758", draws)

    # --- textures ----------------------------------------------------------
    tex_dir = out_dir / "textures"
    tex_dir.mkdir(parents=True, exist_ok=True)
    texture_meta = []
    texture_index_by_offset = {}
    builder = GltfBuilder("rareware")

    # Every tile in this logo is RGBA16 32x32 wrapping at 2^5 on both axes.
    # B-088: the F2 settilesize on D_02004758 is a 20x3 CLAMP RECTANGLE; the
    # image the RDP actually consumes is the full 32x32 wrap span, so that is
    # what is exported.
    known = [
        (RAREWARE_ENVMAP_A, "D_02004FE8",
         "environment/reflection map, bound by src/game/title.c before D_020043E8"),
        (RAREWARE_ENVMAP_B, "D_02005FF0",
         "environment/reflection map, bound by src/game/title.c before D_02004758"),
    ]
    for draw in draws:
        image = draw.state.get("tile_image")
        if image and image["offset"] not in [k[0] for k in known]:
            known.append((image["offset"],
                          "seg_0x%04X" % image["offset"],
                          "mipmapped letter texture loaded by DL_RAREWARETEXT"))

    for index, (tex_offset, symbol, role) in enumerate(known):
        rows = decode_rgba16(seg[tex_offset:tex_offset + 32 * 32 * 2], 32, 32)
        filename = "texture_%02d_%s.png" % (index, symbol)
        write_png(tex_dir / filename, 32, 32, "RGBA", rows)
        texture_index_by_offset[tex_offset] = builder.add_image(
            "textures/" + filename)
        texture_meta.append({
            "index": index, "file": "textures/" + filename, "symbol": symbol,
            "role": role,
            "segment_offset": "0x%04X" % tex_offset,
            "format": "G_IM_FMT_RGBA", "size": "G_IM_SIZ_16b",
            "dimensions": [32, 32],
            "exported_dimensions_note":
                "32x32 is the real wrap span (masks=maskt=5, G_TX_WRAP), which "
                "is what the RDP samples. The F2 settilesize clamp rectangle on "
                "D_02004758 is only 20x3 and is NOT the image extent (B-088).",
            "mip_levels": 6 if role.startswith("mipmapped") else 1,
            "mip_note": ("DL_RAREWARETEXT declares a 6-level chain "
                         "(32,16,8,4,2,1) in one G_LOADBLOCK; only level 0 is "
                         "exported.") if role.startswith("mipmapped") else None,
        })

    return finish(builder, draws, out_dir, "rareware", texture_meta,
                  texture_index_by_offset, notes, source_meta={
        "source_model": "rareware boot logo",
        "source_files": [
            "assets/rarewarelogo.c (committed source)",
            "ROM segment `rarewarelogo` (ge007.ld:129, VMA 0x02000000), "
            "scripts/filelist.u.csv -> assets/rarewarelogo.bin at "
            "ROM 0x%06X, %d bytes" % (offset, length),
            "src/game/title.c load_display_rare_logo / "
            "retrieve_display_rareware_logo (submission, lights, prim colours)",
        ],
        "entry_display_lists": ["D_020043E8", "DL_RAREWARETEXT", "D_02004758"],
        "extraction": "source-static (ROM segment image; no runtime probe)",
    })


# ---------------------------------------------------------------------------
# Nintendo
# ---------------------------------------------------------------------------

def export_nintendo(rom: bytes, out_dir: Path) -> dict:
    offset, length = read_filelist_entry(NINTENDO_FILELIST_KEY)
    model = inflate_1172(rom[offset:offset + length])

    sys.path.insert(0, str(REPO_ROOT / "scripts"))
    from generate_prop_model_c import BinaryModelParser

    parsed = BinaryModelParser(model, {"num_textures": NINTENDO_NUM_TEXTURES,
                                       "num_switches": NINTENDO_NUM_SWITCHES}).parse()

    walker = Walker(model, PROP_BASE_ADDRESS)
    draws = []
    notes = []
    dl_nodes = [(o, n) for o, n in sorted(parsed["nodes"].items()) if n.opcode == 4]
    for node_offset, node in dl_nodes:
        state = new_state()
        walker.walk(node.data["primary_offset"], state,
                    "ModelNode_0x%04X" % node_offset, draws)
    notes.append("%d ModelNode DL nodes (opcode 4) walked; the model tree is a "
                 "BSP (opcode 9) over them." % len(dl_nodes))

    # --- texture -----------------------------------------------------------
    tex_dir = out_dir / "textures"
    tex_dir.mkdir(parents=True, exist_ok=True)
    builder = GltfBuilder("nintendo")
    texture_meta = []
    texture_index_by_offset = {}

    header_tex = parsed["textures"][0]
    # The DLs themselves are authoritative on format; the header agrees.
    tile_image = None
    for draw in draws:
        if draw.state.get("tile_image"):
            tile_image = draw.state["tile_image"]
            tile = draw.state["tile"]
            break
    width = tile["line"] * 8 // 1 if tile["siz"] == 1 else 32
    height = (tile["lrt"] - tile["ult"]) // 4 + 1
    pixel_offset = tile_image["offset"]
    rows = decode_i8(model[pixel_offset:pixel_offset + width * height],
                     width, height)
    write_png(tex_dir / "texture_00.png", width, height, "L", rows)
    texture_index_by_offset[pixel_offset] = builder.add_image(
        "textures/texture_00.png")
    texture_meta.append({
        "index": 0, "file": "textures/texture_00.png",
        "symbol": "embedded in the model file at 0x%04X" % pixel_offset,
        "role": "the model's only texture; every textured DL node loads it",
        "format": G_IM_FMT[tile["fmt"]], "size": G_IM_SIZ[tile["siz"]],
        "dimensions": [width, height],
        "png_note": ("8-bit greyscale, one channel, lossless. The RDP replicates "
                     "an I-format texel to R, G, B and A."),
        "model_header_record": {
            "TextureID": "0x%08X (a pointer, base 0x05000000 -> file offset "
                         "0x%04X)" % (header_tex.texture_id, pixel_offset),
            "Width": header_tex.width, "Height": header_tex.height,
            "MipMapTiles": header_tex.mipmaptiles, "Type": header_tex.type,
            "RenderDepth": header_tex.renderdepth,
            "sflags": header_tex.sflags, "tflags": header_tex.tflags,
        },
        "mip_levels": 1,
    })

    return finish(builder, draws, out_dir, "nintendo", texture_meta,
                  texture_index_by_offset, notes, source_meta={
        "source_model": "PnintendologoZ (prop resource NINTENDOLOGO)",
        "source_files": [
            "assets/obseg/file_resource_table.inc.c:469 "
            "{NINTENDOLOGO, \"PnintendologoZ\", &PnintendologoZ}",
            "assets/obseg/ob_seg.s:533 obseg_file_rz prop, PnintendologoZ",
            "assets/obseg/prop/nintendologo/ModelFileHeader.inc.c:5",
            "scripts/filelist.u.csv -> assets/obseg/prop/PnintendologoZ.bin at "
            "ROM 0x%06X, %d bytes compressed, %d inflated"
            % (offset, length, len(model)),
            "src/game/front.c init_menu01_nintendo / constructor_menu01_nintendo",
        ],
        "model_file_header":
            "MODELFILEHEADER(nintendologo, rootnode=0, "
            "skeleton=standard_object, switches=0, numswitches=0, "
            "nummatrices=1, boundingradius=1868.335, numrecords=0, "
            "numtextures=1)",
        "prop_file_record": "PROPFILERECORD(nintendologo, scale 0.1)",
        "extraction": "source-static (ROM resource, 1172-inflated; no runtime probe)",
    })


# ---------------------------------------------------------------------------
# Generic prop resource
# ---------------------------------------------------------------------------

def decode_prop_texture(model: bytes, offset: int, width: int, height: int,
                        fmt: int, siz: int):
    """Decode one prop-embedded texture. Returns (png_mode, rows, note).

    Raises on any format these props do not actually use, so an unexpected
    texture announces itself instead of being silently approximated.

    NO odd-row word swizzle is applied. MEASURED 2026-09-06: decoding the
    128x32 I4 image at 0x0898 with the swizzle scrambles every odd row into
    noise while the plain linear read yields coherent letterforms. That agrees
    with the rule in docs/doc-routing.json 'textures' -> learned: the swizzle
    belongs only to assets Rare pre-compensated through texSwapAltRowBytes
    (src/game/image.c:2168); prop model textures never pass through image.c.
    """
    if siz == 2 and fmt == 0:                       # RGBA16
        rows = decode_rgba16(model[offset:offset + width * height * 2],
                             width, height)
        return "RGBA", rows, "16-bit RGBA5551 expanded to 8-bit RGBA, lossless."
    if fmt == 4 and siz == 1:                       # I8
        rows = decode_i8(model[offset:offset + width * height], width, height)
        return "L", rows, ("8-bit greyscale, one channel, lossless. The RDP "
                           "replicates an I texel to R, G, B and A.")
    if fmt == 4 and siz == 0:                       # I4
        if width % 2:
            raise ValueError("odd-width I4 texture at 0x%04X" % offset)
        rows = decode_i4(model[offset:offset + (width // 2) * height],
                         width, height)
        return "L", rows, ("4-bit greyscale promoted to 8-bit by nibble "
                           "replication (v*0x11), exactly invertible. The RDP "
                           "replicates an I texel to R, G, B and A.")
    raise ValueError("unsupported texture format at 0x%04X: fmt=%s siz=%s"
                     % (offset, G_IM_FMT.get(fmt), G_IM_SIZ.get(siz)))


def export_prop(rom: bytes, out_dir: Path, name: str) -> dict:
    """Export one PROP_TARGETS entry.

    Structurally identical to export_nintendo above - same resource class, same
    parser, same walker, same assembly - but driven from the target table so a
    new logo is a table entry rather than a new function.
    """
    spec = PROP_TARGETS[name]
    offset, length = read_filelist_entry(spec["filelist_key"])
    model = inflate_1172(rom[offset:offset + length])

    sys.path.insert(0, str(REPO_ROOT / "scripts"))
    from generate_prop_model_c import BinaryModelParser

    parsed = BinaryModelParser(model, {
        "num_textures": spec["num_textures"],
        "num_switches": spec["num_switches"]}).parse()

    walker = Walker(model, PROP_BASE_ADDRESS)
    draws = []
    notes = []
    dl_nodes = [(o, n) for o, n in sorted(parsed["nodes"].items())
                if n.opcode == 4]
    for node_offset, node in dl_nodes:
        state = new_state()
        walker.walk(node.data["primary_offset"], state,
                    "ModelNode_0x%04X" % node_offset, draws)
    notes.append("%d ModelNode DL node(s) (opcode 4) walked; every triangle in "
                 "the model file is reached from them."
                 % len(dl_nodes))
    if walker.unknown:
        notes.append("UNRECOGNISED display-list opcodes encountered: %s"
                     % walker.unknown)

    # --- component separation ---------------------------------------------
    # finish() names one glTF mesh/node per distinct draw.source. Both of these
    # models hang everything off a SINGLE ModelNode, so relabel each draw by the
    # texture image it binds. That split is source data, straight out of the
    # display list; only the human-readable label comes from the table.
    components = spec.get("components", {})
    component_counts = {}
    for draw in draws:
        image = draw.state.get("tile_image")
        key = image["offset"] if image else None
        label = components.get(key)
        if label is None:
            label = ("component_%04X" % key) if key is not None \
                    else "component_untextured"
        draw.source = label
        component_counts[label] = component_counts.get(label, 0) + 1
    notes.append("component split (one mesh per distinct bound texture image): "
                 + ", ".join("%s=%d draws" % (k, v)
                             for k, v in sorted(component_counts.items())))

    # --- textures ----------------------------------------------------------
    # Every texture in the model header is exported, whether or not a draw
    # binds it, so nothing can be silently dropped.
    tex_dir = out_dir / "textures"
    tex_dir.mkdir(parents=True, exist_ok=True)
    builder = GltfBuilder(name)
    texture_meta = []
    texture_index_by_offset = {}

    # What the display list actually set for each bound image, so the header
    # can be cross-checked rather than trusted.
    dl_tile_by_offset = {}
    for draw in draws:
        image = draw.state.get("tile_image")
        if image:
            dl_tile_by_offset.setdefault(image["offset"], draw.state["tile"])

    for index, header_tex in enumerate(parsed["textures"]):
        tex_offset = header_tex.texture_id - PROP_BASE_ADDRESS
        width, height = header_tex.width, header_tex.height
        tile = dl_tile_by_offset.get(tex_offset)
        if tile is not None:
            fmt, siz = tile["fmt"], tile["siz"]
            fmt_source = "the display list's settile for this image"
            span = [(tile["lrs"] - tile["uls"]) // 4 + 1,
                    (tile["lrt"] - tile["ult"]) // 4 + 1]
        else:
            # Not bound by any draw: fall back to the header's renderdepth.
            fmt, siz = (0, 2) if header_tex.renderdepth == 2 else (4, header_tex.renderdepth)
            fmt_source = ("DERIVED from the model header's RenderDepth - no "
                          "draw in this model binds this image")
            span = None

        mode, rows, png_note = decode_prop_texture(
            model, tex_offset, width, height, fmt, siz)
        filename = "texture_%02d_%04X.png" % (index, tex_offset)
        write_png(tex_dir / filename, width, height, mode, rows)
        gltf_index = builder.add_image("textures/" + filename)
        texture_index_by_offset[tex_offset] = gltf_index

        entry = {
            "index": index, "file": "textures/" + filename,
            "symbol": "embedded in the model file at 0x%04X" % tex_offset,
            "role": components.get(tex_offset, "bound by the display list")
                    if tile is not None else "declared in the header, unbound",
            "format": G_IM_FMT[fmt], "size": G_IM_SIZ[siz],
            "format_authority": fmt_source,
            "dimensions": [width, height],
            "png_note": png_note,
            "row_swizzle": "NOT applied - see decode_prop_texture()",
            "model_header_record": {
                "TextureID": "0x%08X (a pointer, base 0x%08X -> file offset "
                             "0x%04X)" % (header_tex.texture_id,
                                          PROP_BASE_ADDRESS, tex_offset),
                "Width": header_tex.width, "Height": header_tex.height,
                "MipMapTiles": header_tex.mipmaptiles, "Type": header_tex.type,
                "RenderDepth": header_tex.renderdepth,
                "sflags": header_tex.sflags, "tflags": header_tex.tflags,
            },
            "mip_levels": 1,
        }
        if span is not None:
            entry["settilesize_span_cross_check"] = {
                "span_from_display_list": span,
                "dimensions_from_header": [width, height],
                "agree": span == [width, height],
            }
        texture_meta.append(entry)

    return finish(builder, draws, out_dir, name, texture_meta,
                  texture_index_by_offset, notes, source_meta={
        "source_model": spec["resource"],
        "source_files": spec["source_files"] + [
            "scripts/filelist.u.csv -> %s at ROM 0x%06X, %d bytes compressed, "
            "%d inflated" % (spec["filelist_key"], offset, length, len(model)),
        ],
        "model_file_header": spec["model_file_header"],
        "prop_file_record": spec["prop_file_record"],
        "extraction": "source-static (ROM resource, 1172-inflated; no runtime probe)",
        "lookat_basis": spec["lookat_basis"],
        "components": spec.get("component_notes", {}),
    })


# ---------------------------------------------------------------------------
# Shared assembly / output
# ---------------------------------------------------------------------------

def finish(builder, draws, out_dir, name, texture_meta, texture_index_by_offset,
           notes, source_meta):
    # One glTF MATERIAL per distinct RDP state, reused wherever that state
    # recurs; one glTF PRIMITIVE per (source display list, state) so the
    # original mesh separation survives into the scene tree.
    material_index_by_state = {}
    material_order = []
    for draw in draws:
        key = state_key(draw.state)
        if key not in material_index_by_state:
            material_index_by_state[key] = len(material_order)
            material_order.append(draw.state)

    material_meta = []
    raw_groups = []
    total_verts = total_tris = total_zero_normals = 0
    total_source_vertex_records = sum(
        len([v for v in d.verts.values() if v is not None]) for d in draws)

    material_extras = []
    for index, snap in enumerate(material_order):
        texgen = snap["texture_gen"]
        lighting = snap["lighting"]
        textured = combiner_names_a_texel(snap["combiner"])
        image = snap.get("tile_image")
        texture_index = (texture_index_by_offset.get(image["offset"])
                         if image is not None and textured else None)
        material_name = "%s_%s_material_%d" % (
            name, "reflective" if texgen and textured
            else ("untextured" if not textured else "uv"), index)
        extras = {
            "cn_interpretation": "vertex normals (NORMAL)" if lighting
                                 else "vertex colour (COLOR_0)",
            "TEXCOORD_0": ("SOURCE - explicit s/t from the Vtx records, S10.5 "
                           "texel coordinates divided by the tile dimensions")
                          if not texgen else DERIVED_UV_NOTE,
            "n64_combiner": snap["combiner"],
            "n64_geometry_mode": snap["geometry_mode_names"],
            "n64_primitive_color_rgba": snap["prim_color"],
            "material_is": ("DERIVED preview approximation; the authoritative "
                            "N64 state is in the metadata.json"),
        }
        material_extras.append(extras)
        prim = snap.get("prim_color") or [255, 255, 255, 255]
        builder.add_material(material_name, texture_index,
                             [prim[0] / 255.0, prim[1] / 255.0,
                              prim[2] / 255.0, 1.0], extras)

    # Group draws by (source list, material state).
    groups = {}
    order = []
    for draw in draws:
        key = (draw.source, state_key(draw.state))
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append(draw)

    node_meta = {}
    for source, key in order:
        members = groups[(source, key)]
        snap = members[0].state
        index = material_index_by_state[key]
        material_name = builder.materials[index]["name"]

        lighting = snap["lighting"]
        texgen = snap["texture_gen"]
        treat_cn_as_colour = not lighting
        has_explicit_uv = not texgen

        image = snap.get("tile_image")
        textured = combiner_names_a_texel(snap["combiner"])
        texture_index = (texture_index_by_offset.get(image["offset"])
                         if image is not None and textured else None)
        tile = snap["tile"]
        tex_dims = (max(1 << tile["masks"], 1), max(1 << tile["maskt"], 1))

        primitive, verts, tris, zero_normals = build_primitive(
            builder, members, index, material_extras[index],
            treat_cn_as_colour, has_explicit_uv, tex_dims)
        if primitive is None:
            continue

        total_verts += verts
        total_tris += tris
        total_zero_normals += zero_normals
        node_meta.setdefault(source, []).append(primitive)

        material_meta.append({
            "material": material_name,
            "gltf_material_index": index,
            "source_display_lists": [source],
            "vertex_count": verts, "triangle_count": tris,
            "geometry_mode": "0x%08X" % snap["geometry_mode"],
            "geometry_mode_names": snap["geometry_mode_names"],
            "G_LIGHTING": lighting,
            "G_TEXTURE_GEN": texgen,
            "cn_is": "vertex NORMALS" if lighting else "vertex COLOUR (shade)",
            "uv_source": "explicit s/t in the Vtx records" if has_explicit_uv
                         else "GENERATED by the RSP from the normal (G_TEXTURE_GEN)",
            "combiner": snap["combiner"],
            "cycle_type": snap["cycle_type"],
            "render_mode": snap["render_mode"],
            "primitive_color_rgba": snap["prim_color"],
            "environment_color_rgba": snap["env_color"],
            "gSPTexture": snap["texture"],
            "textured": combiner_names_a_texel(snap["combiner"]),
            "untextured_note":
                None if combiner_names_a_texel(snap["combiner"]) else
                "This material's combiner names NO texel, so it draws pure "
                "shade and the tile block below is whatever was last left set - "
                "it is not consumed. No texture is bound in the glTF either.",
            "tile": None if not combiner_names_a_texel(snap["combiner"]) else {
                "format": G_IM_FMT.get(tile["fmt"]),
                "size": G_IM_SIZ.get(tile["siz"]),
                "line": tile["line"], "tmem": tile["tmem"],
                "cms": tile["cms"], "masks": tile["masks"], "shifts": tile["shifts"],
                "cmt": tile["cmt"], "maskt": tile["maskt"], "shiftt": tile["shiftt"],
                "clamp_s": bool(tile["cms"] & 0x2),
                "clamp_t": bool(tile["cmt"] & 0x2),
                "settilesize_uls_ult_lrs_lrt": [tile["uls"], tile["ult"],
                                                tile["lrs"], tile["lrt"]],
                "settilesize_span": [(tile["lrs"] - tile["uls"]) // 4 + 1,
                                     (tile["lrt"] - tile["ult"]) // 4 + 1],
                "wrap_span_2_pow_mask": list(tex_dims),
            },
            "texture_image": image if textured else None,
            "gltf_texture_index": texture_index,
        })

        raw_groups.append({
            "material": material_name,
            "source_display_lists": [source],
            "vertices": [
                {"slot": slot, "x": rec["x"], "y": rec["y"], "z": rec["z"],
                 "flag": rec["flag"], "s": rec["s"], "t": rec["t"],
                 "cn": rec["cn"]}
                for d in members for slot, rec in sorted(d.verts.items())
                if rec is not None],
            "triangles": [list(t) for d in members for t in d.tris],
        })

    for source, primitives in node_meta.items():
        builder.meshes.append({"name": source, "primitives": primitives})
        builder.nodes.append({"name": source, "mesh": len(builder.meshes) - 1})

    out_dir.mkdir(parents=True, exist_ok=True)
    gltf_path = out_dir / (name + ".gltf")

    summary = dict(source_meta)
    summary.update({
        "vertex_count": total_verts,
        "vertex_count_note":
            "Unique vertices written to the glTF. The source loads %d Vtx "
            "records in total across all G_VTX commands; the N64 reloads its "
            "16-entry batch window constantly, so the same record can be "
            "loaded more than once." % total_source_vertex_records,
        "source_vertex_records_loaded": total_source_vertex_records,
        "triangle_count": total_tris,
        "mesh_group_count": len(builder.meshes),
        "material_group_count": len(material_meta),
        "texture_count": len(texture_meta),
        "zero_length_normals_substituted_in_gltf": total_zero_normals,
        "notes": notes,
    })
    builder.write(gltf_path, {"summary": summary})

    # ---- model-level facts, measured from the walked state ----------------
    lit = [m for m in material_meta if m["G_LIGHTING"]]
    unlit = [m for m in material_meta if not m["G_LIGHTING"]]
    texgen = [m for m in material_meta if m["G_TEXTURE_GEN"]]
    plain = [m for m in material_meta if not m["G_TEXTURE_GEN"]]
    raw_st = {(v["s"], v["t"]) for g in raw_groups for v in g["vertices"]}

    model_facts = {
        "cn_is_vertex_colour_or_normals":
            ("NORMALS on %d of %d material groups (%d triangles) - those set "
             "G_LIGHTING. VERTEX COLOUR on %d group(s) (%d triangles), which "
             "clear it."
             % (len(lit), len(material_meta), sum(m["triangle_count"] for m in lit),
                len(unlit), sum(m["triangle_count"] for m in unlit))
             if lit and unlit else
             ("NORMALS on every material group - all of them set G_LIGHTING."
              if lit else
              "VERTEX COLOUR on every material group - none set G_LIGHTING.")),
        "G_LIGHTING": "on for %d of %d material groups"
                      % (len(lit), len(material_meta)),
        "G_TEXTURE_GEN": "on for %d of %d material groups"
                         % (len(texgen), len(material_meta)),
        "explicit_uvs_exist":
            ("YES on %d material group(s) (%d triangles), which clear "
             "G_TEXTURE_GEN and sample their own textures with the Vtx s/t."
             % (len(plain), sum(m["triangle_count"] for m in plain))
             if plain else
             "NO. Every material group sets G_TEXTURE_GEN, so no vertex s/t is "
             "ever consumed."),
        "coordinates_generated_via_G_TEXTURE_GEN":
            "YES for %d material group(s), %d of %d triangles."
            % (len(texgen), sum(m["triangle_count"] for m in texgen), total_tris),
        "distinct_raw_st_pairs_in_source": len(raw_st),
        "raw_st_note":
            ("Every source vertex carries s=0, t=0, so there is no explicit UV "
             "data at all." if raw_st == {(0, 0)} else
             "Some vertices carry non-zero s/t bytes. Under G_TEXTURE_GEN the "
             "RSP ignores them and computes coordinates from the normal; they "
             "are consumed only by the groups that clear G_TEXTURE_GEN. The raw "
             "bytes are preserved in the .raw.json regardless."),
        "lookat_reflection_mapping_used":
            "YES - G_TEXTURE_GEN is reflection mapping against the RSP's LookAt "
            "basis.",
        # Per-target, because it is NOT the same answer for every screen: the
        # GoldenEye logo uploads its own basis while the Nintendo and Rareware
        # logos do not. Targets that know their own answer supply it.
        "uploads_its_own_lookat_basis": source_meta.get(
            "lookat_basis",
            "NO. MEASURED 2026-09-06 by tree-wide grep over src/ for both "
            "gSPLookAt and guLookAtReflect: the call sites are bg.c:1393, "
            "gunfire.c:1526, gunfire.c:1748, bondview2.c:8371, front.c:2039 "
            "and front.c:2047 (the GOLDENEYE logo screen), and front.c:8183 "
            "and front.c:8189. None is on THIS screen's path, so on the "
            "cartridge this logo reflects against whatever LookAt basis the "
            "previous screen left resident. Sightline seeds an IDENTITY basis "
            "as a compatibility repair; that identity is Sightline's, NOT "
            "original asset data, and the DERIVED UVs in the glTF assume it."),
    }

    metadata = {
        "logo": name,
        "generated_by": "sightline tools/export/logo_models.py",
        "source": summary,
        "model_facts": model_facts,
        "where_visible_colour_comes_from": colour_summary(name),
        "textures": texture_meta,
        "materials": material_meta,
        "gltf_approximations": {
            "positions": "SOURCE. Raw N64 s16 model-space integers, written as "
                         "floats with no scaling applied.",
            "normals": "SOURCE where G_LIGHTING is set - the Vtx cn[] bytes are "
                       "s8 normals, normalised to unit length for glTF.",
            "colors": "SOURCE where G_LIGHTING is CLEAR - the Vtx cn[] bytes are "
                      "shade colour there.",
            "texcoords": "SOURCE only on primitives whose G_TEXTURE_GEN is clear. "
                         "Everywhere else TEXCOORD_0 is DERIVED - see each "
                         "primitive's extras.",
            "materials": "DERIVED preview only. glTF metallic-roughness cannot "
                         "express an N64 colour combiner; baseColorFactor carries "
                         "the primitive colour and baseColorTexture the render "
                         "tile's image. The real combiner is recorded per "
                         "material above.",
        },
    }
    (out_dir / (name + ".metadata.json")).write_text(
        json.dumps(metadata, indent=1), encoding="utf-8")
    (out_dir / (name + ".raw.json")).write_text(
        json.dumps({"logo": name, "note":
                    "Exact source values, before any glTF normalisation. "
                    "Positions are the N64 s16 integers, cn[] the raw bytes, "
                    "s/t the raw S10.5 fixed-point values, triangles the "
                    "in-batch vertex slots.",
                    "groups": raw_groups}, indent=1), encoding="utf-8")

    write_obj(out_dir, name, raw_groups, material_meta)
    return metadata


def write_obj(out_dir: Path, name: str, raw_groups: list, material_meta: list):
    """Convenience copy only. OBJ has no defined vertex-colour or N64 material
    support; the glTF is the real deliverable."""
    lines = ["# Sightline %s logo - CONVENIENCE COPY ONLY." % name,
             "# OBJ cannot carry vertex colour, the N64 combiner, texture "
             "generation or the tile state. Use the glTF.",
             "mtllib %s.mtl" % name]
    base = 1
    for group, meta in zip(raw_groups, material_meta):
        lines.append("o %s" % group["material"])
        lines.append("usemtl %s" % group["material"])
        vertices = group["vertices"]
        index_of = {}
        for i, v in enumerate(vertices):
            index_of[v["slot"]] = base + i
            lines.append("v %d %d %d" % (v["x"], v["y"], v["z"]))
        for tri in group["triangles"]:
            try:
                lines.append("f %d %d %d" % tuple(index_of[s] for s in tri))
            except KeyError:
                continue
        base += len(vertices)
    (out_dir / (name + ".obj")).write_text("\n".join(lines) + "\n",
                                           encoding="utf-8")
    mtl = []
    for meta in material_meta:
        mtl.append("newmtl %s" % meta["material"])
        prim = meta.get("primitive_color_rgba") or [255, 255, 255, 255]
        mtl.append("Kd %.4f %.4f %.4f" % (prim[0] / 255.0, prim[1] / 255.0,
                                          prim[2] / 255.0))
    (out_dir / (name + ".mtl")).write_text("\n".join(mtl) + "\n",
                                           encoding="utf-8")


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------

def validate(out_dir: Path, name: str, expected_tris: int) -> list:
    results = []
    gltf_path = out_dir / (name + ".gltf")
    doc = json.loads(gltf_path.read_text(encoding="utf-8"))
    results.append(("JSON parses", True, gltf_path.name))

    bin_path = out_dir / doc["buffers"][0]["uri"]
    ok = bin_path.exists() and bin_path.stat().st_size == doc["buffers"][0]["byteLength"]
    results.append((".bin exists and matches byteLength", ok,
                    "%d bytes" % bin_path.stat().st_size if bin_path.exists() else "MISSING"))

    blob = bin_path.read_bytes()
    ok = all(v["byteOffset"] + v["byteLength"] <= len(blob) for v in doc["bufferViews"])
    results.append(("all bufferView ranges inside the buffer", ok,
                    "%d views" % len(doc["bufferViews"])))

    sizes = {5126: 4, 5125: 4, 5123: 2, 5121: 1}
    counts = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
    ok = True
    for accessor in doc["accessors"]:
        view = doc["bufferViews"][accessor["bufferView"]]
        need = accessor["count"] * counts[accessor["type"]] * sizes[accessor["componentType"]]
        if need > view["byteLength"]:
            ok = False
    results.append(("every accessor fits its bufferView", ok,
                    "%d accessors" % len(doc["accessors"])))

    bad_index = 0
    nan = 0
    tris = 0
    for mesh in doc["meshes"]:
        for prim in mesh["primitives"]:
            pos = doc["accessors"][prim["attributes"]["POSITION"]]
            idx = doc["accessors"][prim["indices"]]
            tris += idx["count"] // 3
            view = doc["bufferViews"][idx["bufferView"]]
            values = struct.unpack_from("<%dI" % idx["count"], blob,
                                        view["byteOffset"])
            if max(values) >= pos["count"]:
                bad_index += 1
            for attr, accessor_index in prim["attributes"].items():
                acc = doc["accessors"][accessor_index]
                v = doc["bufferViews"][acc["bufferView"]]
                n = acc["count"] * counts[acc["type"]]
                for f in struct.unpack_from("<%df" % n, blob, v["byteOffset"]):
                    if f != f or f in (float("inf"), float("-inf")):
                        nan += 1
    results.append(("index values all < vertex count", bad_index == 0,
                    "%d primitives out of range" % bad_index))
    results.append(("no NaN/Inf in any float attribute", nan == 0,
                    "%d non-finite values" % nan))
    results.append(("triangle count matches source", tris == expected_tris,
                    "gltf %d vs source %d" % (tris, expected_tris)))

    missing = []
    for image in doc.get("images", []):
        path = out_dir / image["uri"]
        if not path.exists() or path.stat().st_size == 0:
            missing.append(image["uri"])
    results.append(("all referenced textures exist and are non-empty",
                    not missing, "%d images, %d missing"
                    % (len(doc.get("images", [])), len(missing))))

    # Normals, where present, must be finite and near unit length.
    off_unit = 0
    for mesh in doc["meshes"]:
        for prim in mesh["primitives"]:
            if "NORMAL" not in prim["attributes"]:
                continue
            acc = doc["accessors"][prim["attributes"]["NORMAL"]]
            v = doc["bufferViews"][acc["bufferView"]]
            data = struct.unpack_from("<%df" % (acc["count"] * 3), blob,
                                      v["byteOffset"])
            for i in range(acc["count"]):
                x, y, z = data[3 * i:3 * i + 3]
                if abs((x * x + y * y + z * z) ** 0.5 - 1.0) > 1e-3:
                    off_unit += 1
    results.append(("all normals unit length", off_unit == 0,
                    "%d off-unit" % off_unit))
    return results


# ---------------------------------------------------------------------------
# README
# ---------------------------------------------------------------------------

README = """\
{title} logo - Sightline model export
{underline}

OPEN THIS IN BLENDER
    {name}.gltf        (File > Import > glTF 2.0)
The .obj/.mtl beside it is a convenience copy only: OBJ has no defined support
for vertex colour, the N64 colour combiner, texture generation or tile state,
so it loses most of what this export is for.

COUNTS
    vertices  {verts}
    triangles {tris}
    meshes    {meshes}   (one per source display list / model node)
    materials {mats}
    textures  {texs}

WHAT IS SOURCE DATA
    POSITION      the raw N64 s16 model-space coordinates, unscaled.
    NORMAL        present on primitives whose geometry mode sets G_LIGHTING.
                  There the Vtx cn[] bytes are SIGNED NORMALS, not colours.
    COLOR_0       present only on primitives whose G_LIGHTING is CLEAR. There
                  the same cn[] bytes really are shade colour.
    TEXCOORD_0    source only where G_TEXTURE_GEN is clear (see below).
    indices, primitive grouping, tile state, combiner words - all source.

WHAT IS DERIVED (preview only, not asset data)
{derived}

WHERE THE VISIBLE COLOUR COMES FROM
{colour}

TEXTURES
{textures}

glTF LIMITATIONS VERSUS THE N64 MATERIAL
    glTF metallic-roughness cannot express a colour combiner. baseColorFactor
    carries the primitive colour and baseColorTexture the render tile's image,
    which is an approximation. The real per-material combiner words, cycle
    type, render mode, tile masks/shifts and clamp state are all recorded in
    {name}.metadata.json, and the exact source integers in {name}.raw.json.
"""


def write_readme(out_dir: Path, name: str, metadata: dict, derived: str,
                 colour: str) -> None:
    source = metadata["source"]
    textures = "\n".join(
        "    %s  %s %s %dx%d - %s" % (
            t["file"], t["format"], t["size"], t["dimensions"][0],
            t["dimensions"][1], t["role"])
        for t in metadata["textures"])
    title = name.capitalize()
    (out_dir / "README.txt").write_text(README.format(
        title=title, underline="=" * (len(title) + 30), name=name,
        verts=source["vertex_count"], tris=source["triangle_count"],
        meshes=source["mesh_group_count"], mats=source["material_group_count"],
        texs=source["texture_count"], derived=derived, colour=colour,
        textures=textures), encoding="utf-8")


GOLDENEYELOGO_DERIVED = """\
    TEXCOORD_0    DERIVED on the `lettering` mesh only. Those vertices carry
                  s=0, t=0 throughout and set G_TEXTURE_GEN, so the RSP
                  generates coordinates from the vertex normal. The exported
                  UVs are u = 0.5 + nx/2, v = 0.5 - ny/2 from the object-space
                  normal, which assumes an IDENTITY LookAt basis. The
                  `flatshaded_slab` mesh does NOT set G_TEXTURE_GEN and its UVs
                  are the source s/t.
    LOOKAT BASIS  Unlike the Nintendo logo, this screen uploads its OWN basis:
                  constructor_menu04_goldeneyelogo calls guLookAtReflect from
                  (0, 0, 4000) toward the origin with up (0, 1, 0) and issues
                  gSPLookAt (src/game/front.c:2024). The derived UVs above do
                  NOT model that basis - they are a preview approximation only.
    NORMALS       23 source vertices in the lettering are all-zero and cannot be
                  normalised; glTF requires unit normals, so (0, 0, 1) was
                  written and the original bytes kept in the .raw.json. Commit
                  d855a50c is the runtime repair for the same zero-normal
                  texgen case.
    materials     preview approximations - see the limitations note below."""

GOLDENEYELOGO_COLOUR = """\
    The `lettering` mesh takes its colour from the 32x32 RGBA16 REFLECTANCE MAP
    at file offset 0x0068, addressed per pixel by the VERTEX NORMAL through
    G_TEXTURE_GEN, modulated by the SHADE that G_LIGHTING computes from those
    same normals under gelogolight (src/game/front.c:320) - ambient 0x96 grey,
    diffuse white, direction (77, 77, 46). The model sets no primitive colour
    and no environment colour. Because the geometry is entirely FLAT (every
    lettering vertex is at Z=0), all of the apparent relief is that reflectance
    map plus the per-vertex normals - not shape.

    The `flatshaded_slab` mesh clears both G_LIGHTING and G_TEXTURE_GEN, so its
    cn[] bytes are read as vertex COLOUR (shade) and its bound texture is a
    single 1x1 texel. Its appearance is therefore vertex colour, not a texture.

    This screen also uploads its own reflection basis - see LOOKAT BASIS above."""

LEGALPAGE_DERIVED = """\
    TEXCOORD_0    SOURCE on every textured mesh. No primitive here sets
                  G_TEXTURE_GEN, so the Vtx s/t records are used directly
                  (S10.5 texel coordinates divided by the tile span).
    COLOR_0       SOURCE. No primitive sets G_LIGHTING, so every cn[] is read
                  as vertex colour rather than as a normal, and no NORMAL
                  attribute is written.
    materials     preview approximations - see the limitations note below.
    NOT IN HERE   This model is only the ARTWORK layer of the legal screen. The
                  twelve lines of wording are NOT part of it - they are font-
                  rendered at runtime from the language bank. See
                  legal-original.txt and legal-layout.txt."""

LEGALPAGE_COLOUR = """\
    Flat, unlit, textured quads. Nothing on this model sets G_LIGHTING or
    G_TEXTURE_GEN, so the lights do not participate at all: each quad simply
    samples its own texture with explicit UVs and modulates it by vertex shade.
    One mesh (`rule_untextured`) uses a combiner that names no texel and draws
    pure shade.

    The black behind the page is NOT part of this model. It is a runtime fill:
    constructor_menu00_legalscreen calls clear_framebuffer_black
    (src/game/blood_animation.c:204), a G_CYC_FILL rectangle over the whole
    framebuffer."""

NINTENDO_DERIVED = """\
    TEXCOORD_0    DERIVED on every primitive. The source vertices carry s=0,
                  t=0 throughout; the RSP generates coordinates from the vertex
                  normal because G_TEXTURE_GEN is set. The exported UVs are
                  u = 0.5 + nx/2, v = 0.5 - ny/2 from the object-space normal,
                  which assumes an IDENTITY LookAt basis.
    LOOKAT BASIS  This logo never uploads a LookAt basis of its own. Tree-wide,
                  gSPLookAt appears once in the whole of src/ (src/game/bg.c),
                  in the in-level path, and guLookAtReflect once
                  (src/game/bondview2.c) - neither on this boot path. On the
                  cartridge the reflection therefore rides whatever basis the
                  previous screen left resident. Sightline seeds an identity
                  basis as a compatibility repair; that identity is an
                  assumption of the derived UVs above, NOT original asset data.
    materials     preview approximations - see the limitations note below."""

NINTENDO_COLOUR = """\
    The visible colour comes from the ENVIRONMENT-MAPPED TEXTURE, selected per
    pixel by the VERTEX NORMAL through G_TEXTURE_GEN, and modulated by the
    SHADE that G_LIGHTING computes from those same normals under the boot
    screen's single ramped white light.

    Concretely: the geometry mode sets G_LIGHTING and G_TEXTURE_GEN, so cn[] is
    read as normals; the normal drives both the lighting result and the
    generated texture coordinate; and the combiner multiplies the sampled texel
    by shade. The model's own vertex bytes contribute NO RGB colour of their
    own - reading cn[] as vertex colour would be wrong and would paint the logo
    as a smooth normal-following rainbow. src/game/front.c ramps
    ninlogolight's colour from 0 to 0xFF over the screen's duration, which is
    the fade-in.

    One exception, measured: 6 of the 1018 triangles (in ModelNode_0x03B4) draw
    under a combiner that names NO texel at all - FCFFFFFF FFFE793C, plain
    SHADE. Those 6 are lit flat colour with no texture. The other 1012 are the
    environment-mapped path described above."""

RAREWARE_DERIVED = """\
    TEXCOORD_0    DERIVED on the reflection-mapped primitives (the ones whose
                  geometry mode sets G_TEXTURE_GEN). Those vertices DO carry
                  s/t bytes, but the RSP ignores them under G_TEXTURE_GEN and
                  computes coordinates from the normal instead, so they are not
                  usable UVs; they are preserved verbatim in the .raw.json. The
                  exported UVs are u = 0.5 + nx/2, v = 0.5 - ny/2 from the
                  object-space normal, assuming an IDENTITY LookAt basis.
                  TEXCOORD_0 is SOURCE on the four lettering quads, which clear
                  G_LIGHTING and G_TEXTURE_GEN and carry real s/t.
    LOOKAT BASIS  Like the Nintendo logo, this one uploads no LookAt basis.
                  title.c's guLookAt call builds a MODELVIEW matrix, which is a
                  different thing from the texgen basis. Identity is Sightline's
                  compatibility default, not original asset data.
    materials     preview approximations - see the limitations note below."""

RAREWARE_COLOUR = """\
    The visible colour comes from a REFLECTION-MAPPED TEXTURE modulated by the
    PRIMITIVE COLOUR. Vertex data contributes no colour at all.

    The bulk of the logo (display lists D_020043E8 and D_02004758) sets
    G_LIGHTING and G_TEXTURE_GEN, so cn[] is read as NORMALS; the normal
    selects a texel from a 32x32 RGBA16 environment map, and the combiner is
    TEXEL0 * PRIMITIVE. Note that SHADE is not in that combiner at all: the
    lights title.c sets do not tint the result directly, they only matter
    because lighting is what feeds texture generation. The primitive colour is
    what carries the tint and the boot fade - white for the first group, and
    (240, 208, 240) for D_02004758, which is the pale lavender cast of the
    finished logo.

    The four lettering quads (DL_RAREWARETEXT) work differently: they CLEAR
    G_LIGHTING and G_TEXTURE_GEN, use explicit s/t against their own mipmapped
    32x32 textures, and run a 2-cycle trilerp whose second cycle is again
    multiplied by PRIMITIVE. Their cn[] bytes are shade colour, but SHADE is
    not in that combiner either, so they too contribute nothing visible."""


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def _prop_exporter(target_name: str):
    return lambda rom, out_dir: export_prop(rom, out_dir, target_name)


# name -> (exporter(rom, out_dir), DERIVED text, COLOUR text)
EXPORTERS = {
    "nintendo": (export_nintendo, NINTENDO_DERIVED, NINTENDO_COLOUR),
    "rareware": (export_rareware, RAREWARE_DERIVED, RAREWARE_COLOUR),
    "goldeneyelogo": (_prop_exporter("goldeneyelogo"),
                      GOLDENEYELOGO_DERIVED, GOLDENEYELOGO_COLOUR),
    "legalpage": (_prop_exporter("legalpage"),
                  LEGALPAGE_DERIVED, LEGALPAGE_COLOUR),
}

TARGET_NAMES = list(EXPORTERS)


def resolve_output_root(explicit: str) -> Path:
    if explicit:
        return Path(explicit)
    env = os.environ.get("SL_EXPORT_DIR")
    if env:
        return Path(env)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    base = os.environ.get("LOCALAPPDATA") or os.environ.get("TEMP") or os.getcwd()
    return Path(base) / "sightline" / "exports" / "logos" / stamp


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Export front-end logo and artwork models to glTF 2.0.",
        epilog="Output goes outside the repository: --output, else $SL_EXPORT_DIR, "
               "else %LOCALAPPDATA%/sightline/exports/logos/<timestamp>. "
               "Exports are ROM-derived and must never be committed.")
    parser.add_argument("--target", "--logo", dest="target",
                        choices=TARGET_NAMES + ["all"], default="all",
                        help="which model to export (default: all). --logo is "
                             "kept as an alias for the original spelling.")
    parser.add_argument("--output", default=None,
                        help="output directory root")
    parser.add_argument("--rom", default=None,
                        help="path to the base ROM (default: <repo>/baserom.u.z64)")
    args = parser.parse_args(argv)

    rom_path = Path(args.rom) if args.rom else REPO_ROOT / "baserom.u.z64"
    if not rom_path.exists():
        print("ERROR: ROM not found: %s" % rom_path, file=sys.stderr)
        print("       Supply one with --rom. Nothing is embedded in this tool.",
              file=sys.stderr)
        return 2
    rom = rom_path.read_bytes()

    root = resolve_output_root(args.output)
    root.mkdir(parents=True, exist_ok=True)

    wanted = list(TARGET_NAMES) if args.target == "all" else [args.target]

    failures = 0
    for name in wanted:
        exporter, derived, colour = EXPORTERS[name]
        out_dir = root / name
        print("\n=== %s ===" % name)
        metadata = exporter(rom, out_dir)
        write_readme(out_dir, name, metadata, derived, colour)
        source = metadata["source"]
        print("  vertices %d  triangles %d  meshes %d  materials %d  textures %d"
              % (source["vertex_count"], source["triangle_count"],
                 source["mesh_group_count"], source["material_group_count"],
                 source["texture_count"]))
        print("  validation:")
        for label, ok, detail in validate(out_dir, name, source["triangle_count"]):
            print("    [%s] %-46s %s" % ("PASS" if ok else "FAIL", label, detail))
            if not ok:
                failures += 1
        print("  -> %s" % out_dir)

    print("\n" + "=" * 70)
    print("OUTPUT ROOT: %s" % root.resolve())
    print("=" * 70)
    if failures:
        print("%d validation check(s) FAILED" % failures, file=sys.stderr)
        return 1
    print("0 validation failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
