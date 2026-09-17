#!/usr/bin/env python3
"""Passive tinted-glass probe for the NORMAL parity ROM. Read-only.

WHY THIS EXISTS
---------------
The debug ROM (SIGHTLINE_ROM_DEBUG / rominspect.py) can name the object the
crosshair is on, but it is a MODIFIED ROM: its RNG seed diverges at tick 79
(boss.c:389 seeds from osGetCount(), and even ~0x230 bytes of inert const
padding moves it - see ROM_SIZECTL), and each mailbox command perturbs the run
from about 110 game ticks later. Its FRAMEBUFFERS are therefore not cartridge
pixel truth.

This probe touches nothing. It runs the unmodified parity ROM
(build/u/direct/ge007.u.<input>.z64), replays the recording's own input stream
and eeprom, and READS RDRAM. No mailbox, no poke, no ROM change, so every pixel
it captures is the cartridge's own.

IDENTITY IS BY GAME DATA, NEVER BY A DEBUG-ROM POINTER
------------------------------------------------------
A pane is found by walking the game's own prop list from g_ActivePropsTail
(exactly as propsTick and sltrace.state._read_props walk it) and taking props
whose PropDefHeaderRecord.type is PROPDEF_TINTED_GLASS (47). It is then PINNED
by its ObjectRecord address AND its runtime_pos AND its type, all three of which
must keep agreeing; a prop slot that is freed and reused fails the position test
and the probe publishes LOST rather than silently reporting the new occupant.
That failure was measured once already inside rominspect (a pane watched at
0x8006cb58 came back as objtype 11 at the same address, reading healthy).

TRAPS THIS FILE EXISTS NOT TO REPEAT
------------------------------------
* RDRAM in this core is WORD-BYTE-SWAPPED. Every read here goes through
  sltrace.state.Memory. There is no struct.unpack on raw RDRAM in this file,
  on purpose. tools/native/romprobe.py still defines big-endian u32/f32
  helpers it never calls - do not copy them.
* Host offsetof is not MIPS offsetof. Every offset below is taken from the
  decomp's own annotated headers and is cited in the comment beside it.
* A stream recorded through trace-record answers its DIRECT-BOOT ROM, never
  baserom.u.z64. This refuses to run against baserom.
* One libretro core per process. This script runs standalone.
* An instrument that cannot fail is worthless. --expect is the known-positive:
  give it the state a previous measurement established and the run FAILS
  (exit 4) if the probe cannot reproduce it. Run it that way once before any
  number out of this tool is believed.

* AN INPUT INDEX IS NOT A STATE, ACROSS ROM IMAGES OR ACROSS COMMANDS.
  MEASURED, three configurations, one identical input stream, index 4068:

      facility-pane   parity ROM   player prop (971.4, -257.9, 1266.1)
      facility-glass  parity ROM   player prop (-228.9,  51.2,  288.9)
      facility-glass  debug ROM,
                      recorded SELECTs replayed
                                   player prop (947.7, -257.9,  377.7)

  Three different places in the level. boss.c:389 seeds the RNG from
  osGetCount() so every ROM IMAGE gets its own seed (the project's own
  ROM_SIZECTL control proved 0x230 bytes of inert const is enough), the main
  loop is cycle-paced so the two builds fit different numbers of game ticks
  into a frame, and a mailbox command perturbs the run from ~110 ticks later.
  Over four thousand frames that compounds into a different playthrough.

  The PANE half of a state description survives all of it: runtime_pos,
  portalnum, TintDist and CullDist are setup data and read identically in
  every configuration above. The PLAYER half does not. That is why this tool
  samples by DISTANCE and LOOK ANGLE (--shot-dist, --max-look-err) and never
  by frame number.

USAGE
-----
    tools/native/panescope.py --input facility-pane \\
        --expect 4068:pane=943.2,-265.6,544.6:dist=740.3:opacity=255:portal=18

    tools/native/panescope.py --input facility-pane --portal 18 \\
        --shot-dist 740,600,550,500,450,402,380,300,96 --out /tmp/pane
"""

from __future__ import annotations

import argparse
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/trace"))

# ---------------------------------------------------------------------------
# Layout. Every one of these is cited; none comes from a host compiler.
# ---------------------------------------------------------------------------
# struct player, src/game/bondview.h - the annotated /* 0xNNNN */ offsets.
# struct player.pos at 0x004 is annotated "memcampos ?" and it is NOT a
# per-frame camera position: MEASURED over facility-pane-close it took 50
# distinct values in 6137 frames and stayed at (136.15, 596.07, -993.80)
# through an entire walk down the corridor. It is reported as field_0x04 and
# nothing is concluded from it. The per-frame VIEWPOINT this probe publishes
# is the player prop's own position plus vv_theta / vv_verta, all three of
# which move every frame.
PLAYER_CAMPOS = 0x004     # coord3d pos - NOT a live camera, see above
PLAYER_PROP = 0x0A8       # PropRecord* prop  (getCurrentPlayerProp, bondview2.c:9176)
PLAYER_VV_THETA = 0x148   # f32 vv_theta      (annotated /* 0x0148 */)
PLAYER_VV_VERTA = 0x158   # f32 vv_verta      (0x148 + 4 fields: speedtheta,
                          #                    vv_costheta, vv_sintheta)
PLAYER_VV_VERTA360 = 0x15C

# PropRecord, src/bondtypes.h - same constants sltrace.state uses.
PROP_TYPE_OFF = 0x00
PROP_UNION_OFF = 0x04
PROP_POS_OFF = 0x08
PROP_PREV_OFF = 0x24

# PropDefHeaderRecord (src/bondtypes.h:2364): extrascale u16, state u8, type u8.
PROPDEF_TYPE_OFF = 0x03
PROPDEF_STATE_OFF = 0x02
PROPDEF_TINTED_GLASS = 47

# ObjectRecord (src/bondtypes.h:2616): mtx at 0x18, then
#   coord3d runtime_pos; /*0x58 - 0x60*/
OBJ_RUNTIME_POS = 0x58
OBJ_FLAGS = 0x08

# TintedGlassRecord (src/bondtypes.h:3740) - inherits ObjectRecord (0x80), then
#   s32 TintDist; s32 CullDist; s32 calculatedopacity; s32 portalnum; f32 unk90;
# Corroborated by Rule 7: goldeneye_docs "object block types.txt" type 2F puts
# "distance to full illumination" at 0x80 and "distance before you can see
# through window" at 0x84, both signed 4-byte.
GLASS_TINTDIST = 0x80
GLASS_CULLDIST = 0x84
GLASS_OPACITY = 0x88
GLASS_PORTALNUM = 0x8C
GLASS_UNK90 = 0x90

MAX_PROPS = 2048          # same walk guard sltrace.state uses


def glass_opacity(dist: float, xludist: float, opadist: float,
                  arg3: float) -> int:
    """Rare's own curve, propobj.c:4203 glassCalculateOpacity, reproduced here
    ONLY as a cross-check against the value the ROM actually stored. The probe
    reports both; a mismatch means an offset is wrong, not that Rare is."""
    if dist > opadist:
        return 255
    if dist < xludist:
        return int(arg3 * 255)
    return int((((dist - xludist) * (1.0 - arg3)) / (opadist - xludist)
                + arg3) * 255)


