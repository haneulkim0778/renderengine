"""Regression for a successful renderer process that produces no fresh image."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import acceptance
from check_pixels import check


def stale_image(path, width, height):
    pixels = bytearray(width * height * 3)
    for u, v, rgb in [(0.25, 0.25, (255, 0, 0)), (0.75, 0.75, (0, 255, 0)),
                      (0.50, 0.50, (0, 255, 0))]:
        offset = (int(v * height) * width + int(u * width)) * 3
        pixels[offset:offset + 3] = bytes(rgb)
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + pixels)
    check(path, width, height)  # Establish that stale output would satisfy the oracle.


class AcceptanceTest(unittest.TestCase):
    def test_success_without_output_cannot_reuse_stale_images(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "renderer"
            executable.touch()
            output = root / "images"
            output.mkdir()
            for width, height in [(256, 256), (96, 64), (32, 32)]:
                for order in ["forward", "reverse"]:
                    stale_image(output / f"{width}x{height}-{order}.ppm", width, height)
            stale_image(output / "repeat.ppm", 256, 256)
            (output / "evidence.json").write_text('{"pixel_oracle": "passed"}', encoding="utf-8")

            def process_without_output(command, **_kwargs):
                if "--help" in command:
                    return subprocess.CompletedProcess(command, 0, "Usage:", "")
                if "--device" in command and "software" in command and "--output" in command:
                    if command[command.index("--output") + 1] == str(output):
                        return subprocess.CompletedProcess(command, 1, "", "error: output")
                    return subprocess.CompletedProcess(command, 0,
                                                       "device=software\nvalidation=sync errors=0 warnings=0\n", "")
                return subprocess.CompletedProcess(command, 1, "", "error: invalid argument")

            arguments = ["acceptance.py", str(executable), str(output), "--device", "software", "--validation"]
            # Substitute only the external process boundary; run the real harness and oracle.
            with patch.object(sys, "argv", arguments), \
                    patch.object(acceptance.subprocess, "run", side_effect=process_without_output):
                with self.assertRaises(FileNotFoundError):
                    acceptance.main()
            self.assertFalse((output / "evidence.json").exists())


if __name__ == "__main__":
    unittest.main()
