"""Tests for the ROM reference probe's pure logic.

No core, no ROM, no display. What is covered is the part where a silent
mistake is expensive: the framebuffer statistic that decides whether a frame
was ON SCREEN or in a black gap, and the guard that keeps ROM-derived samples
out of the working tree.

The numpy/pure-Python agreement test is the important one. The fallback exists
so the tool runs in the repo's venv, which has no numpy - and a fallback that
quietly computed a DIFFERENT number would turn "the Legal screen is visible for
67 frames" into a measurement that depends on which machine ran it. That is
exactly the class of instrument fault this project keeps catching in itself, so
it is asserted rather than assumed.
"""

import ctypes as C
import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import bootprobe                                            # noqa: E402
from bootprobe import (BLACK_LEVEL, SAMPLE_STRIDE, VideoStats,   # noqa: E402
                       frame_stats, menu_name, progression,
                       refuse_inside_repo)


def make_frame(width, height, pitch, fill):
    """A XRGB8888 buffer; fill(x, y) -> (b, g, r)."""
    buf = (C.c_uint8 * (height * pitch))()
    for y in range(height):
        base = y * pitch
        for x in range(width):
            b, g, r = fill(x, y)
            buf[base + x * 4 + 0] = b
            buf[base + x * 4 + 1] = g
            buf[base + x * 4 + 2] = r
            buf[base + x * 4 + 3] = 0xFF        # X byte, must be ignored
    return buf


class TestFrameStats(unittest.TestCase):
    W, H = 64, 32
    PITCH = 64 * 4 + 16                 # deliberately NOT width*4: pitch padding
                                        # is real and indexing it as width*4
                                        # skews every row after the first.

    def _ptr(self, buf):
        return C.cast(buf, C.c_void_p).value

    def test_black_frame_is_not_visible(self):
        buf = make_frame(self.W, self.H, self.PITCH, lambda x, y: (0, 0, 0))
        mean, nonblack = frame_stats(self._ptr(buf), self.W, self.H, self.PITCH)
        self.assertEqual(mean, 0.0)
        # Printed EXPLICITLY as zero. An empty result is never "zero" here.
        self.assertEqual(nonblack, 0.0)

    def test_white_frame_is_fully_visible(self):
        buf = make_frame(self.W, self.H, self.PITCH, lambda x, y: (255, 255, 255))
        mean, nonblack = frame_stats(self._ptr(buf), self.W, self.H, self.PITCH)
        self.assertEqual(nonblack, 1.0)
        self.assertAlmostEqual(mean, 255.0)

    def test_x_byte_is_ignored(self):
        """A frame that is black except for a full alpha byte reads as black."""
        buf = (C.c_uint8 * (self.H * self.PITCH))()
        for y in range(self.H):
            for x in range(self.W):
                buf[y * self.PITCH + x * 4 + 3] = 0xFF
        mean, nonblack = frame_stats(self._ptr(buf), self.W, self.H, self.PITCH)
        self.assertEqual((mean, nonblack), (0.0, 0.0))

    def test_black_level_is_a_threshold_not_a_zero_test(self):
        buf = make_frame(self.W, self.H, self.PITCH,
                         lambda x, y: (BLACK_LEVEL, BLACK_LEVEL, BLACK_LEVEL))
        _mean, nonblack = frame_stats(self._ptr(buf), self.W, self.H, self.PITCH)
        self.assertEqual(nonblack, 0.0)     # at the level is still black
        buf = make_frame(self.W, self.H, self.PITCH,
                         lambda x, y: (BLACK_LEVEL + 1,) * 3)
        _mean, nonblack = frame_stats(self._ptr(buf), self.W, self.H, self.PITCH)
        self.assertEqual(nonblack, 1.0)     # one above it is not

    def test_numpy_and_fallback_agree(self):
        """The two paths must produce the SAME numbers on the same buffer."""
        if bootprobe.np is None:
            self.skipTest("numpy not installed; only one path exists here")
        rng = random.Random(20260903)
        buf = make_frame(self.W, self.H, self.PITCH,
                         lambda x, y: (rng.randrange(256), rng.randrange(256),
                                       rng.randrange(256)))
        ptr = self._ptr(buf)
        fast = frame_stats(ptr, self.W, self.H, self.PITCH)
        real_np, bootprobe.np = bootprobe.np, None
        try:
            slow = frame_stats(ptr, self.W, self.H, self.PITCH)
        finally:
            bootprobe.np = real_np
        self.assertAlmostEqual(fast[0], slow[0], places=9)
        self.assertEqual(fast[1], slow[1])

    def test_sampling_grid_is_shared(self):
        """Both paths sample the same count, so fractions are comparable."""
        expected = len(range(0, self.H, SAMPLE_STRIDE)) * \
            len(range(0, self.W * 4, 4 * SAMPLE_STRIDE))
        self.assertGreater(expected, 0)
        buf = make_frame(self.W, self.H, self.PITCH,
                         lambda x, y: (255, 0, 0) if x == 0 else (0, 0, 0))
        _m, nonblack = frame_stats(self._ptr(buf), self.W, self.H, self.PITCH)
        # Column 0 is sampled once per sampled row.
        lit = len(range(0, self.H, SAMPLE_STRIDE))
        self.assertAlmostEqual(nonblack, lit / expected)