class PaneProbe:
    """Everything read out of one frame of RDRAM."""

    def __init__(self, mem, syms):
        self.mem = mem
        self.a = syms

    def player(self) -> dict | None:
        mem = self.mem
        p = mem.u32(self.a.addr("g_CurrentPlayer"))
        if not mem.valid_ptr(p):
            return None
        prop = mem.u32(p + PLAYER_PROP)
        if not mem.valid_ptr(prop):
            return None
        return {
            "player_ptr": p,
            "prop_ptr": prop,
            # glassCalculateOpacity measures from getCurrentPlayerProp()->pos.
            "pos": [mem.f32(prop + PROP_POS_OFF + i * 4) for i in range(3)],
            "cam": [mem.f32(p + PLAYER_CAMPOS + i * 4) for i in range(3)],
            "theta": mem.f32(p + PLAYER_VV_THETA),
            "verta": mem.f32(p + PLAYER_VV_VERTA),
            "verta360": mem.f32(p + PLAYER_VV_VERTA360),
        }

    def read_glass(self, obj: int) -> dict | None:
        """One TintedGlassRecord, or None if it is no longer one."""
        mem = self.mem
        try:
            if mem.u8(obj + PROPDEF_TYPE_OFF) != PROPDEF_TINTED_GLASS:
                return None
            return {
                "obj": obj,
                "pos": [mem.f32(obj + OBJ_RUNTIME_POS + i * 4)
                        for i in range(3)],
                "tint": mem.s32(obj + GLASS_TINTDIST),
                "cull": mem.s32(obj + GLASS_CULLDIST),
                "opacity": mem.s32(obj + GLASS_OPACITY),
                "portal": mem.s32(obj + GLASS_PORTALNUM),
                "unk90": mem.f32(obj + GLASS_UNK90),
                "state": mem.u8(obj + PROPDEF_STATE_OFF),
                "flags": mem.u32(obj + OBJ_FLAGS),
            }
        except ValueError:
            return None

    def scan(self) -> list[dict]:
        """Every tinted-glass pane on the game's own active prop list.

        Walked tail-to-head via prev, exactly as propsTick does, so membership
        here IS the game's liveness answer rather than a heuristic.
        """
        mem = self.mem
        out = []
        p = mem.u32(self.a.addr("g_ActivePropsTail"))
        seen = set()
        n = 0
        while p and mem.valid_ptr(p) and n < MAX_PROPS and p not in seen:
            seen.add(p)
            n += 1
            try:
                if mem.u8(p + PROP_TYPE_OFF) == 1:          # PROP_TYPE_OBJ
                    u = mem.u32(p + PROP_UNION_OFF)
                    if mem.valid_ptr(u):
                        g = self.read_glass(u)
                        if g is not None:
                            g["prop"] = p
                            out.append(g)
                p = mem.u32(p + PROP_PREV_OFF)
            except ValueError:
                break
        return out


# ---------------------------------------------------------------------------
# THE PANE'S OWN RESOURCE INVENTORY
#
# The question this answers: what textures and material passes belong to the
# EXACT pane, and does the native renderer draw all of them. Every aggregate
# already in the tree - sl_aeqrgb, sl_texa, the AEQ census - spans every
# tinted-glass draw in a frame, and facility has twenty panes, so none of them
# can speak about this one.
#
# It is produced HOST-SIDE and PASSIVELY, off the parity ROM, from the
# ObjectRecord this file already pins by portal number and setup position.
# That matters: the debug ROM's live SELECT reports carry the same addresses,
# but they are a different ROM image and a replay of that stream returns MISS
# (an input index is not a state across ROM images - see the header). Pinning
# by game data needs no live capture and no ROM modification at all.
#
# LAYOUT AUTHORITY, field by field:
#   ObjectRecord.model               src/bondtypes.h:2616 block, /*0x14*/
#   Model.obj / render_pos / scale   the same offsets tools/trace/sltrace/
#                                    romdbg.py already mirrors (0x08/0x0C/0x14)
#   ModelFileHeader                  src/bondtypes.h:1411 - RootNode 0x00,
#                                    Skeleton 0x04, Switches 0x08,
#                                    numSwitches 0x0C, numMatrices 0x0E,
#                                    BoundingVolumeRadius 0x10,
#                                    numRecords 0x14, numtextures 0x16,
#                                    Textures 0x18
#   ModelFileTextures                src/bondtypes.h:802 - TextureID u32 then
#                                    Width, Height, MipMapTiles, Type,
#                                    RenderDepth, sflags, tflags as u8
#   ModelNode                        src/bondtypes.h:818 - Opcode u16 0x00,
#                                    Data 0x04, Parent 0x08, Next 0x0C,
#                                    Prev 0x10, Child 0x14
#   Op 0x04 ModelRoData_DisplayListRecord      Primary 0x00, Secondary 0x04,
#                                    BaseAddr 0x08, Vertices 0x0C,
#                                    numVertices 0x10, ModelType 0x12
#   Op 0x16 ModelRoData_DisplayListPrimaryRecord  numVertices 0x00,
#                                    Vertices 0x04, Primary 0x08, BaseAddr 0x0C
#   Op 0x18 ModelRoData_DisplayList_CollisionRecord  Primary 0x00,
#                                    Secondary 0x04, Vertices 0x08,
#                                    numVertices 0x0C, ... ModelType 0x18
#
# The texture-table STRIDE is not taken on trust. objecthandler_2.c:107 sets
#   RootNode = &Textures[numtextures]
# so the table's own end address is published in the header, and the inventory
# CHECKS that RootNode - Textures == numtextures * stride before believing a
# single table entry. A stride that does not reconcile is reported as such
# rather than silently producing plausible-looking texture ids.
#
# DL OPCODES: /mnt/projects/goldeneye_docs, "Display Lists and Object
# Generation/ucode05.txt", with ucode05_old.txt for the two tables ucode05.txt
# punts to a .HTM that is not in the corpus (F5 settile, FC setcombine). The
# routing file's `learned` field records the corrections already measured
# against this corpus, and the ones that bite here are: opcodes 04=vertex,
# B1=GE_TRI4 (Rare's own four-triangle command, NOT F3DEX's G_TRI2), BF=tri1,
# B8=enddl, B9/BA=setothermode_l/h; and the F5 lower word's shift fields,
# shiftt 0x00003C00 and shifts 0x0000000F, which were dropped once in this
# project and cost a 4x sizing error on the explosion's fire tile. They are
# decoded explicitly below for that reason, along with mask, line and TMEM -
# never inferring the image extent from settilesize alone.
#
# Byte order: every read goes through sltrace.state.Memory, which undoes the
# core's word swizzle, so a DL word here is the true big-endian value and the
# opcode is byte 3 of w0. The native side's swap trap (a byte-0 read of a list
# sl_gfx_dl_swap had already rewritten) does not apply to this path.
# ---------------------------------------------------------------------------

