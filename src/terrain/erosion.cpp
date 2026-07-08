#include "terrain/erosion.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

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

void thermal_erode(Heightfield& hf, int iterations, float talus_slope) {
    const int res = hf.resolution();
    const float max_diff = talus_slope * hf.cell_size();
    constexpr float kTransfer = 0.5f; // fracción del exceso que se desprende

    std::vector<float> delta(static_cast<size_t>(res) * static_cast<size_t>(res));
    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };

    for (int it = 0; it < iterations; ++it) {
        std::fill(delta.begin(), delta.end(), 0.0f);

        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                const float h = hf.at(x, z);
                // Reparto entre TODOS los vecinos que exceden el talus,
                // proporcional al exceso: relaja paredes grandes mucho más
                // rápido que verter solo al vecino más bajo.
                float total_excess = 0.0f;
                float excess[4] = {};
                const std::pair<int, int> dirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (int n = 0; n < 4; ++n) {
                    const float diff = h - hf.at(x + dirs[n].first, z + dirs[n].second);
                    if (diff > max_diff) {
                        excess[n] = diff - max_diff;
                        total_excess += excess[n];
                    }
                }
                if (total_excess <= 0.0f) {
                    continue;
                }
                // Nunca mover más de la mitad del mayor exceso (estabilidad).
                const float moved = kTransfer * 0.5f
                    * std::max({excess[0], excess[1], excess[2], excess[3]});
                delta[idx(x, z)] -= moved;
                for (int n = 0; n < 4; ++n) {
                    if (excess[n] > 0.0f) {
                        delta[idx(x + dirs[n].first, z + dirs[n].second)] +=
                            moved * excess[n] / total_excess;
                    }
                }
            }
        }

        for (size_t i = 0; i < delta.size(); ++i) {
            hf.data()[i] += delta[i];
        }
    }
}

void smooth_peaks(Heightfield& hf, int passes, float slope_threshold) {
    const int res = hf.resolution();
    const float max_diff = slope_threshold * hf.cell_size();
    std::vector<float> next(hf.data().size());

    for (int p = 0; p < passes; ++p) {
        next = hf.data();
        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                const float h = hf.at(x, z);
                const float n0 = hf.at(x - 1, z);
                const float n1 = hf.at(x + 1, z);
                const float n2 = hf.at(x, z - 1);
                const float n3 = hf.at(x, z + 1);
                const float max_d = std::max(
                    {std::abs(h - n0), std::abs(h - n1),
                     std::abs(h - n2), std::abs(h - n3)});
                if (max_d > max_diff) {
                    const float avg = (n0 + n1 + n2 + n3) * 0.25f;
                    next[static_cast<size_t>(z) * res + x] = h * 0.55f + avg * 0.45f;
                }
            }
        }
        hf.data().swap(next);
    }
}

