module;

#include <cstdint>
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

// Returns tightly packed RGB pixels, top row first. Throws on any failed requirement.
std::vector<std::uint8_t> render(const Settings &settings);
} // namespace renderengine
