# renderengine

A small C++20 rendering library and command-line demo that draw colored triangles
through Vulkan and return checked RGB images. The demo draws two overlapping
triangles with a real depth attachment: the green triangle stays in front of the
red triangle when draw order is reversed. There is
no window, surface, asset loader or runtime shader compiler.

## Requirements

- Windows x64 with Visual Studio 2022 C++ Build Tools (MSVC 14.44), or
  Linux x86-64 with Clang 18 and its matching `clang-scan-deps` tool.
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

## Use from C++

The build-tree target `renderengine::renderengine` supplies the C++20 named module,
its implementation and link dependencies. A consuming project can add this checkout
as a subdirectory and link the target; it does not include implementation sources,
Vulkan headers or generated shaders:

```cmake
cmake_minimum_required(VERSION 3.28)
# Use the renderer manifest, or provide equivalent dependencies in the parent manifest.
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/renderengine")
project(example LANGUAGES C CXX)
# Configure with the same vcpkg toolchain and SLANG_ROOT used above.
add_subdirectory(renderengine)
add_executable(example main.cpp)
target_link_libraries(example PRIVATE renderengine::renderengine)
set_target_properties(example PROPERTIES CXX_EXTENSIONS OFF)
```

Use Ninja and the supported compiler versions listed above. The toolchain must be
selected before the parent `project()` call, for example through
`-DCMAKE_TOOLCHAIN_FILE=renderengine/.tools/vcpkg/scripts/buildsystems/vcpkg.cmake`.
Set `SLANG_ROOT` to the provisioned Slang directory in that checkout. This is a
source/build-tree integration, not an installed SDK; compiler-specific module
artifacts are rebuilt for each toolchain. `BUILD_TESTING=OFF` omits the test consumer.
The target supplies C++20; importers must also disable compiler extensions to match
the module's compiler mode. In particular, Clang cannot import its strict C++20
module from a translation unit compiled in GNU C++20 mode.
`tests/consumer` also supports a standalone top-level configure, using this
checkout's vcpkg manifest, toolchain and Slang directory; it exercises the same
`add_subdirectory` integration without relying on the renderer's top-level build.

```cpp
#include <array>
#include <cstdint>
#include <span>
#include <vector>

import renderengine;

int main() {
    const std::array<renderengine::Vertex, 3> triangle{{
        {{-0.75F, -0.75F, 0.5F}, {1, 0, 0}},
        {{ 0.75F, -0.75F, 0.5F}, {0, 1, 0}},
        {{-0.75F,  0.75F, 0.5F}, {0, 0, 1}},
    }};
    const auto pixels = renderengine::render(renderengine::Settings{}, triangle);
    return pixels.size() == 256 * 256 * 3 ? 0 : 1;
}
```

`render(settings, std::span<const Vertex>)` accepts consecutive triples as ordered,
non-indexed triangles. The vertex count must be a multiple of three and at most
`max_vertices` (196608, or 65536 triangles). An empty span returns a black image.
All components must be finite: position x/y are in [-1,1], z is in [0,1], and RGB
is in [0,1]. Out-of-range values are rejected rather than clamped or clipped.
Positions use homogeneous w=1: x increases rightward, y downward; (-1,-1) is the
top-left image boundary and (1,1) the bottom-right. Pixel centers sample the
interior. This API accepts already normalized geometry, not world coordinates.

Vertex RGB is smoothly interpolated, written to an RGBA8 UNORM attachment and
returned as RGB without gamma conversion. Rendering is opaque and single-sampled,
with no face culling. Depth clears to 1 and uses strict `LESS`: smaller z wins,
z=1 does not draw against the clear value, and equal-depth fragments keep the first
triangle. `Settings::reverse` reverses complete triangles without changing their
vertex order. Only unequal-depth visibility is independent of triangle order;
boundary coverage follows rasterization rules and is not a cross-driver byte-hash
guarantee.

Both dimensions must be 32..2048. Bad dimensions, counts or components throw
`std::invalid_argument` before Vulkan setup, including when no Vulkan runtime is
available. A valid call still requires the selected Vulkan device and may fail
on runtime/resource requirements. Input is read-only, borrowed only for the
synchronous call, and must remain valid and unchanged by the caller until return.
Returned vectors own their tightly packed `width * height * 3` bytes, top row first;
later calls or input changes do not alter earlier results. Sequential calls are
independent; concurrent calls are not supported. The original `render(settings)`
overload retains the two-triangle demo and all CLI defaults.

### Object/world-space geometry

`render(settings, vertices, transform)` applies one caller-supplied `Transform` to
the entire triangle list. Its `elements` store a 4x4 matrix in **row-major** order
(`elements[row * 4 + column]`); the default is identity. Multiplication uses column
vectors: `clip = M * [x,y,z,1]`, so translation occupies elements 3, 7 and 11.
Compose model, view and projection in the caller as `projection * view * model`.
The library provides no camera or matrix-building utilities.

Positions in this overload are input-space coordinates. Position components and
all 16 matrix elements must be finite and in [-1000000,1000000], inclusive
(`max_transform_component`). This conservative arithmetic domain bounds each
shader dot product by `3 * 10^12 + 10^6`, preventing float overflow even before
cancellation; it is not a promise of precise rendering at extreme scales or near
degeneracies. Invalid matrices are rejected even for empty input, before Vulkan
setup. Singular matrices are accepted; degenerate triangles have no area.
RGB, dimensions, triangle counts, reversal, synchronous borrowing and owned-result
contracts are unchanged. The original two overloads still reject positions outside
their normalized ranges.

