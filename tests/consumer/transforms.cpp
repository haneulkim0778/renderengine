#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

import renderengine;

namespace {
using renderengine::Transform;
using renderengine::Vertex;
const std::array<Vertex, 3> triangle{{
    {{-0.75F, -0.75F, 0.5F}, {1, 0, 0}},
    {{0.75F, -0.75F, 0.5F}, {0, 1, 0}},
    {{-0.75F, 0.75F, 0.5F}, {0, 0, 1}},
}};

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function> void rejects(Function function) {
    try {
        function();
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error("invalid transform input was not rejected");
}

void write(const std::filesystem::path &output, const char *name, const std::vector<std::uint8_t> &pixels) {
    require(pixels.size() == 96 * 64 * 3, "incorrect transform image size");
    std::ofstream stream(output / (std::string("transform-") + name + ".ppm"),
                         std::ios::binary | std::ios::trunc);
    stream << "P6\n96 64\n255\n";
    stream.write(reinterpret_cast<const char *>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    stream.close();
    require(static_cast<bool>(stream), "cannot write transform image");
}
} // namespace

void transformInputs() {
    renderengine::Settings settings;
    settings.validation = true;
    for (const float bad :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(),
          -std::numeric_limits<float>::max(),
          std::nextafter(renderengine::max_transform_component, std::numeric_limits<float>::infinity()),
          std::nextafter(-renderengine::max_transform_component, -std::numeric_limits<float>::infinity())}) {
        for (std::size_t i = 0; i < 16; ++i) {
            Transform matrix;
            matrix.elements[i] = bad;
            rejects([&] { (void)renderengine::render(settings, triangle, matrix); });
            rejects([&] { (void)renderengine::render(settings, {}, matrix); });
        }
        for (std::size_t i = 0; i < 3; ++i) {
            auto vertices = triangle;
            vertices[0].position[i] = bad;
            rejects([&] { (void)renderengine::render(settings, vertices, Transform{}); });
            vertices = triangle;
            vertices[0].color[i] = bad;
            rejects([&] { (void)renderengine::render(settings, vertices, Transform{}); });
        }
    }
    for (const auto size : {0U, 31U, 2049U, std::numeric_limits<std::uint32_t>::max()}) {
        settings.width = size;
        rejects([&] { (void)renderengine::render(settings, triangle, Transform{}); });
        settings.width = 64;
        settings.height = size;
        rejects([&] { (void)renderengine::render(settings, {}, Transform{}); });
        settings.height = 64;
    }
    for (const auto count : {1U, 2U, 4U, 196609U}) {
        const std::vector<Vertex> vertices(count);
        rejects([&] { (void)renderengine::render(settings, vertices, Transform{}); });
    }
    for (const float bad : {-0.1F, 1.1F}) {
        auto vertices = triangle;
        vertices[1].color[2] = bad;
        rejects([&] { (void)renderengine::render(settings, vertices, Transform{}); });
    }
}

void transformScenes(const std::filesystem::path &output, renderengine::Settings settings) {
    settings.width = 96;
    settings.height = 64;
    const auto identity = renderengine::render(settings, triangle, Transform{});
    require(renderengine::render(settings, triangle) == identity,
            "identity transform differs from normalized rendering");
    write(output, "identity", identity);

    const std::array<Vertex, 3> object{{
        {{2, 1, 0}, {1, 0, 0}},
        {{4, 1, 0}, {0, 1, 0}},
        {{2, 3, 0}, {0, 0, 1}},
    }};
    const Transform affine{{0, -0.5F, 0, 0.75F, 0.5F, 0, 0, -1.25F, 0, 0, 1, 0.4F, 0, 0, 0, 1}};
    write(output, "affine", renderengine::render(settings, object, affine));
    rejects([&] { (void)renderengine::render(settings, object); });

    // +Z camera, vertical field of view 90 degrees, aspect 3:2, near 1, far 4.
    Transform perspective{{2.0F / 3, 0, 0, 0, 0, 1, 0, 0, 0, 0, 4.0F / 3, -4.0F / 3, 0, 0, 1, 0}};
    std::array<Vertex, 3> world{{
        {{-1.125F, -0.75F, 1}, {1, 0, 0}},
        {{2.25F, -1.5F, 2}, {0, 1, 0}},
        {{-4.5F, 3, 4}, {0, 0, 1}},
    }};
    const auto original = world;
    const auto matrix = perspective;
    auto first = renderengine::render(settings, world, perspective);
    const auto saved = first;
    write(output, "perspective", first);
    require(std::equal(world.begin(), world.end(), original.begin(),
                       [](const auto &a, const auto &b) {
                           return a.position == b.position && a.color == b.color;
                       }) &&
                perspective.elements == matrix.elements,
            "rendering modified borrowed vertices or transform");
    perspective.elements[3] = 10;
    world[0].color = {1, 1, 0};
    require(renderengine::render(settings, world, perspective) != saved && first == saved,
            "renderer retained input or output aliases");
    world = original;
    perspective = matrix;

    std::vector<Vertex> depth(world.begin(), world.end());
    const std::array<Vertex, 3> middle{{
        {{-1.8F, -1.2F, 1.6F}, {1, 1, 0}},
        {{1.8F, -1.2F, 1.6F}, {1, 1, 0}},
        {{-1.8F, 1.2F, 1.6F}, {1, 1, 0}},
    }};
    depth.insert(depth.end(), middle.begin(), middle.end());
    const auto forward = renderengine::render(settings, depth, perspective);
    write(output, "depth", forward);
    settings.reverse = true;
    const auto reverse = renderengine::render(settings, depth, perspective);
    write(output, "depth-reverse", reverse);
    require(forward == reverse, "perspective depth depends on triangle order");
    settings.reverse = false;

    const std::array<Vertex, 3> near{{
        {{-0.5625F, -0.375F, 0.5F}, {1, 0, 0}},
        {{2.25F, -1.5F, 2}, {0, 1, 0}},
        {{-2.25F, 1.5F, 2}, {0, 0, 1}},
    }};
    write(output, "near", renderengine::render(settings, near, perspective));
    const std::array<Vertex, 3> side{{
        {{-6, -1.5F, 2}, {1, 0, 0}},
        {{2.25F, -1.5F, 2}, {0, 1, 0}},
        {{2.25F, 1.5F, 2}, {0, 0, 1}},
    }};
    write(output, "side", renderengine::render(settings, side, perspective));

    const auto empty = renderengine::render(settings, {}, perspective);
    require(std::all_of(empty.begin(), empty.end(), [](auto byte) { return byte == 0; }),
            "empty transform draw retained color or depth");
    write(output, "empty", empty);
    auto outside = world;
    for (auto &vertex : outside) {
        vertex.position[0] += 30;
    }
    write(output, "outside", renderengine::render(settings, outside, perspective));
    auto behind = triangle;
    for (auto &vertex : behind) {
        for (auto &coordinate : vertex.position) {
            coordinate = -coordinate;
        }
    }
    Transform negativeW;
    negativeW.elements[15] = -1;
    // Dividing before clipping would incorrectly turn this into the visible identity triangle.
    write(output, "behind", renderengine::render(settings, behind, negativeW));
    outside = world;
    for (auto &vertex : outside) {
        vertex.position[2] = 8;
    }
    write(output, "far", renderengine::render(settings, outside, perspective));
    auto collapse = perspective;
    collapse.elements[0] = 0;
    write(output, "degenerate", renderengine::render(settings, world, collapse));

    // Both ends of the inclusive arithmetic domain are usable, not merely rejected safely.
    const auto bound = renderengine::max_transform_component;
    auto large = triangle;
    large[0].position = {-bound, -bound, 0};
    large[1].position = {bound, -bound, 0};
    large[2].position = {-bound, bound, 0};
    Transform scale;
    scale.elements[0] = scale.elements[5] = 1 / bound;
    scale.elements[11] = 0.5F;
    write(output, "large", renderengine::render(settings, large, scale));
    scale.elements[0] = -bound;
    scale.elements[5] = bound;
    scale.elements[11] = bound;
    write(output, "large-products", renderengine::render(settings, large, scale));

    const std::vector<Vertex> limit(renderengine::max_vertices, Vertex{{0, 0, 2}, {1, 1, 1}});
    require(renderengine::render(settings, limit, perspective) == empty, "transform count limit failed");
    auto again = renderengine::render(settings, world, perspective);
    require(again == saved && first == saved, "successive transform calls retained state");
    first[0] = 123;
    require(again[0] == 0, "transform results share storage");
}
