"""Prove a standalone parent project can consume the public module target."""

import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
build = Path(sys.argv[1]).resolve()
cache = {}
for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
    if line and not line.startswith(("#", "//")) and "=" in line:
        key, value = line.split("=", 1)
        cache[key.split(":", 1)[0]] = value

consumer = build / "standalone-consumer"
command = ["cmake", "-S", str(root / "tests" / "consumer"), "-B", str(consumer), "-G", "Ninja",
           f"-DVCPKG_MANIFEST_DIR={root}", f"-DPython3_EXECUTABLE={sys.executable}"]
for key in ("CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_MAKE_PROGRAM",
            "CMAKE_TOOLCHAIN_FILE", "VCPKG_INSTALLED_DIR", "SLANG_ROOT"):
    assert cache.get(key), f"configured build is missing {key}"
    command.append(f"-D{key}={cache[key]}")
subprocess.run(command, check=True, timeout=180)
subprocess.run(["cmake", "--build", str(consumer), "--target", "scene_consumer", "--parallel", "2"],
               check=True, timeout=180)
executable = consumer / ("scene_consumer.exe" if os.name == "nt" else "scene_consumer")
environment = dict(os.environ, VK_DRIVER_FILES="renderengine-no-such-icd.json")
subprocess.run([str(executable), "--invalid-only"], check=True, timeout=30, env=environment)
print("standalone parent-project module consumption passed")
