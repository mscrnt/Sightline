"""Per-tick game state capture and hashing.

Schema covers what ROADMAP Phase 0 requires: entity positions and rotations,
health, weapon state, RNG cursor, alarm and objective flags, animation frames.

Two things worth knowing about how this reads memory:

1. mupen64plus stores RDRAM with 32-bit words in HOST byte order. A naive byte
   read of a u32 comes back reversed, which is why every read goes through
   _word_swap. Getting this wrong yields values that look like plausible garbage
   rather than an obvious error.

2. State is captured per GAME TICK (currentFrameCounter advancing), never per
   emulator VI interrupt. The game's logic loop is gated on osGetCount(), so
   VI-keyed sampling lands at a different point inside the update each run and
   produces constant false divergence.

Storage strategy: the trace records a composite hash per tick plus one hash per
entity. That is small enough to commit, localises a divergence to a specific
entity, and - because replay is deterministic - full field values can always be
recovered by re-running with capture_detail=True around the divergent tick.
"""

from __future__ import annotations

import hashlib
import math
import struct
from dataclasses import dataclass, field

SCHEMA_VERSION = 13  # v13: player ammo, hands, hit registers; camera and turret aim

KSEG0 = 0x80000000
# sizeof(ChrRecord). MEASURED, not taken from the header: 0x180 was wrong, and
# because the error scales with the slot index, slot 0 always read correctly
# while every other slot landed in the middle of its neighbour.
#
# At tick 4000 of a Dam sweep, over 46 pool slots:
#   0x180 -> 1 slot with sane fields, 4 actiontypes in range
#   0x1DC -> 33 slots with sane fields, 33 actiontypes in range, chrnum
#            running 0, 1, 5037... and maxdamage a consistent 4.0
#
# Everything strange about the entity data traces back here: invalid
# actiontypes, maxdamage of 1e35, positions containing nan, "characters" that
# never moved, and ACT_INIT (enum 0) dominating because zeroed misaligned
# memory reads as it. Three rounds of filtering in v5-v7 were attempts to cope
# with the symptom.
#
# The gate was never wrong - misaligned memory is still deterministic, which is
# exactly why 97 traces passed while hashing it.
CHR_RECORD_SIZE = 0x1DC
MAX_CHRS = 256                   # sanity bound; real levels are far below this

# ChrRecord field offsets (src/bondtypes.h)
CHR_CHRNUM = 0x000               # s16
CHR_CHRFLAGS = 0x014             # u32
CHR_MODEL = 0x01C                # Model*
CHR_FALLSPEED = 0x0B0            # coord3d
CHR_PREVPOS = 0x0BC              # coord3d
CHR_LASTKNOWNTARGETPOS = 0x0D8   # coord3d - AI knowledge, not just physics
CHR_DAMAGE = 0x0FC               # f32
CHR_MAXDAMAGE = 0x100            # f32
CHR_TIMER60 = 0x110              # s32
CHR_WEAPONS_HELD = 0x160         # PropRecord*[2]

# AI state. Positions and health prove determinism; these are what a Phase 1
# regression would actually move. Perception and reaction fields matter most:
# the alarm graph is built out of who saw or heard what, and when.
# Appearance, which identifies WHAT a character is - guard, scientist,
# civilian or a named character. Read for reporting only: deliberately absent
# from _ENTITY_FIELDS, so adding it does not change any hash or stale a trace.
CHR_HEADNUM = 0x006              # s8
CHR_BODYNUM = 0x00F              # s8
CHR_ACCURACYRATING = 0x002       # s8
CHR_SPEEDRATING = 0x003          # s8
CHR_FIRECOUNT = 0x004            # u8[2]
CHR_ACTIONTYPE = 0x007           # ACT_TYPE, 8-bit bitfield
CHR_SLEEP = 0x008                # s8
CHR_INVALIDMOVE = 0x009          # s8
CHR_NUMCLOSEARGHS = 0x00A        # s8 - hit reactions at close range
CHR_NUMARGHS = 0x00B             # s8
CHR_ARGHRATING = 0x00D           # s8
CHR_AIMENDCOUNT = 0x00E          # s8
CHR_GRENADEPROB = 0x010          # u8
CHR_FLINCHCNT = 0x011            # s8
CHR_HIDDEN = 0x012               # u16
CHR_LASTWALK60 = 0x0C8           # s32
CHR_LASTMOVEOK60 = 0x0CC         # s32
CHR_VISIONRANGE = 0x0D0          # f32
CHR_LASTSEETARGET60 = 0x0D4      # s32 - when this chr last SAW its target
CHR_LASTSHOOTER = 0x0E8          # s16 - who shot it; squad communication
CHR_TIMESHOOTER = 0x0EA          # s16
CHR_HEARINGSCALE = 0x0EC         # f32
CHR_LASTHEARTARGET60 = 0x0F0     # s32 - when it last HEARD its target
CHR_AIOFFSET = 0x108             # u16 - position within the AI script
CHR_AIRETURNLIST = 0x10A         # s16
# One guard's knowledge of another's fate - information propagation, the alarm
# graph in miniature. Fire rarely (a handful of samples per level), which is
# exactly what a regression breaks without anything noticing. morale (0x10C)
# and alertness (0x10D) are deliberately absent: measured across four levels,
# the shipped AI scripts never set either - always zero.
CHR_CHRSEESHOT = 0x118           # s16 - chrnum seen shooting, -1 none
CHR_CHRSEEDIE = 0x11A            # s16 - chrnum seen dying, -1 none

