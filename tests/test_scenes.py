"""Ensure scene checks reject corrupt rendering and stale consumer output."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from scene_pixels import check
import scenes


class SceneTest(unittest.TestCase):
    def test_oracle_rejects_color_orientation_depth_missing_geometry_and_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "scene.ppm"
            pixels = bytearray(64 * 64 * 3)
            probes = [(16, 40, (37, 45, 173)), (40, 16, (37, 173, 45)),
                      (20, 20, (0, 255, 255)), (48, 44, (255, 0, 255))]
            for x, y, rgb in probes:
                offset = (y * 64 + x) * 3
                pixels[offset:offset + 3] = bytes(rgb)
            header = b"P6\n64 64\n255\n"
            image.write_bytes(header + pixels)
            check(image, "multiple")
            for x, y, rgb in probes:
                broken = bytearray(pixels)
                offset = (y * 64 + x) * 3
                broken[offset:offset + 3] = b"\0\0\0"
                image.write_bytes(header + broken)
                with self.subTest(x=x, y=y), self.assertRaises(AssertionError):
                    check(image, "multiple")
            image.write_bytes(header + pixels[:-1])
            with self.assertRaisesRegex(AssertionError, "payload"):
                check(image, "multiple")
            image.write_bytes(header + pixels)
            with self.assertRaisesRegex(AssertionError, "black"):
                check(image, "empty")

    def test_success_without_images_cannot_reuse_stale_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "gradient.ppm").write_bytes(b"stale")
            (output / "evidence.json").write_text("stale", encoding="utf-8")
            result = subprocess.CompletedProcess([], 0, "scene consumer passed", "")
            arguments = ["scenes.py", "unused-consumer", str(output)]
            with patch.object(sys, "argv", arguments), \
                    patch.object(scenes.subprocess, "run", return_value=result):
                with self.assertRaises(FileNotFoundError):
                    scenes.main()
            self.assertFalse((output / "evidence.json").exists())
            self.assertFalse((output / "gradient.ppm").exists())


if __name__ == "__main__":
    unittest.main()
