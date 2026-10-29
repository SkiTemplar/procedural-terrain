#include "terrain/landform_stats.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace terrain {

namespace {

constexpr float kRadToDeg = 57.2957795f;
constexpr float kWallTan = 1.0f;      // pared: > 45°
constexpr float kFootMaxTan = 0.85f;  // pie: ya no es roca por pendiente
constexpr float kTan45 = 1.0f;
constexpr float kTan55 = 1.4281480f;
// Histograma fino de pendiente para percentiles por formación (tan 0..4).
constexpr int kFineBins = 400;
constexpr float kFineBinTan = 0.01f;
constexpr int kScreeModeBins = static_cast<int>(LandformStats::kScreeSlopeBins);
constexpr float kScreeModeBinTan = LandformStats::kScreeSlopeBinTan;

struct Grid {
    const TerrainData& data;
    int res;
    float cell;

    [[nodiscard]] std::size_t idx(int x, int z) const {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
             + static_cast<std::size_t>(x);
    }
    [[nodiscard]] float h(int x, int z) const { return data.height.at(x, z); }
    [[nodiscard]] bool interior(int x, int z) const {
        return x >= 1 && z >= 1 && x < res - 1 && z < res - 1;
    }

    [[nodiscard]] float slope_tan(int x, int z) const {
        const float gx = (h(x + 1, z) - h(x - 1, z)) / (2.0f * cell);
        const float gz = (h(x, z + 1) - h(x, z - 1)) / (2.0f * cell);
        return std::sqrt(gx * gx + gz * gz);
    }

    [[nodiscard]] bool is_land(int x, int z) const {
        const std::size_t i = idx(x, z);
        return h(x, z) > data.config.sea_level
            && std::isnan(data.hydrology.water_level[i]);
    }

    [[nodiscard]] bool is_scree(int x, int z) const {
        const std::size_t i = idx(x, z);
        const bool littoral =
            !data.wave_energy.empty() && data.wave_energy[i] > 0.0f;
        return data.material[i] == Material::Gravel && !littoral;
    }

    // Tercil de litología (0 dura, 1 media, 2 blanda) por vecino más cercano
    // del grid macro; 1 si no hay campo.
    [[nodiscard]] int litho_class(int x, int z) const {
        const int lr = data.litho_res;
        if (lr < 2 || data.lithology.empty()) {
            return 1;
        }
        const float k = static_cast<float>(lr - 1) / static_cast<float>(res - 1);
        const int lx = std::clamp(static_cast<int>(std::lround(static_cast<float>(x) * k)),
                                  0, lr - 1);
        const int lz = std::clamp(static_cast<int>(std::lround(static_cast<float>(z) * k)),
                                  0, lr - 1);
        const float lith = data.lithology[static_cast<std::size_t>(lz)
                                              * static_cast<std::size_t>(lr)
                                          + static_cast<std::size_t>(lx)];
        return lith < 1.0f / 3.0f ? 0 : (lith < 2.0f / 3.0f ? 1 : 2);
    }
};

std::size_t slope_bin(float deg) {
    const auto& edges = LandformStats::kSlopeEdgesDeg;
    return static_cast<std::size_t>(
        std::upper_bound(edges.begin(), edges.end(), deg) - edges.begin());
}

float percentile_from_bins(const std::vector<std::size_t>& bins, std::size_t total,
                           double q) {
    if (total == 0) {
        return 0.0f;
    }
    const auto target = static_cast<std::size_t>(static_cast<double>(total) * q);
    std::size_t acc = 0;
    for (std::size_t b = 0; b < bins.size(); ++b) {
        acc += bins[b];
        if (acc > target) {
            return (static_cast<float>(b) + 0.5f) * kFineBinTan;
        }
    }
    return static_cast<float>(bins.size()) * kFineBinTan;
}

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

float neighbour_dist(int k) {
    return (kDx[k] != 0 && kDz[k] != 0) ? 1.41421356f : 1.0f;
}

// Vecino interior de máximo descenso (caída / distancia real), -1 si no baja.
int steepest_neighbour(const Grid& g, int x, int z) {
    int best = -1;
    float best_drop = 0.0f;
    for (int k = 0; k < 8; ++k) {
        const int nx = x + kDx[k];
        const int nz = z + kDz[k];
        if (nx < 1 || nz < 1 || nx >= g.res - 1 || nz >= g.res - 1) {
            continue;
        }
        const float drop = (g.h(x, z) - g.h(nx, nz)) / neighbour_dist(k);
        if (drop > best_drop) {
            best_drop = drop;
            best = k;
        }
    }
    return best;
}

// Marca el pie de cada pared: se baja por máxima pendiente (8 vecinos,
// distancia diagonal real) hasta kFootRunM y se marcan las celdas de tierra
// con pendiente de no-roca. Solo lectura del relieve final.
void mark_wall_feet(const Grid& g, const std::vector<float>& slope,
                    std::vector<std::uint8_t>& foot) {
    const int res = g.res;
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            if (slope[g.idx(x, z)] <= kWallTan || !g.is_land(x, z)) {
                continue;
            }
            int cx = x;
            int cz = z;
            float run = 0.0f;
            while (run < LandformStats::kFootRunM) {
                const int best = steepest_neighbour(g, cx, cz);
                if (best < 0) {
                    break;
                }
                run += g.cell * neighbour_dist(best);
                cx += kDx[best];
                cz += kDz[best];
                if (!g.is_land(cx, cz)) {
                    break;
                }
                if (slope[g.idx(cx, cz)] < kFootMaxTan) {
                    foot[g.idx(cx, cz)] = 1;
                }
            }
        }
    }
}

