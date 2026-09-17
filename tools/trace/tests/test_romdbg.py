"""Known-positive controls for the debug mailbox's HOST half.

An instrument that cannot fail is worthless, and one that cannot be made to
fire on demand cannot be shown to work at all. The recorder's live capture path
is driven here against a SYNTHETIC mailbox - no ROM, no core, no display - so
every branch the owner can hit while playing is exercised, including the ones a
headless run cannot reach because nobody is holding the controller.

The other control in here is the one that would have caught the expensive
mistake: the offsets in sltrace.romdbg are hand-mirrored from
src/game/sl_romdbg.h, and this compares them field by field against the header's
own annotations. A silently shifted field is exactly the confident-wrong answer
the whole instrument exists to remove.
"""

import ctypes
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from sltrace import romdbg
from sltrace.emu_libretro import InputFrame
from sltrace.state import Memory, KSEG0

REPO = Path(__file__).resolve().parents[3]
BASE = KSEG0 + 0x100000


class FakeRom:
    """Just enough ROM to answer commands: it acks, it publishes, it lies still.

    Writes go through Memory.w32 for the same reason every read does - RDRAM in
    the real core is word-byte-swapped, and a test that packed its own words
    would be testing a different memory than the one the tool reads.
    """

    def __init__(self):
        self.ram = (ctypes.c_ubyte * 0x800000)()
        self.mem = Memory(self.ram)
        self.rom_seq = 0
        self.live = True                  # False = the hook does not run
        self.w("magic", romdbg.MAGIC)
        self.w("version", romdbg.VERSION)
        self.w("size", romdbg.EXPECTED_SIZE)
        self.w("watch_objtype", -1)
        self.w("glass_opacity", -1)
        self.w("glass_portalnum", -1)

    def w(self, field, value):
        self.mem.w32(BASE + romdbg.MB[field][0], value & 0xFFFFFFFF)

    def r(self, field):
        return romdbg.read_field(self.mem, BASE, field)

    def hit(self, slot, prop, objtype, dist, penetrates=1):
        h = BASE + romdbg.HITS_OFF + slot * romdbg.HIT_STRIDE
        self.mem.w32(h + 0x00, prop)
        self.mem.w32(h + 0x04, prop + 0x10)
        self.mem.w32(h + 0x08, prop + 0x20)
        self.mem.w32(h + 0x0C, _f32_bits(dist))
        self.mem.w32(h + 0x10, 4)
        self.mem.w32(h + 0x14, objtype & 0xFFFFFFFF)
        self.mem.w32(h + 0x18, 0x06)
        self.mem.w32(h + 0x1C, penetrates)

    def tick(self, event=romdbg.EV_NONE, nhits=0, frame=0, sight_mode=0,
             onscreen=0, watch_objtype=-1, watch_prop=0):
        """One frame of the ROM hook: serve any command, then publish."""
        if not self.live:
            return
        seq = self.r("host_seq")
        if seq != self.r("rom_ack"):
            self.w("rom_ack", seq)
            self.w("event", event)
            self.w("nhits", nhits)
        else:
            self.w("event", romdbg.EV_NONE)
            self.w("nhits", 0)
        self.w("frame", frame)
        self.w("sight_mode", sight_mode)
        self.w("sight_mp_menu", 0)
        self.w("onscreen_count", onscreen)
        self.w("watch_objtype", watch_objtype)
        self.w("watch_prop", watch_prop)
        self.rom_seq += 1
        self.w("rom_seq", self.rom_seq)


def _f32_bits(value):
    return int.from_bytes(ctypes.c_float(value), "little")


PRESS = InputFrame(1 << InputFrame.DBG_SELECT)
IDLE = InputFrame(0)


