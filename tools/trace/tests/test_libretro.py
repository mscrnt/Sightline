"""Tests for the libretro backend's pure logic.

No core, no ROM, no display: these cover the parts where a silent mistake is
expensive - replay file parsing, input decoding, and the analog swap - so they
run anywhere, including a runner with no ROM.
"""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from sltrace.emu_libretro import (LibretroEmulator, SecondCoreError,
                                  BsvReplay, InputFrame, BUTTON_MAP,
                                  LIBRETRO_TO_N64)


def make_replay(samples, state=b"M64+SAVE" + b"\x00" * 24) -> str:
    """Build a minimal BSV2 file: magic, sizes, savestate, int16 samples."""
    hdr = bytearray(0x18)
    hdr[0:4] = b"2VSB"
    hdr[0x0C:0x10] = struct.pack("<I", len(state))
    body = b"".join(struct.pack("<h", s) for s in samples)
    f = tempfile.NamedTemporaryFile(suffix=".replay", delete=False)
    f.write(bytes(hdr) + state + body)
    f.close()
    return f.name


class TestBsvReplay(unittest.TestCase):
    def test_parses_savestate_and_samples(self):
        r = BsvReplay(make_replay([0, 1, -1, 32767, -32768]))
        self.assertTrue(r.savestate.startswith(b"M64+SAVE"))
        self.assertEqual(r.samples, [0, 1, -1, 32767, -32768])

    def test_rejects_foreign_file(self):
        f = tempfile.NamedTemporaryFile(suffix=".replay", delete=False)
        f.write(b"NOPE" + b"\x00" * 64)
        f.close()
        with self.assertRaises(ValueError):
            BsvReplay(f.name)

    def test_rejects_implausible_state_size(self):
        hdr = bytearray(0x18)
        hdr[0:4] = b"2VSB"
        hdr[0x0C:0x10] = struct.pack("<I", 1 << 30)      # far beyond the file
        f = tempfile.NamedTemporaryFile(suffix=".replay", delete=False)
        f.write(bytes(hdr) + b"\x00" * 32)
        f.close()
        with self.assertRaises(ValueError):
            BsvReplay(f.name)

    def test_exhaustion_is_flagged_not_silent(self):
        r = BsvReplay(make_replay([7, 8]))
        self.assertEqual(r.next_sample(), 7)
        self.assertEqual(r.next_sample(), 8)
        self.assertEqual(r.next_sample(), 0)     # neutral past the end
        self.assertTrue(r.exhausted)

    def test_analog_swap_reorders_the_quad(self):
        # Recorded order is RIGHT X, RIGHT Y, LEFT X, LEFT Y.
        r = BsvReplay(make_replay([100, 200, 300, 400]))
        r.swap_analog = True
        got = [r.next_sample_swapped_analog(5, 1, 0),
               r.next_sample_swapped_analog(5, 1, 1),
               r.next_sample_swapped_analog(5, 0, 0),
               r.next_sample_swapped_analog(5, 0, 1)]
        self.assertEqual(got, [300, 400, 100, 200])

    def test_swap_leaves_joypad_queries_alone(self):
        r = BsvReplay(make_replay([1, 2, 3]))
        r.swap_analog = True
        self.assertEqual(r.next_sample_swapped_analog(1, 0, 0), 1)
        self.assertEqual(r.next_sample_swapped_analog(1, 0, 1), 2)

    def test_no_swap_is_pure_passthrough(self):
        r = BsvReplay(make_replay([9, 8, 7, 6]))
        got = [r.next_sample_swapped_analog(5, 1, 0),
               r.next_sample_swapped_analog(5, 1, 1)]
        self.assertEqual(got, [9, 8])


class TestInputFrame(unittest.TestCase):
    def test_button_bits(self):
        f = InputFrame(1 << InputFrame.START)
        self.assertTrue(f.pressed(InputFrame.START))
        self.assertFalse(f.pressed(InputFrame.A_BUTTON))

    def test_axes_are_signed(self):
        self.assertEqual(InputFrame(80 << 16).x_axis, 80)
        self.assertEqual(InputFrame(0xB0 << 16).x_axis, -80)
        self.assertEqual(InputFrame(0xB0 << 24).y_axis, -80)

    def test_from_bytes_is_big_endian(self):
        self.assertEqual(InputFrame.from_bytes(b"\x00\x00\x00\x10").value, 0x10)

    def test_button_map_is_a_bijection(self):
        # A duplicated libretro id would silently merge two N64 buttons.
        self.assertEqual(len(BUTTON_MAP), len(set(BUTTON_MAP.values())))
        self.assertEqual(len(LIBRETRO_TO_N64), len(BUTTON_MAP))

    def test_button_map_ids_are_valid_joypad_ids(self):
        for lid in BUTTON_MAP.values():
            self.assertTrue(0 <= lid <= 15, f"id {lid} outside RetroPad range")


class OneCorePerProcessTest(unittest.TestCase):
    """The guard against two emulators sharing one dlopen'd core.

    Without it a second LibretroEmulator silently resumes the first one's core
    instead of booting, which reads as a behavioural divergence and sends you
    looking for a bug in input handling that is not there.
    """

    def setUp(self):
        self._saved = LibretroEmulator._process_has_core
        self.addCleanup(setattr, LibretroEmulator, "_process_has_core",
                        self._saved)

    def test_second_construction_is_refused(self):
        LibretroEmulator._process_has_core = True
        with self.assertRaises(SecondCoreError):
            LibretroEmulator("/nonexistent.z64")

    def test_message_names_the_remedy(self):
        # A guard that only says "no" costs the next reader the same session
        # this one cost. It has to say what to do instead.
        LibretroEmulator._process_has_core = True
        try:
            LibretroEmulator("/nonexistent.z64")
        except SecondCoreError as exc:
            self.assertIn("own process", str(exc))
        else:
            self.fail("expected SecondCoreError")


if __name__ == "__main__":
    unittest.main(verbosity=2)
