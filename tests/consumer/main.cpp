#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import renderengine;

namespace {
using renderengine::Vertex;
using Pixels = std::vector<std::uint8_t>;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const std::array<Vertex, 3> gradient{{
    {{-0.75F, -0.75F, 0.7F}, {1, 0, 0}},
    {{0.75F, -0.75F, 0.7F}, {0, 1, 0}},
    {{-0.75F, 0.75F, 0.7F}, {0, 0, 1}},
}};

template <typename Function> void rejects(Function function) {
    try {
        function();
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error("invalid input was not rejected with invalid_argument");
}

void invalidInputs() {
    renderengine::Settings settings;
    // Validation must precede even a request for unavailable Vulkan validation layers.
    settings.validation = true;
    for (const auto size : {0U, 31U, 2049U, std::numeric_limits<std::uint32_t>::max()}) {
        settings.width = size;
        rejects([&] { (void)renderengine::render(settings, gradient); });
        rejects([&] { (void)renderengine::render(settings); });
        settings.width = 64;
        settings.height = size;
        rejects([&] { (void)renderengine::render(settings, gradient); });
        settings.height = 64;
    }
    for (const auto count : {1U, 2U, 4U}) {
        const std::vector<Vertex> incomplete(count);
        rejects([&] { (void)renderengine::render(settings, incomplete); });
    }
    const std::vector<Vertex> excessive(renderengine::max_vertices + 3);
    rejects([&] { (void)renderengine::render(settings, excessive); });
    for (std::size_t vertex = 0; vertex < gradient.size(); ++vertex) {
        for (std::size_t component = 0; component < 3; ++component) {
            for (const float value :
                 {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity(), -1.1F, 1.1F}) {
                auto input = gradient;
                input[vertex].position[component] = value;
                rejects([&] { (void)renderengine::render(settings, input); });
                input = gradient;
                input[vertex].color[component] = value;
                rejects([&] { (void)renderengine::render(settings, input); });
            }
            auto input = gradient;
            input[vertex].color[component] = -0.1F;
            rejects([&] { (void)renderengine::render(settings, input); });
        }
        auto input = gradient;
        input[vertex].position[2] = -0.1F;
        rejects([&] { (void)renderengine::render(settings, input); });
    }
    std::cout << "input validation passed\n";
}

void write(const std::filesystem::path &path, const Pixels &pixels) {
    require(pixels.size() == 64 * 64 * 3, "incorrect owned RGB result size");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << "P6\n64 64\n255\n";
    stream.write(reinterpret_cast<const char *>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    stream.close();
    require(static_cast<bool>(stream), "cannot write consumer image");
}

void scenes(const std::filesystem::path &output, renderengine::Settings settings) {
    settings.width = 64;
    settings.height = 64;
    auto input = gradient;
    auto first = renderengine::render(settings, input);
    const auto saved = first;
    for (std::size_t i = 0; i < input.size(); ++i) {
        require(input[i].position == gradient[i].position && input[i].color == gradient[i].color,
                "renderer mutated caller input");
    }
    write(output / "gradient.ppm", first);
    for (auto &vertex : input) {
        vertex.color = {1, 1, 0};
    }
    const auto changed = renderengine::render(settings, input);
    require(changed != saved && first == saved, "input changes or later calls alias owned output");
    write(output / "changed.ppm", changed);

    // Three triangles: a far gradient, an oppositely wound near triangle and a lower-right triangle.
    std::vector<Vertex> multiple(gradient.begin(), gradient.end());
    const std::array<Vertex, 6> extra{{
        {{-0.5F, -0.5F, 0.2F}, {0, 1, 1}},
        {{-0.5F, 0.0F, 0.2F}, {0, 1, 1}},
        {{0.0F, -0.5F, 0.2F}, {0, 1, 1}},
        {{0.25F, 0.25F, 0.4F}, {1, 0, 1}},
        {{0.75F, 0.25F, 0.4F}, {1, 0, 1}},
        {{0.75F, 0.75F, 0.4F}, {1, 0, 1}},
    }};
    multiple.insert(multiple.end(), extra.begin(), extra.end());
    const auto original = multiple;
    const auto forward = renderengine::render(settings, multiple);
    write(output / "multiple.ppm", forward);
    settings.reverse = true;
    const auto reverse = renderengine::render(settings, multiple);
    require(forward == reverse, "unequal depth depends on triangle order");
    require(std::equal(
                multiple.begin(), multiple.end(), original.begin(),
                [](const auto &a, const auto &b) { return a.position == b.position && a.color == b.color; }),
            "reverse rendering mutated caller input");
    write(output / "reverse.ppm", reverse);
    settings.reverse = false;

    const auto empty = renderengine::render(settings, {});
    require(std::all_of(empty.begin(), empty.end(), [](auto byte) { return byte == 0; }),
            "empty geometry retained prior color");
    write(output / "empty.ppm", empty);
    const auto again = renderengine::render(settings, gradient);
    require(again == saved && first == saved, "successive calls retained scene or depth state");
    first[0] = 123;
    require(again[0] == 0, "results share storage");

    std::vector<Vertex> tied(gradient.begin(), gradient.end());
    for (auto vertex : gradient) {
        vertex.color = {1, 1, 0};
        tied.push_back(vertex);
    }
    require(renderengine::render(settings, tied) == saved, "equal-depth fragment replaced first triangle");
    settings.reverse = true;
    write(output / "tie-reverse.ppm", renderengine::render(settings, tied));
    settings.reverse = false;
    for (auto &vertex : input) {
        vertex.position[2] = 1;
    }
    require(renderengine::render(settings, input) == empty, "far-plane fragment passed strict depth");

    // Accept the documented count limit and normalized endpoints without drawing visible fragments.
    const std::vector<Vertex> limit(renderengine::max_vertices, Vertex{{-1, 1, 0}, {0, 0, 0}});
    require(renderengine::render(settings, limit) == empty, "count boundary failed");
    std::cout << "scene consumer passed\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--invalid-only") {
            invalidInputs();
            return 0;
        }
        require(argc == 3 || argc == 4, "expected output directory, device and optional --validation");
        renderengine::Settings settings;
        const std::string_view device = argv[2];
        require(device == "any" || device == "hardware" || device == "software", "invalid test device");
        settings.device = device == "hardware"   ? renderengine::DeviceClass::hardware
                          : device == "software" ? renderengine::DeviceClass::software
                                                 : renderengine::DeviceClass::any;
        settings.validation = argc == 4;
        if (settings.validation) {
            require(std::string_view(argv[3]) == "--validation", "invalid validation option");
        }
        scenes(argv[1], settings);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
