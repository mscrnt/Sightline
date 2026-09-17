"""The host half of SIGHTLINE_ROM_DEBUG: one mailbox mirror, one decoder.

This module exists so that the two things that talk to the debug mailbox -
`tools/native/rominspect.py`, which replays a recording offline, and the LIVE
recorder in `record_libretro.py`, which talks to it while the owner plays -
cannot drift apart. The layout is mirrored from `src/game/sl_romdbg.h` exactly
once, here, and the ROM publishes its own sizeof so a drift is an error rather
than a plausible-looking wrong answer.

TRAPS THIS FILE EXISTS TO NOT REPEAT
------------------------------------
* RDRAM in this libretro core is WORD-BYTE-SWAPPED. Every read goes through
  sltrace.state.Memory and every write through Memory.w32(). There is no
  ad-hoc struct.unpack here on purpose: a pointer read big-endian came back
  0x60630b80 (invalid) where little-endian gave 0x800b6360 (valid), and the
  wrong answer looks like plausible garbage rather than an error.

* Host offsetof is not MIPS offsetof. Nothing here derives a struct layout from
  the host compiler; MB below is the ROM's own byte offsets, and the magic and
  size words are checked before any other field is believed.

* An instrument that cannot fail is worthless. `hook_calls` is the ROM's
  known-positive control, and LiveCapture adds the live one: a command that the
  ROM never acknowledges is REPORTED as unserved, never silently dropped.
"""

from __future__ import annotations

import ctypes
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[3]

# ---------------------------------------------------------------------------
# The mailbox, mirroring src/game/sl_romdbg.h. Offsets are the ROM's, byte for
# byte; if that header changes, change these together or the magic check is the
# only thing standing between you and confident nonsense.
# ---------------------------------------------------------------------------
MAGIC = 0x534C4442          # 'SLDB'
VERSION = 4

MB = {
    "magic":                 (0x00, "u32"),
    "version":               (0x04, "u32"),
    "size":                  (0x08, "u32"),
    "hook_calls":            (0x0C, "u32"),
    "host_seq":              (0x10, "u32"),
    "host_cmd":              (0x14, "u32"),
    "host_arg":              (0x18, "u32"),
    "rom_seq":               (0x20, "u32"),
    "rom_ack":               (0x24, "u32"),
    "frame":                 (0x28, "s32"),
    "event":                 (0x2C, "s32"),
    "watch_prop":            (0x30, "u32"),
    "watch_obj":             (0x34, "u32"),
    "watch_model":           (0x38, "u32"),
    "watch_objtype":         (0x3C, "s32"),
    "watch_proptype":        (0x40, "s32"),
    "watch_propflags":       (0x44, "s32"),
    "watch_onscreen_listed": (0x48, "s32"),
    "watch_zdepth":          (0x4C, "f32"),
    "watch_pos":             (0x50, "f32x3"),
    "watch_runtime_pos":     (0x5C, "f32x3"),
    "watch_rooms":           (0x68, "s32x4"),
    "watch_stan_room":       (0x78, "s32"),
    "watch_objflags":        (0x7C, "u32"),
    "watch_objflags2":       (0x80, "u32"),
    "watch_runtime_bitflags": (0x84, "u32"),
    "watch_damage":          (0x88, "f32"),
    "watch_maxdamage":       (0x8C, "f32"),
    "glass_tintdist":        (0x90, "s32"),
    "glass_culldist":        (0x94, "s32"),
    "glass_opacity":         (0x98, "s32"),
    "glass_portalnum":       (0x9C, "s32"),
    "glass_unk90":           (0xA0, "f32"),
    "portal_ctrl":           (0xA4, "s32"),
    "portal_room1":          (0xA8, "s32"),
    "portal_room2":          (0xAC, "s32"),
    "player_pos":            (0xB0, "f32x3"),
    "aim_origin":            (0xBC, "f32x3"),
    "aim_dir":               (0xC8, "f32x3"),
    "watch_dist":            (0xD4, "f32"),
    "player_room":           (0xD8, "s32"),
    "weapon":                (0xDC, "s32"),
    "onscreen_count":        (0xE0, "s32"),
    "nhits":                 (0xE4, "s32"),
    "watch_obj_sel":         (0xE8, "u32"),
    "watch_objtype_sel":     (0xEC, "s32"),
    "sight_mode":            (0xF0, "s32"),
    "sight_mp_menu":         (0xF4, "s32"),
}
HITS_OFF = 0xF8
HIT_STRIDE = 0x20
MAX_HITS = 10
EXPECTED_SIZE = HITS_OFF + HIT_STRIDE * MAX_HITS   # 0x238

