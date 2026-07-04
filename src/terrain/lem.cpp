#include "terrain/lem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <execution>
#include <numeric>
#include <queue>

namespace terrain {

namespace {

constexpr float kDiffusion = 0.24f;      // difusión de laderas fuerte: laderas
                                         // anchas y crestas redondeadas (≤0.25
                                         // por estabilidad del esquema explícito)
constexpr float kBaseUplift = 0.02f;     // por debajo: nivel de base (mar)
constexpr float kHeadwaterSlope = 0.7f;  // pendiente de equilibrio en cabeceras

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// Priority-flood + epsilon (Barnes): rellena los pits con pendiente mínima
// hacia el borde. Dentro del bucle LEM es el manejo de lagos: sin él, las
// cuencas endorreicas se inflan en bloque hasta formar mesetas.
void fill_pits(std::vector<float>& h, int res) {
    struct Cell {
        float height;
        int x;
        int z;
    };
    struct Higher {
        bool operator()(const Cell& a, const Cell& b) const {
            return a.height > b.height;
        }
    };
    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };

    std::vector<bool> done(h.size(), false);
    std::priority_queue<Cell, std::vector<Cell>, Higher> open;
    for (int x = 0; x < res; ++x) {
        for (const int z : {0, res - 1}) {
            open.push({h[idx(x, z)], x, z});
            done[idx(x, z)] = true;
        }
    }
    for (int z = 1; z < res - 1; ++z) {
        for (const int x : {0, res - 1}) {
            open.push({h[idx(x, z)], x, z});
            done[idx(x, z)] = true;
        }
    }
    while (!open.empty()) {
        const Cell c = open.top();
        open.pop();
        for (int n = 0; n < 8; ++n) {
            const int nx = c.x + kDx[n];
            const int nz = c.z + kDz[n];
            if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                continue;
            }
            const size_t ni = idx(nx, nz);
            if (done[ni]) {
                continue;
            }
            done[ni] = true;
            h[ni] = std::max(h[ni], c.height + 0.01f);
            open.push({h[ni], nx, nz});
        }
    }
}

} // namespace

