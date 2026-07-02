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

} // namespace terrain