CMD_SELECT, CMD_SNAPSHOT, CMD_CLEAR, CMD_FINDTYPE, CMD_NEAREST = 1, 2, 3, 4, 5
EV_NONE, EV_SAMPLE, EV_SELECT, EV_MISS, EV_FIND, EV_LOST, EV_NEAR = range(7)
EV_NAME = {EV_NONE: "none", EV_SAMPLE: "sample", EV_SELECT: "SELECT",
           EV_MISS: "MISS", EV_FIND: "FIND",
           EV_LOST: "LOST (the watched prop's slot was reused)",
           EV_NEAR: "NEAREST"}

# PropRecord.flags, src/bondconstants.h
PROPFLAG_ONSCREEN = 0x02
PROPFLAG_ENABLED = 0x04

# GUNSIGHTREASON_*, src/bondconstants.h:2866. gunDrawSight() draws the
# crosshair when and only when gunsightmode == 0 and mpmenuon is false, so a
# non-zero mask IS "no crosshair on screen", with the reason in the bits.
GUNSIGHT_REASONS = [
    (0x01, "reason 1"),
    (0x02, "not aiming"),
    (0x04, "no control (cutscene, death or the watch menu)"),
    (0x10, "taking damage"),
]

# InputFrame bit 14: the reserved DBG_SELECT. Recorded in the stream, never
# handed to the core. See src/game/sl_romdbg.h.
DBG_SELECT_BIT = 14

# PROPDEF_* ids that get a type-specific decoder. Glass first, because it is
# the live question. Everything else is named by propdef_name() from the enum
# itself - an earlier hand-written PROPDEF_DOOR = 4 sat here unused and WRONG
# (the enum says 1), which is what a second copy of a table is for.
PROPDEF_TINTED_GLASS = 47

_PROPDEF_NAMES: dict[int, str] | None = None


def propdef_name(objtype: int) -> str:
    """PROPDEF_* name for a type id, read from the enum rather than a copy.

    src/bondconstants.h is the definition; duplicating the list here would be a
    second table to keep in step, and a stale one would mislabel exactly the
    object this whole instrument exists to identify. Parsed once, and a failure
    to parse degrades to the bare number rather than to a wrong name.
    """
    global _PROPDEF_NAMES
    if _PROPDEF_NAMES is None:
        _PROPDEF_NAMES = {}
        try:
            text = (ROOT / "src/bondconstants.h").read_text(errors="replace")
            body = text.split("typedef enum PROPDEF_TYPE", 1)[1]
            body = body.split("}", 1)[0]
            n = 0
            for m in re.finditer(r"^\s*(PROPDEF_[A-Z0-9_]+)\s*,", body, re.M):
                _PROPDEF_NAMES[n] = m.group(1)[len("PROPDEF_"):]
                n += 1
        except Exception:
            _PROPDEF_NAMES = {}
    return _PROPDEF_NAMES.get(objtype, f"type{objtype}")