class TestVideoAccounting(unittest.TestCase):
    """presented + carried must equal the frame count, always.

    The first version of this probe reported "1400 frames, 1327 presented,
    0 duped" - three numbers that cannot all be true. It counted a NULL-data
    callback as a dupe but had no way to notice a callback that never fired at
    all. The arithmetic was the only thing that gave it away.
    """

    def test_missing_callback_counts_as_carried(self):
        v = VideoStats()
        for _ in range(5):
            v.begin_frame()
            v.end_frame()                    # core never called back
        self.assertEqual((v.presented, v.carried), (0, 5))
        self.assertTrue(v.carried_now)

    def test_null_data_callback_counts_as_carried(self):
        v = VideoStats()
        v.begin_frame()
        v.on_video(None, 0, 0, 0)
        v.end_frame()
        self.assertEqual((v.presented, v.carried, v.null_data), (0, 1, 1))

    def test_presented_plus_carried_is_every_frame(self):
        v = VideoStats()
        buf = make_frame(8, 8, 32, lambda x, y: (10, 20, 30))
        ptr = C.cast(buf, C.c_void_p).value
        frames = 0
        for i in range(20):
            v.begin_frame()
            if i % 3:
                v.on_video(ptr, 8, 8, 32)
            v.end_frame()
            frames += 1
        self.assertEqual(v.presented + v.carried, frames)

    def test_carried_frame_keeps_the_previous_picture(self):
        """A carry is the SAME picture, never a black one."""
        v = VideoStats()
        buf = make_frame(8, 8, 32, lambda x, y: (200, 200, 200))
        v.begin_frame()
        v.on_video(C.cast(buf, C.c_void_p).value, 8, 8, 32)
        v.end_frame()
        lit = v.nonblack
        self.assertEqual(lit, 1.0)
        v.begin_frame()
        v.end_frame()
        self.assertEqual(v.nonblack, lit)    # carried forward, not reset to 0


class TestMenuNames(unittest.TestCase):
    def test_known_values(self):
        self.assertEqual(menu_name(-1), "INVALID")
        self.assertEqual(menu_name(0), "LEGAL_SCREEN")
        self.assertEqual(menu_name(1), "NINTENDO_LOGO")
        self.assertEqual(menu_name(2), "RAREWARE_LOGO")
        self.assertEqual(menu_name(3), "EYE_INTRO")
        self.assertEqual(menu_name(23), "SWITCH_SCREENS")

    def test_unknown_value_is_reported_not_guessed(self):
        self.assertEqual(menu_name(999), "MENU_999")


class TestRepoGuard(unittest.TestCase):
    """ROM-derived samples must not land in the working tree."""

    def test_path_inside_repo_is_refused(self):
        with self.assertRaises(SystemExit):
            refuse_inside_repo(bootprobe.ROOT / "tools" / "boot.csv")

    def test_path_outside_repo_is_allowed(self):
        refuse_inside_repo(Path(C.__file__).parent / "boot.csv")


