module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module renderengine;

export namespace renderengine {
enum class DeviceClass { any, hardware, software };
struct Settings {
    std::uint32_t width = 256;
    std::uint32_t height = 256;
    bool reverse = false;
    bool validation = false;
    DeviceClass device = DeviceClass::any;
};

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> color;
};

// At most 65,536 non-indexed triangles per synchronous call.
inline constexpr std::size_t max_vertices = 196608;

// Consecutive triples form triangles; empty input clears to black. Input is not retained or modified.
// Finite position components: x/y in [-1,1] (right/down), z in [0,1]; RGB in [0,1].
// Depth clears to 1 and uses strict LESS. Reverse changes triangle order, not vertex winding.
// Returns owned tightly packed RGB bytes, top row first. Dimensions must be 32..2048.
// Invalid input throws std::invalid_argument before Vulkan setup; Vulkan failures throw std::runtime_error.
std::vector<std::uint8_t> render(const Settings &settings, std::span<const Vertex> vertices);

// Returns tightly packed RGB pixels, top row first. Throws on any failed requirement.
// Compatibility demo: renders the original two-triangle depth fixture.
std::vector<std::uint8_t> render(const Settings &settings);
} // namespace renderengine