# World props. The game walks g_ActivePropsTail via prev (propsTick), and so
# do we. PROP_TYPE enum: NUL=0 OBJ=1 DOOR=2 CHR=3 WEAPON=4 PLAYER=5 VIEWER=6
# EXPLOSION=7 SMOKE=8. CHR/PLAYER/VIEWER are skipped here - the chr pool and
# player are hashed separately.
PROP_TYPE_OFF = 0x00             # u8 - union discriminator
PROP_FLAGS_OFF = 0x01            # u8
PROP_TIMETOREGEN_OFF = 0x02      # s16
PROP_UNION_OFF = 0x04            # ObjectRecord*/DoorRecord*/WeaponObjRecord*
PROP_POS_OFF = 0x08              # coord3d
PROP_PREV_OFF = 0x24             # PropRecord*
# ObjectRecord (via the union): state is the byte that says destroyed/
# activated/damaged - what happened to an alarm panel, a camera, a crate.
OBJ_STATE_OFF = 0x02             # u8 bitmask: 0x80 destroyed, 0x01 damaged
OBJ_ID_OFF = 0x04                # s16 - PROP_* identity (PROP_ALARM1, PROP_CCTV...)
OBJ_FLAGS_OFF = 0x08             # u32
OBJ_FLAGS2_OFF = 0x0C            # u32
DOOR_FRAC_OFF = 0xAC             # f32 - current travel
DOOR_OPENSTATE_OFF = 0xBC        # s8 - 0 closed, 1..3 moving states
WEAPON_NUM_OFF = 0x80            # s8 - ITEM_IDS of a dropped weapon
WEAPON_TIMER_OFF = 0x82          # s16
#: Walk guard: props per tick measured at 200-290; a corrupt list must not spin.
MAX_PROPS = 2048
#: Reserved entity keys. Player rides at 0xFFFF; all world props fold into one
#: composite digest at 0xFFFE - per-prop hashes would multiply trace size ~40x,
#: and field-level localisation comes from detail replay anyway.
PROPS_KEY = 0xFFFE

# Model field offsets (src/bondtypes.h)
MODEL_RENDER_POS = 0x0C          # RenderPosView*
MODEL_SCALE = 0x14               # f32
MODEL_ANIMFRAME1 = 0x28          # f32
MODEL_ANIMFRAME2 = 0x58          # f32

# struct player (src/game/bondview.h). The PLAYER is not a ChrRecord, so a
# schema built only from g_ChrSlots misses Bond entirely - position, health,
# armour, everything. A replay where the player ends up somewhere else while
# the guards behave identically then hashes as "almost identical", which is the
# worst possible failure for an oracle: confident and wrong.
PLAYER_POS = 0x004                # coord3d pos
PLAYER_POS2 = 0x010               # coord3d pos2
PLAYER_MODEL_POS = 0x038          # coord3d current_model_pos
PLAYER_ROOM_POS = 0x050           # coord3d current_room_pos
PLAYER_CROUCHPOS = 0x09C          # s32
PLAYER_VERT_BOUNCE = 0x090        # f32
PLAYER_BONDDEAD = 0x0D8           # s32
PLAYER_HEALTH = 0x0DC             # f32
PLAYER_ARMOUR = 0x0E0             # f32

