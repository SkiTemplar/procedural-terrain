#include "terrain/water_metrics.h"

#include "terrain/coast.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <deque>

namespace terrain {

namespace {

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
constexpr float kRadToDeg = 57.2957795f;
constexpr float kDryFlowFactor = 1.5f;
constexpr float kShallowSeaM = 2.4f;
constexpr float kExposedE = 0.45f;
constexpr float kShelteredE = 0.16f;
constexpr float kMinEnergy = 0.005f;
// Supresión de bocas vecinas: dentro de ~100 m solo cuenta la de más caudal
// (la cinta de un río ancho toca el mar en varias celdas).
constexpr int kMouthSuppressCells = 40;
constexpr size_t kMouthsReported = 5;

struct Grid {
    int res;
    [[nodiscard]] size_t at(int x, int z) const {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    }
    [[nodiscard]] bool inside(int x, int z) const {
        return x >= 0 && z >= 0 && x < res && z < res;
    }
};

// Mar abierto: celdas bajo el nivel del mar sin lámina de río/lago, conexas
// con el borde del dominio (invariante isla: fuera siempre hay mar).
std::vector<std::uint8_t> open_sea_mask(const TerrainData& data, const Grid& g) {
    const auto& h = data.height.data();
    const auto& wl = data.hydrology.water_level;
    const float sea = data.config.sea_level;
    const size_t n = h.size();
    const auto is_sea = [&](size_t i) { return h[i] <= sea && std::isnan(wl[i]); };
    std::vector<std::uint8_t> open(n, 0);
    std::deque<size_t> queue;
    for (int t = 0; t < g.res; ++t) {
        for (const auto [x, z] : {std::pair{t, 0}, {t, g.res - 1}, {0, t}, {g.res - 1, t}}) {
            const size_t i = g.at(x, z);
            if (!open[i] && is_sea(i)) {
                open[i] = 1;
                queue.push_back(i);
            }
        }
    }
    while (!queue.empty()) {
        const size_t i = queue.front();
        queue.pop_front();
        const int x = static_cast<int>(i % static_cast<size_t>(g.res));
        const int z = static_cast<int>(i / static_cast<size_t>(g.res));
        for (int d = 0; d < 8; ++d) {
            const int nx = x + kDx[d];
            const int nz = z + kDz[d];
            if (!g.inside(nx, nz)) {
                continue;
            }
            const size_t ni = g.at(nx, nz);
            if (!open[ni] && is_sea(ni)) {
                open[ni] = 1;
                queue.push_back(ni);
            }
        }
    }
    return open;
}

float angle_deg(const glm::vec2& a, const glm::vec2& b) {
    const float c = glm::dot(glm::normalize(a), glm::normalize(b));
    return std::acos(std::clamp(c, -1.0f, 1.0f)) * kRadToDeg;
}

// Ángulo de cada celda de río respecto a la dirección aguas abajo de su eje
// más cercano (receptora D8 cruda del eje, propagada por BFS) y salto angular
// entre vecinas. `dir_of(i)` da la corriente evaluada (vec2 nulo = sin dato).
template <typename DirOf>
FlowAngleStats angle_stats(const TerrainData& data, const Grid& g, float threshold,
                           const DirOf& dir_of) {
    const auto& hydro = data.hydrology;
    const size_t n = hydro.flow.size();
    const auto wet = [&](size_t i) {
        return !std::isnan(hydro.water_level[i]) && !hydro.lake[i];
    };
    std::vector<glm::vec2> ref(n, glm::vec2{0.0f});
    std::vector<std::uint8_t> seen(n, 0);
    std::deque<size_t> queue;
    for (size_t i = 0; i < n; ++i) {
        if (!wet(i) || hydro.flow[i] < threshold) {
            continue;
        }
        const int x = static_cast<int>(i % static_cast<size_t>(g.res));
        const int z = static_cast<int>(i / static_cast<size_t>(g.res));
        float best_flow = hydro.flow[i];
        int best = -1;
        for (int d = 0; d < 8; ++d) {
            if (g.inside(x + kDx[d], z + kDz[d])
                && hydro.flow[g.at(x + kDx[d], z + kDz[d])] > best_flow) {
                best_flow = hydro.flow[g.at(x + kDx[d], z + kDz[d])];
                best = d;
            }
        }
        if (best >= 0) {
            ref[i] = glm::vec2{static_cast<float>(kDx[best]), static_cast<float>(kDz[best])};
            seen[i] = 1;
            queue.push_back(i);
        }
    }
    while (!queue.empty()) {
        const size_t i = queue.front();
        queue.pop_front();
        const int x = static_cast<int>(i % static_cast<size_t>(g.res));
        const int z = static_cast<int>(i / static_cast<size_t>(g.res));
        for (int d = 0; d < 8; ++d) {
            if (!g.inside(x + kDx[d], z + kDz[d])) {
                continue;
            }
            const size_t ni = g.at(x + kDx[d], z + kDz[d]);
            if (!seen[ni] && wet(ni)) {
                seen[ni] = 1;
                ref[ni] = ref[i];
                queue.push_back(ni);
            }
        }
    }

    FlowAngleStats s;
    int over = 0;
    std::vector<float> jumps;
    for (int z = 0; z < g.res - 1; ++z) {
        for (int x = 0; x < g.res - 1; ++x) {
            const size_t i = g.at(x, z);
            if (!seen[i]) {
                continue;
            }
            const glm::vec2 v = dir_of(i);
            if (glm::length(v) < 1e-6f) {
                continue;
            }
            ++s.cells;
            if (angle_deg(v, ref[i]) > 60.0f) {
                ++over;
            }
            for (const size_t ni : {g.at(x + 1, z), g.at(x, z + 1)}) {
                const glm::vec2 vn = dir_of(ni);
                if (seen[ni] && glm::length(vn) > 1e-6f) {
                    jumps.push_back(angle_deg(v, vn));
                }
            }
        }
    }
    if (s.cells > 0) {
        s.pct_over_60deg = 100.0f * static_cast<float>(over) / static_cast<float>(s.cells);
    }
    if (!jumps.empty()) {
        const auto mid = jumps.begin() + static_cast<std::ptrdiff_t>(jumps.size() / 2);
        std::nth_element(jumps.begin(), mid, jumps.end());
        s.median_jump_deg = *mid;
    }
    return s;
}

void measure_mouths(const TerrainData& data, const Grid& g, float threshold,
                    const std::vector<std::uint8_t>& open_sea, WaterReport& r) {
    const auto& hydro = data.hydrology;
    const float sea = data.config.sea_level;
    std::vector<RiverMouthReport> all;
    for (int z = 1; z < g.res - 1; ++z) {
        for (int x = 1; x < g.res - 1; ++x) {
            const size_t i = g.at(x, z);
            if (std::isnan(hydro.water_level[i]) || hydro.lake[i]
                || hydro.flow[i] < threshold) {
                continue;
            }
            bool touches_sea = false;
            for (int d = 0; d < 8 && !touches_sea; ++d) {
                touches_sea = open_sea[g.at(x + kDx[d], z + kDz[d])] != 0;
            }
            if (touches_sea) {
                all.push_back({x, z, hydro.flow[i], hydro.water_level[i] - sea});
            }
        }
    }
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) {
        return a.flow != b.flow ? a.flow > b.flow : (a.z != b.z ? a.z < b.z : a.x < b.x);
    });
    std::vector<RiverMouthReport> kept;
    for (const auto& m : all) {
        const bool near_kept = std::any_of(kept.begin(), kept.end(), [&](const auto& k) {
            return std::abs(k.x - m.x) <= kMouthSuppressCells
                && std::abs(k.z - m.z) <= kMouthSuppressCells;
        });
        if (!near_kept) {
            kept.push_back(m);
        }
    }
    std::vector<float> steps;
    for (const auto& m : kept) {
        steps.push_back(std::abs(m.step_m));
    }
    if (!steps.empty()) {
        std::sort(steps.begin(), steps.end());
        r.mouth_step_max = steps.back();
        r.mouth_step_p95 = steps[static_cast<size_t>(
            static_cast<float>(steps.size() - 1) * 0.95f)];
    }
    kept.resize(std::min(kept.size(), kMouthsReported));
    r.mouths = std::move(kept);
}

} // namespace