class TestProgression(unittest.TestCase):
    """The measured instrument fault this function exists to prevent.

    The NINTENDO_LOGO numbers below are the real ones, from a 900-frame boot of
    baserom.u.z64 under parallel_n64: the stage runs 501 frames, the first two
    samples are the stale pre-init 0, ``init_menu01_nintendo`` sets the angle
    to -1.39626348019 and the constructor then steps it by +0.017453292 once a
    frame. End-minus-start reads 7.3129 and the accumulated travel is 8.7092 -
    a gap of about 80 increments. The report must not present the first as the
    second.
    """

    INIT = -1.39626348019
    INC = 0.017453292

    def nintendo_stage(self):
        # The real shape, 501 samples: two pre-init samples reading the stale
        # 0, then init + 2 increments folded into the frame init ran on, then
        # 497 plain increments, and one STALLED frame - the ROM steps
        # g_ClockTimer to 3 once in this stage and the constructor does not
        # run that frame. 499 increments across 500 frame steps, 498 of which
        # changed the value, which is what the cartridge measured.
        vals = [0.0, 0.0]
        v = self.INIT + 2 * self.INC
        vals.append(v)
        for _ in range(497):
            v += self.INC
            vals.append(v)
        vals.append(v)                           # the stalled frame
        return vals

    def test_net_and_travel_are_different_numbers(self):
        pr = progression(self.nintendo_stage())
        self.assertAlmostEqual(pr["net"], 7.3129, places=3)
        self.assertAlmostEqual(pr["travel"], 497 * self.INC, places=6)
        self.assertNotAlmostEqual(pr["net"], pr["travel"], places=2)

    def test_the_reset_is_named_not_averaged_away(self):
        pr = progression(self.nintendo_stage())
        self.assertEqual(len(pr["outliers"]), 1)
        idx, delta = pr["outliers"][0]
        self.assertEqual(idx, 2)                 # the frame init ran on
        self.assertAlmostEqual(delta, self.INIT + 2 * self.INC, places=6)

    def test_modal_step_is_the_constructor_increment(self):
        pr = progression(self.nintendo_stage())
        self.assertAlmostEqual(pr["modal"], self.INC, places=9)
        self.assertEqual(pr["modal_count"], 497)
        self.assertEqual(pr["changes"], 498)
        self.assertEqual(pr["n_steps"], 500)

    def test_f32_low_bit_jitter_is_one_modal_step_not_many(self):
        # A float counter's deltas differ in their low bits. Bucketing must
        # collapse them, or every frame reads as its own "distinct step" and
        # the modal count becomes meaningless.
        import struct

        def f32(x):
            return struct.unpack("<f", struct.pack("<f", x))[0]

        v, vals = f32(0.0), [0.0]
        for _ in range(200):
            v = f32(v + f32(self.INC))
            vals.append(v)
        pr = progression(vals)
        self.assertEqual(pr["changes"], 200)
        self.assertEqual(pr["outliers"], [])
        self.assertGreater(pr["modal_count"], 100)

    def test_clean_counter_reports_net_as_travel(self):
        # RAREWARE's D_8002A89C: -38 to 128 in steps of 2, no reset.
        vals = [float(-38 + 2 * i) for i in range(84)]
        pr = progression(vals)
        self.assertEqual(pr["outliers"], [])
        self.assertAlmostEqual(pr["net"], pr["travel"], places=9)
        self.assertAlmostEqual(pr["modal"], 2.0, places=9)

    def test_motionless_counter_states_zero_explicitly(self):
        pr = progression([7] * 40)
        self.assertEqual(pr["changes"], 0)
        self.assertEqual(pr["n_steps"], 39)
        self.assertIsNone(pr["modal"])
        self.assertEqual(pr["outliers"], [])
        self.assertEqual(pr["travel"], 0)

    def test_empty_and_single_sample_do_not_raise(self):
        self.assertEqual(progression([])["n_steps"], 0)
        self.assertEqual(progression([3])["changes"], 0)


if __name__ == "__main__":
    unittest.main()