GLOBAL_SYMBOLS = [
    "g_randomSeed",
    "g_CurrentPlayer",
    "currentFrameCounter",
    "g_ChrSlots",
    "g_NumChrSlots",
    # The array the game itself ticks (chraction.c walks g_ActiveChrs[0..count)
    # calling chrlvActionTick on each). g_ChrSlots is the allocation pool and
    # is NOT the set of live characters - measured on Facility, 75 slots with
    # 15 guards active gave only 4 passing a model!=0 test and 1 surviving the
    # sanity filters. A schema built on it hashes almost no AI at all.
    "g_ActiveChrs",
    "g_ActiveChrsCount",
    "g_ActivePropsTail",
    "alarm_timer",
    "objective_count",
    "objectiveregisters1",
    # v12. objective_count plus one register word is far too coarse: it cannot
    # tell "objective 3 failed" from "objective 3 still running". The runtime
    # keeps a status per objective, OBJECTIVES_MAX (10) of them, each a 4-byte
    # OBJECTIVESTATUS (0 incomplete, 1 complete, 2 failed). Sizes here are not
    # counted by hand - the ELF gives objectiveStatuses 0x28 bytes and
    # g_CheatActivated 0x50, which is what fixes them at 10 and CHEAT_MAX=80.
    # v13. Per-player permanent record; shot_count[7] is its first field.
    # Reached as a pointer, like g_CurrentPlayer - both are NULL when read
    # through the wrong map, which is how the player went unhashed for the
    # whole corpus. See _map_for() in tools/trace/trace.py.
    "g_playerPerm",
    "objectiveStatuses",
    # Cheats change damage, physics and spawns, so a run with one active is a
    # different simulation; a cheat toggled mid-run would otherwise read as an
    # unexplained divergence. One byte per cheat.
    "g_CheatActivated",
]

OBJECTIVE_SLOTS = 10        # ELF: objectiveStatuses is 0x28 bytes / 4
CHEAT_SLOTS = 80            # ELF: g_CheatActivated is 0x50 bytes, u8 each

# v13 ---------------------------------------------------------------------
# Ammo is a pure simulation quantity that moves on every shot and pickup, which
# makes it one of the sharpest divergence detectors available. It is not a
# global - it hangs off the player, after gunsightmode (0x1128) and
# field_112C, whose name confirms the offset.
PLAYER_AMMO_OFF, PLAYER_AMMO_N = 0x1130, 30
# hands[2] at 0x870. Field names inside struct hand encode their own absolute
# offsets (field_87D, field_884, field_8A0), which is what fixes weaponnum at
# +0x00 and weapon_ammo_in_magazine at +0x2C. The STRIDE is derived, not
# annotated: struct hand runs to field_A48 and gunposamplitude lands at 0xFC0,
# leaving 0x3A8 per hand.
PLAYER_HAND0, PLAYER_HAND_STRIDE = 0x870, 0x3A8
HAND_WEAPONNUM, HAND_MAGAZINE = 0x00, 0x2C
# g_playerPerm->shot_count[7]: total, head, body, limb, gun, hat, object.
# HIT_GUN and HIT_HAT do zero damage, so these separate "the AI got shot" from
# "the AI got shot somewhere that mattered" - which a health delta cannot.
SHOT_REGISTERS = 7
# Cameras and turrets already appear in the prop digest as objects, with their
# destroyed/activated state. What was missing is where they POINT.
PROPDEF_TYPE_OFF = 0x03          # PropDefHeaderRecord.type
PROPDEF_CCTV, PROPDEF_AUTOGUN = 6, 13
CCTV_ROT_OFF = 0xC8              # f32, integrated as unkC8 += unkD8 * dt
CCTV_TIMER_OFF = 0xE0            # s32, += g_ClockTimer only while it sees you
AUTOGUN_ROT_OFF = 0x84           # f32 rot_related
AUTOGUN_ACTIVE_OFF = 0xD0        # s32 is_active


def _word_swap(raw: bytes) -> bytes:
    """RDRAM holds 32-bit words in host order; swap each word to true value."""
    out = bytearray(len(raw))
    for i in range(0, len(raw) - 3, 4):
        out[i:i + 4] = raw[i:i + 4][::-1]
    return bytes(out)


