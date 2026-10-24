#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

import renderengine;

namespace {
std::uint32_t dimension(std::string_view text) {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 32 || value > 2048) {
        throw std::runtime_error("dimensions must be integers between 32 and 2048");
    }
    return value;
}
} // namespace

int main(int argc, char **argv) {
    try {
        renderengine::Settings settings;
        std::string output = "render.ppm";
        for (int i = 1; i < argc; ++i) {
            const std::string_view option = argv[i];
            if (option == "--help") {
                std::cout << "Usage: renderengine [--output FILE] [--width 32..2048] "
                             "[--height 32..2048]\n"
                             "  [--order forward|reverse] [--device any|hardware|software] "
                             "[--validation]\n"
                             "Defaults: render.ppm, 256x256, forward, any, validation off.\n";
                return 0;
            }
            if (option == "--validation") {
                settings.validation = true;
                continue;
            }
            if (option != "--output" && option != "--width" && option != "--height" && option != "--order" &&
                option != "--device") {
                throw std::runtime_error("unknown option; use --help");
            }
            if (++i == argc || std::string_view(argv[i]).empty()) {
                throw std::runtime_error("missing option value; use --help");
            }
            const std::string_view value = argv[i];
            if (option == "--output") {
                output = value;
            } else if (option == "--width") {
                settings.width = dimension(value);
            } else if (option == "--height") {
                settings.height = dimension(value);
            } else if (option == "--order") {
                if (value != "forward" && value != "reverse") {
                    throw std::runtime_error("order must be forward or reverse");
                }
                settings.reverse = value == "reverse";
            } else {
                if (value == "hardware") {
                    settings.device = renderengine::DeviceClass::hardware;
                } else if (value == "software") {
                    settings.device = renderengine::DeviceClass::software;
                } else if (value != "any") {
                    throw std::runtime_error("device must be any, hardware or software");
                } else {
                    settings.device = renderengine::DeviceClass::any;
                }
            }
        }
        const auto pixels = renderengine::render(settings);
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot open output file; check its parent directory and permissions");
        }
        stream << "P6\n" << settings.width << ' ' << settings.height << "\n255\n";
        stream.write(reinterpret_cast<const char *>(pixels.data()),
                     static_cast<std::streamsize>(pixels.size()));
        stream.close();
        if (!stream) {
            throw std::runtime_error("cannot write output file; check available space and permissions");
        }
        std::cout << "wrote " << settings.width << 'x' << settings.height << " RGB PPM\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
