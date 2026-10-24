"""Check no-op builds, named-module importer rebuilds and offline shader rebuilds."""

from pathlib import Path
import subprocess
import sys
import time

root = Path(__file__).resolve().parent.parent
build = Path(sys.argv[1]).resolve()


def compile_project():
    subprocess.run(["cmake", "--build", str(build), "--target", "renderengine", "scene_consumer"], check=True)


def object_times():
    # Name the participating objects so obsolete artifacts cannot satisfy a rebuild check.
    objects = {
        "interface": "CMakeFiles/renderengine_core.dir/src/renderengine.cppm",
        "implementation": "CMakeFiles/renderengine_core.dir/src/renderer.cpp",
        "cli": "CMakeFiles/renderengine.dir/src/main.cpp",
        "consumer": "tests/consumer/CMakeFiles/scene_consumer.dir/main.cpp",
        "transform_consumer": "tests/consumer/CMakeFiles/scene_consumer.dir/transforms.cpp",
    }
    times = {}
    for name, stem in objects.items():
        candidates = [build / (stem + extension) for extension in (".obj", ".o")]
        found = [path for path in candidates if path.is_file()]
        assert len(found) == 1, f"expected one {name} object"
        times[name] = found[0].stat().st_mtime_ns
    return times


def executable_times():
    times = {}
    for stem in ("renderengine", "tests/consumer/scene_consumer"):
        found = [build / (stem + extension) for extension in ("", ".exe")
                 if (build / (stem + extension)).is_file()]
        assert len(found) == 1, f"expected executable {stem}"
        times[stem] = found[0].stat().st_mtime_ns
    return times


compile_project()
before = object_times()
linked = executable_times()
compile_project()
assert object_times() == before, "no-op build recompiled project objects"
assert executable_times() == linked, "no-op build relinked consumers"
time.sleep(1.1)  # Account for filesystems with coarse timestamp granularity.
(root / "src" / "renderengine.cppm").touch()
compile_project()
after = object_times()
assert all(after[path] > previous for path, previous in before.items()), "module edit did not rebuild importers"
assert all(executable_times()[path] > previous for path, previous in linked.items()), "module edit did not relink"
headers = [build / "generated" / f"{stage}_spv.hpp" for stage in ("vertex", "fragment")]
previous = {path: path.stat().st_mtime_ns for path in headers}
linked = executable_times()
time.sleep(1.1)
(root / "shaders" / "fixture.slang").touch()
compile_project()
assert all(path.stat().st_mtime_ns > stamp for path, stamp in previous.items()), "shader rebuild missing"
updated = object_times()
assert updated["implementation"] > after["implementation"], "shader edit did not rebuild renderer"
assert all(updated[name] == after[name] for name in ("interface", "cli", "consumer", "transform_consumer")), "unrelated shader rebuild"
assert all(executable_times()[path] > previous for path, previous in linked.items()), "shader edit did not relink"
print("incremental module and shader rebuild checks passed")
