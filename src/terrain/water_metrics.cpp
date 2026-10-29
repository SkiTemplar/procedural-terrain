#include "terrain/water_metrics.h"

#include "terrain/coast.h"
#include "terrain/water_surface.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

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
// más cercano y salto angular entre vecinas. La referencia del eje es la
// CUERDA de 4 pasos D8 aguas abajo (no la receptora cruda, cuantizada a 45°:
// en un cauce en escalera alterna E/S y no es la dirección del río), y se
// propaga a la anchura mojada por BFS. `dir_of(i)` da la corriente evaluada
// (vec2 nulo = sin dato).
template <typename DirOf>
FlowAngleStats angle_stats(const TerrainData& data, const Grid& g, float threshold,
                           const DirOf& dir_of) {
    constexpr int kChordSteps = 4;
    const auto& hydro = data.hydrology;
    const size_t n = hydro.flow.size();
    const auto wet = [&](size_t i) {
        return !std::isnan(hydro.water_level[i]) && !hydro.lake[i];
    };
    std::vector<std::int8_t> receiver(n, -1);
    for (size_t i = 0; i < n; ++i) {
        if (!wet(i) || hydro.flow[i] < threshold) {
            continue;
        }
        const int x = static_cast<int>(i % static_cast<size_t>(g.res));
        const int z = static_cast<int>(i / static_cast<size_t>(g.res));
        float best_flow = hydro.flow[i];
        for (int d = 0; d < 8; ++d) {
            if (g.inside(x + kDx[d], z + kDz[d])
                && hydro.flow[g.at(x + kDx[d], z + kDz[d])] > best_flow) {
                best_flow = hydro.flow[g.at(x + kDx[d], z + kDz[d])];
                receiver[i] = static_cast<std::int8_t>(d);
            }
        }
    }
    std::vector<glm::vec2> ref(n, glm::vec2{0.0f});
    std::vector<std::uint8_t> seen(n, 0);
    std::deque<size_t> queue;
    for (size_t i = 0; i < n; ++i) {
        if (receiver[i] < 0) {
            continue;
        }
        glm::ivec2 p{static_cast<int>(i % static_cast<size_t>(g.res)),
                     static_cast<int>(i / static_cast<size_t>(g.res))};
        const glm::ivec2 start = p;
        for (int k = 0; k < kChordSteps; ++k) {
            const std::int8_t d = receiver[g.at(p.x, p.y)];
            if (d < 0) {
                break;
            }
            p += glm::ivec2{kDx[d], kDz[d]};
        }
        ref[i] = glm::vec2(p - start);
        seen[i] = 1;
        queue.push_back(i);
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

// Mayor salto de lámina en el tramo final de una boca: la caída boca → mar y,
// subiendo kMouthReachM metros por el eje, el mayor desnivel de cada celda
// visitada hacia cualquier vecina mojada (escalones a lo largo y a lo ancho de
// la cinta). El eje aguas arriba es la vecina mojada (no lago) de más caudal
// entre las que tienen menos que la actual (el caudal crece aguas abajo).
float mouth_reach_step(const TerrainData& data, const Grid& g, int x0, int z0) {
    const auto& hydro = data.hydrology;
    float step = hydro.water_level[g.at(x0, z0)] - data.config.sea_level;
    const int reach_steps =
        std::max(1, static_cast<int>(std::lround(kMouthReachM / data.height.cell_size())));
    int x = x0;
    int z = z0;
    for (int k = 0; k <= reach_steps; ++k) {
        const size_t i = g.at(x, z);
        int best = -1;
        float best_flow = 0.0f;
        for (int d = 0; d < 8; ++d) {
            if (!g.inside(x + kDx[d], z + kDz[d])) {
                continue;
            }
            const size_t ni = g.at(x + kDx[d], z + kDz[d]);
            if (std::isnan(hydro.water_level[ni]) || hydro.lake[ni]) {
                continue;
            }
            step = std::max(step, hydro.water_level[ni] - hydro.water_level[i]);
            if (hydro.flow[ni] >= hydro.flow[i]) {
                continue;
            }
            if (best < 0 || hydro.flow[ni] > best_flow) {
                best = d;
                best_flow = hydro.flow[ni];
            }
        }
        if (best < 0) {
            break;
        }
        x += kDx[best];
        z += kDz[best];
    }
    return step;
}

// Bocas: celdas de río contiguas a mar sin lámina (abierto o encerrado: el
// mismo criterio que usa la hidrología para el nivel de base). Los contadores
// cubren TODAS las celdas de boca; la lista, una por río con caudal de río.
void measure_mouths(const TerrainData& data, const Grid& g, float threshold,
                    WaterReport& r) {
    constexpr float kStepSmallM = 0.2f;
    constexpr float kStepLargeM = 1.0f;
    const auto& hydro = data.hydrology;
    const float sea = data.config.sea_level;
    std::vector<RiverMouthReport> all;
    for (int z = 1; z < g.res - 1; ++z) {
        for (int x = 1; x < g.res - 1; ++x) {
            const size_t i = g.at(x, z);
            if (std::isnan(hydro.water_level[i]) || hydro.lake[i]) {
                continue;
            }
            bool touches_sea = false;
            for (int d = 0; d < 8 && !touches_sea; ++d) {
                const size_t ni = g.at(x + kDx[d], z + kDz[d]);
                touches_sea = std::isnan(hydro.water_level[ni])
                           && data.height.data()[ni] <= sea;
            }
            if (!touches_sea) {
                continue;
            }
            const float step = mouth_reach_step(data, g, x, z);
            const RiverMouthReport m{x, z, hydro.flow[i], step,
                                     hydro.water_level[i] - sea};
            ++r.mouth_cells;
            r.mouth_cells_step_over_02 += step > kStepSmallM ? 1 : 0;
            r.mouth_cells_step_over_1 += step > kStepLargeM ? 1 : 0;
            if (step > r.worst_mouth_cell.step_m) {
                r.worst_mouth_cell = m;
            }
            if (hydro.flow[i] >= threshold) {
                all.push_back(m);
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
        steps.push_back(std::max(std::abs(m.drop_to_sea_m), m.step_m));
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

SpeedStats speed_stats(std::vector<float> speeds) {
    constexpr float kCapTolerance = 1e-3f;
    SpeedStats st;
    st.cells = static_cast<int>(speeds.size());
    if (speeds.empty()) {
        return st;
    }
    std::sort(speeds.begin(), speeds.end());
    const auto at = [&](float q) {
        return speeds[static_cast<size_t>(q * static_cast<float>(speeds.size() - 1))];
    };
    st.p50 = at(0.5f);
    st.p95 = at(0.95f);
    st.max = speeds.back();
    const auto capped = std::count_if(speeds.begin(), speeds.end(), [](float v) {
        return v >= kMaxRiverSpeed - kCapTolerance;
    });
    st.pct_at_cap = 100.0f * static_cast<float>(capped) / static_cast<float>(speeds.size());
    return st;
}

// Recorte del mar: vértices de la lámina interior con el lecho sumergido que
// quedan por debajo del mar dentro de un quad donde el mar se descarta.
void measure_sea_clip(const TerrainData& data, WaterReport& r) {
    const WaterSurface surface = compute_water_surface(data);
    const auto& h = data.height.data();
    const float sea = data.config.sea_level;
    const int res = surface.res;
    std::vector<std::uint8_t> counted(h.size(), 0);
    for (int z = 0; z < res - 1; ++z) {
        for (int x = 0; x < res - 1; ++x) {
            if (!surface.quad_covered[surface.quad_index(x, z)]) {
                continue;
            }
            ++r.sea_clip_quads;
            for (const auto [dx, dz] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) {
                const size_t i = static_cast<size_t>(z + dz) * static_cast<size_t>(res)
                               + static_cast<size_t>(x + dx);
                const float sink = sea - surface.vertex_y[i];
                if (h[i] < sea && sink > 1e-3f && !counted[i]) {
                    counted[i] = 1;
                    ++r.sea_clip_sunk_vertices;
                    r.sea_clip_worst_sink_m = std::max(r.sea_clip_worst_sink_m, sink);
                }
            }
        }
    }
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

    measure_mouths(data, g, threshold, r);
    measure_sea_clip(data, r);

    for (size_t i = 0; i < n; ++i) {
        const bool has_water = !std::isnan(hydro.water_level[i]);
        if (has_water && !hydro.lake[i] && h[i] < sea) {
            ++r.river_bed_under_sea_plane;
        }
        if (!has_water && h[i] <= sea && !open_sea[i]) {
            ++r.enclosed_sea_cells;
        }
        // Río seco: todo cauce sin lámina que no sea mar abierto, también el
        // que queda bajo el mar encerrado entre la lámina del estuario (ahí
        // se escondía el eje del río mayor).
        if (!has_water && !open_sea[i] && hydro.flow[i] >= kDryFlowFactor * threshold) {
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

    {
        std::vector<float> wet_speeds;
        std::vector<float> axis_speeds;
        for (size_t i = 0; i < n && flow.wet_id.size() == n; ++i) {
            const float v = glm::length(flow.at(i));
            if (v <= 0.0f) {
                continue;
            }
            wet_speeds.push_back(v);
            if (hydro.flow[i] >= threshold) {
                axis_speeds.push_back(v);
            }
        }
        r.speed_wet = speed_stats(std::move(wet_speeds));
        r.speed_axis = speed_stats(std::move(axis_speeds));
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