def sight_reason(box) -> str:
    """Why there is no crosshair, in the game's own words - or '' if there is."""
    mode = box["sight_mode"]
    if mode == 0 and not box["sight_mp_menu"]:
        return ""
    if box["sight_mp_menu"]:
        return "in the multiplayer menu"
    named = [name for bit, name in GUNSIGHT_REASONS if mode & bit]
    other = mode & ~sum(bit for bit, _ in GUNSIGHT_REASONS)
    if other:
        named.append(f"gunsightmode 0x{other:02x}")
    return " + ".join(named) if named else f"gunsightmode 0x{mode:02x}"


def read_field(mem, base, name):
    off, kind = MB[name]
    a = base + off
    if kind == "u32":
        return mem.u32(a)
    if kind == "s32":
        return mem.s32(a)
    if kind == "f32":
        return mem.f32(a)
    if kind == "f32x3":
        return [mem.f32(a + 4 * i) for i in range(3)]
    if kind == "s32x4":
        return [mem.s32(a + 4 * i) for i in range(4)]
    raise AssertionError(kind)


def read_mailbox(mem, base):
    box = {k: read_field(mem, base, k) for k in MB}
    hits = []
    for i in range(min(MAX_HITS, max(0, box["nhits"]))):
        h = base + HITS_OFF + i * HIT_STRIDE
        hits.append({
            "prop": mem.u32(h + 0x00),
            "obj": mem.u32(h + 0x04),
            "model": mem.u32(h + 0x08),
            "dist": mem.f32(h + 0x0C),
            "proptype": mem.s32(h + 0x10),
            "objtype": mem.s32(h + 0x14),
            "hitpart": mem.s32(h + 0x18),
            "penetrates": mem.s32(h + 0x1C),
        })
    box["hits"] = hits
    return box


def check_layout(mem, base) -> str:
    """Refuse to decode a mailbox this table does not describe.

    The offsets above are hand-mirrored from the ROM header, and a silently
    shifted field is exactly the confident-wrong answer this tool exists to
    remove. The ROM publishes its own sizeof; if it disagrees, stop.
    """
    got_size = mem.u32(base + MB["size"][0])
    got_ver = mem.u32(base + MB["version"][0])
    if got_size != EXPECTED_SIZE or got_ver != VERSION:
        raise SystemExit(
            f"romdbg: mailbox layout mismatch - ROM says size=0x{got_size:x} "
            f"version={got_ver}, this tool expects size=0x{EXPECTED_SIZE:x} "
            f"version={VERSION}. src/game/sl_romdbg.h and "
            f"tools/trace/sltrace/romdbg.py have drifted.")
    return f"mailbox v{got_ver}, size 0x{got_size:x} - layout OK"


def hexdump(mem, addr, n, indent="    "):
    """Raw N64-order bytes. The setup record as it stands in RDRAM."""
    out = []
    for row in range(0, n, 16):
        blk = mem.block(addr + row, 16)
        out.append(f"{indent}{addr + row:08x}  {blk.hex(' ')}")
    return "\n".join(out)


def write_ppm(path, fb, w, h, pitch):
    raw = ctypes.string_at(fb, pitch * h)
    rows = []
    for y in range(h):
        line = raw[y * pitch: y * pitch + w * 4]
        row = bytearray(w * 3)
        # XRGB8888 little-endian: memory order is B,G,R,X.
        row[0::3] = line[2::4]
        row[1::3] = line[1::4]
        row[2::3] = line[0::4]
        rows.append(bytes(row))
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(b"".join(rows))


