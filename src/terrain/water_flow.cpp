#include "terrain/water_flow.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <deque>

namespace terrain {

namespace {

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
constexpr float kDiagonal = 1.41421356f;

// Suavizado a lo largo del eje: ventana Chebyshev de ±3 celdas (~7 m), dos
// pasadas. Solo se promedian celdas del MISMO río (caudal dentro de un factor
// 2): en una confluencia el afluente no tuerce la corriente del principal.
constexpr int kAxisSmoothRadius = 3;
constexpr int kAxisSmoothPasses = 2;
constexpr float kSameRiverFlowRatio = 2.0f;
// Costuras entre ramas al propagar el eje a la anchura mojada.
constexpr int kWetBlurPasses = 2;
constexpr float kMinSurfaceSlope = 1e-4f;
constexpr float kMaxSurfaceSlope = 0.3f;

} // namespace

RiverFlowField compute_river_flow(const TerrainData& data) {
    const auto& hf = data.height;
    const auto& hydro = data.hydrology;
    const int res = hf.resolution();
    const size_t n = hf.data().size();
    const float cell = hf.cell_size();
    const float threshold = data.config.river_flow_threshold
                          * (static_cast<float>(res) * static_cast<float>(res))
                          / (1024.0f * 1024.0f);
    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res)
             + static_cast<size_t>(x);
    };

    RiverFlowField field;
    field.wet_id.assign(n, RiverFlowField::kDry);
    if (hydro.water_level.size() != n) {
        return field;
    }
    std::vector<size_t> wet_cells;
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(hydro.water_level[i]) && !hydro.lake[i]) {
            field.wet_id[i] = static_cast<std::uint32_t>(wet_cells.size());
            wet_cells.push_back(i);
        }
    }
    const size_t m = wet_cells.size();
    field.velocity.assign(m, glm::vec2{0.0f});
    if (m == 0) {
        return field;
    }

    // 1) Eje: dirección hacia la receptora D8 (la vecina con más caudal entre
    //    las que tienen más que la propia: el caudal solo crece aguas abajo)
    //    y pendiente de la lámina en ese paso.
    std::vector<std::uint8_t> is_axis(m, 0);
    std::vector<glm::vec2> axis_dir(m, glm::vec2{0.0f});
    std::vector<float> axis_slope(m, 0.0f);
    for (size_t k = 0; k < m; ++k) {
        const size_t i = wet_cells[k];
        if (hydro.flow[i] < threshold) {
            continue;
        }
        const int x = static_cast<int>(i % static_cast<size_t>(res));
        const int z = static_cast<int>(i / static_cast<size_t>(res));
        float best_flow = hydro.flow[i];
        int best = -1;
        for (int d = 0; d < 8; ++d) {
            const int nx = x + kDx[d];
            const int nz = z + kDz[d];
            if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                continue;
            }
            const float f = hydro.flow[idx(nx, nz)];
            if (f > best_flow) {
                best_flow = f;
                best = d;
            }
        }
        if (best < 0) {
            continue;
        }
        is_axis[k] = 1;
        axis_dir[k] = glm::normalize(glm::vec2{static_cast<float>(kDx[best]),
                                               static_cast<float>(kDz[best])});
        const size_t r = idx(x + kDx[best], z + kDz[best]);
        if (!std::isnan(hydro.water_level[r])) {
            const float dist = cell * ((kDx[best] != 0 && kDz[best] != 0) ? kDiagonal : 1.0f);
            axis_slope[k] = std::max(0.0f, hydro.water_level[i] - hydro.water_level[r]) / dist;
        }
    }

    // 2) Suavizado a lo largo del río: borra los quiebros de 45° del D8 y los
    //    escalones de la lámina sin mezclar ríos distintos.
    {
        std::vector<glm::vec2> tmp_dir(m);
        std::vector<float> tmp_slope(m);
        for (int pass = 0; pass < kAxisSmoothPasses; ++pass) {
            for (size_t k = 0; k < m; ++k) {
                tmp_dir[k] = axis_dir[k];
                tmp_slope[k] = axis_slope[k];
                if (!is_axis[k]) {
                    continue;
                }
                const size_t i = wet_cells[k];
                const int x = static_cast<int>(i % static_cast<size_t>(res));
                const int z = static_cast<int>(i / static_cast<size_t>(res));
                const float own_flow = hydro.flow[i];
                glm::vec2 sum_dir{0.0f};
                float sum_slope = 0.0f;
                int count = 0;
                for (int dz = -kAxisSmoothRadius; dz <= kAxisSmoothRadius; ++dz) {
                    for (int dx = -kAxisSmoothRadius; dx <= kAxisSmoothRadius; ++dx) {
                        const int nx = x + dx;
                        const int nz = z + dz;
                        if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                            continue;
                        }
                        const size_t ni = idx(nx, nz);
                        const std::uint32_t kn = field.wet_id[ni];
                        if (kn == RiverFlowField::kDry || !is_axis[kn]) {
                            continue;
                        }
                        const float ratio = hydro.flow[ni] / own_flow;
                        if (ratio > kSameRiverFlowRatio || ratio < 1.0f / kSameRiverFlowRatio) {
                            continue;
                        }
                        sum_dir += axis_dir[kn];
                        sum_slope += axis_slope[kn];
                        ++count;
                    }
                }
                if (glm::length(sum_dir) > 1e-4f) {
                    tmp_dir[k] = glm::normalize(sum_dir);
                }
                tmp_slope[k] = sum_slope / static_cast<float>(std::max(count, 1));
            }
            std::swap(axis_dir, tmp_dir);
            std::swap(axis_slope, tmp_slope);
        }
    }

    // 3) Propagación a la anchura mojada: BFS multi-fuente desde el eje (cada
    //    celda hereda la corriente de su eje más cercano). Determinista: las
    //    fuentes entran en orden de fila y la cola es FIFO.
    constexpr std::uint32_t kNoSource = RiverFlowField::kDry;
    std::vector<std::uint32_t> source(m, kNoSource);
    std::deque<std::uint32_t> queue;
    for (size_t k = 0; k < m; ++k) {
        if (is_axis[k]) {
            source[k] = static_cast<std::uint32_t>(k);
            queue.push_back(static_cast<std::uint32_t>(k));
        }
    }
    while (!queue.empty()) {
        const std::uint32_t k = queue.front();
        queue.pop_front();
        const size_t i = wet_cells[k];
        const int x = static_cast<int>(i % static_cast<size_t>(res));
        const int z = static_cast<int>(i / static_cast<size_t>(res));
        for (int d = 0; d < 8; ++d) {
            const int nx = x + kDx[d];
            const int nz = z + kDz[d];
            if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                continue;
            }
            const std::uint32_t kn = field.wet_id[idx(nx, nz)];
            if (kn == RiverFlowField::kDry || source[kn] != kNoSource) {
                continue;
            }
            source[kn] = source[k];
            queue.push_back(kn);
        }
    }

    std::vector<glm::vec2> dir(m, glm::vec2{0.0f});
    std::vector<float> slope(m, 0.0f);
    for (size_t k = 0; k < m; ++k) {
        if (source[k] != kNoSource) {
            dir[k] = axis_dir[source[k]];
            slope[k] = axis_slope[source[k]];
        }
    }
    // Costuras de Voronoi entre ramas: caja 3×3 sobre las celdas mojadas.
    {
        std::vector<glm::vec2> tmp(m);
        for (int pass = 0; pass < kWetBlurPasses; ++pass) {
            for (size_t k = 0; k < m; ++k) {
                tmp[k] = dir[k];
                if (source[k] == kNoSource) {
                    continue;
                }
                const size_t i = wet_cells[k];
                const int x = static_cast<int>(i % static_cast<size_t>(res));
                const int z = static_cast<int>(i / static_cast<size_t>(res));
                glm::vec2 sum = dir[k];
                for (int d = 0; d < 8; ++d) {
                    const int nx = x + kDx[d];
                    const int nz = z + kDz[d];
                    if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                        continue;
                    }
                    const std::uint32_t kn = field.wet_id[idx(nx, nz)];
                    if (kn != RiverFlowField::kDry && source[kn] != kNoSource) {
                        sum += dir[kn];
                    }
                }
                if (glm::length(sum) > 1e-4f) {
                    tmp[k] = glm::normalize(sum);
                }
            }
            std::swap(dir, tmp);
        }
    }

    // 4) Módulo de Manning con el calado local: el agua corre en el eje y se
    //    frena hacia la orilla, donde el calado tiende a cero.
    for (size_t k = 0; k < m; ++k) {
        if (source[k] == kNoSource) {
            continue;
        }
        const size_t i = wet_cells[k];
        const float depth = std::max(0.0f, hydro.water_level[i] - hf.data()[i]);
        const float s = std::clamp(slope[k], kMinSurfaceSlope, kMaxSurfaceSlope);
        const float n_manning =
            (!hydro.whitewater.empty() && hydro.whitewater[i] >= 1) ? kManningRapids
                                                                    : kManningGravel;
        const float v = std::min(
            std::pow(depth, 2.0f / 3.0f) * std::sqrt(s) / n_manning, kMaxRiverSpeed);
        field.velocity[k] = dir[k] * v;
    }
    return field;
}

} // namespace terrain
