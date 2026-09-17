"""Tests for the cartridge save editor. No ROM required."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from sltrace.savefile import (SAVE_SIZE, TIMES_OFF, TIMES_LEN, checksums,
                              find_records, reseal, unlock_all)


def _slot(times: bytes = b"\x00" * TIMES_LEN) -> bytearray:
    rec = bytearray(SAVE_SIZE)
    rec[TIMES_OFF:TIMES_OFF + TIMES_LEN] = times
    reseal(rec)
    return rec


class SaveFileTest(unittest.TestCase):

    def test_layout_matches_the_struct(self):
        # save_data is 0x60 with times last; if these drift apart the editor
        # writes into the wrong field and the game rejects the save.
        self.assertEqual(SAVE_SIZE, 0x60)
        self.assertLessEqual(TIMES_OFF + TIMES_LEN, SAVE_SIZE)
        self.assertEqual(TIMES_LEN, (20 - 1) * 4)

    def test_checksums_are_deterministic(self):
        self.assertEqual(checksums(b"sightline" * 8), checksums(b"sightline" * 8))

    def test_checksums_depend_on_content(self):
        self.assertNotEqual(checksums(b"\x00" * 88), checksums(b"\x01" + b"\x00" * 87))

    def test_resealed_slot_validates(self):
        blob = bytes(_slot(b"\x11" * TIMES_LEN))
        self.assertEqual(find_records(blob), [0])

    def test_edit_without_reseal_is_rejected(self):
        # The whole safety property: a slot edited but not resealed must NOT
        # look valid, or we would write saves the game silently discards.
        rec = _slot()
        rec[TIMES_OFF] = 0xFF
        self.assertEqual(find_records(bytes(rec)), [])

    def test_unlock_fills_times_and_reseals(self):
        blob = bytes(_slot())
        out, touched = unlock_all(blob)
        self.assertEqual(touched, [0])
        self.assertTrue(all(b == 0xFF for b in out[TIMES_OFF:TIMES_OFF + TIMES_LEN]))
        self.assertEqual(find_records(out), [0])

    def test_unlock_leaves_unrelated_bytes_alone(self):
        rec = _slot()
        rec[9] = 0x5A                      # flag_007, outside times
        reseal(rec)
        out, _ = unlock_all(bytes(rec))
        self.assertEqual(out[9], 0x5A)


if __name__ == "__main__":
    unittest.main(verbosity=2)