def report(mem, box, index, out_dir, shot_name):
    """One capture, written as text a human reads and a diff compares."""
    L = []
    a = L.append
    a("=" * 74)
    a(f"rominspect capture   input index {index}   frame {box['frame']}")
    a(f"event: {EV_NAME.get(box['event'], box['event'])}")
    a("=" * 74)

    a("")
    a("-- the shooter, this frame ------------------------------------------")
    a(f"  player pos      ({box['player_pos'][0]:.1f}, {box['player_pos'][1]:.1f}, "
      f"{box['player_pos'][2]:.1f})   room {box['player_room']}")
    a(f"  aim origin      ({box['aim_origin'][0]:.1f}, {box['aim_origin'][1]:.1f}, "
      f"{box['aim_origin'][2]:.1f})     [world space, shotdata.gunpos]")
    a(f"  aim direction   ({box['aim_dir'][0]:.4f}, {box['aim_dir'][1]:.4f}, "
      f"{box['aim_dir'][2]:.4f})   [world space, shotdata.dir]")
    a(f"  weapon (ITEM)   {box['weapon']}")
    a(f"  on-screen props {box['onscreen_count']}")
    why = sight_reason(box)
    a(f"  crosshair       {'on screen' if not why else 'NOT on screen - ' + why}"
      f"   [gunsightmode 0x{box['sight_mode']:02x}]")

    a("")
    if box["event"] in (EV_FIND, EV_NEAR):
        a("-- on-screen props of the requested type, nearest first -------------")
        if not box["hits"]:
            a("  (none - no prop of that PROPDEF is in g_OnScreenPropList)")
        for i, h in enumerate(box["hits"]):
            mark = " <- watched" if h["prop"] == box["watch_prop"] else ""
            fl = h["hitpart"]
            a(f"  [{i}] prop 0x{h['prop']:08x}  obj 0x{h['obj']:08x}  "
              f"dist {h['dist']:9.1f}  objtype {h['objtype']} "
              f"{propdef_name(h['objtype'])}  "
              f"flags 0x{fl & 0xff:02x}"
              f"(ONSCREEN={'y' if fl & PROPFLAG_ONSCREEN else 'n'},"
              f"ENABLED={'y' if fl & PROPFLAG_ENABLED else 'n'})  "
              f"shot-gate {'PASS' if h['penetrates'] else 'FAIL'}{mark}")
        a("  shot-gate is the test at the top of sub_GAME_7F04E9BC. A prop that")
        a("  FAILS it is on screen and invisible to every shot, so SELECT can")
        a("  never name it however precisely you aim.")
        if box["event"] == EV_NEAR:
            a("  NEAREST, not FINDTYPE: this listing did NOT take over the")
            a("  watched prop, so a SELECT's identity survives it.")
    else:
        a("-- the game's own hit list, from sub_GAME_7F04E9BC ------------------")
        if not box["hits"]:
            a("  (empty - the crosshair intersected no prop)")
        for i, h in enumerate(box["hits"]):
            mark = " <- watched" if h["prop"] == box["watch_prop"] else ""
            a(f"  [{i}] prop 0x{h['prop']:08x}  obj 0x{h['obj']:08x}  "
              f"dist {h['dist']:9.1f}  proptype {h['proptype']}  "
              f"objtype {h['objtype']} {propdef_name(h['objtype'])}  "
              f"hitpart {h['hitpart']}  "
              f"penetrates {h['penetrates']}{mark}")
        a("  NOTE: maxdist is unbounded in the debug select - it does NOT test")
        a("        the background, so a prop behind a wall can appear here. That")
        a("        is why the whole list is printed, not only the nearest.")

    if not box["watch_prop"]:
        a("")
        a("-- no watched prop --------------------------------------------------")
        text = "\n".join(L) + "\n"
        (out_dir / f"capture-{index:06d}.txt").write_text(text)
        return text

    a("")
    a("-- the watched prop -------------------------------------------------")
    a(f"  PropRecord      0x{box['watch_prop']:08x}")
    a(f"  ObjectRecord    0x{box['watch_obj']:08x}   (this IS the setup record in RAM)")
    a(f"  Model           0x{box['watch_model']:08x}")
    a(f"  prop type       {box['watch_proptype']}")
    a(f"  obj type        {box['watch_objtype']}   "
      f"PROPDEF_{propdef_name(box['watch_objtype'])}")
    fl = box["watch_propflags"]
    a(f"  prop->flags     0x{fl & 0xff:02x}"
      f"   ENABLED={'yes' if fl & PROPFLAG_ENABLED else 'no '}"
      f"   ONSCREEN={'yes' if fl & PROPFLAG_ONSCREEN else 'no '}")
    a( "                  ONSCREEN is what posIsOnScreen() returned for this prop")
    a( "                  this frame (propobj.c objTick sets it from that call).")
    a(f"  in g_OnScreenPropList: {'yes' if box['watch_onscreen_listed'] else 'no'}")
    a(f"  zDepth          {box['watch_zdepth']:.3f}")
    a(f"  prop->pos       ({box['watch_pos'][0]:.1f}, {box['watch_pos'][1]:.1f}, "
      f"{box['watch_pos'][2]:.1f})")
    a(f"  runtime_pos     ({box['watch_runtime_pos'][0]:.1f}, "
      f"{box['watch_runtime_pos'][1]:.1f}, {box['watch_runtime_pos'][2]:.1f})")
    rooms = [r for r in box["watch_rooms"] if r != 0xFF]
    a(f"  rooms[]         {box['watch_rooms']}  -> {rooms}")
    a(f"  stan room       {box['watch_stan_room']}")
    a(f"  obj->flags      0x{box['watch_objflags']:08x}")
    a(f"  obj->flags2     0x{box['watch_objflags2']:08x}")
    a(f"  runtime_bitflags 0x{box['watch_runtime_bitflags']:08x}")
    a(f"  damage          {box['watch_damage']:.2f} / {box['watch_maxdamage']:.2f}")
    a(f"  distance to player  {box['watch_dist']:.1f}")
    a( "                  MEASURED FROM THE PLAYER PROP, not the camera - which")
    a( "                  is what glassCalculateOpacity uses. During a cutscene")
    a( "                  camera the two are thousands of units apart.")

    if box["watch_objtype"] == PROPDEF_TINTED_GLASS:
        a("")
        a("-- tinted glass -----------------------------------------------------")
        op = box["glass_opacity"]
        a(f"  TintDist        {box['glass_tintdist']}")
        a(f"  CullDist        {box['glass_culldist']}")
        a(f"  unk90           {box['glass_unk90']:.4f}")
        a(f"  calculatedopacity {op}"
          + ("   (0xFF - fully opaque)" if op == 0xFF else ""))
        a(f"  portalnum       {box['glass_portalnum']}")
        if box["glass_portalnum"] >= 0:
            ctrl = box["portal_ctrl"]
            a(f"  portal controlbytes1 0x{ctrl & 0xff:02x}"
              f"   bit0={'set -> portal DISABLED' if ctrl & 1 else 'clear -> portal enabled'}")
            a(f"  connected rooms {box['portal_room1']} <-> {box['portal_room2']}")
        else:
            a("  portal          none (portalnum < 0)")
        a("")
        a("  The mechanism, for reference (propobj.c objTick):")
        a("    calculatedopacity = glassCalculateOpacity(runtime_pos, TintDist,")
        a("                                              CullDist, unk90)")
        a("    opacity == 0xFF  ->  bgToggleDataPortalsContrlBytes1Bit1(portal, 0)")
        a("    otherwise        ->  bgToggleDataPortalsContrlBytes1Bit1(portal, 1)")
        a("  Nothing in this tool changes any of those; it only reads them.")

    if box["watch_obj"]:
        a("")
        a("-- setup record, raw (ObjectRecord 0x00..0x80, glass tail to 0x94) --")
        n = 0x94 if box["watch_objtype"] == PROPDEF_TINTED_GLASS else 0x80
        a(hexdump(mem, box["watch_obj"], n))

    if box["watch_model"] and mem.valid_ptr(box["watch_model"]):
        mf = mem.u32(box["watch_model"] + 0x08)      # Model->obj
        a("")
        a("-- model ------------------------------------------------------------")
        a(f"  Model->obj (ModelFileHeader) 0x{mf:08x}")
        if mem.valid_ptr(mf):
            a(f"  RootNode      0x{mem.u32(mf + 0x00):08x}")
            a(f"  Skeleton      0x{mem.u32(mf + 0x04):08x}")
            a(f"  numSwitches   {mem.s16(mf + 0x0C)}")
            a(f"  numMatrices   {mem.s16(mf + 0x0E)}")
            a(f"  BoundingRadius {mem.f32(mf + 0x10):.2f}")
            a(f"  numtextures   {mem.s16(mf + 0x16)}")
            a(f"  Textures      0x{mem.u32(mf + 0x18):08x}")
        a(f"  render_pos    0x{mem.u32(box['watch_model'] + 0x0C):08x}")
        a(f"  scale         {mem.f32(box['watch_model'] + 0x14):.4f}")

    a("")
    a("-- NOT captured, and why --------------------------------------------")
    a("  Combiner words, simplified RGB/alpha equations, PRIM/ENV/SHADE, the")
    a("  blender mode, fog state, texture statistics and triangles emitted are")
    a("  NOT here. On the cartridge those live in a display list the game hands")
    a("  to the RSP; recovering them means finding and walking that DL in RDRAM,")
    a("  which is a second decoder with its own failure modes. The native")
    a("  renderer already decodes exactly those (SL_ALPHA_EQ, sl_gfx_dl.c), and")
    a("  this capture's job is to say WHICH object the native side should be")
    a("  compared against. Treat their absence here as unbuilt, not as absent.")
    if shot_name:
        a(f"  framebuffer: {shot_name}")

    text = "\n".join(L) + "\n"
    (out_dir / f"capture-{index:06d}.txt").write_text(text)
    return text