class Memory:
    """Reads RDRAM through the emulator's debug pointer."""

    def __init__(self, base, size: int = 0x800000):
        self._base = base
        self._size = size

    def _off(self, addr: int) -> int:
        off = addr - KSEG0
        if not (0 <= off < self._size):
            raise ValueError(f"address 0x{addr:08x} outside RDRAM")
        return off

    def block(self, addr: int, n: int) -> bytes:
        o = self._off(addr)
        return _word_swap(bytes(self._base[o:o + n]))

    def u8(self, addr: int) -> int:
        return self.block(addr & ~3, 4)[addr & 3]

    def s8(self, addr: int) -> int:
        v = self.u8(addr)
        return v - 0x100 if v & 0x80 else v

    def u16(self, addr: int) -> int:
        return struct.unpack(">H", self.block(addr & ~3, 4)[addr & 3:(addr & 3) + 2])[0]

    def u32(self, addr: int) -> int:
        return struct.unpack(">I", self.block(addr, 4))[0]

    def s32(self, addr: int) -> int:
        return struct.unpack(">i", self.block(addr, 4))[0]

    def s16(self, addr: int) -> int:
        # s16 lives in the high or low half of a swapped word; read the word.
        return struct.unpack(">h", self.block(addr & ~3, 4)[addr & 3:(addr & 3) + 2])[0]

    def f32(self, addr: int) -> float:
        return struct.unpack(">f", self.block(addr, 4))[0]

    def u64(self, addr: int) -> int:
        return struct.unpack(">Q", self.block(addr, 8))[0]

    def valid_ptr(self, p: int) -> bool:
        return KSEG0 <= p < KSEG0 + self._size

    def w32(self, addr: int, value: int) -> None:
        """Write one 32-bit word into RDRAM, through the same swap as reads.

        The ONE writer, deliberately alongside the one reader. Every host->ROM
        poke in this repository goes through here for the same reason every
        read goes through block(): an ad-hoc struct.pack('>I', ...) writes the
        word backwards, and the symptom is not a crash but a command the game
        never sees. That reads as 'the instrument does nothing', which is the
        most expensive failure this codebase has.

        The ROM must invalidate its data cache before reading anything written
        here - see slRomDbgHostRead() in src/game/lv.c.
        """
        o = self._off(addr)
        # Byte at a time: self._base is a ctypes POINTER(c_ubyte), whose
        # slice assignment wants a list of ints rather than bytes.
        for i, b in enumerate(struct.pack("<I", value & 0xFFFFFFFF)):
            self._base[o + i] = b


@dataclass
class TickState:
    """One sampled game tick."""
    tick: int
    composite: bytes                       # hash over everything below
    entity_hashes: dict[int, bytes] = field(default_factory=dict)
    globals_hash: bytes = b""
    detail: dict | None = None             # populated only when capturing detail


#: (struct format, field) pairs hashed for every character, in a fixed order.
#: Keeping them paired means the format can never drift out of step with the
#: values it describes.
_ENTITY_FIELDS = (
    (">h", "chrnum"), (">I", "chrflags"),
    (">f", "damage"), (">f", "maxdamage"),
    (">i", "timer60"), (">I", "weapon_r"), (">I", "weapon_l"),
    (">f", "animframe1"), (">f", "animframe2"), (">f", "scale"),
    # AI state
    (">B", "actiontype"), (">b", "accuracyrating"), (">b", "speedrating"),
    (">B", "firecount0"), (">B", "firecount1"),
    (">b", "sleep"), (">b", "invalidmove"),
    (">b", "numclosearghs"), (">b", "numarghs"), (">b", "arghrating"),
    (">b", "aimendcount"), (">B", "grenadeprob"), (">b", "flinchcnt"),
    (">H", "hidden"),
    (">i", "lastwalk60"), (">i", "lastmoveok60"),
    (">f", "visionrange"), (">i", "lastseetarget60"),
    (">h", "lastshooter"), (">h", "timeshooter"),
    (">f", "hearingscale"), (">i", "lastheartarget60"),
    (">H", "aioffset"), (">h", "aireturnlist"),
    (">h", "chrseeshot"), (">h", "chrseedie"),
)


def _weapon_identity(mem, propaddr: int) -> int:
    """A held weapon's stable identity, not its address.

    v10 hashed the raw PropRecord pointer, which can never agree across
    backends - so every armed guard mismatched by construction and the chr
    hashes verified nothing.  The ObjectRecord carries a setup identity (the
    PROP_* value) that is the same number everywhere.

    Absent slot is 0xFFFFFFFF.  The slot legitimately CHANGES mid-run: guards
    drop what they hold (chrDropItems, AI_ChrDropAllHeldItems), so a differing
    value here is a real behavioural difference, not noise.
    """
    if not mem.valid_ptr(propaddr):
        return 0xFFFFFFFF
    obj = mem.u32(propaddr + PROP_UNION_OFF)
    if not mem.valid_ptr(obj):
        return 0xFFFFFFFF
    return mem.s16(obj + OBJ_ID_OFF) & 0xFFFFFFFF


