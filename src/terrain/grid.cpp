#include "terrain/grid.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace terrain {

namespace {

float placeholder_height(float x, float z) {
    // Colinas suaves de varias escalas; amplitudes moderadas (relieve natural).
    return 18.0f * std::sin(x * 0.008f) * std::cos(z * 0.008f)
         + 7.0f * std::sin(x * 0.03f + 1.3f) * std::cos(z * 0.021f)
         + 2.0f * std::sin(x * 0.09f) * std::sin(z * 0.11f);
}

glm::vec3 color_for_height(float h) {
    const glm::vec3 grass{0.35f, 0.55f, 0.25f};
    const glm::vec3 rock{0.48f, 0.44f, 0.40f};
    const float t = glm::clamp((h - 8.0f) / 14.0f, 0.0f, 1.0f);
    return glm::mix(grass, rock, t);
}

} // namespace

render::Mesh make_grid(float size_m, int resolution) {
    std::vector<render::Vertex> vertices;
    vertices.reserve(static_cast<size_t>(resolution + 1) * (resolution + 1));

    const float half = size_m * 0.5f;
    const float step = size_m / static_cast<float>(resolution);

    for (int row = 0; row <= resolution; ++row) {
        for (int col = 0; col <= resolution; ++col) {
            const float x = -half + static_cast<float>(col) * step;
            const float z = -half + static_cast<float>(row) * step;
            const float y = placeholder_height(x, z);
            vertices.push_back({{x, y, z}, color_for_height(y)});
        }
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<size_t>(resolution) * resolution * 6);
    const auto stride = static_cast<std::uint32_t>(resolution + 1);

    for (std::uint32_t row = 0; row < static_cast<std::uint32_t>(resolution); ++row) {
        for (std::uint32_t col = 0; col < static_cast<std::uint32_t>(resolution); ++col) {
            const std::uint32_t top_left = row * stride + col;
            const std::uint32_t top_right = top_left + 1;
            const std::uint32_t bottom_left = top_left + stride;
            const std::uint32_t bottom_right = bottom_left + 1;

            indices.insert(indices.end(),
                           {top_left, bottom_left, top_right,
                            top_right, bottom_left, bottom_right});
        }
    }

    return render::Mesh{vertices, indices};
}

} // namespace terrain
