#include "terrain/coast.h"

#include <algorithm>
#include <cmath>
#include <execution>
#include <numeric>

namespace terrain {

namespace {

constexpr int kRayCount = 16;
constexpr int kRayStepCells = 4;
constexpr float kFetchCapM = 2500.0f;
constexpr int kContourBlurPasses = 4;
// Alcance del penacho de sedimento de una desembocadura a lo largo de la
// costa. Con σ=400 m el aporte se nota ~1,2 km a cada lado de la boca.
constexpr float kMouthSigmaM = 400.0f;

std::vector<int> row_indices(int res) {
    std::vector<int> rows(static_cast<size_t>(res));
    std::iota(rows.begin(), rows.end(), 0);
    return rows;
}

} // namespace

CoastExposure compute_coast_exposure(const Heightfield& hf,
                                     const Hydrology& hydro,
                                     const TerrainConfig& cfg) {
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    const size_t n = hf.data().size();
    const auto rows = row_indices(res);

    CoastExposure out;
    out.wave_energy.assign(n, 0.0f);
    out.river_supply.assign(n, 0.0f);
    out.shore_band.assign(n, 0);

    // --- Mar y contorno de costa (tierra con mar en el vecindario 8) ---
    // El SURCO del río no es mar (feedback 2026-07-13 "huella rara en los
    // finales de los ríos"): con el francobordo, el lecho del tramo final va
    // por debajo del nivel del mar tierra adentro, y tratarlo como mar creaba
    // "costa" a ambos lados del canal — la banda litoral (BFS Chebyshev, de
    // frentes cuadrados) rodeaba la desembocadura con un RECTÁNGULO de arena.
    // El canal somero con caudal queda fuera; el fondo marino hondo sigue
    // siendo mar aunque el D8 acumule caudal por él.
    const float channel_flow = cfg.river_flow_threshold
                             * (static_cast<float>(res) * static_cast<float>(res))
                             / (1024.0f * 1024.0f);
    std::vector<std::uint8_t> sea(n);
    for (size_t i = 0; i < n; ++i) {
        const bool submerged = hf.data()[i] <= cfg.sea_level;
        const bool river_groove = hydro.flow[i] >= channel_flow
                               && hf.data()[i] > cfg.sea_level - 3.5f;
        sea[i] = (submerged && !river_groove) ? 1 : 0;
    }
    std::vector<std::uint8_t> coast(n, 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            if (sea[i]) {
                continue;
            }
            for (int dz = -1; dz <= 1 && !coast[i]; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                        continue;
                    }
                    if (sea[static_cast<size_t>(nz) * res + nx]) {
                        coast[i] = 1;
                        break;
                    }
                }
            }
        }
    });

    // --- Rosa de vientos: peso por dirección de rayo ---
    // wind_dir_deg es DE dónde sopla, en convención brújula: 0 = del norte
    // (arriba del mapa, -z), 90 = del este (+x), 315 = del noroeste. El fetch
    // que importa es el mar abierto mirando HACIA el viento, así que el rayo
    // pesa por su alineación con ese rumbo. focus=0 → mar isótropo; focus=1 →
    // solo el sector del viento dominante genera oleaje.
    float weights[kRayCount];
    {
        const float wind_rad = cfg.wind_dir_deg * 3.14159265f / 180.0f;
        // Rumbo brújula → vector (x, z) del mapa: N=(0,-1), E=(1,0).
        const float wind_x = std::sin(wind_rad);
        const float wind_z = -std::cos(wind_rad);
        float sum = 0.0f;
        for (int k = 0; k < kRayCount; ++k) {
            const float ang =
                static_cast<float>(k) * 2.0f * 3.14159265f / kRayCount;
            const float align = std::max(
                0.0f, std::cos(ang) * wind_x + std::sin(ang) * wind_z);
            weights[k] = (1.0f - cfg.wind_focus) + cfg.wind_focus * 3.0f * align * align;
            sum += weights[k];
        }
        for (float& w : weights) {
            w /= sum;
        }
    }

    // --- REI/fetch por celda de costa ---
    const int cap_cells = static_cast<int>(kFetchCapM / cell);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            if (!coast[i]) {
                continue;
            }
            float energy = 0.0f;
            for (int k = 0; k < kRayCount; ++k) {
                const float ang =
                    static_cast<float>(k) * 2.0f * 3.14159265f / kRayCount;
                const float dx = std::cos(ang);
                const float dz = std::sin(ang);
                float fetch_cells = 0.0f;
                for (int t = kRayStepCells; t <= cap_cells; t += kRayStepCells) {
                    const int sx = x + static_cast<int>(std::lround(dx * t));
                    const int sz = z + static_cast<int>(std::lround(dz * t));
                    if (sx < 0 || sz < 0 || sx >= res || sz >= res) {
                        // Fuera del dominio siempre hay mar (invariante isla):
                        // el rayo sigue en aguas abiertas hasta el cap.
                        fetch_cells = static_cast<float>(cap_cells);
                        break;
                    }
                    if (!sea[static_cast<size_t>(sz) * res + sx]) {
                        break; // tocó tierra: fin del fetch en esta dirección
                    }
                    fetch_cells = static_cast<float>(t);
                }
                energy += weights[k] * fetch_cells / static_cast<float>(cap_cells);
            }
            out.wave_energy[i] = std::min(energy, 1.0f);
        }
    });

    // --- Desembocaduras: última celda de tierra de cada río ---
    const float threshold_flow = cfg.river_flow_threshold
                               * (static_cast<float>(res) * static_cast<float>(res))
                               / (1024.0f * 1024.0f);
    std::vector<std::pair<int, int>> mouths;
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            if (coast[i] && hydro.flow[i] >= threshold_flow) {
                mouths.emplace_back(x, z);
            }
        }
    }

    // --- Aporte fluvial: penacho gaussiano alrededor de cada boca ---
    if (!mouths.empty()) {
        const float inv_two_sigma2 =
            1.0f / (2.0f * kMouthSigmaM * kMouthSigmaM);
        const float reach2 = 9.0f * kMouthSigmaM * kMouthSigmaM; // 3σ
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            for (int x = 0; x < res; ++x) {
                const size_t i = static_cast<size_t>(z) * res + x;
                if (!coast[i]) {
                    continue;
                }
                float supply = 0.0f;
                for (const auto& [mx, mz] : mouths) {
                    const float dx = static_cast<float>(x - mx) * cell;
                    const float dz = static_cast<float>(z - mz) * cell;
                    const float d2 = dx * dx + dz * dz;
                    if (d2 > reach2) {
                        continue;
                    }
                    supply += std::exp(-d2 * inv_two_sigma2);
                }
                out.river_supply[i] = std::min(supply, 1.0f);
            }
        });
    }

    // --- Blur 1D a lo largo del contorno (promedia con vecinos de costa) ---
    // Evita cambios bruscos de tipo de orilla entre celdas contiguas.
    std::vector<float> tmp_e(n);
    std::vector<float> tmp_s(n);
    for (int pass = 0; pass < kContourBlurPasses; ++pass) {
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            for (int x = 0; x < res; ++x) {
                const size_t i = static_cast<size_t>(z) * res + x;
                if (!coast[i]) {
                    tmp_e[i] = 0.0f;
                    tmp_s[i] = 0.0f;
                    continue;
                }
                float sum_e = out.wave_energy[i];
                float sum_s = out.river_supply[i];
                int count = 1;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        const int nz = z + dz;
                        if ((dx == 0 && dz == 0) || nx < 0 || nz < 0
                            || nx >= res || nz >= res) {
                            continue;
                        }
                        const size_t ni = static_cast<size_t>(nz) * res + nx;
                        if (coast[ni]) {
                            sum_e += out.wave_energy[ni];
                            sum_s += out.river_supply[ni];
                            ++count;
                        }
                    }
                }
                tmp_e[i] = sum_e / static_cast<float>(count);
                tmp_s[i] = sum_s / static_cast<float>(count);
            }
        });
        std::swap(out.wave_energy, tmp_e);
        std::swap(out.river_supply, tmp_s);
    }

    // --- Banda litoral: BFS multi-fuente tierra adentro ---
    // Cada celda de la banda hereda la exposición de su celda de costa más
    // cercana (en saltos de BFS). La profundidad cubre la orilla más ancha
    // posible con el slider al máximo, con margen.
    const float max_width_m = (4.0f + std::max(cfg.beach_band_m - 2.0f, 0.0f))
                            * std::max(cfg.shore_width_scale, 1.0f) * 2.0f;
    const int band_cells =
        static_cast<int>(std::ceil(max_width_m / cell)) + 2;

    std::vector<std::pair<int, int>> frontier;
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            if (coast[i]) {
                out.shore_band[i] = 1;
                frontier.emplace_back(x, z);
            }
        }
    }
    std::vector<std::pair<int, int>> next;
    for (int depth = 0; depth < band_cells && !frontier.empty(); ++depth) {
        next.clear();
        for (const auto& [x, z] : frontier) {
            const size_t i = static_cast<size_t>(z) * res + x;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                        continue;
                    }
                    const size_t ni = static_cast<size_t>(nz) * res + nx;
                    if (out.shore_band[ni] || sea[ni]) {
                        continue;
                    }
                    out.shore_band[ni] = 1;
                    out.wave_energy[ni] = out.wave_energy[i];
                    out.river_supply[ni] = out.river_supply[i];
                    next.emplace_back(nx, nz);
                }
            }
        }
        std::swap(frontier, next);
    }

    return out;
}

} // namespace terrain