CSV_COLUMNS = [
    "index", "frame", "prop", "obj", "objtype", "dist",
    "opacity", "portalnum", "portal_ctrl", "portal_disabled",
    "onscreen_flag", "onscreen_listed", "enabled_flag", "zdepth",
    "player_x", "player_y", "player_z",
    "prop_x", "prop_y", "prop_z", "rooms", "player_room", "onscreen_count",
]


def csv_row(index, box):
    ctrl = box["portal_ctrl"]
    fl = box["watch_propflags"]
    return ",".join(str(v) for v in [
        index, box["frame"], f"0x{box['watch_prop']:08x}",
        f"0x{box['watch_obj']:08x}", box["watch_objtype"],
        f"{box['watch_dist']:.2f}",
        box["glass_opacity"], box["glass_portalnum"], ctrl,
        (1 if (ctrl >= 0 and ctrl & 1) else 0) if ctrl >= 0 else -1,
        1 if fl & PROPFLAG_ONSCREEN else 0,
        box["watch_onscreen_listed"],
        1 if fl & PROPFLAG_ENABLED else 0,
        f"{box['watch_zdepth']:.3f}",
        f"{box['player_pos'][0]:.1f}", f"{box['player_pos'][1]:.1f}",
        f"{box['player_pos'][2]:.1f}",
        f"{box['watch_runtime_pos'][0]:.1f}", f"{box['watch_runtime_pos'][1]:.1f}",
        f"{box['watch_runtime_pos'][2]:.1f}",
        "|".join(str(r) for r in box["watch_rooms"]),
        box["player_room"], box["onscreen_count"],
    ])


