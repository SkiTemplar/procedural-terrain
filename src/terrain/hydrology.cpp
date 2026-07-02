#include "terrain/hydrology.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

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

    // Flujo D8: cada celda vierte a su vecino más bajo (superficie rellenada).
    for (const size_t i : order) {
        const int x = static_cast<int>(i % static_cast<size_t>(res));
        const int z = static_cast<int>(i / static_cast<size_t>(res));

        float best_drop = 0.0f;
        size_t best = i;
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
        if (best != i) {
            out.flow[best] += out.flow[i];
        }
    }

    // Lagos: donde el relleno superó el suelo original.
    for (size_t i = 0; i < filled.size(); ++i) {
        if (filled[i] > hf.data()[i] + kLakeEpsilon && filled[i] > cfg.sea_level) {
            out.water_level[i] = filled[i];
        }
    }

    // Ríos: pocos pero grandes. El cauce se talla con anchura y profundidad
    // proporcionales al log del flujo, con perfil parabólico (hondo en el eje,
    // suave en las orillas).
    const std::vector<float> pre_carve(hf.data());
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = idx(x, z);
            if (out.flow[i] < cfg.river_flow_threshold
                || pre_carve[i] <= cfg.sea_level) {
                continue;
            }
            const float intensity = std::min(
                std::log(out.flow[i] / cfg.river_flow_threshold + 1.0f) / 3.0f, 1.0f);
            const float depth = cfg.river_max_depth_m * intensity;
            const int radius = std::max(
                1, static_cast<int>(std::round(cfg.river_max_width_cells * intensity)));

            const float axis_level = pre_carve[i] - depth;

            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (nx < 1 || nz < 1 || nx >= res - 1 || nz >= res - 1) {
                        continue;
                    }
                    const float r = std::sqrt(static_cast<float>(dx * dx + dz * dz))
                                  / static_cast<float>(radius);
                    if (r > 1.0f) {
                        continue;
                    }
                    const size_t ni = idx(nx, nz);
                    // Perfil parabólico: máximo en el eje, 0 en la orilla.
                    const float carve_depth = depth * (1.0f - r * r);
                    const float target = axis_level + (depth - carve_depth);
                    if (hf.data()[ni] > target) {
                        hf.data()[ni] = target;
                    }
                    // Lámina de agua común al tramo, algo por encima del eje.
                    const float level = axis_level + depth * 0.55f;
                    if (std::isnan(out.water_level[ni]) || out.water_level[ni] < level) {
                        if (hf.data()[ni] < level) {
                            out.water_level[ni] = level;
                        }
                    }
                }
            }
        }
    }

    return out;
}

} // namespace terrain
