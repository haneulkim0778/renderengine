"""Independent image-space expectations for caller geometry, never imported from C++."""

from pathlib import Path


def check(path, scene):
    data = Path(path).read_bytes()
    header = b"P6\n64 64\n255\n"
    assert data.startswith(header), "unexpected scene PPM header"
    pixels = data[len(header):]
    assert len(pixels) == 64 * 64 * 3, "incorrect scene payload size"
    if scene == "empty":
        assert not any(pixels), "empty scene is not black"
        return pixels
    # The gradient covers a right triangle from (8,8) to (56,8) to (8,56).
    # At pixel center (16.5,16.5), barycentric weights are 31/48,17/96,17/96.
    probes = [(2, 2, (0, 0, 0)), (48, 60, (0, 0, 0))]
    if scene in ("gradient", "multiple", "reverse"):
        probes += [(16, 40, (37, 45, 173)), (40, 16, (37, 173, 45))]
        if scene == "gradient":
            probes += [(16, 16, (165, 45, 45)), (48, 44, (0, 0, 0))]
        else:
            probes += [(20, 20, (0, 255, 255)), (48, 44, (255, 0, 255)),
                       (44, 50, (0, 0, 0))]
    elif scene in ("changed", "tie-reverse"):
        probes += [(16, 16, (255, 255, 0)), (16, 40, (255, 255, 0)),
                   (40, 16, (255, 255, 0)), (48, 44, (0, 0, 0))]
    else:
        raise AssertionError("unknown scene expectation")
    for x, y, expected in probes:
        offset = (y * 64 + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        assert all(abs(a - b) <= 2 for a, b in zip(actual, expected)), (
            f"{scene}: expected {expected}, got {actual} at ({x}, {y})"
        )
    return pixels
