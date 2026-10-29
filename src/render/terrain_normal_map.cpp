#include "render/terrain_normal_map.h"

#include <algorithm>
#include <cmath>
#include <execution>
#include <numeric>

namespace render {

namespace {

inline float sample_height(const terrain::Heightfield& hf, int x, int z) {
    const int res = hf.resolution();
    const int x_clamped = std::clamp(x, 0, res - 1);
    const int z_clamped = std::clamp(z, 0, res - 1);
    float h = hf.at(x_clamped, z_clamped);
    if (x < 0 && res >= 2) {
        h += static_cast<float>(x) * (hf.at(1, z_clamped) - hf.at(0, z_clamped));
    } else if (x >= res && res >= 2) {
        h += static_cast<float>(x - (res - 1)) * (hf.at(res - 1, z_clamped) - hf.at(res - 2, z_clamped));
    }
    if (z < 0 && res >= 2) {
        h += static_cast<float>(z) * (hf.at(x_clamped, 1) - hf.at(x_clamped, 0));
    } else if (z >= res && res >= 2) {
        h += static_cast<float>(z - (res - 1)) * (hf.at(x_clamped, res - 1) - hf.at(x_clamped, res - 2));
    }
    return h;
}

inline std::int16_t float_to_snorm16(float v) {
    const float clamped = std::clamp(v, -1.0f, 1.0f);
    return static_cast<std::int16_t>(std::round(clamped * 32767.0f));
}

} // namespace

glm::vec3 compute_scharr_normal(const terrain::Heightfield& hf, int x, int z) {
    const int res = hf.resolution();
    if (res < 2 || hf.cell_size() <= 0.0f) {
        return glm::vec3{0.0f, 1.0f, 0.0f};
    }

    const float h00 = sample_height(hf, x - 1, z - 1);
    const float h10 = sample_height(hf, x,     z - 1);
    const float h20 = sample_height(hf, x + 1, z - 1);

    const float h01 = sample_height(hf, x - 1, z);
    const float h21 = sample_height(hf, x + 1, z);

    const float h02 = sample_height(hf, x - 1, z + 1);
    const float h12 = sample_height(hf, x,     z + 1);
    const float h22 = sample_height(hf, x + 1, z + 1);

    // Operador de Scharr 3x3 normalizado:
    // Derivada respecto a x: kernel [-3 0 +3; -10 0 +10; -3 0 +3] / (32 * cell_size)
    const float dx = (3.0f * (h20 - h00) + 10.0f * (h21 - h01) + 3.0f * (h22 - h02))
                   / (32.0f * hf.cell_size());

    // Derivada respecto a z: kernel [-3 -10 -3; 0 0 0; +3 +10 +3] / (32 * cell_size)
    const float dz = (3.0f * (h02 - h00) + 10.0f * (h12 - h10) + 3.0f * (h22 - h20))
                   / (32.0f * hf.cell_size());

    return glm::normalize(glm::vec3{-dx, 1.0f, -dz});
}

glm::vec3 decode_snorm16_normal(std::int16_t sx, std::int16_t sz) {
    const float x = static_cast<float>(sx) / 32767.0f;
    const float z = static_cast<float>(sz) / 32767.0f;
    const float y = std::sqrt(std::max(0.0f, 1.0f - x * x - z * z));
    return glm::normalize(glm::vec3{x, y, z});
}

std::vector<std::int16_t> compute_scharr_normal_map(const terrain::Heightfield& hf) {
    const int res = hf.resolution();
    std::vector<std::int16_t> out(static_cast<std::size_t>(res) * res * 2);
    if (res < 2) {
        return out;
    }

    std::vector<int> rows(static_cast<std::size_t>(res));
    std::iota(rows.begin(), rows.end(), 0);

    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        const std::size_t row_offset = static_cast<std::size_t>(z) * res * 2;
        for (int x = 0; x < res; ++x) {
            const glm::vec3 n = compute_scharr_normal(hf, x, z);
            out[row_offset + static_cast<std::size_t>(x) * 2] = float_to_snorm16(n.x);
            out[row_offset + static_cast<std::size_t>(x) * 2 + 1] = float_to_snorm16(n.z);
        }
    });

    return out;
}

} // namespace render