class LiveCaptureTest(unittest.TestCase):
    """Every line the owner can see while recording, made to appear on demand."""

    def setUp(self):
        self.said = []
        self.rom = FakeRom()
        self.live = romdbg.LiveCapture(BASE, out_dir=None,
                                       emit=self.said.append,
                                       miss_detail=False)

    def press_and_serve(self, index, **rom_kwargs):
        """One frame with the button down, then one frame where the ROM answers."""
        self.live.before_frame(index, PRESS, self.rom.mem)
        self.rom.tick(**rom_kwargs)
        self.live.after_frame(index, self.rom.mem)

    def test_hit_names_the_object(self):
        self.rom.hit(0, 0x800C93F0, romdbg.PROPDEF_TINTED_GLASS, 512.4)
        self.press_and_serve(4812, event=romdbg.EV_SELECT, nhits=1, frame=4812,
                             watch_objtype=romdbg.PROPDEF_TINTED_GLASS,
                             watch_prop=0x800C93F0)
        line = self.said[-1]
        self.assertIn("capture 1", line)
        self.assertIn("frame 4812", line)
        self.assertIn("HIT", line)
        self.assertIn("0x800c93f0", line)
        self.assertIn("TINTED_GLASS", line)
        self.assertIn("0x2f", line)
        self.assertIn("512.4", line)
        self.assertEqual(self.live.hits, 1)

    def test_nearest_of_several_wins(self):
        """The ROM fills whichever hit slot is free, so order is not distance."""
        self.rom.hit(0, 0x80001000, 3, 900.0)
        self.rom.hit(1, 0x80002000, romdbg.PROPDEF_TINTED_GLASS, 120.0)
        self.press_and_serve(10, event=romdbg.EV_SELECT, nhits=2,
                             watch_objtype=romdbg.PROPDEF_TINTED_GLASS,
                             watch_prop=0x80002000)
        self.assertIn("0x80002000", self.said[-2])
        self.assertIn("120.0", self.said[-2])
        self.assertIn("also on the ray", self.said[-1])

    def test_miss_with_a_crosshair_says_so(self):
        self.press_and_serve(5140, event=romdbg.EV_MISS, frame=5140,
                             sight_mode=0, onscreen=14)
        self.assertIn("MISS", self.said[-1])
        self.assertIn("crosshair on no prop", self.said[-1])
        self.assertIn("14 props", self.said[-1])

    def test_miss_without_a_crosshair_says_WHY(self):
        """The whole point of the discriminator: a different mistake entirely."""
        self.press_and_serve(5390, event=romdbg.EV_MISS, frame=5390,
                             sight_mode=0x02)
        self.assertIn("MISS", self.said[-1])
        self.assertIn("not aiming", self.said[-1])
        self.assertIn("no crosshair", self.said[-1])

    def test_no_control_is_not_reported_as_not_aiming(self):
        self.press_and_serve(20, event=romdbg.EV_MISS, sight_mode=0x04)
        self.assertIn("no control", self.said[-1])

    def test_a_press_the_rom_never_serves_is_reported_not_dropped(self):
        """A menu press must not look like a press that found nothing."""
        self.rom.live = False
        self.live.before_frame(100, PRESS, self.rom.mem)
        for i in range(101, 101 + romdbg.SERVE_TIMEOUT_FRAMES + 2):
            self.rom.tick()
            self.live.after_frame(i, self.rom.mem)
        self.assertTrue(any("NOT SERVED" in s for s in self.said), self.said)
        self.assertEqual(self.live.unserved, 1)

    def test_a_held_press_collapses_to_one_capture(self):
        for i in range(40):
            self.live.before_frame(i, PRESS, self.rom.mem)
            self.rom.tick(event=romdbg.EV_MISS)
            self.live.after_frame(i, self.rom.mem)
        self.assertEqual(self.live.presses, 1)
        self.assertEqual(self.live.captures, 1)

    def test_releasing_and_pressing_again_captures_twice(self):
        for i, frame in enumerate([PRESS, IDLE, PRESS]):
            self.live.before_frame(i, frame, self.rom.mem)
            self.rom.tick(event=romdbg.EV_MISS)
            self.live.after_frame(i, self.rom.mem)
        self.assertEqual(self.live.captures, 2)

    def test_the_aim_aid_is_off_unless_asked_for(self):
        for i in range(500):
            self.live.before_frame(i, IDLE, self.rom.mem)
            self.rom.tick()
            self.live.after_frame(i, self.rom.mem)
        # The layout control still speaks once - that one is not optional.
        chatter = [s for s in self.said if "layout OK" not in s]
        self.assertEqual(chatter, [])

    def test_the_aim_aid_repeats_itself_only_when_it_changes(self):
        live = romdbg.LiveCapture(BASE, out_dir=None, emit=self.said.append,
                                  aid_type=romdbg.PROPDEF_TINTED_GLASS,
                                  aid_every=10)
        self.rom.hit(0, 0x8006CB58, romdbg.PROPDEF_TINTED_GLASS, 640.0)
        for i in range(100):
            live.before_frame(i, IDLE, self.rom.mem)
            self.rom.tick(event=romdbg.EV_NEAR, nhits=1)
            live.after_frame(i, self.rom.mem)
        aid = [s for s in self.said if "aim aid" in s]
        self.assertEqual(len(aid), 1, aid)
        self.assertIn("nearest TINTED_GLASS 640", aid[0])
        self.assertIn("0x8006cb58", aid[0])

    def test_an_aid_peek_never_becomes_a_capture(self):
        live = romdbg.LiveCapture(BASE, out_dir=None, emit=self.said.append,
                                  aid_type=romdbg.PROPDEF_TINTED_GLASS,
                                  aid_every=10)
        for i in range(60):
            live.before_frame(i, IDLE, self.rom.mem)
            self.rom.tick(event=romdbg.EV_NEAR)
            live.after_frame(i, self.rom.mem)
        self.assertEqual(live.captures, 0)
        self.assertEqual(live.presses, 0)

    def test_a_miss_follows_up_with_what_was_on_screen(self):
        live = romdbg.LiveCapture(BASE, out_dir=None, emit=self.said.append,
                                  miss_detail=True)
        live.before_frame(1, PRESS, self.rom.mem)
        self.rom.tick(event=romdbg.EV_MISS, onscreen=3)
        live.after_frame(1, self.rom.mem)
        # The follow-up NEAREST is issued from inside the MISS; serve it.
        self.rom.hit(0, 0x80005000, 3, 384.2)
        live.before_frame(2, IDLE, self.rom.mem)
        self.rom.tick(event=romdbg.EV_NEAR, nhits=1)
        live.after_frame(2, self.rom.mem)
        self.assertIn("on screen, nearest first", self.said[-1])
        self.assertIn("PROP 384", self.said[-1])


