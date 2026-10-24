"""Check no-op builds, named-module importer rebuilds and offline shader rebuilds."""

from pathlib import Path
import subprocess
import sys
import time

root = Path(__file__).resolve().parent.parent
build = Path(sys.argv[1]).resolve()


def compile_project():
    subprocess.run(["cmake", "--build", str(build), "--target", "renderengine"], check=True)


def object_times():
    objects = list((build / "CMakeFiles" / "renderengine.dir").rglob("*.obj"))
    objects += list((build / "CMakeFiles" / "renderengine.dir").rglob("*.o"))
    assert len(objects) == 3, "expected interface, implementation and CLI objects"
    return {path: path.stat().st_mtime_ns for path in objects}


compile_project()
before = object_times()
compile_project()
assert object_times() == before, "no-op build recompiled project objects"
time.sleep(1.1)  # Account for filesystems with coarse timestamp granularity.
(root / "src" / "renderengine.cppm").touch()
compile_project()
after = object_times()
assert all(after[path] > previous for path, previous in before.items()), "module edit did not rebuild importers"
header = build / "generated" / "vertex_spv.hpp"
previous = header.stat().st_mtime_ns
time.sleep(1.1)
(root / "shaders" / "fixture.slang").touch()
compile_project()
assert header.stat().st_mtime_ns > previous, "shader edit did not rebuild validated SPIR-V"
print("incremental module and shader rebuild checks passed")
