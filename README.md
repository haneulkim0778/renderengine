# renderengine

A small C++20 command-line renderer that draws two overlapping triangles through
Vulkan and writes a checked RGB image. It uses a real depth attachment: the green
triangle stays in front of the red triangle when draw order is reversed. There is
no window, surface, asset loader or runtime shader compiler.

## Requirements

- Windows x64 with Visual Studio 2022 C++ Build Tools (MSVC 14.34 or newer), or
  Linux x86-64 with Clang 18.1.8 and its matching `clang-scan-deps` tool.
- CMake 3.28+, Ninja 1.11+, Python 3.12+, Git, and network access for provisioning.
  CI uses CMake 3.31.6, Ninja 1.11.1.4 and Python 3.12.10.
- A Vulkan loader and a device supporting Vulkan 1.2, a graphics queue,
  single-sampled RGBA8 color/transfer images and D32 depth images.
- For `--validation`, the Khronos validation layer with `VK_EXT_validation_features`
  and debug-utils support. A Vulkan SDK provides this on Windows; Linux distributions
  commonly package it as `vulkan-validationlayers`.

SDK/header versions do not establish device capability. The executable queries the
runtime and required image usages. Hardware is preferred by default; CPU Vulkan
implementations are usable for software correctness testing.

## Build

On Windows, run from an **x64 Native Tools Command Prompt for VS 2022**. On Linux,
install Clang 18 plus `clang-tools-18`, a C++ standard library/development headers,
and the normal vcpkg prerequisites (CMake, Ninja, curl, zip, unzip, tar and pkg-config).
Use `python3` in place of `python` where required.

```sh
python scripts/bootstrap.py
cmake --preset debug
cmake --build --preset debug
```

On Linux, select the matched compiler before configuring:

```sh
CC=clang-18 CXX=clang++-18 cmake --preset debug
cmake --build --preset debug
```

Use `release` instead of `debug` for a separate optimized build. Do not reuse a
build directory across operating systems or compilers. When sharing a checkout
with WSL, override the binary directory, for example `cmake --preset debug -B
build/linux-debug`, then use `cmake --build build/linux-debug` and `ctest --test-dir
build/linux-debug --output-on-failure`.

The bootstrap script checks out the vcpkg commit also recorded in `vcpkg.json`,
downloads Slang **2026.17**, verifies its SHA-256 and extracts it under `.tools`.
vcpkg supplies volk, Vulkan headers and host SPIRV-Tools from that baseline.
Slang emits SPIR-V 1.5 offline; every shader build runs `spirv-val --target-env
vulkan1.2` before embedding words into the executable. Shader edits rebuild these
artifacts automatically. The executable can be moved without adjacent shader files.
Bootstrap changes no drivers or global tool configuration. Preserve dependency
licenses if redistributing tool or dependency binaries; their licenses are separate
from this project's MIT license.

## Run

```sh
build/debug/renderengine --output forward.ppm --device hardware --validation
build/debug/renderengine --output reverse.ppm --order reverse --device hardware --validation
python tests/check_pixels.py forward.ppm
python tests/check_pixels.py reverse.ppm
```

On Windows the executable has an `.exe` suffix. PPM P6 output is tightly packed
8-bit RGB, top row first, with a black background. Image viewers supporting Netpbm
can open it directly.

| Option | Accepted values / default |
| --- | --- |
| `--output` | File path; default `render.ppm`; overwrites an existing file |
| `--width`, `--height` | Integers 32 through 2048; default 256 each |
| `--order` | `forward` (red then green) or `reverse` |
| `--device` | `any` (default), `hardware`, or `software` |
| `--validation` | Require core and synchronization validation; fail on warnings/errors |
| `--help` | Print usage without creating a Vulkan instance |