std::vector<float> run_lem(const std::vector<float>& uplift, int macro_res,
                           float cell_size_m, float uplift_total_m,
                           int iterations) {
    const int res = macro_res;
    const size_t n = static_cast<size_t>(res) * static_cast<size_t>(res);
    const float uplift_per_iter = uplift_total_m / static_cast<float>(iterations);
    // Calibración física: en cabeceras (A = 1 celda) la pendiente de
    // equilibrio Δh/d = U/(k·√A) vale kHeadwaterSlope; en cauces grandes
    // decae con √A. La pendiente máxima del macro queda acotada por diseño.
    const float k_dt = uplift_per_iter / (kHeadwaterSlope * cell_size_m);

    // Semilla: gradiente inicial proporcional al uplift, para que el primer
    // routing ya tenga pendientes hacia el mar.
    std::vector<float> h(n);
    for (size_t i = 0; i < n; ++i) {
        h[i] = uplift[i] * 2.0f;
    }
    fill_pits(h, res);
    std::vector<std::uint32_t> receiver(n);
    std::vector<float> rcv_dist(n);
    std::vector<float> area(n);
    std::vector<std::uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    std::vector<float> lap(n);

    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };

    for (int it = 0; it < iterations; ++it) {
        // 1) Receptor de cada celda: vecino con máxima pendiente de bajada.
        for (int z = 0; z < res; ++z) {
            for (int x = 0; x < res; ++x) {
                const size_t i = idx(x, z);
                float best_slope = 0.0f;
                std::uint32_t best = static_cast<std::uint32_t>(i);
                float best_d = cell_size_m;
                for (int nb = 0; nb < 8; ++nb) {
                    const int nx = x + kDx[nb];
                    const int nz = z + kDz[nb];
                    if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                        continue;
                    }
                    const float d = (kDx[nb] != 0 && kDz[nb] != 0)
                                  ? cell_size_m * 1.4142f
                                  : cell_size_m;
                    const float slope = (h[i] - h[idx(nx, nz)]) / d;
                    if (slope > best_slope) {
                        best_slope = slope;
                        best = static_cast<std::uint32_t>(idx(nx, nz));
                        best_d = d;
                    }
                }
                receiver[i] = best;
                rcv_dist[i] = best_d;
            }
        }

        // 2) Orden por altura descendente (los donantes antes que receptores).
        std::sort(std::execution::par, order.begin(), order.end(),
                  [&h](std::uint32_t a, std::uint32_t b) { return h[a] > h[b]; });

        // 3) Área de drenaje acumulada aguas abajo.
        const float cell_area = cell_size_m * cell_size_m;
        std::fill(area.begin(), area.end(), cell_area);
        for (const std::uint32_t i : order) {
            if (receiver[i] != i) {
                area[receiver[i]] += area[i];
            }
        }

        // 4) Solver implícito (ascendente: receptores ya resueltos este paso).
        for (auto rit = order.rbegin(); rit != order.rend(); ++rit) {
            const std::uint32_t i = *rit;
            if (uplift[i] < kBaseUplift) {
                h[i] = 0.0f; // nivel de base: mar
                continue;
            }
            const std::uint32_t r = receiver[i];
            if (r == i) {
                h[i] += uplift_per_iter * uplift[i]; // pit: se rellena y drenará
                continue;
            }
            const float f = k_dt * std::sqrt(area[i]) / rcv_dist[i];
            h[i] = (h[i] + uplift_per_iter * uplift[i] + f * h[r]) / (1.0f + f);
        }

        // 5) Difusión de laderas: crestas redondeadas, sin agujas.
        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                const size_t i = idx(x, z);
                lap[i] = h[idx(x - 1, z)] + h[idx(x + 1, z)] + h[idx(x, z - 1)]
                       + h[idx(x, z + 1)] - 4.0f * h[i];
            }
        }
        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                const size_t i = idx(x, z);
                h[i] += kDiffusion * lap[i];
            }
        }

        // 6) Manejo de lagos: relleno periódico para que las cuencas
        // endorreicas drenen en vez de inflarse como mesetas.
        if (it % 10 == 9) {
            fill_pits(h, res);
        }
    }

    // Dos pasadas de suavizado 3×3: elimina las transiciones valle-cresta de
    // una sola celda macro antes de la amplificación bicúbica. Sin
    // normalización: el LEM devuelve metros con pendientes físicas.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> smoothed(h);
        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                const size_t i = idx(x, z);
                smoothed[i] = 0.4f * h[i]
                            + 0.15f * (h[idx(x - 1, z)] + h[idx(x + 1, z)]
                                       + h[idx(x, z - 1)] + h[idx(x, z + 1)]);
            }
        }
        h.swap(smoothed);
    }
    for (float& v : h) {
        v = std::max(v, 0.0f);
    }
    return h;
}

namespace {

float catmull_rom(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t
                   + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                   + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

} // namespace

float sample_bicubic(const std::vector<float>& grid, int res, float fx, float fz) {
    const auto at = [&](int x, int z) {
        x = std::clamp(x, 0, res - 1);
        z = std::clamp(z, 0, res - 1);
        return grid[static_cast<size_t>(z) * static_cast<size_t>(res)
                    + static_cast<size_t>(x)];
    };
    const int x1 = static_cast<int>(std::floor(fx));
    const int z1 = static_cast<int>(std::floor(fz));
    const float tx = fx - static_cast<float>(x1);
    const float tz = fz - static_cast<float>(z1);

    float rows[4];
    for (int r = 0; r < 4; ++r) {
        const int z = z1 - 1 + r;
        rows[r] = catmull_rom(at(x1 - 1, z), at(x1, z), at(x1 + 1, z),
                              at(x1 + 2, z), tx);
    }
    return catmull_rom(rows[0], rows[1], rows[2], rows[3], tz);
}

} // namespace terrain
