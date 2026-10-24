"""Analytical screen-space oracle; no renderer matrices or C++ geometry are read."""

from pathlib import Path

WIDTH, HEIGHT = 96, 64
NAMES = ("identity", "affine", "perspective", "depth", "depth-reverse", "near", "side",
         "outside", "behind", "far", "empty", "degenerate", "large", "large-products")
BLACK = {"outside", "behind", "far", "empty", "degenerate", "large-products"}


def weights(x, y, points):
    """Signed area coordinates at a pixel center."""
    a, b, c = points
    denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    u = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / denominator
    v = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / denominator
    return u, v, 1 - u - v


def probes(scene, *, linear=False, negative_near=False, wrong_depth=False):
    assert scene in NAMES, "unknown transform scene"
    # These are independently derived projected pixel positions and clip w values.
    points, clip_w, depths = ((12, 8), (84, 8), (12, 56)), (1, 1, 1), (0.5, 0.5, 0.5)
    if scene == "affine":
        points = ((60, 24), (60, 56), (12, 24))
    elif scene in ("perspective", "depth", "depth-reverse"):
        clip_w, depths = (1, 2, 4), (0, 2 / 3, 1)
    elif scene == "near":
        clip_w, depths = (0.5, 2, 2), (-4 / 3, 2 / 3, 2 / 3)
    elif scene == "side":
        points, clip_w = ((-48, 8), (84, 8), (84, 56)), (2, 2, 2)
    elif scene == "large":
        points = ((0, 0), (96, 0), (0, 64))
    for y in range(3, HEIGHT, 6):
        for x in range(3, WIDTH, 6):
            if scene in BLACK:
                yield x, y, (0, 0, 0)
                continue
            bary = weights(x + 0.5, y + 0.5, points)
            if any(abs(value) < 0.025 for value in bary):
                continue  # Do not turn edge coverage precision into a cross-driver promise.
            rgb = (0, 0, 0)
            depth = sum(a * b for a, b in zip(bary, depths))
            near = -1 if negative_near else 0
            if scene == "near" and abs(depth - near) < 0.04:
                continue
            if min(bary) > 0 and near <= depth < 1:
                corrected = [a / w for a, w in zip(bary, clip_w)]
                color = bary if linear else [a / sum(corrected) for a in corrected]
                rgb = tuple(round(255 * a) for a in color)
                if scene in ("depth", "depth-reverse"):
                    if abs(depth - 0.5) < 0.04:
                        continue
                    if (depth > 0.5) != wrong_depth:
                        rgb = (255, 255, 0)
            yield x, y, rgb


def check(path, scene):
    data = Path(path).read_bytes()
    header = b"P6\n96 64\n255\n"
    assert data.startswith(header), "unexpected transform image header"
    pixels = data[len(header):]
    assert len(pixels) == WIDTH * HEIGHT * 3, "incorrect transform payload size"
    if scene in BLACK:
        assert not any(pixels), f"{scene}: expected black image"
    for x, y, expected in probes(scene):
        offset = (y * WIDTH + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        assert all(abs(a - b) <= 3 for a, b in zip(actual, expected)), (
            f"{scene}: expected {expected}, got {actual} at ({x}, {y})"
        )
    return pixels
