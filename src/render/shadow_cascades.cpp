#include "render/shadow_cascades.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace render {

namespace {

constexpr float kDepthMarginM = 10.0f; // holgura del rango de profundidad del sol

// Rango [cerca, lejos] (distancias positivas en -z del sol) que cubre el AABB
// del mundo entero.
glm::vec2 world_depth_range(const glm::mat4& rot, const glm::vec3& lo, const glm::vec3& hi) {
    float z_min = std::numeric_limits<float>::max();
    float z_max = std::numeric_limits<float>::lowest();
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner{(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y,
                               (i & 4) ? hi.z : lo.z};
        const float z = (rot * glm::vec4(corner, 1.0f)).z;
        z_min = std::min(z_min, z);
        z_max = std::max(z_max, z);
    }
    return {-z_max - kDepthMarginM, -z_min + kDepthMarginM};
}

} // namespace

std::array<float, kShadowCascadeCount + 1> practical_splits(float near_m, float far_m,
                                                            float lambda) {
    std::array<float, kShadowCascadeCount + 1> splits{};
    splits[0] = near_m;
    for (int i = 1; i <= kShadowCascadeCount; ++i) {
        const float f = static_cast<float>(i) / static_cast<float>(kShadowCascadeCount);
        const float log_split = near_m * std::pow(far_m / near_m, f);
        const float uni_split = near_m + (far_m - near_m) * f;
        splits[static_cast<size_t>(i)] = lambda * log_split + (1.0f - lambda) * uni_split;
    }
    splits[kShadowCascadeCount] = far_m; // exacto, sin error de pow
    return splits;
}

glm::vec2 slice_bounding_sphere(float near_m, float far_m, float fov_y_rad, float aspect) {
    // k = semidiagonal de la sección del frustum por metro de profundidad.
    const float ty = std::tan(fov_y_rad * 0.5f);
    const float k2 = ty * ty * (1.0f + aspect * aspect);
    // Centro equidistante de las esquinas cercanas y lejanas; si cae más
    // allá del plano lejano, la esfera mínima es la del disco lejano.
    const float center = std::min(0.5f * (near_m + far_m) * (1.0f + k2), far_m);
    const float r_near = std::sqrt((center - near_m) * (center - near_m) + near_m * near_m * k2);
    const float r_far = std::sqrt((far_m - center) * (far_m - center) + far_m * far_m * k2);
    return {center, std::max(r_near, r_far)};
}

glm::mat4 light_rotation(const glm::vec3& sun_dir) {
    const glm::vec3 dir = glm::normalize(sun_dir);
    const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3{0.0f, 0.0f, 1.0f}
                                                 : glm::vec3{0.0f, 1.0f, 0.0f};
    return glm::lookAt(glm::vec3{0.0f}, -dir, up);
}

ShadowCascades fit_cascades(const CascadeSetup& setup, const glm::vec3& cam_pos,
                            const glm::vec3& cam_forward, const glm::vec3& sun_dir,
                            const glm::vec3& world_min, const glm::vec3& world_max) {
    const auto splits = practical_splits(setup.near_m, setup.shadow_distance_m, setup.lambda);
    const glm::mat4 rot = light_rotation(sun_dir);
    const glm::vec2 depth = world_depth_range(rot, world_min, world_max);
    const float res = static_cast<float>(setup.resolution);

    ShadowCascades out{};
    for (int i = 0; i < kShadowCascadeCount; ++i) {
        ShadowCascade& c = out[static_cast<size_t>(i)];
        c.split_near = splits[static_cast<size_t>(i)];
        c.split_far = splits[static_cast<size_t>(i) + 1];
        const glm::vec2 sphere =
            slice_bounding_sphere(c.split_near, c.split_far, setup.fov_y_rad, setup.aspect);
        // Radio redondeado al metro: texel exacto e idéntico entre frames.
        const float radius = std::ceil(sphere.y);
        c.texel_m = 2.0f * radius / res;
        const glm::vec3 center = cam_pos + cam_forward * sphere.x;
        glm::vec2 ls = glm::vec2(rot * glm::vec4(center, 1.0f));
        ls = glm::floor(ls / c.texel_m) * c.texel_m;
        const glm::mat4 ortho = glm::ortho(ls.x - radius, ls.x + radius, ls.y - radius,
                                           ls.y + radius, depth.x, depth.y);
        c.light_vp = ortho * rot;
        c.depth_range_m = depth.y - depth.x;
    }
    return out;
}

} // namespace render
