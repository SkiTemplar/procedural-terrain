#include "terrain/heightfield.h"

#include <algorithm>

namespace terrain {

Heightfield::Heightfield(int resolution, float size_m)
    : resolution_(resolution),
      size_m_(size_m),
      data_(static_cast<size_t>(resolution) * static_cast<size_t>(resolution), 0.0f) {}

float Heightfield::sample(float wx, float wz) const {
    const float half = size_m_ * 0.5f;
    const float inv_cell = 1.0f / cell_size();

    float fx = (wx + half) * inv_cell;
    float fz = (wz + half) * inv_cell;
    fx = std::clamp(fx, 0.0f, static_cast<float>(resolution_ - 1));
    fz = std::clamp(fz, 0.0f, static_cast<float>(resolution_ - 1));

    const int x0 = std::min(static_cast<int>(fx), resolution_ - 2);
    const int z0 = std::min(static_cast<int>(fz), resolution_ - 2);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    const float h00 = at(x0, z0);
    const float h10 = at(x0 + 1, z0);
    const float h01 = at(x0, z0 + 1);
    const float h11 = at(x0 + 1, z0 + 1);

    const float h0 = h00 + (h10 - h00) * tx;
    const float h1 = h01 + (h11 - h01) * tx;
    return h0 + (h1 - h0) * tz;
}

glm::vec3 Heightfield::normal_at(int x, int z) const {
    const int xm = std::max(x - 1, 0);
    const int xp = std::min(x + 1, resolution_ - 1);
    const int zm = std::max(z - 1, 0);
    const int zp = std::min(z + 1, resolution_ - 1);

    const float dx = (at(xp, z) - at(xm, z)) / (cell_size() * static_cast<float>(xp - xm));
    const float dz = (at(x, zp) - at(x, zm)) / (cell_size() * static_cast<float>(zp - zm));
    return glm::normalize(glm::vec3{-dx, 1.0f, -dz});
}

namespace {

// Desnivel (máx − mín) en la ventana cuadrada de radio r centrada en (x, z).
float local_range(const Heightfield& hf, int x, int z, int r) {
    const int res = hf.resolution();
    float lo = hf.at(x, z);
    float hi = lo;
    for (int dz = -r; dz <= r; ++dz) {
        const int zz = std::clamp(z + dz, 0, res - 1);
        for (int dx = -r; dx <= r; ++dx) {
            const float v = hf.at(std::clamp(x + dx, 0, res - 1), zz);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    return hi - lo;
}

} // namespace

WallStats count_walls(const Heightfield& hf, float sea_level) {
    constexpr float kWallSlope = 1.2f;
    WallStats s;
    const int res = hf.resolution();
    const float inv_2cell = 0.5f / hf.cell_size();
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            if (hf.at(x, z) <= sea_level) {
                continue;
            }
            ++s.land;
            const float gx = (hf.at(x + 1, z) - hf.at(x - 1, z)) * inv_2cell;
            const float gz = (hf.at(x, z + 1) - hf.at(x, z - 1)) * inv_2cell;
            if (gx * gx + gz * gz <= kWallSlope * kWallSlope) {
                continue;
            }
            ++s.walls;
            if (local_range(hf, x, z, 3) > 40.0f) {
                ++s.scarp40;
            }
            if (local_range(hf, x, z, 12) > 100.0f) {
                ++s.scarp100;
            }
        }
    }
    return s;
}

} // namespace terrain