OBJ_MODEL = 0x14
MODEL_OBJ, MODEL_RENDER_POS, MODEL_SCALE = 0x08, 0x0C, 0x14
MFH = {"RootNode": 0x00, "Skeleton": 0x04, "Switches": 0x08}
MFH_S16 = {"numSwitches": 0x0C, "numMatrices": 0x0E,
           "numRecords": 0x14, "numtextures": 0x16}
MFH_RADIUS, MFH_TEXTURES = 0x10, 0x18

NODE_OPCODE, NODE_DATA = 0x00, 0x04
NODE_PARENT, NODE_NEXT, NODE_PREV, NODE_CHILD = 0x08, 0x0C, 0x10, 0x14

#: ucode05.txt command set, plus the two the routing file records as needing
#: ucode05_old.txt. Names only - the decode of each is done where it matters.
DL_OPNAMES = {
    0x00: "spnoop", 0x01: "matrix", 0x03: "movemem", 0x04: "vertex",
    0x06: "displaylist", 0xB1: "GE_TRI4", 0xB3: "rdphalf_2",
    0xB4: "rdphalf_1", 0xB6: "cleargeometrymode",
    0xB7: "setgeometrymode", 0xB8: "enddl", 0xB9: "setothermode_l",
    0xBA: "setothermode_h", 0xBB: "texture", 0xBF: "tri1",
    0xE4: "texrect", 0xE6: "loadsync", 0xE7: "pipesync", 0xE8: "tilesync",
    0xE9: "fullsync", 0xED: "setscissor", 0xF0: "loadtlut",
    0xF2: "settilesize", 0xF3: "loadblock", 0xF4: "loadtile",
    0xF5: "settile", 0xF6: "fillrect", 0xF7: "setfillcolor",
    0xF8: "setfogcolor", 0xF9: "setblendcolor", 0xFA: "setprimcolor",
    0xFB: "setenvcolor", 0xFC: "setcombine", 0xFD: "settextureimage",
    0xFE: "setzimage", 0xFF: "setcimage",
}


def resolve_seg(addr, base):
    """A model's display-list pointers are SEGMENTED, and the segment base is
    published by the node itself.

    Measured on the facility pane: the Op18 record carries
    Primary=0x05000138 / Secondary=0x05000150 - segment 5, offsets 0x138 and
    0x150 - while its Vertices pointer in the same record is already a plain
    RDRAM address. So the vertices are relocated at load and the lists are
    not; the game issues the segment before drawing them.

    BaseAddr is the record's own field (src/bondtypes.h:1304 block, /*0x1C*/
    for Op18, /*0x8*/ for Op04), which is what makes this a lookup rather than
    a search for the game's segment table. A pointer whose top byte is already
    0x80 is returned untouched, so the same helper is safe on both forms.
    """
    if addr == 0:
        return 0
    if (addr >> 24) == 0x80:
        return addr
    if base and (base >> 24) == 0x80:
        return base + (addr & 0x00FFFFFF)
    return 0


def walk_dl(mem, addr, max_cmds=4096):
    """Decode one display list. Returns (records, stop_reason).

    Stops at B8 enddl, at an unreadable word, or at the command cap. A DL that
    runs off the end is reported as such - never silently truncated, because
    'the second pass is missing' and 'the walker gave up' look identical in a
    summary.
    """
    out = []
    stop = "cap"
    a = addr
    for _ in range(max_cmds):
        try:
            w0 = mem.u32(a)
            w1 = mem.u32(a + 4)
        except ValueError:
            stop = "ran off RDRAM"
            break
        op = (w0 >> 24) & 0xFF
        out.append((a, op, w0, w1))
        a += 8
        if op == 0xB8:
            stop = "enddl"
            break
    return out, stop


def summarise_dl(mem, addr, label, lines):
    if not addr or not mem.valid_ptr(addr):
        lines.append(f"  {label}: (null or outside RDRAM: 0x{addr:08x})")
        return {}
    cmds, stop = walk_dl(mem, addr)
    counts = {}
    combiners, tiles, tilesizes, loadblocks, images = [], [], [], [], []
    tluts, othermodes, textures = [], [], []
    geoms = []
    tris = 0
    for _, op, w0, w1 in cmds:
        counts[op] = counts.get(op, 0) + 1
        if op in (0xB1, 0xBF):
            tris += 4 if op == 0xB1 else 1
        elif op == 0xFC:
            if (w0, w1) not in combiners:
                combiners.append((w0, w1))
        elif op == 0xF5:
            # ucode05_old.txt "F5 rdp_settile".  upper: fmt 0x00E00000,
            # siz 0x00180000, line 0x0003FE00, tmem 0x1FF.  lower: tile
            # 0x07000000, palette 0x00F00000, cmt/mask t/shift t
            # 0x00040000|0x00080000 / 0x0003C000 / 0x00003C00 and the s
            # trio 0x00000100|0x00000200 / 0x000000F0 / 0x0000000F.
            t = dict(fmt=(w0 >> 21) & 7, siz=(w0 >> 19) & 3,
                     line=(w0 >> 9) & 0x1FF, tmem=w0 & 0x1FF,
                     tile=(w1 >> 24) & 7, pal=(w1 >> 20) & 0xF,
                     cmt=(w1 >> 18) & 3, maskt=(w1 >> 14) & 0xF,
                     shiftt=(w1 >> 10) & 0xF,
                     cms=(w1 >> 8) & 3, masks=(w1 >> 4) & 0xF,
                     shifts=w1 & 0xF)
            tiles.append(t)
        elif op == 0xF2:
            tilesizes.append(dict(uls=(w0 >> 12) & 0xFFF, ult=w0 & 0xFFF,
                                  tile=(w1 >> 24) & 7,
                                  lrs=(w1 >> 12) & 0xFFF, lrt=w1 & 0xFFF))
        elif op in (0xF3, 0xF4):
            loadblocks.append(dict(op=op, uls=(w0 >> 12) & 0xFFF,
                                   ult=w0 & 0xFFF, tile=(w1 >> 24) & 7,
                                   lrs=(w1 >> 12) & 0xFFF, dxt=w1 & 0xFFF))
        elif op == 0xFD:
            images.append(dict(fmt=(w0 >> 21) & 7, siz=(w0 >> 19) & 3,
                               width=(w0 & 0xFFF) + 1, addr=w1))
        elif op == 0xF0:
            tluts.append(dict(count=(w0 >> 14) & 0x3FF, tile=(w1 >> 24) & 7,
                              n=((w1 >> 14) & 0x3FF)))
        elif op in (0xB6, 0xB7):
            # ucode05.txt "B6 rsp_uc05_cleargeometrymode" / "B7". The bit
            # names are gbi.h's (include/PR/gbi.h:348-366), the plain-F3D
            # branch this build compiles.
            geoms.append((op, w1))
        elif op in (0xB9, 0xBA):
            othermodes.append((op, w0, w1))
        elif op == 0xBB:
            textures.append(dict(level=(w0 >> 11) & 7, tile=(w0 >> 8) & 7,
                                 on=w0 & 0xFF, s=(w1 >> 16) & 0xFFFF,
                                 t=w1 & 0xFFFF))
    lines.append(f"  {label}: 0x{addr:08x}  {len(cmds)} commands"
                 f"  (stopped: {stop})   triangles={tris}")
    lines.append("    opcodes: " + " ".join(
        f"{DL_OPNAMES.get(o, '%02X?' % o)}x{c}"
        for o, c in sorted(counts.items(), key=lambda kv: -kv[1])))
    for w0, w1 in combiners:
        lines.append(f"    FC setcombine  {w0:08x} {w1:08x}")
    for t in tiles:
        lines.append(f"    F5 settile     tile={t['tile']} fmt={t['fmt']}"
                     f" siz={t['siz']} line={t['line']} tmem={t['tmem']}"
                     f" pal={t['pal']}"
                     f" cms={t['cms']} masks={t['masks']} shifts={t['shifts']}"
                     f" cmt={t['cmt']} maskt={t['maskt']}"
                     f" shiftt={t['shiftt']}")
    for t in tilesizes:
        w = ((t['lrs'] - t['uls']) >> 2) + 1
        h = ((t['lrt'] - t['ult']) >> 2) + 1
        lines.append(f"    F2 settilesize tile={t['tile']}"
                     f" s[{t['uls']},{t['lrs']}] t[{t['ult']},{t['lrt']}]"
                     f" (10.2)  -> derived {w}x{h}"
                     f"   [DERIVED, not the image size - see B-048]")
    for l in loadblocks:
        nm = "F3 loadblock " if l["op"] == 0xF3 else "F4 loadtile  "
        lines.append(f"    {nm}  tile={l['tile']} uls={l['uls']}"
                     f" ult={l['ult']} lrs={l['lrs']} dxt={l['dxt']}")
    for i in images:
        lines.append(f"    FD settextimg  fmt={i['fmt']} siz={i['siz']}"
                     f" width={i['width']} addr=0x{i['addr']:08x}")
    for t in tluts:
        lines.append(f"    F0 loadtlut    tile={t['tile']} n={t['n']}")
    for t in textures:
        lines.append(f"    BB texture     on={t['on']} level={t['level']}"
                     f" tile={t['tile']} s={t['s']} t={t['t']}")
    GEOMBITS = [(0x00000001, "G_ZBUFFER"), (0x00000004, "G_SHADE"),
                (0x00000200, "G_CULL_FRONT"), (0x00000400, "G_CULL_BACK"),
                (0x00010000, "G_FOG"), (0x00020000, "G_LIGHTING"),
                (0x00040000, "G_TEXTURE_GEN"),
                (0x00080000, "G_TEXTURE_GEN_LINEAR"),
                (0x00200000, "G_SHADING_SMOOTH")]
    for op, w1 in geoms:
        names = " ".join(n for m, n in GEOMBITS if w1 & m) or "(none named)"
        lines.append(f"    {op:02X} {'setgeometrymode' if op == 0xB7 else 'cleargeometrymode'}"
                     f"  {w1:08x}  {names}")
    for op, w0, w1 in othermodes:
        lines.append(f"    {op:02X} setothermode_{'l' if op == 0xB9 else 'h'}"
                     f"  {w0:08x} {w1:08x}"
                     f"   sft={(w0 >> 8) & 0xFF} len={(w0 & 0xFF) + 1}")
    return counts


