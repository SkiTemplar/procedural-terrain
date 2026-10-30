#include "veg/canopy_layer.h"

#include <algorithm>
#include <cmath>

namespace veg {

CanopyAccumulator::CanopyAccumulator(float size_m, int resolution)
    : size_m_(std::max(size_m, 1.0f)),
      resolution_(std::max(resolution, 1)),
      sum_(static_cast<size_t>(resolution_) * static_cast<size_t>(resolution_),
           glm::vec4{0.0f}) {}

void CanopyAccumulator::add(float wx, float wz, float crown_radius_m,
                            const glm::vec3& color) {
    const float scale = static_cast<float>(resolution_) / size_m_;
    const float fx = (wx + size_m_ * 0.5f) * scale;
    const float fz = (wz + size_m_ * 0.5f) * scale;
    if (fx < 0.0f || fz < 0.0f || fx >= static_cast<float>(resolution_)
        || fz >= static_cast<float>(resolution_)) {
        return;
    }
    const float area = 3.14159265f * crown_radius_m * crown_radius_m;
    const size_t i = static_cast<size_t>(fz) * static_cast<size_t>(resolution_)
                   + static_cast<size_t>(fx);
    sum_[i] += glm::vec4{color * area, area};
}

std::vector<glm::vec4> CanopyAccumulator::resolve() const {
    const float texel = size_m_ / static_cast<float>(resolution_);
    const float inv_texel_area = 1.0f / (texel * texel);
    std::vector<glm::vec4> out(sum_.size(), glm::vec4{0.0f});
    for (size_t i = 0; i < sum_.size(); ++i) {
        const glm::vec4& s = sum_[i];
        if (s.w <= 0.0f) {
            continue;
        }
        out[i] = glm::vec4{glm::vec3{s} / s.w, std::min(s.w * inv_texel_area, 1.0f)};
    }
    return out;
}

} // namespace veg
