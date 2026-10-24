"""Run the independent module consumer and check fresh scene images."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from scene_pixels import check


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--device", choices=["any", "hardware", "software"], default="any")
    parser.add_argument("--validation", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    names = ("gradient", "changed", "multiple", "reverse", "empty", "tie-reverse")
    evidence = args.output / "evidence.json"
    evidence.unlink(missing_ok=True)
    for name in names:
        (args.output / f"{name}.ppm").unlink(missing_ok=True)
    command = [str(args.executable.resolve()), str(args.output.resolve()), args.device]
    if args.validation:
        command.append("--validation")
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "scene consumer passed" in result.stdout, result.stdout
    assert "validation error:" not in result.stderr, result.stderr
    assert "validation warning:" not in result.stderr, result.stderr
    if args.device != "any":
        assert f"device={args.device}" in result.stdout, result.stdout
    if args.validation:
        assert "validation=sync errors=0 warnings=0" in result.stdout, result.stdout
    images = {name: check(args.output / f"{name}.ppm", name) for name in names}
    assert images["multiple"] == images["reverse"], "triangle order changed unequal-depth output"
    evidence.write_text(json.dumps({"device_requirement": args.device,
                                    "validation_required": args.validation,
                                    "scene_oracle": "passed", "scenes": list(names),
                                    "same_process_ownership": "passed"}, indent=2) + "\n",
                        encoding="utf-8")
    print("independent scene acceptance passed")


if __name__ == "__main__":
    main()
