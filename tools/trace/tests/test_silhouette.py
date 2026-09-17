"""Tests for the orientation metric both sides of the comparison share.

The metric's whole value is that ONE piece of code decides what "lit" means for
a libretro XRGB8888 callback and for a PPM written by glReadPixels. If it were
wrong, "the ROM is face-on and the native build is a quarter turn off" would be
a statement about two different measurements - which is exactly the instrument
fault this repository keeps catching in itself.

So what is asserted here is not that the numbers are pretty. It is that the
metric RESPONDS to rotation, DOES NOT respond to scale, and says zero out loud
when it sees nothing.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from silhouette import (BLACK_LEVEL, SAMPLE_STRIDE,          # noqa: E402
                        lit_bbox, ppm_metrics, read_ppm)


def box_is_lit(x0, x1, y0, y1):
    """A filled rectangle - the simplest thing with a known bounding box."""
    return lambda x, y: x0 <= x <= x1 and y0 <= y <= y1


class TestLitBbox(unittest.TestCase):
    W, H = 640, 480

    def test_black_frame_reports_zero_explicitly(self):
        """An absent silhouette must not read as a zero-width one."""
        bb = lit_bbox(self.W, self.H, lambda x, y: False)
        self.assertEqual(bb["lit"], 0)
        self.assertGreater(bb["samples"], 0)        # it DID look
        # -1, not 0: "no logo" and "an infinitely thin logo" are different.
        self.assertEqual(bb["span_x"], -1)
        self.assertEqual(bb["span_y"], -1)
        self.assertEqual(bb["aspect"], -1.0)

    def test_span_is_x1_minus_x0_exactly(self):
        """The name says x1 - x0. It must be x1 - x0, with nothing added.

        The measured fault this guards: a previous instrument printed
        end - start and called it "total", and the wrong number travelled.
        """
        bb = lit_bbox(self.W, self.H, box_is_lit(64, 200, 96, 160))
        self.assertEqual(bb["x0"], 64)              # on the sampling grid
        self.assertEqual(bb["x1"], 200)
        self.assertEqual(bb["span_x"], bb["x1"] - bb["x0"])
        self.assertEqual(bb["span_y"], bb["y1"] - bb["y0"])
        self.assertAlmostEqual(bb["aspect"], bb["span_x"] / bb["span_y"])

    def test_aspect_falls_as_the_shape_narrows(self):
        """The orientation signal: a Y rotation narrows x and leaves y alone."""
        wide = lit_bbox(self.W, self.H, box_is_lit(64, 448, 200, 264))
        narrow = lit_bbox(self.W, self.H, box_is_lit(240, 272, 200, 264))
        self.assertEqual(wide["span_y"], narrow["span_y"])   # height unchanged
        self.assertLess(narrow["span_x"], wide["span_x"])
        self.assertLess(narrow["aspect"], wide["aspect"])

    def test_aspect_is_invariant_to_scale(self):
        """Why aspect and not width: the logo zooms in during its first second.

        A metric that moved with scale would report that ramp as rotation.
        """
        small = lit_bbox(self.W, self.H, box_is_lit(160, 288, 208, 240))
        big = lit_bbox(self.W, self.H, box_is_lit(64, 320, 176, 240))
        self.assertEqual(big["span_x"], 2 * small["span_x"])
        self.assertEqual(big["span_y"], 2 * small["span_y"])
        self.assertAlmostEqual(small["aspect"], big["aspect"])

    def test_threshold_is_above_black_level_not_at_it(self):
        at = lit_bbox(self.W, self.H, lambda x, y: BLACK_LEVEL > BLACK_LEVEL)
        self.assertEqual(at["lit"], 0)
        above = lit_bbox(self.W, self.H,
                         lambda x, y: BLACK_LEVEL + 1 > BLACK_LEVEL)
        self.assertEqual(above["lit"], above["samples"])

    def test_sample_count_matches_the_declared_grid(self):
        bb = lit_bbox(self.W, self.H, lambda x, y: True)
        expected = len(range(0, self.W, SAMPLE_STRIDE)) * \
            len(range(0, self.H, SAMPLE_STRIDE))
        self.assertEqual(bb["samples"], expected)
        self.assertEqual(bb["lit"], expected)

    def test_a_single_lit_sample_has_zero_span_not_absent_span(self):
        """One lit pixel is a real finding with span 0 - distinct from none."""
        bb = lit_bbox(self.W, self.H, lambda x, y: x == 64 and y == 96)
        self.assertEqual(bb["lit"], 1)
        self.assertEqual(bb["span_x"], 0)
        self.assertEqual(bb["span_y"], 0)
        # span_y == 0 cannot divide; that is reported, not raised.
        self.assertEqual(bb["aspect"], -1.0)


class TestPpm(unittest.TestCase):
    """The native side's decoder, checked against a file it did not write."""

    def write_ppm(self, path, w, h, pixel):
        body = bytearray()
        for y in range(h):
            for x in range(w):
                body.extend(bytes(pixel(x, y)))
        path.write_bytes(b"P6\n%d %d\n255\n" % (w, h) + bytes(body))

    def setUp(self):
        import tempfile
        self.dir = tempfile.TemporaryDirectory()
        self.tmp = Path(self.dir.name)

    def tearDown(self):
        self.dir.cleanup()

    def test_roundtrip_dimensions_and_pixels(self):
        p = self.tmp / "a.ppm"
        self.write_ppm(p, 64, 32, lambda x, y: (x % 256, 0, 0))
        w, h, px = read_ppm(p)
        self.assertEqual((w, h), (64, 32))
        self.assertEqual(len(px), 64 * 32 * 3)

    def test_comments_in_the_header_are_skipped(self):
        """A parser that assumes its own writer's layout checks nothing."""
        p = self.tmp / "c.ppm"
        p.write_bytes(b"P6\n# written by something else\n4 2\n255\n"
                      + bytes(4 * 2 * 3))
        w, h, px = read_ppm(p)
        self.assertEqual((w, h, len(px)), (4, 2, 24))

    def test_truncated_file_is_refused_not_padded(self):
        p = self.tmp / "t.ppm"
        p.write_bytes(b"P6\n64 32\n255\n" + bytes(10))
        with self.assertRaises(ValueError):
            read_ppm(p)

    def test_metrics_find_a_known_rectangle(self):
        p = self.tmp / "r.ppm"
        self.write_ppm(p, 640, 480,
                       lambda x, y: (255, 255, 255)
                       if 64 <= x <= 448 and 200 <= y <= 264 else (0, 0, 0))
        m = ppm_metrics(p)
        self.assertEqual((m["x0"], m["x1"]), (64, 448))
        self.assertEqual(m["span_x"], 384)
        self.assertGreater(m["nonblack"], 0.0)
        # Same denominator as the ROM side, so the fractions are comparable.
        self.assertAlmostEqual(m["nonblack"], m["lit"] / m["samples"])

    def test_black_ppm_reports_zero_not_emptiness(self):
        p = self.tmp / "b.ppm"
        self.write_ppm(p, 64, 64, lambda x, y: (0, 0, 0))
        m = ppm_metrics(p)
        self.assertEqual(m["lit"], 0)
        self.assertEqual(m["nonblack"], 0.0)
        self.assertEqual(m["mean_luma"], 0.0)


if __name__ == "__main__":
    unittest.main()
