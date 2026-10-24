"""Prove the independent oracle rejects plausible transform/rendering defects."""

from pathlib import Path
import tempfile
import unittest
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from transform_pixels import WIDTH, HEIGHT, NAMES, probes, check


def image(scene, **mutations):
    pixels = bytearray(WIDTH * HEIGHT * 3)
    for x, y, rgb in probes(scene, **mutations):
        offset = (y * WIDTH + x) * 3
        pixels[offset:offset + 3] = bytes(rgb)
    return b"P6\n96 64\n255\n" + pixels


class TransformOracleTest(unittest.TestCase):
    def test_known_analytical_values(self):
        # At (15.5,9.5), screen weights are (265,14,9)/288, and w=(1,2,4).
        perspective = {(x, y): rgb for x, y, rgb in probes("perspective")}
        self.assertEqual(perspective[15, 9], (246, 7, 2))
        # At (51.5,27.5), affine weights are (137,21,34)/192.
        affine = {(x, y): rgb for x, y, rgb in probes("affine")}
        self.assertEqual(affine[51, 27], (182, 28, 45))
        near = {(x, y): rgb for x, y, rgb in probes("near")}
        self.assertEqual(near[51, 9], (0, 0, 0))

    def test_accepts_analytical_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.ppm"
            for name in NAMES:
                with self.subTest(name=name):
                    path.write_bytes(image(name))
                    check(path, name)

    def test_rejects_wrong_position_interpolation_clipping_depth_and_missing_output(self):
        corruptions = [
            ("affine", image("identity")),
            ("perspective", image("perspective", linear=True)),
            ("near", image("near", negative_near=True)),
            ("depth", image("depth", wrong_depth=True)),
            ("side", image("identity")),
            ("behind", image("perspective")),
            ("perspective", image("empty")),
            ("perspective", image("perspective")[:-1]),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.ppm"
            for name, data in corruptions:
                with self.subTest(name=name):
                    path.write_bytes(data)
                    with self.assertRaises(AssertionError):
                        check(path, name)


if __name__ == "__main__":
    unittest.main()