The vertex shader preserves homogeneous `w`. Vulkan clips against `-w <= x,y <= w`
and `0 <= z <= w` before perspective division, and interpolates RGB with perspective
correction. Off-screen or behind-camera triangles can be supplied, and partially
visible triangles are clipped. After division, x increases rightward and y downward;
depth still clears to 1 and uses strict `LESS`. A conventional y-up camera must
include a y flip in its supplied transform.

This example supplies a +Z-looking, y-down perspective camera with a 90-degree
vertical field of view, aspect ratio 3:2, near plane 1 and far plane 4:

```cpp
#include <array>
#include <cstdint>
#include <span>
#include <vector>

import renderengine;

int main() {
    renderengine::Settings settings;
    settings.width = 96;
    settings.height = 64;
    const std::array<renderengine::Vertex, 3> vertices{{
        {{-1.125F, -0.75F, 1}, {1, 0, 0}},
        {{ 2.25F,  -1.5F, 2}, {0, 1, 0}},
        {{ -4.5F,     3, 4}, {0, 0, 1}},
    }};
    const renderengine::Transform projection{{
        2.0F / 3, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 4.0F / 3, -4.0F / 3,
        0, 0, 1, 0,
    }};
    const auto pixels = renderengine::render(settings, vertices, projection);
    return pixels.size() == 96 * 64 * 3 ? 0 : 1;
}
```

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
and truncation. `harness` proves a successful process that writes nothing cannot
reuse stale images or leave a stale success report. `render` checks both orders at
256x256, 96x64 and 32x32, validates background/triangle/depth probes with a two-byte
channel tolerance, compares the two
orders on the same device, checks CLI/output failures and repeats ten renders.
Images remain under the build directory's `images` subdirectory. These probes do
not constitute an exhaustive rasterization test or cross-driver image hash.

`scene_consumer` is compiled separately in `tests/consumer` and links only the
public target. `consumer_build` additionally compiles the README's CMake and all complete C++
examples literally in a standalone parent project and runs the consumer's input checks.
It reuses the configured compiler, triplets and installed dependencies with
`VCPKG_MANIFEST_INSTALL=OFF`, so verification cannot reprovision shared packages
when `CC`/`CXX` are absent. It checks that installed package files remain unchanged.
`scene_input` rejects invalid dimensions, counts, NaN/infinity and component ranges
with an unavailable ICD, without entering Vulkan setup.
`scenes` renders caller geometry in one process: an asymmetric interpolated
triangle, changed colors, three triangles in both orders, empty input, repeated
input, depth ties, far-plane depth and the count limit. It checks immutable input
and independent owned results. `tests/scene_pixels.py` independently checks image
placement, interpolation, orientation and occlusion without reading renderer or
consumer geometry. `scene_oracle` checks corrupt-output rejection and stale-output
protection. Scene images and compact evidence are under `images/scenes`.

The same import-only consumer tests transformed geometry at 96x64: identity,
asymmetric affine placement, unequal-w perspective interpolation, depth in both
orders, near/side clipping, fully outside/behind/far geometry, empty/degenerate
input and the arithmetic/count boundaries. It checks borrowed vertex/matrix
immutability, changed inputs, cleared depth and independent owned results.
`tests/transform_pixels.py` derives expected colors and depth from independent
screen-space triangle coordinates and analytical barycentric weights, without
reading the renderer or consumer geometry. `transform_oracle` proves rejection of
wrong placement, affine interpolation, negative near-depth, reversed depth and
blank/truncated output. Invalid matrices/coordinates, including extreme finite
values and invalid empty calls, are checked without an available Vulkan ICD.

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
clang-format --dry-run --Werror src/*.cpp src/*.cppm tests/consumer/*.cpp
clang-tidy-18 -p build/debug src/main.cpp src/renderer.cpp src/renderengine.cppm tests/consumer/main.cpp tests/consumer/transforms.cpp
```

Build before running clang-tidy so module artifacts and dependency maps exist.
`python tests/incremental.py build/debug` checks a no-op build, then touches the
module interface and shader to verify dependency rebuilds without changing content.
It checks both CLI and independent consumer rebuild/relink behavior; shader edits
recompile the renderer without recompiling unrelated importer objects.
The focused checks are in `.clang-tidy`; compiler warnings are errors. GitHub
Actions builds both Windows configurations, runs the CPU-only input/oracle/harness
checks, and runs Linux software rendering, validation, formatting and analysis.
Hosted Windows builds do not establish hardware-rendering support; hardware
validation remains a separate execution gate.

## Design and scope

`src/renderengine.cppm` exports the named C++20 module `renderengine` with its
settings, vertex, transform and render interface. CMake's public `CXX_MODULES` file set on
`renderengine_core` and Ninja dependency scanning order interface and importer
builds. Standard library and Vulkan/volk
headers stay in global module fragments; neither `import std` nor third-party
header units are required. Module binary artifacts are compiler-specific and are
never shared between toolchains.

The renderer uses one graphics queue, one render pass and one submission. Vertex
data is flushed before submission. A 64-byte vertex push constant carries the
transform; Slang compilation explicitly selects row-major layout. The render pass clears depth to 1, tests with
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
There are no performance targets, swapchain, camera helpers, textures or scene-file API in
this milestone.

References: [Vulkan version compatibility](https://docs.vulkan.org/guide/latest/versions.html),
[Slang SPIR-V targets](https://shader-slang.org/slang/user-guide/spirv-target-specific.html),
[CMake named modules](https://cmake.org/cmake/help/v3.31/manual/cmake-cxxmodules.7.html).
