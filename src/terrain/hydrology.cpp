#include "terrain/hydrology.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace terrain {

namespace {

constexpr float kNoWater = std::numeric_limits<float>::quiet_NaN();
constexpr float kLakeEpsilon = 0.05f; // m de relleno para considerar lago

struct Cell {
    float height;
    int x;
    int z;
};

struct HigherThan {
    bool operator()(const Cell& a, const Cell& b) const { return a.height > b.height; }
};

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// Priority-flood (Barnes 2014, versión simple): partiendo del borde, propaga
// hacia dentro elevando cada celda al menos al nivel de su vecino más bajo ya
// procesado. Devuelve la superficie "rellenada".
std::vector<float> fill_depressions(const Heightfield& hf) {
    const int res = hf.resolution();
    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };

    std::vector<float> filled(hf.data());
    std::vector<bool> processed(filled.size(), false);
    std::priority_queue<Cell, std::vector<Cell>, HigherThan> open;

    for (int x = 0; x < res; ++x) {
        for (const int z : {0, res - 1}) {
            open.push({filled[idx(x, z)], x, z});
            processed[idx(x, z)] = true;
        }
    }
    for (int z = 1; z < res - 1; ++z) {
        for (const int x : {0, res - 1}) {
            open.push({filled[idx(x, z)], x, z});
            processed[idx(x, z)] = true;
        }
    }

    while (!open.empty()) {
        const Cell cell = open.top();
        open.pop();

        for (int n = 0; n < 8; ++n) {
            const int nx = cell.x + kDx[n];
            const int nz = cell.z + kDz[n];
            if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                continue;
            }
            const size_t ni = idx(nx, nz);
            if (processed[ni]) {
                continue;
            }
            processed[ni] = true;
            filled[ni] = std::max(filled[ni], cell.height);
            open.push({filled[ni], nx, nz});
        }
    }
    return filled;
}

} // namespace

Hydrology compute_hydrology(Heightfield& hf, const TerrainConfig& cfg) {
    const int res = hf.resolution();
    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };
    constexpr size_t kNone = std::numeric_limits<size_t>::max();

    const std::vector<float> filled = fill_depressions(hf);

    // Orden de proceso: de mayor a menor altura rellenada (el flujo baja).
    std::vector<size_t> order(filled.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&filled](size_t a, size_t b) { return filled[a] > filled[b]; });

    Hydrology out;
    out.flow.assign(filled.size(), 1.0f);
    out.water_level.assign(filled.size(), kNoWater);

    // Flujo D8: cada celda vierte a su vecino más bajo. Se guarda el índice
    // aguas abajo para poder recorrer los cauces después.
    std::vector<size_t> downstream(filled.size(), kNone);
    for (const size_t i : order) {
        const int x = static_cast<int>(i % static_cast<size_t>(res));
        const int z = static_cast<int>(i / static_cast<size_t>(res));

        float best_drop = 0.0f;
        size_t best = kNone;
        for (int n = 0; n < 8; ++n) {
            const int nx = x + kDx[n];
            const int nz = z + kDz[n];
            if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                continue;
            }
            const float drop = filled[i] - filled[idx(nx, nz)];
            if (drop > best_drop) {
                best_drop = drop;
                best = idx(nx, nz);
            }
        }
        downstream[i] = best;
        if (best != kNone) {
            out.flow[best] += out.flow[i];
        }
    }

    // Lagos: donde el relleno superó el suelo original.
    for (size_t i = 0; i < filled.size(); ++i) {
        if (filled[i] > hf.data()[i] + kLakeEpsilon && filled[i] > cfg.sea_level) {
            out.water_level[i] = filled[i];
        }
    }

    // ---- Ríos ----
    // El umbral se normaliza: expresado en celdas de un mapa 1024, escala con
    // el área real para que el slider signifique lo mismo a cualquier resolución.
    const float threshold = cfg.river_flow_threshold
                          * (static_cast<float>(res) * static_cast<float>(res))
                          / (1024.0f * 1024.0f);
    const std::vector<float> pre_carve(hf.data());

    const auto is_river = [&](size_t i) {
        return out.flow[i] >= threshold && pre_carve[i] > cfg.sea_level;
    };

    // 1) Nivel de agua por celda de río, monótono decreciente aguas abajo:
    //    el agua nunca "sube" siguiendo el cauce → sin escalones ni charcos raros.
    std::vector<float> level(filled.size(), std::numeric_limits<float>::infinity());
    for (const size_t i : order) { // de arriba a abajo
        if (!is_river(i)) {
            continue;
        }
        if (level[i] == std::numeric_limits<float>::infinity()) {
            level[i] = pre_carve[i];
        }
        // Los lagos imponen su lámina.
        if (!std::isnan(out.water_level[i])) {
            level[i] = out.water_level[i];
        }
        const size_t down = downstream[i];
        if (down != kNone && is_river(down)) {
            const float candidate = std::min(level[i], pre_carve[down]);
            if (level[down] == std::numeric_limits<float>::infinity()
                || candidate < level[down]) {
                level[down] = candidate;
            }
        }
    }

    // 2) Tallado con anchura respecto al nivel suavizado: perfil parabólico,
    //    radio y profundidad según el log del caudal.
    const int max_radius = std::max(
        1, static_cast<int>(std::round(cfg.river_max_width_m / hf.cell_size())));

    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = idx(x, z);
            if (!is_river(i) || level[i] == std::numeric_limits<float>::infinity()) {
                continue;
            }
            const float intensity =
                std::min(std::log(out.flow[i] / threshold + 1.0f) / 3.0f, 1.0f);
            const float depth = cfg.river_max_depth_m * (0.35f + 0.65f * intensity);
            const int radius = std::max(
                1, static_cast<int>(std::round(max_radius * intensity)));
            const float surface = level[i];

            for (int dz = -radius - 1; dz <= radius + 1; ++dz) {
                for (int dx = -radius - 1; dx <= radius + 1; ++dx) {
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (nx < 1 || nz < 1 || nx >= res - 1 || nz >= res - 1) {
                        continue;
                    }
                    const float r = std::sqrt(static_cast<float>(dx * dx + dz * dz))
                                  / static_cast<float>(radius);
                    const size_t ni = idx(nx, nz);

                    if (r <= 1.0f) {
                        // Lecho: hondo en el eje, sube hacia la orilla.
                        const float bed = surface - depth * (1.0f - r * r) - 0.4f;
                        if (hf.data()[ni] > bed) {
                            hf.data()[ni] = bed;
                        }
                    }
                    // Lámina de agua (una celda más allá de la orilla para
                    // que el agua toque tierra sin huecos).
                    if (r <= 1.35f && hf.data()[ni] < surface - 0.15f) {
                        const float water = surface - 0.1f;
                        if (std::isnan(out.water_level[ni])
                            || out.water_level[ni] < water) {
                            out.water_level[ni] = water;
                        }
                    }
                }
            }
        }
    }

    return out;
}

} // namespace terrain
