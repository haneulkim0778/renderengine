"""Ensure the independent oracle rejects plausible rendering failures."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_pixels import check


class OracleTest(unittest.TestCase):
    def test_rejects_blank_wrong_depth_and_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "fixture.ppm"
            pixels = bytearray(256 * 256 * 3)
            header = b"P6\n256 256\n255\n"
            image.write_bytes(header + pixels)
            with self.assertRaisesRegex(AssertionError, "far triangle"):
                check(image)
            for x, y, rgb in [(64, 64, (255, 0, 0)), (192, 192, (0, 255, 0)),
                              (128, 128, (255, 0, 0))]:
                offset = (y * 256 + x) * 3
                pixels[offset:offset + 3] = bytes(rgb)
            image.write_bytes(header + pixels)
            with self.assertRaisesRegex(AssertionError, "depth overlap"):
                check(image)
            image.write_bytes(header + pixels[:-1])
            with self.assertRaisesRegex(AssertionError, "payload size"):
                check(image)


if __name__ == "__main__":
    unittest.main()