def inventory(mem, g, lines):
    """Everything attached to ONE pinned pane."""
    a = lines.append
    obj = g["obj"]
    a("=" * 74)
    a(f"PANE RESOURCE INVENTORY   ObjectRecord 0x{obj:08x}"
      f"  portal {g['portal']}")
    a(f"  runtime_pos ({g['pos'][0]:.1f}, {g['pos'][1]:.1f}, {g['pos'][2]:.1f})"
      f"  TintDist {g['tint']}  CullDist {g['cull']}"
      f"  opacity {g['opacity']}")
    a("=" * 74)

    model = mem.u32(obj + OBJ_MODEL)
    a(f"  Model                 0x{model:08x}")
    if not mem.valid_ptr(model):
        a("  MODEL POINTER INVALID - nothing further can be believed")
        return
    mfh = mem.u32(model + MODEL_OBJ)
    a(f"  Model->render_pos     0x{mem.u32(model + MODEL_RENDER_POS):08x}")
    a(f"  Model->scale          {mem.f32(model + MODEL_SCALE):.4f}")
    a(f"  ModelFileHeader       0x{mfh:08x}")
    if not mem.valid_ptr(mfh):
        a("  MODELFILEHEADER INVALID - stopping")
        return
    for nm, off in MFH.items():
        a(f"    {nm:<14s} 0x{mem.u32(mfh + off):08x}")
    for nm, off in MFH_S16.items():
        a(f"    {nm:<14s} {mem.s16(mfh + off)}")
    a(f"    BoundingRadius {mem.f32(mfh + MFH_RADIUS):.2f}")
    tex = mem.u32(mfh + MFH_TEXTURES)
    root = mem.u32(mfh + MFH["RootNode"])
    ntex = mem.s16(mfh + MFH_S16["numtextures"])
    a(f"    Textures       0x{tex:08x}")

    # --- the texture table, with its stride MEASURED not assumed -----------
    a("")
    a("-- texture table ----------------------------------------------------")
    if ntex <= 0 or not mem.valid_ptr(tex):
        a(f"  numtextures={ntex} / Textures=0x{tex:08x} - no table to read")
    else:
        span = root - tex
        stride = span // ntex if ntex else 0
        ok = (stride * ntex == span) and 8 <= stride <= 16
        a(f"  RootNode - Textures = {span} bytes over {ntex} entries"
          f"  -> stride {stride}"
          f"   {'CONSISTENT' if ok else 'DOES NOT RECONCILE'}")
        a("  (objecthandler_2.c:107 sets RootNode = &Textures[numtextures],"
          " so the header publishes the table's own end and the stride is a"
          " measurement rather than a guess at MIPS padding.)")
        if ok:
            for i in range(ntex):
                e = tex + i * stride
                a(f"  [{i}] TextureID 0x{mem.u32(e):08x}"
                  f"  {mem.u8(e + 4)}x{mem.u8(e + 5)}"
                  f"  mips={mem.u8(e + 6)} Type={mem.u8(e + 7)}"
                  f" RenderDepth={mem.u8(e + 8)}"
                  f" sflags=0x{mem.u8(e + 9):02x} tflags=0x{mem.u8(e + 10):02x}")

    # --- the node tree ------------------------------------------------------
    a("")
    a("-- model node tree --------------------------------------------------")
    dls = []
    vtxsets = []
    seen = set()
    stack = [(root, 0)]
    nodes = 0
    while stack and nodes < 256:
        node, depth = stack.pop()
        if not node or not mem.valid_ptr(node) or node in seen:
            continue
        seen.add(node)
        nodes += 1
        op = mem.u16(node + NODE_OPCODE)
        data = mem.u32(node + NODE_DATA)
        a(f"  {'  ' * depth}node 0x{node:08x} op=0x{op:02x}"
          f" data=0x{data:08x}")
        if mem.valid_ptr(data):
            if op == 0x04:
                pri, sec = mem.u32(data), mem.u32(data + 4)
                a(f"  {'  ' * depth}  Op04 DisplayList  Primary=0x{pri:08x}"
                  f" Secondary=0x{sec:08x} Vertices=0x{mem.u32(data+0xC):08x}"
                  f" numVertices={mem.u16(data+0x10)}"
                  f" ModelType={mem.u16(data+0x12)}")
                base = mem.u32(data + 0x08)
                a(f"  {'  ' * depth}  Op04 BaseAddr=0x{base:08x}")
                vtxsets.append(("Op04 Vertices", mem.u32(data + 0x0C),
                                mem.u16(data + 0x10)))
                dls += [("Op04 Primary", resolve_seg(pri, base), pri),
                        ("Op04 Secondary", resolve_seg(sec, base), sec)]
            elif op == 0x16:
                pri = mem.u32(data + 8)
                a(f"  {'  ' * depth}  Op16 PrimaryOnly  Primary=0x{pri:08x}"
                  f" numVertices={mem.s32(data)}"
                  f" Vertices=0x{mem.u32(data+4):08x}")
                base = mem.u32(data + 0x0C)
                a(f"  {'  ' * depth}  Op16 BaseAddr=0x{base:08x}")
                vtxsets.append(("Op16 Vertices", mem.u32(data + 0x04),
                                mem.s32(data)))
                dls += [("Op16 Primary", resolve_seg(pri, base), pri)]
            elif op == 0x18:
                pri, sec = mem.u32(data), mem.u32(data + 4)
                a(f"  {'  ' * depth}  Op18 DL+Collision Primary=0x{pri:08x}"
                  f" Secondary=0x{sec:08x} Vertices=0x{mem.u32(data+8):08x}"
                  f" numVertices={mem.s16(data+0xC)}"
                  f" ModelType={mem.s16(data+0x18)}")
                base = mem.u32(data + 0x1C)
                a(f"  {'  ' * depth}  Op18 BaseAddr=0x{base:08x}"
                  f"  CollisionVertices=0x{mem.u32(data+0x10):08x}"
                  f" numCollisionVertices={mem.s16(data+0xE)}"
                  f" RwDataIndex={mem.u16(data+0x1A)}")
                vtxsets.append(("Op18 Vertices", mem.u32(data + 0x08),
                                mem.s16(data + 0x0C)))
                dls += [("Op18 Primary", resolve_seg(pri, base), pri),
                        ("Op18 Secondary", resolve_seg(sec, base), sec)]
        nxt, chld = mem.u32(node + NODE_NEXT), mem.u32(node + NODE_CHILD)
        if nxt:
            stack.append((nxt, depth))
        if chld:
            stack.append((chld, depth + 1))
    a(f"  {nodes} node(s) walked")

    # --- the vertices, read out of the cartridge's own RAM ------------------
    #
    # THE DECISIVE CHECK. The native renderer's per-draw probe reports all
    # three vertices of the pane's triangle with s = t = 0, while fourteen of
    # eighteen vertex lines in the same log carry real coordinates. Either the
    # native DL interpreter is losing this list's texture coordinates, or the
    # cartridge's vertex records genuinely hold zero and the structure on
    # screen comes from somewhere else. The setup data settles it, and it is
    # readable passively.
    #
    # struct Vertex, src/bondtypes.h:708 - coord16 x,y,z (s16 each) at 0x00,
    # index s16 at 0x06, s at 0x08, t at 0x0A, then r,g,b,a as u8 at
    # 0x0C..0x0F. sizeof is 16.
    a("")
    a("-- vertices (struct Vertex, 16 bytes, bondtypes.h:708) --------------")
    for nm, vaddr, nv in vtxsets:
        if not vaddr or not mem.valid_ptr(vaddr) or nv <= 0 or nv > 64:
            a(f"  {nm}: 0x{vaddr:08x} n={nv} - not readable")
            continue
        a(f"  {nm}: 0x{vaddr:08x}  {nv} vertices")
        for i in range(nv):
            v = vaddr + i * 16
            a(f"    v{i}  xyz=({mem.s16(v):6d},{mem.s16(v+2):6d},"
              f"{mem.s16(v+4):6d})  index={mem.s16(v+6):5d}"
              f"  s={mem.s16(v+8):6d} t={mem.s16(v+10):6d}"
              f"  ({mem.s16(v+8)/32.0:7.2f},{mem.s16(v+10)/32.0:7.2f} texels)"
              f"  rgba={mem.u8(v+12):3d},{mem.u8(v+13):3d},"
              f"{mem.u8(v+14):3d},{mem.u8(v+15):3d}")

    # --- the display lists --------------------------------------------------
    a("")
    a("-- display lists ----------------------------------------------------")
    live = [(nm, ad, raw) for nm, ad, raw in dls if raw]
    a(f"  {len(live)} non-null display list slot(s) of {len(dls)}"
      f"   <- MORE THAN ONE PASS?  {'YES' if len(live) > 1 else 'NO'}")
    for nm, ad, raw in dls:
        a(f"  {nm}: segmented 0x{raw:08x} -> resolved 0x{ad:08x}")
        if raw:
            summarise_dl(mem, ad, nm, lines)