class SightReasonTest(unittest.TestCase):

    def test_a_drawn_crosshair_has_no_reason(self):
        self.assertEqual(
            romdbg.sight_reason({"sight_mode": 0, "sight_mp_menu": 0}), "")

    def test_several_reasons_are_all_named(self):
        why = romdbg.sight_reason({"sight_mode": 0x06, "sight_mp_menu": 0})
        self.assertIn("not aiming", why)
        self.assertIn("no control", why)

    def test_an_unknown_bit_is_shown_rather_than_swallowed(self):
        why = romdbg.sight_reason({"sight_mode": 0x80, "sight_mp_menu": 0})
        self.assertIn("0x80", why)


class LayoutTest(unittest.TestCase):
    """The mirror must match the ROM. This is the drift that costs days."""

    def test_the_offsets_match_the_c_header(self):
        header = (REPO / "src/game/sl_romdbg.h").read_text()
        declared = {}
        for m in re.finditer(
                r"^\s+\w+\s+(\w+)(?:\[\d+\])?;\s*/\*\s*0x([0-9a-f]{2,4})",
                header, re.M):
            declared[m.group(1)] = int(m.group(2), 16)
        self.assertIn("sight_mode", declared, "header parse found nothing")
        missing = [k for k in romdbg.MB if k not in declared]
        self.assertEqual(missing, [], f"not declared in sl_romdbg.h: {missing}")
        for name, (off, _) in romdbg.MB.items():
            self.assertEqual(off, declared[name],
                             f"{name}: romdbg.py says 0x{off:x}, "
                             f"sl_romdbg.h says 0x{declared[name]:x}")
        self.assertEqual(romdbg.HITS_OFF, declared["hits"])

    def test_the_version_matches_the_c_header(self):
        header = (REPO / "src/game/sl_romdbg.h").read_text()
        m = re.search(r"#define SL_ROMDBG_VERSION\s+(\d+)", header)
        self.assertEqual(romdbg.VERSION, int(m.group(1)))

    def test_a_mailbox_of_the_wrong_size_is_refused(self):
        rom = FakeRom()
        rom.w("size", romdbg.EXPECTED_SIZE + 8)
        with self.assertRaises(SystemExit):
            romdbg.check_layout(rom.mem, BASE)

    def test_propdef_names_come_from_the_enum(self):
        self.assertEqual(romdbg.propdef_name(romdbg.PROPDEF_TINTED_GLASS),
                         "TINTED_GLASS")
        # Positions, not guesses: 1 is DOOR and 42 is GLASS in the enum. A
        # hand-written PROPDEF_DOOR = 4 used to sit in this codebase.
        self.assertEqual(romdbg.propdef_name(1), "DOOR")
        self.assertEqual(romdbg.propdef_name(42), "GLASS")
        self.assertEqual(romdbg.propdef_name(999), "type999")


if __name__ == "__main__":
    unittest.main()
