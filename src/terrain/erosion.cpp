#include "terrain/erosion.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace terrain {

namespace {

constexpr int kMaxLifetime = 40;
constexpr float kGravity = 4.0f;
constexpr float kEvaporation = 0.02f;
constexpr float kMinCapacity = 0.01f;
constexpr float kCapacityFactor = 6.0f;

struct GradientSample {
    float height;
    glm::vec2 gradient;
};

// Altura y gradiente bilineales en coordenadas de celda (continuas).
GradientSample sample_gradient(const Heightfield& hf, float cx, float cz) {
    const int x0 = std::clamp(static_cast<int>(cx), 0, hf.resolution() - 2);
    const int z0 = std::clamp(static_cast<int>(cz), 0, hf.resolution() - 2);
    const float tx = cx - static_cast<float>(x0);
    const float tz = cz - static_cast<float>(z0);

    const float h00 = hf.at(x0, z0);
    const float h10 = hf.at(x0 + 1, z0);
    const float h01 = hf.at(x0, z0 + 1);
    const float h11 = hf.at(x0 + 1, z0 + 1);

    GradientSample out;
    out.gradient.x = (h10 - h00) * (1.0f - tz) + (h11 - h01) * tz;
    out.gradient.y = (h01 - h00) * (1.0f - tx) + (h11 - h10) * tx;
    out.height = h00 * (1.0f - tx) * (1.0f - tz) + h10 * tx * (1.0f - tz)
               + h01 * (1.0f - tx) * tz + h11 * tx * tz;
    return out;
}

// Reparte un delta de altura bilinealmente entre las 4 celdas de la posición.
void deposit_at(Heightfield& hf, float cx, float cz, float amount) {
    const int x0 = std::clamp(static_cast<int>(cx), 0, hf.resolution() - 2);
    const int z0 = std::clamp(static_cast<int>(cz), 0, hf.resolution() - 2);
    const float tx = cx - static_cast<float>(x0);
    const float tz = cz - static_cast<float>(z0);

    hf.at(x0, z0) += amount * (1.0f - tx) * (1.0f - tz);
    hf.at(x0 + 1, z0) += amount * tx * (1.0f - tz);
    hf.at(x0, z0 + 1) += amount * (1.0f - tx) * tz;
    hf.at(x0 + 1, z0 + 1) += amount * tx * tz;
}

// Erosiona en un entorno 3x3 con pesos, para no dejar agujas de un píxel.
void erode_at(Heightfield& hf, float cx, float cz, float amount) {
    const int x = std::clamp(static_cast<int>(std::round(cx)), 1, hf.resolution() - 2);
    const int z = std::clamp(static_cast<int>(std::round(cz)), 1, hf.resolution() - 2);
    constexpr float kCenter = 0.4f;
    constexpr float kSide = 0.1f;
    constexpr float kDiag = 0.05f;

    hf.at(x, z) -= amount * kCenter;
    hf.at(x - 1, z) -= amount * kSide;
    hf.at(x + 1, z) -= amount * kSide;
    hf.at(x, z - 1) -= amount * kSide;
    hf.at(x, z + 1) -= amount * kSide;
    hf.at(x - 1, z - 1) -= amount * kDiag;
    hf.at(x + 1, z - 1) -= amount * kDiag;
    hf.at(x - 1, z + 1) -= amount * kDiag;
    hf.at(x + 1, z + 1) -= amount * kDiag;
}

} // namespace

void erode(Heightfield& hf, const TerrainConfig& cfg) {
    std::mt19937 rng(cfg.seed ^ 0x9E3779B9u);
    std::uniform_real_distribution<float> dist(1.0f, static_cast<float>(hf.resolution() - 2));

    for (int i = 0; i < cfg.erosion_droplets; ++i) {
        float cx = dist(rng);
        float cz = dist(rng);
        glm::vec2 dir{0.0f};
        float speed = 1.0f;
        float water = 1.0f;
        float sediment = 0.0f;

        for (int step = 0; step < kMaxLifetime; ++step) {
            const GradientSample here = sample_gradient(hf, cx, cz);

            dir = dir * cfg.erosion_inertia - here.gradient * (1.0f - cfg.erosion_inertia);
            const float len = glm::length(dir);
            if (len < 1e-6f) {
                break; // llano: la gota muere
            }
            dir /= len;

            const float nx = cx + dir.x;
            const float nz = cz + dir.y;
            if (nx < 1.0f || nz < 1.0f
                || nx >= static_cast<float>(hf.resolution() - 2)
                || nz >= static_cast<float>(hf.resolution() - 2)) {
                break; // sale del mapa
            }

            const float new_height = sample_gradient(hf, nx, nz).height;
            const float dh = new_height - here.height;

            const float capacity =
                std::max(-dh, kMinCapacity) * speed * water * kCapacityFactor;

            if (sediment > capacity || dh > 0.0f) {
                // Frena o sube: deposita.
                const float amount = dh > 0.0f
                    ? std::min(dh, sediment)
                    : (sediment - capacity) * cfg.erosion_deposit;
                sediment -= amount;
                deposit_at(hf, cx, cz, amount);
            } else {
                // Acelera: arranca sedimento, nunca más que el propio desnivel.
                const float amount =
                    std::min((capacity - sediment) * cfg.erosion_strength, -dh);
                sediment += amount;
                erode_at(hf, cx, cz, amount);
            }

            speed = std::sqrt(std::max(0.0f, speed * speed + dh * -kGravity));
            water *= (1.0f - kEvaporation);
            cx = nx;
            cz = nz;
        }
    }
}

} // namespace terrain
