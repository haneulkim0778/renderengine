"""Build the README parent project and run independent public-module input checks."""

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

readme = (root / "README.md").read_text(encoding="utf-8")
source = build / "documented-consumer-source"
source.mkdir(exist_ok=True)
cmake_example = readme.split("```cmake\n", 1)[1].split("```", 1)[0]
# Relocate only the checkout; compile the documented setup and main unchanged.
cmake_example = cmake_example.replace("${CMAKE_CURRENT_SOURCE_DIR}/renderengine", root.as_posix())
cmake_example = cmake_example.replace("add_subdirectory(renderengine)",
                                      f'add_subdirectory("{root.as_posix()}" renderengine)')
cmake_example += f'\nadd_subdirectory("{root.as_posix()}/tests/consumer" consumer)\n'
(source / "CMakeLists.txt").write_text(cmake_example, encoding="utf-8")
(source / "main.cpp").write_text(readme.split("```cpp\n", 1)[1].split("```", 1)[0], encoding="utf-8")

consumer = build / "documented-consumer"
# Dependencies already belong to this compiler/build. A second manifest install
# can select a different dependency compiler from the test runner's environment.
command = ["cmake", "-S", str(source), "-B", str(consumer), "-G", "Ninja",
           "-DBUILD_TESTING=OFF", "-DVCPKG_MANIFEST_INSTALL=OFF",
           f"-DPython3_EXECUTABLE={sys.executable}"]
for key in ("CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_MAKE_PROGRAM",
            "CMAKE_TOOLCHAIN_FILE", "VCPKG_INSTALLED_DIR", "VCPKG_TARGET_TRIPLET",
            "VCPKG_HOST_TRIPLET", "SLANG_ROOT"):
    assert cache.get(key), f"configured build is missing {key}"
    command.append(f"-D{key}={cache[key]}")


def dependency_state():
    """Detect package replacement or writes, even when file contents match."""
    installed = Path(cache["VCPKG_INSTALLED_DIR"])
    state = {}
    for path in installed.rglob("*"):
        if path.is_file():
            stat = path.stat()
            state[path.relative_to(installed)] = (stat.st_size, stat.st_mtime_ns)
    return state


dependencies = dependency_state()
assert dependencies, "configured build has no installed dependencies"
environment = dict(os.environ)
for key in ("CC", "CXX"):
    environment.pop(key, None)
subprocess.run(command, check=True, timeout=180, env=environment)
subprocess.run(["cmake", "--build", str(consumer), "--target", "example", "scene_consumer", "--parallel", "2"],
               check=True, timeout=180, env=environment)
assert dependency_state() == dependencies, "standalone build modified provisioned dependencies"
executable = consumer / "consumer" / ("scene_consumer.exe" if os.name == "nt" else "scene_consumer")
environment["VK_DRIVER_FILES"] = "renderengine-no-such-icd.json"
subprocess.run([str(executable), "--invalid-only"], check=True, timeout=30, env=environment)
print("standalone parent-project module consumption passed")
