#include "render/frustum.h"
#include "render/shadow_cascades.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cmath>

namespace {

const glm::vec3 kSun = glm::normalize(glm::vec3{0.54f, 0.64f, 0.54f});
const glm::vec3 kWorldMin{-5000.0f, -300.0f, -5000.0f};
const glm::vec3 kWorldMax{5000.0f, 2500.0f, 5000.0f};

glm::vec3 forward_of(float yaw_deg, float pitch_deg) {
    const float y = glm::radians(yaw_deg);
    const float p = glm::radians(pitch_deg);
    return glm::normalize(glm::vec3{std::cos(y) * std::cos(p), std::sin(p),
                                    std::sin(y) * std::cos(p)});
}

// Las 8 esquinas de la rodaja [n, f] de una cámara en el origen mirando a -z.
std::array<glm::vec3, 8> slice_corners(float n, float f, float fov_y, float aspect) {
    const float ty = std::tan(fov_y * 0.5f);
    const float tx = ty * aspect;
    std::array<glm::vec3, 8> out{};
    for (int i = 0; i < 8; ++i) {
        const float d = (i & 4) ? f : n;
        out[static_cast<size_t>(i)] = {((i & 1) ? 1.0f : -1.0f) * tx * d,
                                       ((i & 2) ? 1.0f : -1.0f) * ty * d, -d};
    }
    return out;
}

} // namespace

TEST(Sombras, SplitsPracticosCrecenYCubrenLaDistancia) {
    const auto s = render::practical_splits(0.5f, 2500.0f, 0.7f);
    EXPECT_FLOAT_EQ(s.front(), 0.5f);
    EXPECT_FLOAT_EQ(s.back(), 2500.0f);
    for (size_t i = 1; i < s.size(); ++i) {
        EXPECT_GT(s[i], s[i - 1]);
    }
    // lambda 0 = uniforme; lambda 1 = geométrico (razón constante).
    const auto uni = render::practical_splits(1.0f, 1001.0f, 0.0f);
    EXPECT_NEAR(uni[1], 251.0f, 1e-2f);
    const auto geo = render::practical_splits(1.0f, 10000.0f, 1.0f);
    EXPECT_NEAR(geo[2] / geo[1], geo[1] / geo[0], 1e-3f);
}

TEST(Sombras, LaEsferaEnvuelveLaRodaja) {
    const float fov = glm::radians(70.0f);
    const float aspect = 16.0f / 9.0f;
    for (const auto& [n, f] : {std::pair{0.5f, 190.0f}, std::pair{400.0f, 770.0f},
                               std::pair{10.0f, 12.0f}}) {
        const glm::vec2 sphere = render::slice_bounding_sphere(n, f, fov, aspect);
        const glm::vec3 center{0.0f, 0.0f, -sphere.x};
        for (const glm::vec3& c : slice_corners(n, f, fov, aspect)) {
            EXPECT_LE(glm::length(c - center), sphere.y * 1.0001f) << n << "-" << f;
        }
    }
}

// Estabilidad: al trasladar la cámara, un punto fijo del mundo se desplaza en
// el mapa de sombras un número ENTERO de texeles (sin parpadeo de bordes); al
// girarla, el tamaño de texel no cambia.
TEST(Sombras, CascadasAjustadasALaRejillaDeTexeles) {
    render::CascadeSetup setup;
    const glm::vec3 fwd = forward_of(-40.0f, -20.0f);
    const auto a = render::fit_cascades(setup, {-3950.0f, 120.0f, -1542.0f}, fwd, kSun,
                                        kWorldMin, kWorldMax);
    const auto b = render::fit_cascades(setup, {-3949.37f, 120.21f, -1541.13f}, fwd, kSun,
                                        kWorldMin, kWorldMax);
    const auto r = render::fit_cascades(setup, {-3950.0f, 120.0f, -1542.0f},
                                        forward_of(65.0f, -5.0f), kSun, kWorldMin, kWorldMax);
    const glm::vec4 p{-3900.0f, 80.0f, -1500.0f, 1.0f};
    for (int i = 0; i < render::kShadowCascadeCount; ++i) {
        const auto& ca = a[static_cast<size_t>(i)];
        const auto& cb = b[static_cast<size_t>(i)];
        const glm::vec2 shift = (glm::vec2(ca.light_vp * p) - glm::vec2(cb.light_vp * p))
                              * (0.5f * static_cast<float>(setup.resolution));
        EXPECT_NEAR(shift.x, std::round(shift.x), 2e-2f) << "cascada " << i;
        EXPECT_NEAR(shift.y, std::round(shift.y), 2e-2f) << "cascada " << i;
        EXPECT_FLOAT_EQ(ca.texel_m, r[static_cast<size_t>(i)].texel_m);
    }
    EXPECT_FLOAT_EQ(a.back().split_far, setup.shadow_distance_m);
}

// Cobertura: todo punto de la rodaja de cada cascada cae dentro de su caja
// ortográfica, y el rango de profundidad incluye todo el mundo.
TEST(Sombras, CadaCascadaContieneSuRodaja) {
    render::CascadeSetup setup;
    const glm::vec3 cam{1200.0f, 450.0f, 300.0f};
    const glm::vec3 fwd = forward_of(180.0f, -12.0f);
    const auto cascades = render::fit_cascades(setup, cam, fwd, kSun, kWorldMin, kWorldMax);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3{0.0f, 1.0f, 0.0f}));
    const glm::vec3 up = glm::cross(right, fwd);
    for (const auto& c : cascades) {
        for (const glm::vec3& k :
             slice_corners(c.split_near, c.split_far, setup.fov_y_rad, setup.aspect)) {
            const glm::vec3 world = cam + right * k.x + up * k.y - fwd * k.z;
            const glm::vec4 ndc = c.light_vp * glm::vec4(world, 1.0f);
            EXPECT_LE(std::abs(ndc.x), 1.0f);
            EXPECT_LE(std::abs(ndc.y), 1.0f);
            EXPECT_LE(std::abs(ndc.z), 1.0f);
        }
    }
}

TEST(Sombras, FrustumOrtograficoRecortaCajas) {
    const auto cascades = render::fit_cascades(render::CascadeSetup{}, {0.0f, 100.0f, 0.0f},
                                               forward_of(-90.0f, -10.0f), kSun, kWorldMin,
                                               kWorldMax);
    const render::Frustum light(cascades.front().light_vp);
    EXPECT_TRUE(light.intersects({-5.0f, 0.0f, -105.0f}, {5.0f, 20.0f, -95.0f}));
    EXPECT_FALSE(light.intersects({3000.0f, 0.0f, 3000.0f}, {3010.0f, 20.0f, 3010.0f}));
}