// Canchal con más canchal en su entorno (suma por tabla integral).
void find_scree_hotspot(const Grid& g, LandformStats& st) {
    if (st.scree_cells == 0) {
        return;
    }
    const int res = g.res;
    const auto stride = static_cast<std::size_t>(res + 1);
    std::vector<std::uint32_t> integral(stride * stride, 0);
    for (int z = 0; z < res; ++z) {
        std::uint32_t row = 0;
        for (int x = 0; x < res; ++x) {
            row += (g.interior(x, z) && g.is_scree(x, z)) ? 1u : 0u;
            integral[static_cast<std::size_t>(z + 1) * stride + static_cast<std::size_t>(x + 1)] =
                integral[static_cast<std::size_t>(z) * stride + static_cast<std::size_t>(x + 1)]
                + row;
        }
    }
    const auto box = [&](int x0, int z0, int x1, int z1) {
        const auto at = [&](int x, int z) {
            return integral[static_cast<std::size_t>(z) * stride + static_cast<std::size_t>(x)];
        };
        return at(x1, z1) - at(x0, z1) - at(x1, z0) + at(x0, z0);
    };
    constexpr int r = LandformStats::kHotspotRadius;
    std::uint32_t best = 0;
    for (int z = r; z < res - r; ++z) {
        for (int x = r; x < res - r; ++x) {
            if (!g.is_scree(x, z)) {
                continue;
            }
            const std::uint32_t n = box(x - r, z - r, x + r + 1, z + r + 1);
            if (n > best) {
                best = n;
                st.scree_hotspot_x = x;
                st.scree_hotspot_z = z;
            }
        }
    }
}

} // namespace

LandformStats compute_landform_stats(const TerrainData& data) {
    LandformStats st;
    const int res = data.height.resolution();
    if (res < 3 || data.material.size() != data.height.data().size()) {
        return st;
    }
    const Grid g{data, res, data.height.cell_size()};

    std::vector<float> slope(data.height.data().size(), 0.0f);
    std::array<std::vector<std::size_t>, 3> fine;
    for (auto& f : fine) {
        f.assign(kFineBins, 0);
    }
    auto& scree_bins = st.scree_slope_bins;

    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const float s = g.slope_tan(x, z);
            slope[g.idx(x, z)] = s;
            if (!g.is_land(x, z)) {
                continue;
            }
            ++st.land_cells;
            ++st.slope_bins[slope_bin(std::atan(s) * kRadToDeg)];
            if (s > kTan45) {
                const bool coast =
                    g.h(x, z) - data.config.sea_level < LandformStats::kCoastBandM;
                ++(coast ? st.steep45_coast : st.steep45_inland);
            }
            const int lc = g.litho_class(x, z);
            const auto c = static_cast<std::size_t>(lc);
            ++st.litho_cells[c];
            st.litho_steep45[c] += s > kTan45 ? 1 : 0;
            st.litho_steep55[c] += s > kTan55 ? 1 : 0;
            ++fine[c][static_cast<std::size_t>(
                std::min(static_cast<int>(s / kFineBinTan), kFineBins - 1))];

            if (g.is_scree(x, z)) {
                ++st.scree_cells;
                ++scree_bins[static_cast<std::size_t>(std::min(
                    static_cast<int>(s / kScreeModeBinTan), kScreeModeBins - 1))];
            }
        }
    }
    for (std::size_t c = 0; c < 3; ++c) {
        st.litho_p90_tan[c] = percentile_from_bins(fine[c], st.litho_cells[c], 0.90);
    }
    st.scree_area_km2 = static_cast<float>(st.scree_cells) * g.cell * g.cell * 1e-6f;
    if (st.scree_cells > 0) {
        const auto mode = std::max_element(scree_bins.begin(), scree_bins.end())
                        - scree_bins.begin();
        st.scree_slope_mode_tan = (static_cast<float>(mode) + 0.5f) * kScreeModeBinTan;
    }

    std::vector<std::uint8_t> foot(slope.size(), 0);
    mark_wall_feet(g, slope, foot);
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            if (foot[g.idx(x, z)] != 0) {
                ++st.wall_foot_cells;
                st.wall_foot_scree += g.is_scree(x, z) ? 1 : 0;
            }
            if (!g.is_land(x, z) || !g.is_scree(x, z)) {
                continue;
            }
            const int k = steepest_neighbour(g, x, z);
            if (k < 0) {
                continue;
            }
            const float s_here = slope[g.idx(x, z)];
            const float s_down = slope[g.idx(x + kDx[k], z + kDz[k])];
            st.scree_concave += s_down < s_here - LandformStats::kProfileEpsTan ? 1 : 0;
            st.scree_convex += s_down > s_here + LandformStats::kProfileEpsTan ? 1 : 0;
        }
    }
    find_scree_hotspot(g, st);
    return st;
}

} // namespace terrain