Exit code 0 means success; 1 means invalid input, unavailable requirements, Vulkan
failure, validation diagnostics or output failure. Diagnostics identify the failed
operation. `hardware` accepts discrete, integrated and virtual GPUs; `software`
accepts CPU devices. Neither silently substitutes the other class. Successful runs
report device class and API version without device identifiers. A ten-second fence
deadline detects stalled GPU work; tests additionally enforce a 30-second process
timeout because driver calls and safe teardown are outside application deadline
control. Output is written after rendering; file writes are not atomic.

## Test and quality checks

```sh
cmake --preset debug -DRENDERENGINE_TEST_DEVICE=hardware -DRENDERENGINE_TEST_VALIDATION=ON
cmake --build --preset debug
ctest --preset debug
```

`oracle` proves the independent Python checker rejects blank output, wrong overlap
and truncation. `render` checks both orders at 256×256, 96×64 and 32×32, validates
background/triangle/depth probes with a two-byte channel tolerance, compares the two
orders on the same device, checks CLI/output failures and repeats ten renders.
Images remain under the build directory's `images` subdirectory. These probes do
not constitute an exhaustive rasterization test or cross-driver image hash.

For software Vulkan, restrict the loader to your installed Mesa lavapipe ICD using
`VK_DRIVER_FILES`, then configure `RENDERENGINE_TEST_DEVICE=software`. On Debian or
Ubuntu the manifest can be discovered without assuming its filename:

```sh
export VK_DRIVER_FILES="$(dpkg -L mesa-vulkan-drivers | grep '/lvp_icd.*\.json$')"
cmake --preset debug -DRENDERENGINE_TEST_DEVICE=software -DRENDERENGINE_TEST_VALIDATION=ON
cmake --build --preset debug
ctest --preset debug
python tests/reject_hardware.py build/debug/renderengine
```

For source quality, use clang-format **18.1.8** and clang-tidy from the **same Clang
18 distribution used to compile the Linux modules**:

```sh
clang-format --dry-run --Werror src/*.cpp src/*.cppm
clang-tidy-18 -p build/debug src/main.cpp src/renderer.cpp src/renderengine.cppm
```

Build before running clang-tidy so module artifacts and dependency maps exist.
`python tests/incremental.py build/debug` checks a no-op build, then touches the
module interface and shader to verify dependency rebuilds without changing content.
The focused checks are in `.clang-tidy`; compiler warnings are errors. GitHub
Actions builds both Windows configurations and runs the Linux software-render,
validation, formatting and analysis checks. Hosted Windows builds do not establish
hardware-rendering support; hardware validation remains a separate execution gate.

## Design and scope

`src/renderengine.cppm` exports the named C++20 module `renderengine` with its small
settings/render interface. CMake's `CXX_MODULES` file set and Ninja dependency
scanning order interface and importer builds. Standard library and Vulkan/volk
headers stay in global module fragments; neither `import std` nor third-party
header units are required. Module binary artifacts are compiler-specific and are
never shared between toolchains.

The renderer uses one graphics queue, one render pass and one submission. Vertex
data is flushed before submission. The render pass clears depth to 1, tests with
`LESS`, and makes color writes available to the transfer copy. A transfer-to-host
barrier, completion fence and mapped-memory invalidation precede readback, including
on non-coherent memory. The resource owner waits for device completion and destroys
objects before their backing memory. Validation remains active through teardown.

The fixture has red depth 0.7 and green depth 0.2, with no blending or multisampling.
Depth is tested independently of submission order. The pixel checker contains its
own normalized probes and expected colors, without importing renderer geometry.

WebGPU is a later option when browser distribution is needed; there is no universal
backend interface yet. Robot Framework would add another runner without improving
the current pixel oracle, so CTest and Python own acceptance. WSL is useful for
Linux software checks, not evidence of native Linux GPU or presentation support.
There are no performance targets, swapchain, cameras, textures or scene-file API in
this milestone.

References: [Vulkan version compatibility](https://docs.vulkan.org/guide/latest/versions.html),
[Slang SPIR-V targets](https://shader-slang.org/slang/user-guide/spirv-target-specific.html),
[CMake named modules](https://cmake.org/cmake/help/v3.31/manual/cmake-cxxmodules.7.html).