class StateReader:
    def __init__(self, symbols):
        symbols.require(GLOBAL_SYMBOLS)
        self.sym = symbols
        self._a = {n: symbols.addr(n) for n in GLOBAL_SYMBOLS}

    def frame_counter(self, mem: Memory) -> int:
        return mem.s32(self._a["currentFrameCounter"])

    @staticmethod
    def _player_is_plausible(d: dict) -> bool:
        """Is this actually a player, or memory that is not one yet?

        g_CurrentPlayer can hold a pointer that passes a range check while the
        struct behind it is uninitialised - during boot it yields NaN positions
        and nonsense health. Hashing that produces run-to-run differences with
        no relation to the game, which is indistinguishable from a real
        divergence in the player's state. Validate the values, not just the
        pointer.
        """
        nums = list(d["pos"]) + list(d["model_pos"]) + [d["health"], d["armour"]]
        if any(not math.isfinite(v) for v in nums):
            return False
        if any(abs(v) > 1e6 for v in d["pos"]):
            return False
        # Health is a small positive scalar in this game; anything else means
        # we are looking at memory that is not a player.
        if not (0.0 <= d["health"] <= 1e4 and 0.0 <= d["armour"] <= 1e4):
            return False
        # v13. Health and position do NOT notice the level tearing down: on dam
        # 76 ticks of 22,686 (frames 24248..24297, the end of the run) kept a
        # valid pointer and sane position and health while ammo read +/-1e9.
        # Ammo is the field that notices, so it is part of the judgement rather
        # than something read after it. -1 is the codebase's "unset" sentinel,
        # the same convention as the weapon fuse timers, so it is legitimate;
        # the counts themselves are small (a full pickup is ~120).
        if any(v < -1 or v > 10000 for v in d.get("ammo", ())):
            return False
        return all(-1 <= m <= 10000 for _, m in d.get("hands", ()))

    def _read_player(self, mem: Memory) -> dict:
        """Bond's own state, which lives outside the chr table."""
        p = mem.u32(self._a["g_CurrentPlayer"])
        if not mem.valid_ptr(p):
            return {}
        d = {
            "pos": [mem.f32(p + PLAYER_POS + i * 4) for i in range(3)],
            "model_pos": [mem.f32(p + PLAYER_MODEL_POS + i * 4) for i in range(3)],
            "room_pos": [mem.f32(p + PLAYER_ROOM_POS + i * 4) for i in range(3)],
            "crouchpos": mem.s32(p + PLAYER_CROUCHPOS),
            "vert_bounce": mem.f32(p + PLAYER_VERT_BOUNCE),
            "dead": mem.s32(p + PLAYER_BONDDEAD),
            "health": mem.f32(p + PLAYER_HEALTH),
            "armour": mem.f32(p + PLAYER_ARMOUR),
        }
        # v13 fields are read BEFORE the plausibility gate, because they are
        # part of what the gate has to judge. Measured on dam: 76 ticks of
        # 22,686 - frames 24248..24297, the level tearing down at the end of
        # the run - kept a valid pointer and plausible position and health
        # while ammo read as +/-1e9. Position and health alone do not notice
        # that, so gating on them let garbage into the hash.
        d["ammo"] = list(struct.unpack(
            f">{PLAYER_AMMO_N}i",
            mem.block(p + PLAYER_AMMO_OFF, PLAYER_AMMO_N * 4)))
        d["hands"] = []
        for h in range(2):
            base = p + PLAYER_HAND0 + h * PLAYER_HAND_STRIDE
            d["hands"].append((mem.s32(base + HAND_WEAPONNUM),
                               mem.s32(base + HAND_MAGAZINE)))
        perm = mem.u32(self._a["g_playerPerm"])
        d["shot_count"] = (
            list(struct.unpack(f">{SHOT_REGISTERS}i",
                               mem.block(perm, SHOT_REGISTERS * 4)))
            if mem.valid_ptr(perm) else [0] * SHOT_REGISTERS)
        return d if self._player_is_plausible(d) else {}

    def _read_globals(self, mem: Memory) -> tuple[bytes, dict]:
        vals = {
            "rng_seed": mem.u64(self._a["g_randomSeed"]),
            "alarm_timer": mem.s32(self._a["alarm_timer"]),
            "objective_count": mem.s32(self._a["objective_count"]),
            "objective_registers": mem.s32(self._a["objectiveregisters1"]),
            "num_chr_slots": mem.s32(self._a["g_NumChrSlots"]),
        }
        # block() indexes by true address, undoing the RDRAM word swizzle, so a
        # u32 array unpacks big-endian and a byte array reads straight out. Do
        # NOT read the cheat bytes with a per-byte loop: same answer, 80 block
        # reads per tick.
        obj_raw = mem.block(self._a["objectiveStatuses"], OBJECTIVE_SLOTS * 4)
        cheats = mem.block(self._a["g_CheatActivated"], CHEAT_SLOTS)
        vals["objective_status"] = list(
            struct.unpack(f">{OBJECTIVE_SLOTS}I", obj_raw))
        vals["cheats_active"] = sum(1 for c in cheats if c)

        blob = (struct.pack(">QiiiI",
                            vals["rng_seed"], vals["alarm_timer"],
                            vals["objective_count"], vals["objective_registers"],
                            vals["num_chr_slots"] & 0xFFFFFFFF)
                + obj_raw + cheats)
        return hashlib.sha1(blob).digest()[:8], vals

    def _player_hash(self, pl: dict) -> bytes:
        if not pl:
            return b"\x00" * 8
        blob = (struct.pack(">fffffffffiffif",
                            *pl["pos"], *pl["model_pos"], *pl["room_pos"],
                            pl["crouchpos"], pl["vert_bounce"],
                            pl["health"], pl["dead"], pl["armour"])
                + struct.pack(f">{PLAYER_AMMO_N}i", *pl["ammo"])
                + b"".join(struct.pack(">ii", w, m) for w, m in pl["hands"])
                + struct.pack(f">{SHOT_REGISTERS}i", *pl["shot_count"]))
        return hashlib.sha1(blob).digest()[:8]

    def _slot_is_live(self, mem: Memory, addr: int) -> bool:
        """The game's own liveness test for a chr slot.

        g_NumChrSlots counts ALLOCATED slots, not populated ones, so iterating
        it blindly hashes uninitialised memory - denormal floats, NaNs and byte
        patterns that differ run to run even when the game is perfectly
        deterministic. That produced phantom divergences around level load and
        cost most of a debugging session.

        src/game/cleanup_guard_data.c gates on `model != 0`; use the same test.
        """
        model = mem.u32(addr + CHR_MODEL)
        return model != 0 and mem.valid_ptr(model)

    @staticmethod
    def _entity_is_plausible(d: dict) -> bool:
        """Does this slot hold a character, or memory that is not one yet?

        `model != 0` is the game's own liveness test but it is not sufficient:
        slots appear with a non-null model pointer and uninitialised contents -
        chrnum=0, flags=0, maxdamage=-1.7e38 (i.e. -FLT_MAX), pos components at
        the same sentinel. Hashing those produces run-to-run differences that
        have nothing to do with the game, and worse, they surface as the FIRST
        divergence and hide the real one behind them.
        """
        nums = list(d["pos"]) + list(d["fallspeed"]) + [d["damage"], d["maxdamage"]]
        if any(not math.isfinite(v) for v in nums):
            return False
        if any(abs(v) > 1e9 for v in nums):
            return False
        # A real character has a positive maximum health.
        return d["maxdamage"] > 0.0

    def _read_entity(self, mem: Memory, addr: int) -> tuple[bytes, dict]:
        d = {
            "chrnum": mem.s16(addr + CHR_CHRNUM),
            "chrflags": mem.u32(addr + CHR_CHRFLAGS),
            "pos": [mem.f32(addr + CHR_PREVPOS + i * 4) for i in range(3)],
            "fallspeed": [mem.f32(addr + CHR_FALLSPEED + i * 4) for i in range(3)],
            "lastknowntargetpos": [mem.f32(addr + CHR_LASTKNOWNTARGETPOS + i * 4)
                                   for i in range(3)],
            "damage": mem.f32(addr + CHR_DAMAGE),
            "maxdamage": mem.f32(addr + CHR_MAXDAMAGE),
            "timer60": mem.s32(addr + CHR_TIMER60),
            "weapon_r": _weapon_identity(mem, mem.u32(addr + CHR_WEAPONS_HELD)),
            "weapon_l": _weapon_identity(mem, mem.u32(addr + CHR_WEAPONS_HELD + 4)),
        }
        d.update({
            "actiontype": mem.u8(addr + CHR_ACTIONTYPE),
            "bodynum": mem.s8(addr + CHR_BODYNUM),
            "headnum": mem.s8(addr + CHR_HEADNUM),
            "accuracyrating": mem.s8(addr + CHR_ACCURACYRATING),
            "speedrating": mem.s8(addr + CHR_SPEEDRATING),
            "firecount0": mem.u8(addr + CHR_FIRECOUNT),
            "firecount1": mem.u8(addr + CHR_FIRECOUNT + 1),
            "sleep": mem.s8(addr + CHR_SLEEP),
            "invalidmove": mem.s8(addr + CHR_INVALIDMOVE),
            "numclosearghs": mem.s8(addr + CHR_NUMCLOSEARGHS),
            "numarghs": mem.s8(addr + CHR_NUMARGHS),
            "arghrating": mem.s8(addr + CHR_ARGHRATING),
            "aimendcount": mem.s8(addr + CHR_AIMENDCOUNT),
            "grenadeprob": mem.u8(addr + CHR_GRENADEPROB),
            "flinchcnt": mem.s8(addr + CHR_FLINCHCNT),
            "hidden": mem.u16(addr + CHR_HIDDEN),
            "lastwalk60": mem.s32(addr + CHR_LASTWALK60),
            "lastmoveok60": mem.s32(addr + CHR_LASTMOVEOK60),
            "visionrange": mem.f32(addr + CHR_VISIONRANGE),
            "lastseetarget60": mem.s32(addr + CHR_LASTSEETARGET60),
            "lastshooter": mem.s16(addr + CHR_LASTSHOOTER),
            "timeshooter": mem.s16(addr + CHR_TIMESHOOTER),
            "hearingscale": mem.f32(addr + CHR_HEARINGSCALE),
            "lastheartarget60": mem.s32(addr + CHR_LASTHEARTARGET60),
            "aioffset": mem.u16(addr + CHR_AIOFFSET),
            "aireturnlist": mem.s16(addr + CHR_AIRETURNLIST),
            "chrseeshot": mem.s16(addr + CHR_CHRSEESHOT),
            "chrseedie": mem.s16(addr + CHR_CHRSEEDIE),
        })

        model = mem.u32(addr + CHR_MODEL)
        if mem.valid_ptr(model):
            d["animframe1"] = mem.f32(model + MODEL_ANIMFRAME1)
            d["animframe2"] = mem.f32(model + MODEL_ANIMFRAME2)
            d["scale"] = mem.f32(model + MODEL_SCALE)
        else:
            d["animframe1"] = d["animframe2"] = d["scale"] = 0.0

        # Packed field-by-field rather than with one long format string. A
        # combined string has to be counted by hand against its arguments, and
        # getting that wrong once already cost a debugging session - it raised
        # inside a ctypes callback and surfaced as a bogus "game stalled".
        blob = b"".join(struct.pack(fmt, d[key]) for fmt, key in _ENTITY_FIELDS)
        blob += struct.pack(">fffffffff", *d["pos"], *d["fallspeed"],
                            *d["lastknowntargetpos"])
        return hashlib.sha1(blob).digest()[:8], d

    def _read_props(self, mem: Memory, detail: dict | None) -> bytes:
        """Digest of every world prop, walked exactly as propsTick walks them.

        Tail-to-head via prev, matching src/game/chrprop.c. CHR, PLAYER and
        VIEWER props are skipped - characters and the player are hashed from
        their own pools. What this adds is the world: door positions and open
        states, object destroyed/damaged/activated bits (alarm panels, CCTV,
        drone guns, crates), dropped weapons, live explosions and smoke.

        Validated on real recordings before landing: 200-290 props per tick,
        zero implausible positions across ~100k samples on two levels, state
        bytes forming proper bitmasks (0x80 destroyed observed after blowing
        things up), door openstate a clean 4-value enum.
        """
        acc = hashlib.sha1()
        count = 0
        p = mem.u32(self._a["g_ActivePropsTail"])
        seen = set()
        while p and mem.valid_ptr(p) and count < MAX_PROPS and p not in seen:
            seen.add(p)
            try:
                ptype = mem.u8(p + PROP_TYPE_OFF)
                if ptype in (1, 2, 4, 7, 8):     # OBJ, DOOR, WEAPON, EXPLOSION, SMOKE
                    count += 1
                    acc.update(struct.pack(
                        ">BBhfff", ptype, mem.u8(p + PROP_FLAGS_OFF),
                        mem.s16(p + PROP_TIMETOREGEN_OFF),
                        mem.f32(p + PROP_POS_OFF), mem.f32(p + PROP_POS_OFF + 4),
                        mem.f32(p + PROP_POS_OFF + 8)))
                    u = mem.u32(p + PROP_UNION_OFF)
                    if ptype in (1, 2, 4) and mem.valid_ptr(u):
                        objid = mem.s16(u + OBJ_ID_OFF)
                        state = mem.u8(u + OBJ_STATE_OFF)
                        acc.update(struct.pack(
                            ">hBII", objid, state,
                            mem.u32(u + OBJ_FLAGS_OFF), mem.u32(u + OBJ_FLAGS2_OFF)))
                        if ptype == 2:
                            acc.update(struct.pack(
                                ">fB", mem.f32(u + DOOR_FRAC_OFF),
                                mem.u8(u + DOOR_OPENSTATE_OFF)))
                        elif ptype == 4:
                            acc.update(struct.pack(
                                ">bh", mem.s8(u + WEAPON_NUM_OFF),
                                mem.s16(u + WEAPON_TIMER_OFF)))
                        elif ptype == 1:
                            # v13. Cameras and turrets were already in the
                            # hash as objects - existence, destroyed and
                            # activated bits. What was missing is where they
                            # POINT, and the CCTV detection timer, which only
                            # advances inside a +/-45 degree cone with an
                            # unobstructed stan line and so drifts long before
                            # the alarm outcome changes.
                            pdt = mem.u8(u + PROPDEF_TYPE_OFF)
                            if pdt == PROPDEF_CCTV:
                                acc.update(struct.pack(
                                    ">fi", mem.f32(u + CCTV_ROT_OFF),
                                    mem.s32(u + CCTV_TIMER_OFF)))
                            elif pdt == PROPDEF_AUTOGUN:
                                acc.update(struct.pack(
                                    ">fi", mem.f32(u + AUTOGUN_ROT_OFF),
                                    mem.s32(u + AUTOGUN_ACTIVE_OFF)))
                        if detail is not None and (state or ptype == 2):
                            detail.setdefault("props", []).append(
                                {"type": ptype, "obj": objid, "state": state})
                p = mem.u32(p + PROP_PREV_OFF)
            except ValueError:
                break               # ran off RDRAM - treat as end of list
        acc.update(struct.pack(">H", count))
        return acc.digest()[:8]

    def capture(self, mem: Memory, tick: int, detail: bool = False) -> TickState:
        gh, gvals = self._read_globals(mem)
        player = self._read_player(mem)
        ph = self._player_hash(player)
        ents: dict[int, bytes] = {}
        details: dict = ({"globals": gvals, "player": player, "entities": {}}
                         if detail else None)

        # Walk exactly what the game walks. Membership of this array IS the
        # game's liveness answer, so the guesswork that the slot-pool version
        # needed - model!=0, finite floats, positive maxdamage - is gone with
        # it. Those filters were rejecting real guards (a dying one has
        # maxdamage 0) while still missing most of them.
        # The POOL, not g_ActiveChrs. Measured over whole levels: on Archives
        # the pool churns through 65 distinct characters at ~5 alive at once,
        # while g_ActiveChrsCount sat at 3 the entire level; on Facility the
        # two disagree the other way (4 vs 15). Whatever g_ActiveChrs counts,
        # it is not the population, and a schema built on it missed nearly
        # every guard that spawned or died.
        #
        # Occupancy is `model != 0` - the game's own test, chr.c counts free
        # slots as model == 0 - plus a pointer sanity check, because an
        # unused slot holds uninitialised bytes that are often non-zero
        # (measured: 19 slots non-zero at that offset, only 5 real pointers,
        # the rest float bit patterns and small integers).
        #
        # No plausibility filter beyond that. The v5 version also demanded
        # finite floats and maxdamage > 0, which threw away real guards - a
        # dying one has maxdamage 0 - and cut Archives from 5 to 1.
        slots = mem.u32(self._a["g_ChrSlots"])
        n = mem.s32(self._a["g_NumChrSlots"])
        if mem.valid_ptr(slots) and 0 < n <= MAX_CHRS:
            for i in range(n):
                a = slots + i * CHR_RECORD_SIZE
                model = mem.u32(a + CHR_MODEL)
                if not model or not mem.valid_ptr(model):
                    continue
                try:
                    h, d = self._read_entity(mem, a)
                except ValueError:
                    break               # ran off RDRAM - treat as end of table
                # Key by the character's own id, not its position in the array.
                # Guards spawn and die, so an index shifts under everything
                # after it and a diff would report every later guard as moved
                # when one earlier one despawned.
                key = d["chrnum"] & 0xFFFF
                # 0xFFFF is the player's reserved key, and chrnum is signed -
                # a chrnum of -1 masks straight onto it and would silently
                # replace Bond in the hash map. Push any collision, and any
                # duplicate id, into a separate range.
                if key == 0xFFFF or key in ents:
                    key = 0x8000 | (i & 0x7FFF)
                ents[key] = h
                if detail:
                    details["entities"][key] = d

        props_hash = self._read_props(mem, details)

        acc = hashlib.sha1()
        acc.update(gh)
        acc.update(ph)
        acc.update(props_hash)
        for i in sorted(ents):
            acc.update(struct.pack(">H", i))
            acc.update(ents[i])
        # Player is entity slot 0xFFFF in the hash map so trace-diff can name it
        # specifically rather than folding it into "globals". World props ride
        # at 0xFFFE the same way: a divergence there reads as "props", and the
        # per-prop culprit comes from detail replay.
        ents[0xFFFF] = ph
        ents[PROPS_KEY] = props_hash
        return TickState(tick=tick, composite=acc.digest()[:16],
                         entity_hashes=ents, globals_hash=gh, detail=details)
