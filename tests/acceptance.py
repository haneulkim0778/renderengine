"""Bounded end-to-end rendering and failure checks, usable through CTest."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_pixels import check


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--device", choices=["any", "hardware", "software"], default="any")
    parser.add_argument("--validation", action="store_true")
    args = parser.parse_args()
    assert args.executable.is_file(), "renderer executable has not been implemented"
    args.output.mkdir(parents=True, exist_ok=True)

    def run(options, success=True, diagnostic=None):
        result = subprocess.run([str(args.executable.resolve()), *options],
                                capture_output=True, text=True, timeout=30)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        if diagnostic:
            assert diagnostic in result.stdout + result.stderr, result.stdout + result.stderr
        assert "validation error:" not in result.stderr, result.stderr
        return result

    run(["--help"], diagnostic="Usage:")
    for options in (["--width", "0"], ["--width", "31"], ["--height", "2049"], ["--width", "-1"],
                    ["--width", "12x"], ["--height", "999999999999999999999"],
                    ["--unknown"], ["--output"], ["--device", "imaginary"],
                    ["--order", "imaginary"], ["--output", ""]):
        run(options, False, "error:")
    common = ["--device", args.device] + (["--validation"] if args.validation else [])
    runtime = []
    for width, height in [(256, 256), (96, 64), (32, 32)]:
        images = []
        for order in ["forward", "reverse"]:
            output = args.output / f"{width}x{height}-{order}.ppm"
            result = run(common + ["--output", str(output), "--width", str(width),
                                   "--height", str(height), "--order", order])
            if args.device != "any":
                assert f"device={args.device}" in result.stdout, result.stdout
            if args.validation:
                assert "validation=sync errors=0 warnings=0" in result.stdout, result.stdout
            runtime = [line for line in result.stdout.splitlines()
                       if line.startswith(("device=", "validation="))]
            images.append(check(output, width, height))
        assert images[0] == images[1], "submission order changes visible geometry"
    run(common + ["--output", str(args.output)], False, "output")
    # Deliberately repeated to exercise creation, submission and clean teardown.
    for iteration in range(10):
        output = args.output / "repeat.ppm"
        run(common + ["--output", str(output), "--order",
                      "forward" if iteration % 2 else "reverse"])
        check(output)
    evidence = {"device_requirement": args.device, "validation_required": args.validation,
                "pixel_oracle": "passed", "order_invariance": "passed",
                "sizes_checked": ["256x256", "96x64", "32x32"], "repeat_runs": 10,
                "runtime_summary": runtime}
    (args.output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"acceptance passed: device={args.device}, validation={args.validation}")


if __name__ == "__main__":
    main()
