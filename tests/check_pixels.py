"""Independent P6 oracle: expected colors at normalized image-space probes.

Deliberately does not import renderer geometry, shaders or generated data.
Probes avoid rasterization edges. RGB byte tolerance is two.
"""

import argparse
from pathlib import Path


def check(path, width=256, height=256):
    data = Path(path).read_bytes()
    header = f"P6\n{width} {height}\n255\n".encode("ascii")
    assert data.startswith(header), "unexpected PPM header or dimensions"
    pixels = data[len(header):]
    assert len(pixels) == width * height * 3, "incorrect pixel payload size"
    probes = [
        (0.05, 0.05, (0, 0, 0), "background"),
        (0.25, 0.25, (255, 0, 0), "far triangle"),
        (0.75, 0.75, (0, 255, 0), "near triangle"),
        (0.50, 0.50, (0, 255, 0), "depth overlap"),
        (0.95, 0.05, (0, 0, 0), "outside geometry"),
    ]
    for u, v, expected, label in probes:
        x, y = int(u * width), int(v * height)
        offset = (y * width + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        assert all(abs(a - b) <= 2 for a, b in zip(actual, expected)), (
            f"{label}: expected {expected}, got {actual} at ({x}, {y})"
        )
    return pixels


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--width", type=int, default=256)
    parser.add_argument("--height", type=int, default=256)
    args = parser.parse_args()
    check(args.image, args.width, args.height)
    print("pixel oracle passed")