void glacial_erosion(Heightfield& hf, const TerrainConfig& cfg) {
    // Erosión glaciar (N4, feedback 2026-07-08 "glaciares"): proceso emergente,
    // NO estampado (regla A.5). El hielo se acumula por encima de la paleo-línea
    // de nieve (ELA), fluye pendiente abajo acumulando flujo, y SOBREEXCAVA el
    // lecho donde el hielo es grueso y la pendiente baja (cabeceras y fondos de
    // valle). La sobreexcavación crea cubetas cerradas y valles en U; el
    // priority-flood posterior las llena → ibones / lagos de circo. Las paredes
    // y cumbres (pendiente alta) no se tocan: el circo queda con su anfiteatro.
    const int res = hf.resolution();
    const size_t n = static_cast<size_t>(res) * static_cast<size_t>(res);
    auto& h = hf.data();
    const float max_h = *std::max_element(h.begin(), h.end());
    if (max_h < cfg.sea_level + 500.0f) {
        return; // sin alta montaña no hay glaciar
    }
    const float ela = cfg.sea_level + 0.52f * (max_h - cfg.sea_level);
    const float cell = hf.cell_size();
    const int dxs[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int dzs[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    // 1) Fuente de hielo por altitud sobre la ELA (más alto = más nieve/hielo).
    std::vector<float> flux(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        const float above = h[i] - ela;
        flux[i] = above > 0.0f ? above / 90.0f : 0.0f;
    }

    // 2) Orden por altura descendente y acumulación del hielo al vecino más bajo.
    std::vector<std::uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(),
              [&h](std::uint32_t a, std::uint32_t b) { return h[a] > h[b]; });
    for (const std::uint32_t i : order) {
        if (flux[i] <= 0.0f) {
            continue;
        }
        const int x = static_cast<int>(i % static_cast<std::uint32_t>(res));
        const int z = static_cast<int>(i / static_cast<std::uint32_t>(res));
        if (x < 1 || z < 1 || x >= res - 1 || z >= res - 1) {
            continue;
        }
        float best = h[i];
        std::uint32_t bi = i;
        for (int k = 0; k < 8; ++k) {
            const std::uint32_t ni = static_cast<std::uint32_t>(
                (z + dzs[k]) * res + (x + dxs[k]));
            if (h[ni] < best) {
                best = h[ni];
                bi = ni;
            }
        }
        if (bi != i) {
            flux[bi] += flux[i];
        }
    }

    // 3) Sobreexcavación CONCENTRADA (refinada 2026-07-08): solo los canales de
    // hielo PRINCIPALES (flujo > 15% del máximo) sobreexcavan — las cabeceras y
    // troughs por donde funela todo el hielo de la cuenca, no cada rincón llano.
    // Así salen POCOS circos grandes y valles en U limpios, no un alto
    // agujereado de cubetas. La excavación crece con el exceso de flujo sobre el
    // umbral (los troughs mayores son los más hondos).
    float max_flux = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        max_flux = std::max(max_flux, flux[i]);
    }
    const float flux_thresh = 0.15f * max_flux;
    if (flux_thresh <= 0.0f) {
        return;
    }
    std::vector<float> newh(h);
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(res)
                           + static_cast<size_t>(x);
            if (h[i] < ela - 120.0f || flux[i] < flux_thresh) {
                continue;
            }
            const float dhx = (h[i + 1] - h[i - 1]) * 0.5f / cell;
            const float dhz = (h[i + static_cast<size_t>(res)]
                               - h[i - static_cast<size_t>(res)]) * 0.5f / cell;
            const float slope = std::sqrt(dhx * dhx + dhz * dhz);
            // Suave en fondo/cabecera (pendiente baja), nulo en paredes: el
            // circo conserva su anfiteatro.
            const float gentle = std::clamp(1.0f - slope / 0.45f, 0.0f, 1.0f);
            // Excavación ∝ raíz del EXCESO de flujo sobre el umbral: los troughs
            // principales (mucho hielo) se ahondan; los del borde, apenas.
            const float over = (flux[i] - flux_thresh) / std::max(max_flux, 1.0f);
            const float exc = std::min(50.0f, 90.0f * std::sqrt(over)) * gentle;
            newh[i] = h[i] - exc;
        }
    }
    h.swap(newh);
}

void clamp_spikes(Heightfield& hf, float max_over) {
    // Recorta picos de UN vértice (feedback 2026-07-08 "vértices extra"): una
    // celda que sobresale > max_over por encima de TODAS sus 8 vecinas es un
    // artefacto (del LEM / sharpen_crags / depósito de gotas), no relieve real.
    // Se baja al nivel de la vecina más alta + max_over. No toca crestas ni
    // paredes legítimas (esas tienen alguna vecina alta).
    const int res = hf.resolution();
    const std::vector<float> src(hf.data());
    const auto at = [res](const std::vector<float>& d, int x, int z) -> float {
        return d[static_cast<size_t>(z) * static_cast<size_t>(res)
                 + static_cast<size_t>(x)];
    };
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            float maxn = -1e9f;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx || dz) {
                        maxn = std::max(maxn, at(src, x + dx, z + dz));
                    }
                }
            }
            if (at(src, x, z) > maxn + max_over) {
                hf.at(x, z) = maxn + max_over;
            }
        }
    }
}

} // namespace terrain