WaterReport measure_water(const TerrainData& data, const RiverFlowField& flow) {
    WaterReport r;
    const auto& hf = data.height;
    const auto& h = hf.data();
    const auto& hydro = data.hydrology;
    const float sea = data.config.sea_level;
    const Grid g{hf.resolution()};
    const size_t n = h.size();
    const float threshold = data.config.river_flow_threshold
                          * (static_cast<float>(g.res) * static_cast<float>(g.res))
                          / (1024.0f * 1024.0f);
    const std::vector<std::uint8_t> open_sea = open_sea_mask(data, g);

    measure_mouths(data, g, threshold, open_sea, r);

    for (size_t i = 0; i < n; ++i) {
        const bool has_water = !std::isnan(hydro.water_level[i]);
        if (has_water && !hydro.lake[i] && h[i] < sea) {
            ++r.river_bed_under_sea_plane;
        }
        if (!has_water && h[i] <= sea && !open_sea[i]) {
            ++r.enclosed_sea_cells;
        }
        // Río seco en tierra firme, SIN la franja de 0.3 m sobre el mar que
        // excluía el contador anterior (ahí se escondía el tramo final).
        if (!has_water && h[i] > sea && hydro.flow[i] >= kDryFlowFactor * threshold) {
            ++r.dry_cells;
            if (hydro.flow[i] > r.dry_worst_flow) {
                r.dry_worst_flow = hydro.flow[i];
                r.dry_x = static_cast<int>(i % static_cast<size_t>(g.res));
                r.dry_z = static_cast<int>(i / static_cast<size_t>(g.res));
            }
        }
        if (open_sea[i] && sea - h[i] < kShallowSeaM) {
            const float e = data.wave_energy.empty() ? 0.0f : data.wave_energy[i];
            ++r.shallow_sea_cells;
            if (e > kMinEnergy) {
                ++r.shallow_sea_with_energy;
            }
            const float breaker_depth = kOpenSeaWaveHeightM * std::sqrt(e) / kBreakerIndex;
            if (e > kMinEnergy && sea - h[i] < breaker_depth) {
                ++r.surf_cells;
                r.surf_exposed += e > kExposedE ? 1 : 0;
                r.surf_sheltered += e < kShelteredE ? 1 : 0;
            }
        }
    }

    r.flow_field = angle_stats(data, g, threshold, [&](size_t i) { return flow.at(i); });
    // Referencia: el método anterior (descenso más pronunciado del lecho
    // tallado), para medir cuánto corregía el campo nuevo.
    r.bed_descent = angle_stats(data, g, threshold, [&](size_t i) {
        const int x = static_cast<int>(i % static_cast<size_t>(g.res));
        const int z = static_cast<int>(i / static_cast<size_t>(g.res));
        float best = 0.0f;
        glm::vec2 dir{0.0f};
        for (int d = 0; d < 8; ++d) {
            if (!g.inside(x + kDx[d], z + kDz[d])) {
                continue;
            }
            const float dist = (kDx[d] != 0 && kDz[d] != 0) ? 1.41421356f : 1.0f;
            const float s = (h[i] - h[g.at(x + kDx[d], z + kDz[d])]) / dist;
            if (s > best) {
                best = s;
                dir = glm::vec2{static_cast<float>(kDx[d]), static_cast<float>(kDz[d])};
            }
        }
        return dir;
    });
    return r;
}

} // namespace terrain