def dist3(a, b) -> float:
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def look_error(player, pane, theta_deg: float) -> float:
    """Degrees between where the player is FACING and where the pane is.

    Sampling by distance alone is not sampling by state: the same pane at the
    same 550 units can fill the screen or be 40 degrees off to the left, and
    the two frames say nothing about each other. This is what makes a
    comparison "same pane, same opacity, same angle" rather than two of three.

    The convention is CALIBRATED, not assumed: on facility-pane at index 4068
    the recording ends with the pane filling the view, player prop
    (971.41, -257.89, 1266.05), pane (943.24, -265.65, 544.56), vv_theta
    178.4707. atan2(-dx, dz) gives 177.76 deg - 0.7 deg out. The three other
    sign/argument orders give 2.2, 182.2 and 357.8, so the choice is not
    ambiguous.
    """
    dx = pane[0] - player[0]
    dz = pane[2] - player[2]
    bearing = math.degrees(math.atan2(-dx, dz)) % 360.0
    e = abs((bearing - (theta_deg % 360.0) + 180.0) % 360.0 - 180.0)
    return e


def parse_expect(spec: str) -> dict:
    """index:key=value:... - the known-positive control."""
    parts = spec.split(":")
    exp = {"index": int(parts[0])}
    for tok in parts[1:]:
        if not tok.strip():
            continue
        k, _, v = tok.partition("=")
        k = k.strip()
        if k in ("pane", "player", "cam"):
            exp[k] = [float(x) for x in v.split(",")]
        else:
            exp[k] = float(v)
    return exp


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True,
                    help="stream name under tools/trace/inputs (no extension)")
    ap.add_argument("--rom", default=None,
                    help="defaults to the NORMAL direct-boot ROM for --input")
    ap.add_argument("--map", default=None)
    ap.add_argument("--frames", type=int, default=0, help="0 = whole stream")
    ap.add_argument("--scan-every", type=int, default=30,
                    help="frames between full prop-list walks")
    ap.add_argument("--portal", type=int, default=None,
                    help="pin the pane with this portalnum")
    ap.add_argument("--pane-pos", default="",
                    help="x,y,z - pin the pane whose runtime_pos is nearest")
    ap.add_argument("--pin-tol", type=float, default=8.0,
                    help="units the pinned pane's runtime_pos may move before "
                         "the watch is declared LOST")
    ap.add_argument("--shot-dist", default="",
                    help="comma-separated player distances to capture the "
                         "framebuffer at - sampling BY STATE, not by frame")
    ap.add_argument("--shot-tol", type=float, default=6.0)
    ap.add_argument("--max-look-err", type=float, default=180.0,
                    help="degrees the pane may be off the view centre for a "
                         "capture to count. Distance alone is not state: the "
                         "same pane at the same range can fill the screen or "
                         "sit 40 degrees off to the left.")
    ap.add_argument("--expect", default="",
                    help="index:pane=x,y,z:dist=D:opacity=N:portal=P - the "
                         "known-positive. Non-zero exit if not reproduced.")
    ap.add_argument("--expect-tol", type=float, default=1.0)
    ap.add_argument("--verify-mailbox", action="store_true",
                    help="PROBE VALIDATION ONLY, and it needs a DEBUG ROM. "
                         "Issues the recorded SELECT presses at the mailbox "
                         "and compares the ROM's OWN reading of the pane "
                         "against this file's passive walk of the same object "
                         "on the same frame. Two independent paths, one "
                         "number - which is a stronger control than matching a "
                         "remembered figure, because the two paths share no "
                         "code and no offset table. Perturbs the run (measured: "
                         "from ~110 game ticks after a command) so it is never "
                         "used for pixels.")
    ap.add_argument("--verify-at", default="",
                    help="START:END:STEP - extra frames to fire the "
                         "cross-check command at, on top of the stream's own "
                         "recorded presses. More comparisons, same run.")
    ap.add_argument("--inventory", type=int, default=None, metavar="INDEX",
                    help="at this input index, dump every texture, model node "
                         "and display list attached to the PINNED pane, and "
                         "say whether it has more than one pass. Passive: it "
                         "reads the ObjectRecord this tool already pinned by "
                         "portal and setup position, so it needs no live "
                         "capture and no debug ROM.")
    ap.add_argument("--out", default="/tmp/panescope")
    ap.add_argument("--no-shots", action="store_true")
    ap.add_argument("--csv-every", type=int, default=1)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    from sltrace.emu_libretro import LibretroEmulator
    from sltrace.state import Memory
    from sltrace.symbols import SymbolTable
    from sltrace.romdbg import write_ppm

    name = args.input
    rom = args.rom or str(ROOT / "build/u/direct" / f"ge007.u.{name}.z64")
    mp = args.map or str(ROOT / "build/u/direct" / f"ge007.u.{name}.map")
    inp = ROOT / "tools/trace/inputs" / (name + ".input")
    eep = ROOT / "tools/trace/inputs" / (name + ".eeprom")

    if ".dbg." in rom and not args.verify_mailbox:
        print("panescope: REFUSING a debug ROM. This tool exists because the "
              "debug ROM's pixels are not cartridge truth. --verify-mailbox "
              "is the one mode that wants one, and it captures no pixels.",
              file=sys.stderr)
        return 2
    if args.verify_mailbox and ".dbg." not in rom:
        print("panescope: --verify-mailbox needs the DEBUG ROM - it is the "
              "only build with a mailbox to cross-check against.",
              file=sys.stderr)
        return 2
    if "baserom" in rom:
        print("panescope: REFUSING baserom - a trace-record stream answers its "
              "DIRECT-BOOT ROM. A facility stream replayed against baserom "
              "lands in Surface.", file=sys.stderr)
        return 2
    for f in (rom, mp, inp):
        if not pathlib.Path(f).is_file():
            print(f"panescope: missing {f}\n"
                  f"  build it with: make trace-record ... or "
                  f"tools/native/build.sh", file=sys.stderr)
            return 2
    if not eep.is_file():
        print(f"panescope: REFUSING to run without {eep} - the eeprom is not "
              f"optional, the same input answers a different saved game "
              f"without it.", file=sys.stderr)
        return 2

    syms = SymbolTable.from_map(mp)
    syms.require(["g_CurrentPlayer", "g_ActivePropsTail"])
    mb_base = None
    if args.verify_mailbox:
        if "g_SlRomDbg" not in syms:
            print(f"panescope: {mp} is from a ROM built WITHOUT "
                  f"SIGHTLINE_ROM_DEBUG - there is no mailbox to compare with.",
                  file=sys.stderr)
            return 2
        mb_base = syms.addr("g_SlRomDbg")
        print(f"panescope: MAILBOX CROSS-CHECK mode, g_SlRomDbg = "
              f"0x{mb_base:08x}. This run PERTURBS the game and captures no "
              f"pixels; it exists only to prove the passive reader.")
    print(f"panescope: rom = {rom}")
    print(f"panescope: PASSIVE - no mailbox, no poke, no ROM modification")

    raw = inp.read_bytes()
    total = args.frames or (len(raw) // 4)
    print(f"panescope: {len(raw)//4} recorded frames, running {total}")

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    want_pos = ([float(x) for x in args.pane_pos.split(",")]
                if args.pane_pos else None)
    shot_dists = [float(x) for x in args.shot_dist.split(",")
                  if x.strip()] if args.shot_dist else []
    shots_done: set[float] = set()
    expect = parse_expect(args.expect) if args.expect else None
    expect_row = {}

    vid = {"fb": None, "w": 0, "h": 0, "pitch": 0}

    def on_video(data, width, height, pitch):
        vid["fb"], vid["w"], vid["h"], vid["pitch"] = data, width, height, pitch

    emu = LibretroEmulator(rom, input_stream=str(inp), eeprom=str(eep))
    emu._on_video_capture = on_video

    csv = (out / f"{name}-pane.csv").open("w")
    csv.write("index,obj,portal,pane_x,pane_y,pane_z,player_x,player_y,"
              "player_z,playerdist,tint,cull,unk90,opacity,opacity_calc,"
              "field04_x,field04_y,field04_z,theta,verta,look_err,state,"
              "flags\n")

    st = {"pinned": None, "pinned_pos": None, "rows": 0, "scans": 0,
          "panes_seen": 0, "lost": 0, "frames_with_player": 0,
          "shots": 0, "min_dist": 1e30, "max_op": -1, "min_op": 1e9}

    def sample(index, ram):
        mem = Memory(ram)
        pb = PaneProbe(mem, syms)
        pl = pb.player()
        if pl is None:
            return
        st["frames_with_player"] += 1

        # Re-pin periodically, and always while nothing is pinned. The walk is
        # the expensive part, so the pinned object is polled every frame and
        # the full list only every --scan-every.
        if st["pinned"] is None or index % args.scan_every == 0:
            panes = pb.scan()
            st["scans"] += 1
            st["panes_seen"] = max(st["panes_seen"], len(panes))
            if st["pinned"] is None and panes:
                cand = panes
                if args.portal is not None:
                    cand = [g for g in panes if g["portal"] == args.portal]
                if want_pos is not None and cand:
                    cand = [min(cand, key=lambda g: dist3(g["pos"], want_pos))]
                if cand:
                    # Nearest to the player, which is the pane a report about
                    # "the window in front of me" is about.
                    g = min(cand, key=lambda g: dist3(g["pos"], pl["pos"]))
                    st["pinned"] = g["obj"]
                    st["pinned_pos"] = list(g["pos"])
                    print(f"panescope: pinned pane obj=0x{g['obj']:08x} "
                          f"portal={g['portal']} "
                          f"pos=({g['pos'][0]:.1f},{g['pos'][1]:.1f},"
                          f"{g['pos'][2]:.1f}) tint={g['tint']} "
                          f"cull={g['cull']} at index {index} "
                          f"({len(panes)} panes on the prop list)")

        if st["pinned"] is None:
            return
        g = pb.read_glass(st["pinned"])
        # Identity: type AND position must both still agree. A PropRecord
        # pointer is not an identity - measured, rominspect watched a freed
        # slot for thousands of frames reading healthy.
        if g is None or dist3(g["pos"], st["pinned_pos"]) > args.pin_tol:
            st["lost"] += 1
            st["pinned"] = None
            return

        d = dist3(g["pos"], pl["pos"])
        lerr = look_error(pl["pos"], g["pos"], pl["theta"])
        calc = glass_opacity(d, float(g["tint"]), float(g["cull"]),
                             g["unk90"])
        st["min_dist"] = min(st["min_dist"], d)
        st["max_op"] = max(st["max_op"], g["opacity"])
        st["min_op"] = min(st["min_op"], g["opacity"])

        if args.inventory is not None and index == args.inventory \
                and not st.get("inv_done"):
            st["inv_done"] = True
            lines = []
            inventory(mem, g, lines)
            text = "\n".join(lines)
            (out / f"{name}-inventory.txt").write_text(text + "\n")
            print(text)

        row = dict(index=index, d=d, calc=calc, g=g, pl=pl)
        if expect is not None and index == expect["index"]:
            expect_row.update(row)

        if index % args.csv_every == 0:
            csv.write(
                f"{index},0x{g['obj']:08x},{g['portal']},"
                f"{g['pos'][0]:.2f},{g['pos'][1]:.2f},{g['pos'][2]:.2f},"
                f"{pl['pos'][0]:.2f},{pl['pos'][1]:.2f},{pl['pos'][2]:.2f},"
                f"{d:.2f},{g['tint']},{g['cull']},{g['unk90']:.4f},"
                f"{g['opacity']},{calc},"
                f"{pl['cam'][0]:.2f},{pl['cam'][1]:.2f},{pl['cam'][2]:.2f},"
                f"{pl['theta']:.4f},{pl['verta']:.4f},{lerr:.2f},"
                f"{g['state']},0x{g['flags']:08x}\n")
            st["rows"] += 1

        # Sample BY STATE: capture where the DISTANCE is what was asked for,
        # never at a frame number. Frame numbers do not survive a rebuild;
        # a distance is a property of the run.
        for target in shot_dists:
            if target in shots_done or abs(d - target) > args.shot_tol:
                continue
            if lerr > args.max_look_err:
                continue
            shots_done.add(target)
            base = f"d{int(target):04d}-i{index:06d}"
            if not args.no_shots and vid["fb"] is not None:
                write_ppm(out / (base + ".ppm"), vid["fb"], vid["w"],
                          vid["h"], vid["pitch"])
                st["shots"] += 1
            (out / (base + ".txt")).write_text(
                f"input index      {index}\n"
                f"pane obj         0x{g['obj']:08x}\n"
                f"pane runtime_pos {g['pos'][0]:.2f} {g['pos'][1]:.2f} "
                f"{g['pos'][2]:.2f}\n"
                f"portalnum        {g['portal']}\n"
                f"TintDist         {g['tint']}\n"
                f"CullDist         {g['cull']}\n"
                f"unk90            {g['unk90']:.4f}\n"
                f"player prop pos  {pl['pos'][0]:.2f} {pl['pos'][1]:.2f} "
                f"{pl['pos'][2]:.2f}\n"
                f"playerdist       {d:.2f}   (target {target})\n"
                f"opacity (ROM)    {g['opacity']}\n"
                f"opacity (calc)   {calc}\n"
                f"player.pos[0x04] {pl['cam'][0]:.2f} {pl['cam'][1]:.2f} "
                f"{pl['cam'][2]:.2f}   (NOT a live camera - see the comment "
                f"on PLAYER_CAMPOS)\n"
                f"look error       {lerr:.2f} deg off view centre\n"
                f"vv_theta         {pl['theta']:.4f}\n"
                f"vv_verta         {pl['verta']:.4f}\n"
                f"vv_verta360      {pl['verta360']:.4f}\n")
            if not args.quiet:
                print(f"  shot d={d:7.1f} idx={index:6d} opacity="
                      f"{g['opacity']:3d} lookerr={lerr:5.1f} "
                      f"player=({pl['pos'][0]:.1f},{pl['pos'][1]:.1f},"
                      f"{pl['pos'][2]:.1f}) theta={pl['theta']:.2f} "
                      f"verta={pl['verta']:.2f}")

    # ---- mailbox cross-check plumbing (probe validation only) -------------
    select_at: set[int] = set()
    mbx = {"seq": 0, "last_seq": -1, "cmp": 0, "worst": 0.0,
           "lines": [], "events": []}
    if mb_base is not None:
        from sltrace.emu_libretro import InputFrame
        from sltrace.romdbg import (MAGIC, MB, CMD_FINDTYPE, DBG_SELECT_BIT,
                                    read_mailbox, check_layout)
        prev = False
        for i in range(len(raw) // 4):
            f = InputFrame.from_bytes(raw[i * 4:i * 4 + 4])
            now = f.pressed(DBG_SELECT_BIT)
            if now and not prev:
                select_at.add(i)
            prev = now
        if args.verify_at:
            a0, a1, stp = (int(x) for x in args.verify_at.split(":"))
            select_at.update(range(a0, a1, stp))
        print(f"panescope: {len(select_at)} cross-check points; the stream's "
              f"own recorded presses are {sorted(i for i in select_at)[:8]}...")
        # FINDTYPE, not SELECT. SELECT reproduces the crosshair raycast, and
        # MEASURED on facility-glass all three recorded presses come back MISS
        # - the crosshair simply was not on a pane. FINDTYPE takes a watch on
        # the nearest on-screen prop of one PROPDEF, which is what fills the
        # mailbox's glass_* fields, and it is what a cross-check wants: a
        # command that lands on the object whenever one is on screen.

    checked = [False]

    def mailbox_compare(index, ram):
        mem = Memory(ram)
        if mem.u32(mb_base + MB["magic"][0]) != MAGIC:
            return
        if not checked[0]:
            checked[0] = True
            print(f"panescope: {check_layout(mem, mb_base)}")
        box = read_mailbox(mem, mb_base)
        if box["rom_seq"] == mbx["last_seq"]:
            return                      # the hook does not run every frame
        mbx["last_seq"] = box["rom_seq"]
        if box["event"] not in (0, 1):
            mbx["events"].append(
                f"index {index:6d}  rom_seq={box['rom_seq']} "
                f"event={box['event']} nhits={box['nhits']} "
                f"watch_obj=0x{box['watch_obj']:08x} "
                f"objtype={box['watch_objtype']} "
                f"onscreen={box['onscreen_count']} "
                f"sight_mode={box['sight_mode']}")
        if box["event"] not in (2, 4) or box["watch_objtype"] != \
                PROPDEF_TINTED_GLASS:
            return
        obj = box["watch_obj"]
        pb = PaneProbe(mem, syms)
        g = pb.read_glass(obj)
        pl = pb.player()
        if g is None or pl is None:
            mbx["lines"].append(f"index {index}: ROM says objtype 47 at "
                                f"0x{obj:08x}, the passive reader disagrees "
                                f"- MISMATCH")
            mbx["worst"] = 1e9
            return
        d = dist3(g["pos"], pl["pos"])
        pairs = [
            ("runtime_pos", dist3(g["pos"], box["watch_runtime_pos"])),
            ("player_pos", dist3(pl["pos"], box["player_pos"])),
            ("playerdist", abs(d - box["watch_dist"])),
            ("TintDist", abs(g["tint"] - box["glass_tintdist"])),
            ("CullDist", abs(g["cull"] - box["glass_culldist"])),
            ("opacity", abs(g["opacity"] - box["glass_opacity"])),
            ("portalnum", abs(g["portal"] - box["glass_portalnum"])),
            ("unk90", abs(g["unk90"] - box["glass_unk90"])),
        ]
        mbx["cmp"] += 1
        worst = max(e for _, e in pairs)
        mbx["worst"] = max(mbx["worst"], worst)
        mbx["lines"].append(
            f"index {index:6d}  obj 0x{obj:08x}  ROM: pos=("
            f"{box['watch_runtime_pos'][0]:.1f},"
            f"{box['watch_runtime_pos'][1]:.1f},"
            f"{box['watch_runtime_pos'][2]:.1f}) dist={box['watch_dist']:.1f} "
            f"tint={box['glass_tintdist']} cull={box['glass_culldist']} "
            f"op={box['glass_opacity']} portal={box['glass_portalnum']} "
            f"player=({box['player_pos'][0]:.1f},{box['player_pos'][1]:.1f},"
            f"{box['player_pos'][2]:.1f})")
        mbx["lines"].append(
            f"{'':13s}{'':13s}  US : pos=({g['pos'][0]:.1f},"
            f"{g['pos'][1]:.1f},{g['pos'][2]:.1f}) dist={d:.1f} "
            f"tint={g['tint']} cull={g['cull']} op={g['opacity']} "
            f"portal={g['portal']} player=({pl['pos'][0]:.1f},"
            f"{pl['pos'][1]:.1f},{pl['pos'][2]:.1f})   worst err {worst:.4f}")

    emu._load()
    emu._warming_up = True
    emu._core.retro_run()
    emu._warming_up = False
    for i in range(total):
        if mb_base is not None and i in select_at:
            ram = emu.rdram()
            if ram is not None:
                m = Memory(ram)
                mbx["seq"] += 1
                m.w32(mb_base + MB["host_arg"][0], PROPDEF_TINTED_GLASS)
                m.w32(mb_base + MB["host_cmd"][0], CMD_FINDTYPE)
                m.w32(mb_base + MB["host_seq"][0], mbx["seq"])
        emu._core.retro_run()
        emu._frame_index += 1
        ram = emu.rdram()
        if ram is not None:
            sample(i, ram)
            if mb_base is not None:
                mailbox_compare(i, ram)
    emu.close()
    csv.close()

    if mb_base is not None:
        print("panescope: mailbox events seen (fresh rom_seq, non-idle)")
        for ln in mbx["events"]:
            print("    " + ln)
        print("panescope: mailbox cross-check - the ROM's own struct reads "
              "against this file's passive walk, same object, same frame")
        for ln in mbx["lines"]:
            print("    " + ln)
        if mbx["cmp"] == 0:
            print("panescope: CROSS-CHECK FAILED - no SELECT ever landed on a "
                  "tinted-glass pane, so nothing was compared. That is an "
                  "unfired control, not a pass.", file=sys.stderr)
            return 4
        ok = mbx["worst"] <= 0.05
        print(f"panescope: {mbx['cmp']} comparisons, worst disagreement "
              f"{mbx['worst']:.4f} - {'OK' if ok else 'MISMATCH'}")
        if not ok:
            print("panescope: CROSS-CHECK FAILED - the passive reader and the "
                  "ROM disagree. The host offset table is wrong; nothing "
                  "downstream of this probe is evidence.", file=sys.stderr)
            return 4

    print(f"panescope: {st['rows']} rows, {st['scans']} list walks, "
          f"{st['panes_seen']} panes max on the list, {st['lost']} re-pins, "
          f"{st['shots']} framebuffers -> {out}")
    if st["rows"]:
        print(f"panescope: pane playerdist min {st['min_dist']:.1f}, "
              f"opacity {st['min_op']}..{st['max_op']}")

    # ---- the known-positive control ---------------------------------------
    # An instrument that cannot fail is worthless. Two unconditional controls
    # plus the optional --expect.
    if st["frames_with_player"] == 0:
        print("panescope: FAILED - g_CurrentPlayer never resolved. Nothing in "
              "this run means anything.", file=sys.stderr)
        return 3
    if st["rows"] == 0:
        print("panescope: FAILED - no tinted-glass pane was ever found on the "
              "prop list. That is either the wrong stream or a broken walk; "
              "it is NOT evidence that the level has no glass.",
              file=sys.stderr)
        return 3
    if expect is not None:
        if not expect_row:
            print(f"panescope: EXPECT FAILED - no sample at index "
                  f"{expect['index']}", file=sys.stderr)
            return 4
        g, pl, d = expect_row["g"], expect_row["pl"], expect_row["d"]
        checks = []
        if "pane" in expect:
            checks.append(("pane runtime_pos", dist3(g["pos"], expect["pane"]),
                           args.expect_tol,
                           f"{g['pos'][0]:.1f},{g['pos'][1]:.1f},"
                           f"{g['pos'][2]:.1f}"))
        if "player" in expect:
            checks.append(("player prop pos", dist3(pl["pos"], expect["player"]),
                           args.expect_tol,
                           f"{pl['pos'][0]:.1f},{pl['pos'][1]:.1f},"
                           f"{pl['pos'][2]:.1f}"))
        for key, got in (("dist", d), ("opacity", g["opacity"]),
                         ("portal", g["portal"]), ("tint", g["tint"]),
                         ("cull", g["cull"])):
            if key in expect:
                checks.append((key, abs(got - expect[key]), args.expect_tol,
                               f"{got}"))
        bad = [c for c in checks if c[1] > c[2]]
        print(f"panescope: --expect at index {expect['index']}")
        for nm, err, tol, got in checks:
            print(f"    {nm:16s} got {got:24s} err {err:8.3f} "
                  f"{'OK' if err <= tol else 'MISMATCH'}")
        if bad:
            print("panescope: EXPECT FAILED - the probe did not reproduce the "
                  "known-positive. The probe is broken; nothing downstream of "
                  "it is evidence.", file=sys.stderr)
            return 4
        print("panescope: control OK - known-positive reproduced")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