# ---------------------------------------------------------------------------
# LIVE capture: the mailbox driven while a human plays, not while a stream is
# replayed. This is what turns "I pressed Create and the terminal said nothing"
# into a line per press.
# ---------------------------------------------------------------------------

#: Frames to wait for the ROM to acknowledge a command before declaring it
#: unserved. The hook only runs on the in-game path - measured at roughly a
#: third of emulator frames - so a couple of frames of delay is NORMAL and a
#: press in a menu or a cutscene is never acknowledged at all. Three seconds is
#: far beyond the former and decisive about the latter.
SERVE_TIMEOUT_FRAMES = 180


class LiveCapture:
    """Drive the debug mailbox from inside the recorder and narrate it.

    One command is outstanding at a time, by construction: the ROM's ack is a
    single word, so overlapping commands would make "which press did this
    answer" a guess - and guessing object identity is the thing this whole
    instrument was built to delete.
    """

    def __init__(self, base, out_dir=None, emit=print, aid_type=None,
                 aid_every=120, miss_detail=True, framebuffer=None):
        self.base = base
        self.out_dir = pathlib.Path(out_dir) if out_dir else None
        if self.out_dir:
            self.out_dir.mkdir(parents=True, exist_ok=True)
        self.emit = emit
        #: PROPDEF to keep an eye on, or None. Default OFF: it costs a command
        #: every aid_every frames and every command perturbs the run.
        self.aid_type = aid_type
        self.aid_every = aid_every
        self.miss_detail = miss_detail
        self.framebuffer = framebuffer

        self.captures = 0
        self.presses = 0
        self.hits = 0
        self.unserved = 0
        self.checked = False
        self._seq = 0
        self._pending = None        # (seq, kind, index, meta)
        self._queued_select = None
        self._prev_pressed = False
        self._last_rom_seq = -1
        self._last_aid_frame = -10 ** 9
        self._last_aid_text = None

    # -- host -> ROM, before retro_run -------------------------------------

    def before_frame(self, index, frame, mem):
        """Poke at most one command. Called after the pad read, before the frame.

        The ROM reads the command at the top of its hook, so the poke has to be
        in RDRAM already; doing it after retro_run would answer a frame late,
        which is precisely the off-by-one that made the first recorder useless.
        """
        pressed = frame.pressed(DBG_SELECT_BIT)
        edge = pressed and not self._prev_pressed
        self._prev_pressed = pressed
        if edge:
            # Held presses collapse to one capture. That is correct: the owner
            # holds the button for as long as it takes to notice they pressed it.
            self.presses += 1
            if self._pending is None:
                self._issue(mem, CMD_SELECT, 0, "select", index)
            else:
                self._queued_select = index
            return
        if self._pending is not None:
            return
        if self._queued_select is not None:
            self._queued_select = None
            self._issue(mem, CMD_SELECT, 0, "select", index)
            return
        if self.aid_type is not None and index - self._last_aid_frame >= self.aid_every:
            self._last_aid_frame = index
            self._issue(mem, CMD_NEAREST, self.aid_type, "aid", index)

    def _issue(self, mem, cmd, arg, kind, index):
        self._seq += 1
        mem.w32(self.base + MB["host_arg"][0], arg & 0xFFFFFFFF)
        mem.w32(self.base + MB["host_cmd"][0], cmd)
        mem.w32(self.base + MB["host_seq"][0], self._seq)
        self._pending = {"seq": self._seq, "kind": kind, "index": index,
                         "issued": index}

    # -- ROM -> host, after retro_run --------------------------------------

    def after_frame(self, index, mem):
        if mem.u32(self.base + MB["magic"][0]) != MAGIC:
            return
        if not self.checked:
            self.checked = True
            self.emit(f"  {check_layout(mem, self.base)}")
        rom_seq = mem.u32(self.base + MB["rom_seq"][0])
        fresh = rom_seq != self._last_rom_seq
        self._last_rom_seq = rom_seq
        p = self._pending
        if p is None:
            return
        if fresh and mem.u32(self.base + MB["rom_ack"][0]) == p["seq"]:
            self._pending = None
            self._resolve(index, mem, p)
            return
        if index - p["issued"] >= SERVE_TIMEOUT_FRAMES:
            self._pending = None
            if p["kind"] != "aid":
                self.unserved += 1
                self.captures += 1
                self.emit(
                    f"capture {self.captures}  frame ?      NOT SERVED  "
                    f"the press was recorded, but the game never ran the debug "
                    f"hook - you were in a menu, a cutscene or paused, so there "
                    f"was nothing to aim at.")

    def _resolve(self, index, mem, pending):
        box = read_mailbox(mem, self.base)
        kind = pending["kind"]
        if kind == "aid":
            self._say_aid(box)
            return
        if kind == "missdetail":
            self._say_miss_detail(box)
            return

        self.captures += 1
        n = self.captures
        shot = self._shoot(index)
        if self.out_dir:
            report(mem, box, index, self.out_dir, shot)

        if box["event"] == EV_SELECT and box["hits"]:
            self.hits += 1
            h = min(box["hits"], key=lambda x: x["dist"])
            line = (f"capture {n}  frame {box['frame']}  HIT   "
                    f"prop 0x{h['prop']:08x}  "
                    f"type 0x{h['objtype'] & 0xff:02x} {propdef_name(h['objtype'])}  "
                    f"dist {h['dist']:.1f}")
            if box["watch_objtype"] == PROPDEF_TINTED_GLASS:
                line += (f"  opacity {box['glass_opacity']}"
                         f"  portal {box['glass_portalnum']}"
                         f"  playerdist {box['watch_dist']:.1f}")
            self.emit(line)
            if len(box["hits"]) > 1:
                rest = ", ".join(
                    f"{propdef_name(x['objtype'])} {x['dist']:.0f}"
                    for x in sorted(box["hits"], key=lambda x: x["dist"])[1:4])
                self.emit(f"          also on the ray: {rest}")
        else:
            why = sight_reason(box)
            if why:
                self.emit(f"capture {n}  frame {box['frame']}  MISS  ({why} "
                          f"- no crosshair)")
            else:
                self.emit(f"capture {n}  frame {box['frame']}  MISS  "
                          f"(crosshair on no prop; {box['onscreen_count']} props "
                          f"on screen)")
                if self.miss_detail and self._pending is None:
                    # Answer "what WAS on screen" straight away, while the owner
                    # is still standing there. NEAREST, so it cannot take over
                    # the watched prop from an earlier SELECT.
                    self._issue(mem, CMD_NEAREST, -1, "missdetail", index)

    def _say_miss_detail(self, box):
        if not box["hits"]:
            self.emit("          nothing at all in g_OnScreenPropList")
            return
        rest = ", ".join(f"{propdef_name(h['objtype'])} {h['dist']:.0f}"
                         for h in box["hits"][:5])
        self.emit(f"          on screen, nearest first: {rest}")

    def _say_aid(self, box):
        if box["hits"]:
            h = box["hits"][0]
            text = (f"nearest {propdef_name(h['objtype'])} {h['dist']:.0f} units "
                    f"(prop 0x{h['prop']:08x}, "
                    f"{'shootable' if h['penetrates'] else 'NOT shootable'})")
        else:
            text = f"no {propdef_name(self.aid_type)} on screen"
        # Only when it changes. A line every two seconds saying the same thing
        # is noise, and noise during play is what makes an aid get switched off.
        if text != self._last_aid_text:
            self._last_aid_text = text
            self.emit(f"  aim aid: {text}")

    def _shoot(self, index):
        if not self.out_dir or self.framebuffer is None:
            return None
        fb = self.framebuffer()
        if not fb or fb[0] is None:
            return None
        data, w, h, pitch = fb
        name = f"shot-{index:06d}.ppm"
        try:
            write_ppm(self.out_dir / name, data, w, h, pitch)
        except Exception as exc:                  # never lose a take to a shot
            self.emit(f"  (framebuffer not written: {exc})")
            return None
        return name

    def summary(self) -> str:
        return (f"{self.presses} Create presses, {self.captures} captures, "
                f"{self.hits} hits, {self.unserved} never served")
