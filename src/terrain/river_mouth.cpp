#include "terrain/river_mouth.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace terrain {

namespace {

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
constexpr float kDiagonal = 1.41421356f;

constexpr float kRemansoTolM = 0.01f;  // lámina a menos de esto sobre el mar
constexpr float kKnickSlope = 0.05f;   // pendiente de lámina añadida máxima
constexpr float kMinKnickLenM = 10.0f;
constexpr float kMaxKnickLenM = 120.0f;
// Por encima de esto la caída a la costa no es una boca colgada sino una
// cascada de acantilado (valle colgado): se respeta.
constexpr float kMaxGradedHangM = 2.5f;
constexpr float kChannelScourM = 0.35f; // socavón bajo la lámina rebajada

struct Grid {
    int res;
    [[nodiscard]] std::size_t at(int x, int z) const {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
             + static_cast<std::size_t>(x);
    }
    [[nodiscard]] bool inside(int x, int z) const {
        return x >= 0 && z >= 0 && x < res && z < res;
    }
    [[nodiscard]] int x_of(std::size_t i) const {
        return static_cast<int>(i % static_cast<std::size_t>(res));
    }
    [[nodiscard]] int z_of(std::size_t i) const {
        return static_cast<int>(i / static_cast<std::size_t>(res));
    }
};

bool is_river_water(const Hydrology& out, std::size_t i) {
    return !std::isnan(out.water_level[i]) && !out.lake[i];
}

bool is_submerged_dry(const Heightfield& hf, const Hydrology& out, std::size_t i,
                      float sea_level) {
    return std::isnan(out.water_level[i]) && hf.data()[i] <= sea_level;
}

// Rebaja máxima que una boca colgada impone a cada celda mojada a distancia
// d (Dijkstra acotado por la lámina): Δ·(1 − smoothstep(d/L)).
void spread_lowering(const Grid& g, float cell, const Hydrology& out,
                     std::size_t source, float hang, std::vector<float>& lowering,
                     std::vector<float>& dist, std::vector<std::size_t>& touched) {
    const float length =
        std::clamp(1.5f * hang / kKnickSlope, kMinKnickLenM, kMaxKnickLenM);
    using Entry = std::pair<float, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    dist[source] = 0.0f;
    touched.push_back(source);
    queue.emplace(0.0f, source);
    while (!queue.empty()) {
        const auto [d, c] = queue.top();
        queue.pop();
        if (d > dist[c]) {
            continue;
        }
        const float taper = 1.0f - glm::smoothstep(0.0f, 1.0f, d / length);
        lowering[c] = std::max(lowering[c], hang * taper);
        const int x = g.x_of(c);
        const int z = g.z_of(c);
        for (int k = 0; k < 8; ++k) {
            if (!g.inside(x + kDx[k], z + kDz[k])) {
                continue;
            }
            const std::size_t j = g.at(x + kDx[k], z + kDz[k]);
            const float step = cell * ((kDx[k] != 0 && kDz[k] != 0) ? kDiagonal : 1.0f);
            const float nd = d + step;
            if (is_river_water(out, j) && nd < length && nd < dist[j]) {
                if (dist[j] == std::numeric_limits<float>::infinity()) {
                    touched.push_back(j);
                }
                dist[j] = nd;
                queue.emplace(nd, j);
            }
        }
    }
    for (const std::size_t t : touched) {
        dist[t] = std::numeric_limits<float>::infinity();
    }
    touched.clear();
}

} // namespace

int grade_coastal_knickzones(Heightfield& hf, Hydrology& out, float sea_level) {
    const Grid g{hf.resolution()};
    const std::size_t n = hf.data().size();
    const float cell = hf.cell_size();
    std::vector<float> lowering(n, 0.0f);
    std::vector<float> dist(n, std::numeric_limits<float>::infinity());
    std::vector<std::size_t> touched;

    for (int z = 1; z < g.res - 1; ++z) {
        for (int x = 1; x < g.res - 1; ++x) {
            const std::size_t i = g.at(x, z);
            if (!is_river_water(out, i)) {
                continue;
            }
            const float hang = out.water_level[i] - sea_level;
            if (hang <= kRemansoTolM || hang > kMaxGradedHangM) {
                continue;
            }
            bool coastal = false;
            for (int k = 0; k < 8 && !coastal; ++k) {
                coastal = is_submerged_dry(hf, out, g.at(x + kDx[k], z + kDz[k]),
                                           sea_level);
            }
            if (coastal) {
                spread_lowering(g, cell, out, i, hang, lowering, dist, touched);
            }
        }
    }

    int lowered = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (lowering[i] <= 0.0f) {
            continue;
        }
        const float level = std::max(sea_level, out.water_level[i] - lowering[i]);
        out.water_level[i] = level;
        hf.data()[i] = std::min(hf.data()[i], level - kChannelScourM);
        ++lowered;
    }
    return lowered;
}

int flood_estuary_pockets(const Heightfield& hf, Hydrology& out, float sea_level) {
    const Grid g{hf.resolution()};
    const std::size_t n = hf.data().size();
    // 0 = no es mar sin lámina; 1 = mar abierto; 2 = bolsa ya visitada.
    std::vector<std::uint8_t> state(n, 0);
    std::deque<std::size_t> queue;
    const auto seed_open = [&](int x, int z) {
        const std::size_t i = g.at(x, z);
        if (state[i] == 0 && is_submerged_dry(hf, out, i, sea_level)) {
            state[i] = 1;
            queue.push_back(i);
        }
    };
    for (int t = 0; t < g.res; ++t) {
        seed_open(t, 0);
        seed_open(t, g.res - 1);
        seed_open(0, t);
        seed_open(g.res - 1, t);
    }
    while (!queue.empty()) {
        const std::size_t i = queue.front();
        queue.pop_front();
        for (int k = 0; k < 8; ++k) {
            if (g.inside(g.x_of(i) + kDx[k], g.z_of(i) + kDz[k])) {
                seed_open(g.x_of(i) + kDx[k], g.z_of(i) + kDz[k]);
            }
        }
    }

    int flooded = 0;
    std::vector<std::size_t> pocket;
    for (std::size_t start = 0; start < n; ++start) {
        if (state[start] != 0 || !is_submerged_dry(hf, out, start, sea_level)) {
            continue;
        }
        pocket.clear();
        bool touches_remanso = false;
        state[start] = 2;
        queue.push_back(start);
        while (!queue.empty()) {
            const std::size_t i = queue.front();
            queue.pop_front();
            pocket.push_back(i);
            for (int k = 0; k < 8; ++k) {
                const int nx = g.x_of(i) + kDx[k];
                const int nz = g.z_of(i) + kDz[k];
                if (!g.inside(nx, nz)) {
                    continue;
                }
                const std::size_t j = g.at(nx, nz);
                if (is_river_water(out, j)
                    && out.water_level[j] <= sea_level + kRemansoTolM) {
                    touches_remanso = true;
                }
                if (state[j] == 0 && is_submerged_dry(hf, out, j, sea_level)) {
                    state[j] = 2;
                    queue.push_back(j);
                }
            }
        }
        if (touches_remanso) {
            for (const std::size_t i : pocket) {
                out.water_level[i] = sea_level;
            }
            flooded += static_cast<int>(pocket.size());
        }
    }
    return flooded;
}

} // namespace terrain
