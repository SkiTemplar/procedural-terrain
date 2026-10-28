#include "terrain/coast.h"

#include "terrain/lem.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <execution>
#include <iostream>
#include <limits>
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

// Mar = celda sumergida que no es el SURCO de un río (feedback 2026-07-13
// "huella rara en los finales de los ríos"): con el francobordo, el lecho del
// tramo final va por debajo del nivel del mar tierra adentro, y tratarlo como
// mar creaba "costa" a ambos lados del canal — la banda litoral (BFS
// Chebyshev, de frentes cuadrados) rodeaba la desembocadura con un RECTÁNGULO
// de arena. El canal somero con caudal queda fuera; el fondo marino hondo
// sigue siendo mar aunque el D8 acumule caudal por él.
std::vector<std::uint8_t> sea_mask(const Heightfield& hf,
                                   const Hydrology& hydro,
                                   const TerrainConfig& cfg) {
    const int res = hf.resolution();
    const float channel_flow = cfg.river_flow_threshold
                             * (static_cast<float>(res) * static_cast<float>(res))
                             / (1024.0f * 1024.0f);
    const size_t n = hf.data().size();
    std::vector<std::uint8_t> sea(n);
    for (size_t i = 0; i < n; ++i) {
        const bool submerged = hf.data()[i] <= cfg.sea_level;
        const bool river_groove = hydro.flow[i] >= channel_flow
                               && hf.data()[i] > cfg.sea_level - 3.5f;
        sea[i] = (submerged && !river_groove) ? 1 : 0;
    }
    return sea;
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
    const std::vector<std::uint8_t> sea = sea_mask(hf, hydro, cfg);
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

// =====================================================================
// Erosión marina de la costa rocosa: retroceso de acantilado (Sunamura)
// =====================================================================

namespace {

struct Neighbor {
    int dx;
    int dz;
    float len; // en celdas
    bool axial;
};

constexpr float kSqrt2 = 1.41421356f;
// Los 4 primeros son los axiales (frente de erosión 4-conexo).
constexpr Neighbor kNeighbors8[8] = {
    {-1, 0, 1.0f, true},    {1, 0, 1.0f, true},      {0, -1, 1.0f, true},
    {0, 1, 1.0f, true},     {-1, -1, kSqrt2, false}, {1, -1, kSqrt2, false},
    {-1, 1, kSqrt2, false}, {1, 1, kSqrt2, false},
};

enum CellState : std::uint8_t {
    kLand = 0,
    kSea = 1,
    kEroded = 2,
    kFront = 3,      // tierra en el frente de erosión
    kPlatformed = 4, // erosionada y ya rebajada a plataforma
    kRamped = 5,     // mar previo rebajado por la rampa de enlace
};

struct FrontCell {
    std::uint32_t idx;
    float own_force;  // F_0 = √E propio de la celda
    float resistance; // F_r
    float progress;   // metros de retroceso acumulados en la celda
};

struct Erosion {
    std::uint32_t idx;
    float width; // anchura de plataforma que cruzó la ola (m)
    float force; // F_0 que trae la ola
};

bool is_open(std::uint8_t s) {
    return s == kSea || s == kEroded;
}

float smooth01(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

long long neighbor_index(int res, size_t i, const Neighbor& nb) {
    const int x = static_cast<int>(i % static_cast<size_t>(res)) + nb.dx;
    const int z = static_cast<int>(i / static_cast<size_t>(res)) + nb.dz;
    if (x < 0 || z < 0 || x >= res || z >= res) {
        return -1;
    }
    return static_cast<long long>(z) * res + x;
}

// Rebaja las celdas erosionadas a la plataforma: cota de marea al pie de la
// pared y pendiente suave mar adentro (distancia en saltos a la tierra que
// queda en pie).
void lower_to_platform(Heightfield& hf, std::vector<std::uint8_t>& state,
                       std::vector<float>& depth, const CliffRetreatParams& p) {
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    auto& h = hf.data();
    std::vector<std::uint32_t> cur;
    std::vector<std::uint32_t> nxt;
    for (size_t i = 0; i < h.size(); ++i) {
        if (state[i] != kEroded) {
            continue;
        }
        for (const auto& nb : kNeighbors8) {
            const long long ni = neighbor_index(res, i, nb);
            if (ni >= 0 && (state[static_cast<size_t>(ni)] == kLand
                            || state[static_cast<size_t>(ni)] == kFront)) {
                cur.push_back(static_cast<std::uint32_t>(i));
                state[i] = kPlatformed;
                break;
            }
        }
    }
    for (int level = 1; !cur.empty(); ++level) {
        nxt.clear();
        const float d = std::min(
            p.toe_depth_m + p.platform_slope * static_cast<float>(level) * cell,
            p.max_depth_m);
        for (const std::uint32_t i : cur) {
            depth[i] = d;
            h[i] = p.sea_level - d;
            for (const auto& nb : kNeighbors8) {
                const long long ni = neighbor_index(res, i, nb);
                if (ni >= 0 && state[static_cast<size_t>(ni)] == kEroded) {
                    state[static_cast<size_t>(ni)] = kPlatformed;
                    nxt.push_back(static_cast<std::uint32_t>(ni));
                }
            }
        }
        std::swap(cur, nxt);
    }
    // Islote erosionado entero (sin tierra que lo ancle): plataforma honda.
    for (size_t i = 0; i < h.size(); ++i) {
        if (state[i] == kEroded) {
            state[i] = kPlatformed;
            depth[i] = p.max_depth_m;
            h[i] = p.sea_level - p.max_depth_m;
        }
    }
}

// Rampa de enlace: continúa el perfil de la plataforma mar adentro y se
// funde con el fondo previo en ramp_cells celdas. min(): nunca rellena, así
// que no queda un escalón sumergido en el borde de la plataforma.
void ramp_into_seabed(Heightfield& hf, std::vector<std::uint8_t>& state,
                      std::vector<float>& depth, const CliffRetreatParams& p) {
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    auto& h = hf.data();
    std::vector<std::uint32_t> cur;
    std::vector<std::uint32_t> nxt;
    for (size_t i = 0; i < h.size(); ++i) {
        if (state[i] != kPlatformed) {
            continue;
        }
        for (const auto& nb : kNeighbors8) {
            const long long ni = neighbor_index(res, i, nb);
            if (ni >= 0 && state[static_cast<size_t>(ni)] == kSea) {
                cur.push_back(static_cast<std::uint32_t>(i));
                break;
            }
        }
    }
    for (int t = 1; t <= p.ramp_cells && !cur.empty(); ++t) {
        nxt.clear();
        const float blend =
            static_cast<float>(t) / static_cast<float>(p.ramp_cells + 1);
        for (const std::uint32_t i : cur) {
            for (const auto& nb : kNeighbors8) {
                const long long sni = neighbor_index(res, i, nb);
                if (sni < 0 || state[static_cast<size_t>(sni)] != kSea) {
                    continue;
                }
                const size_t ni = static_cast<size_t>(sni);
                state[ni] = kRamped;
                depth[ni] = std::min(depth[i] + p.platform_slope * nb.len * cell,
                                     p.max_depth_m);
                const float target = p.sea_level - depth[ni];
                h[ni] = std::min(h[ni], target + (h[ni] - target) * blend);
                nxt.push_back(static_cast<std::uint32_t>(ni));
            }
        }
        std::swap(cur, nxt);
    }
}

} // namespace

CliffRetreatStats retreat_cliffs(Heightfield& hf, const CliffRetreatInput& in,
                                 const CliffRetreatParams& params) {
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    const size_t n = hf.data().size();
    CliffRetreatStats stats;
    if (in.sea.size() != n || !in.wave_energy || !in.resistance
        || params.steps <= 0 || params.attenuation_m <= 0.0f) {
        return stats;
    }

    std::vector<std::uint8_t> state(n);
    for (size_t i = 0; i < n; ++i) {
        state[i] = in.sea[i] ? kSea : kLand;
    }
    // Por celda abierta: anchura de plataforma que la ola ya ha cruzado al
    // llegar a ella y fuerza F_0 que trae. El mar inicial es mar abierto
    // (anchura 0); la fuerza la pone el pie de cada tramo de costa.
    std::vector<float> width(n, 0.0f);
    std::vector<float> force(n, 0.0f);

    std::vector<FrontCell> front;
    const auto enlist = [&](size_t i) {
        state[i] = kFront;
        const float e = std::clamp(in.wave_energy(i), 0.0f, 1.0f);
        front.push_back({static_cast<std::uint32_t>(i), std::sqrt(e),
                         in.resistance(i), 0.0f});
    };
    const auto exposed_land = [&](size_t i, std::uint8_t open_state) {
        for (int k = 0; k < 4; ++k) {
            const long long ni = neighbor_index(res, i, kNeighbors8[k]);
            if (ni >= 0 && state[static_cast<size_t>(ni)] == open_state) {
                return true;
            }
        }
        return false;
    };
    for (size_t i = 0; i < n; ++i) {
        if (state[i] == kLand && exposed_land(i, kSea)) {
            enlist(i);
        }
    }

    std::vector<Erosion> eroding;
    std::vector<FrontCell> kept;
    for (int step = 0; step < params.steps && !front.empty(); ++step) {
        // 1) Ataque: todo el frente se evalúa con el estado del paso previo.
        eroding.clear();
        for (FrontCell& fc : front) {
            int exposed = 0;
            float w = std::numeric_limits<float>::infinity();
            float f0 = fc.own_force;
            for (const auto& nb : kNeighbors8) {
                const long long sni = neighbor_index(res, fc.idx, nb);
                if (sni < 0 || !is_open(state[static_cast<size_t>(sni)])) {
                    continue;
                }
                const size_t ni = static_cast<size_t>(sni);
                exposed += nb.axial ? 1 : 0;
                w = std::min(w, width[ni] + nb.len * cell);
                f0 = std::max(f0, force[ni]);
            }
            if (exposed == 0) {
                continue;
            }
            const float fw = f0 * std::exp(-w / params.attenuation_m);
            if (!(fw > fc.resistance)) {
                continue; // Sunamura: sin retroceso si F_w <= F_r
            }
            // Cada cara expuesta es superficie de ataque: un saliente (dos
            // caras) o un pilar aislado (cuatro) retroceden más deprisa que
            // un frente recto. √caras deja el avance de un frente diagonal
            // igual al de uno axial (isótropo).
            fc.progress += params.rate_m * std::log(fw / fc.resistance)
                         * std::sqrt(static_cast<float>(exposed));
            if (fc.progress >= cell) {
                eroding.push_back({fc.idx, w, f0});
            }
        }
        if (eroding.empty()) {
            continue;
        }
        // 2) Las celdas socavadas pasan a mar con la ola que las alcanzó.
        for (const Erosion& e : eroding) {
            state[e.idx] = kEroded;
            width[e.idx] = e.width;
            force[e.idx] = e.force;
            ++stats.eroded_cells;
            stats.max_retreat_m = std::max(stats.max_retreat_m, e.width);
        }
        // 3) Nuevo frente: lo que sigue en pie + la tierra recién expuesta.
        kept.clear();
        for (const FrontCell& fc : front) {
            if (state[fc.idx] == kFront) {
                kept.push_back(fc);
            }
        }
        std::swap(front, kept);
        for (const Erosion& e : eroding) {
            for (int k = 0; k < 4; ++k) {
                const long long ni = neighbor_index(res, e.idx, kNeighbors8[k]);
                if (ni >= 0 && state[static_cast<size_t>(ni)] == kLand) {
                    enlist(static_cast<size_t>(ni));
                }
            }
        }
    }

    if (stats.eroded_cells > 0) {
        // width ya no hace falta: se reutiliza como profundidad de plataforma.
        lower_to_platform(hf, state, width, params);
        ramp_into_seabed(hf, state, width, params);
    }
    return stats;
}

CliffRetreatStats erode_rocky_coast(Heightfield& hf, const Hydrology& hydro,
                                    const CoastExposure& coast,
                                    const std::vector<float>& lithology,
                                    int litho_res, const Noise& noise,
                                    const TerrainConfig& cfg) {
    // Calibración: con F_0 = √E, L = 70 m y la roca más blanda a F_r = 0.12,
    // el frente más batido (E≈1) tiene una plataforma de equilibrio de
    // ~150 m; la roca dura (×4) ~50 m, y con E < 0.25 la dura no retrocede.
    constexpr float kSoftResistance = 0.12f;
    constexpr float kHardRatio = 4.0f;
    // Diaclasado: densidad de fracturas a escala de decenas de metros. Es el
    // ÚNICO papel del ruido aquí — modula la resistencia, no pinta formas.
    constexpr float kJointFreqPerKm = 25.0f; // λ ~40 m
    constexpr float kJointSigma = 0.6f;
    // Cobertura de playa: el aporte fluvial pone arena al pie y la ola rompe
    // sobre ella en vez de golpear la roca.
    constexpr float kBeachCover = 0.85f;

    const int res = hf.resolution();
    const float to_macro =
        static_cast<float>(litho_res - 1) / static_cast<float>(res - 1);

    CliffRetreatInput in;
    in.sea = sea_mask(hf, hydro, cfg);
    in.wave_energy = [&coast](std::size_t i) {
        const float cover =
            kBeachCover * smooth01(0.25f, 0.75f, coast.river_supply[i]);
        const float pass = 1.0f - cover;
        return coast.wave_energy[i] * pass * pass;
    };
    in.resistance = [&](std::size_t i) {
        // El agua dulce (cauces y lagos) no se socava: su lámina ya está
        // calculada y el mar no debe abrir el cauce desde el lado.
        if (!std::isnan(hydro.water_level[i])) {
            return std::numeric_limits<float>::infinity();
        }
        const int x = static_cast<int>(i % static_cast<size_t>(res));
        const int z = static_cast<int>(i / static_cast<size_t>(res));
        const float soft = std::clamp(
            sample_bicubic(lithology, litho_res, static_cast<float>(x) * to_macro,
                           static_cast<float>(z) * to_macro),
            0.0f, 1.0f);
        const glm::vec3 p_km{hf.world_x(x) / 1000.0f, 0.0f,
                             hf.world_z(z) / 1000.0f};
        const float joints = noise.fbm(
            p_km * kJointFreqPerKm + glm::vec3{-14.7f, 0.0f, 63.2f}, 2);
        return kSoftResistance * std::pow(kHardRatio, 1.0f - soft)
             * std::exp(kJointSigma * joints);
    };

    CliffRetreatParams params;
    params.sea_level = cfg.sea_level;
    const CliffRetreatStats stats = retreat_cliffs(hf, in, params);
    std::cout << "  abrasion costera: " << stats.eroded_cells
              << " celdas socavadas, retroceso max " << stats.max_retreat_m
              << " m" << std::endl;
    return stats;
}

// =====================================================================
// Métricas de costa rocosa (dump de verificación)
// =====================================================================

namespace {

constexpr float kCliffReliefM = 4.0f;    // pared mínima sobre el mar
constexpr int kCliffRadius = 2;          // ventana de la pared (celdas)
constexpr float kPlatformMaxDepthM = 2.0f;
constexpr float kPlatformMaxSlope = 0.05f;
constexpr float kMaxPlatformM = 400.0f;  // tope de búsqueda de plataforma
constexpr float kStackReachM = 300.0f;
constexpr float kStackMaxM2 = 5000.0f;
constexpr float kTallStackM = 3.0f;

float pearson(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = a.size();
    if (n < 2) {
        return 0.0f;
    }
    double ma = 0.0;
    double mb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);
    double sab = 0.0;
    double saa = 0.0;
    double sbb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double da = a[i] - ma;
        const double db = b[i] - mb;
        sab += da * db;
        saa += da * da;
        sbb += db * db;
    }
    if (saa <= 0.0 || sbb <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>(sab / std::sqrt(saa * sbb));
}

// Rangos con empates promediados (para Spearman).
std::vector<float> ranks(const std::vector<float>& v) {
    std::vector<size_t> order(v.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&v](size_t a, size_t b) { return v[a] < v[b]; });
    std::vector<float> r(v.size());
    for (size_t i = 0; i < order.size();) {
        size_t j = i;
        while (j + 1 < order.size() && v[order[j + 1]] == v[order[i]]) {
            ++j;
        }
        const float avg = 0.5f * static_cast<float>(i + j);
        for (size_t k = i; k <= j; ++k) {
            r[order[k]] = avg;
        }
        i = j + 1;
    }
    return r;
}

// Componentes 8-conexas de tierra: etiqueta por celda (-1 = mar) y área.
std::vector<int> label_land(const std::vector<float>& h, int res, float sea,
                            std::vector<int>& area) {
    std::vector<int> label(h.size(), -1);
    std::vector<std::uint32_t> stack;
    for (size_t s = 0; s < h.size(); ++s) {
        if (h[s] <= sea || label[s] >= 0) {
            continue;
        }
        const int id = static_cast<int>(area.size());
        area.push_back(0);
        label[s] = id;
        stack.push_back(static_cast<std::uint32_t>(s));
        while (!stack.empty()) {
            const std::uint32_t i = stack.back();
            stack.pop_back();
            ++area[static_cast<size_t>(id)];
            for (const auto& nb : kNeighbors8) {
                const long long sni = neighbor_index(res, i, nb);
                if (sni < 0) {
                    continue;
                }
                const size_t ni = static_cast<size_t>(sni);
                if (h[ni] > sea && label[ni] < 0) {
                    label[ni] = id;
                    stack.push_back(static_cast<std::uint32_t>(ni));
                }
            }
        }
    }
    return label;
}

} // namespace

RockyCoastMetrics measure_rocky_coast(const Heightfield& hf,
                                      const std::vector<float>& wave_energy,
                                      float sea_level) {
    RockyCoastMetrics m;
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    const float cell_m2 = cell * cell;
    const auto& h = hf.data();
    const size_t n = h.size();
    const auto at = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res)
             + static_cast<size_t>(x);
    };

    std::vector<int> area;
    const std::vector<int> label = label_land(h, res, sea_level, area);
    const auto stack_sized = [&](int id) {
        return static_cast<float>(area[static_cast<size_t>(id)]) * cell_m2
            <= kStackMaxM2;
    };

    // Pies de acantilado: tierra con mar al lado y pared de >= 4 m a menos
    // de dos celdas (pendiente media del frente > ~0.8).
    std::vector<std::uint32_t> toes;
    std::vector<float> coast_e;
    for (int z = kCliffRadius; z < res - kCliffRadius; ++z) {
        for (int x = kCliffRadius; x < res - kCliffRadius; ++x) {
            const size_t i = at(x, z);
            if (h[i] <= sea_level) {
                continue;
            }
            bool sea_next = false;
            float top = h[i];
            for (int dz = -kCliffRadius; dz <= kCliffRadius; ++dz) {
                for (int dx = -kCliffRadius; dx <= kCliffRadius; ++dx) {
                    const float hn = h[at(x + dx, z + dz)];
                    top = std::max(top, hn);
                    if (std::abs(dx) <= 1 && std::abs(dz) <= 1 && hn <= sea_level) {
                        sea_next = true;
                    }
                }
            }
            if (sea_next) {
                coast_e.push_back(wave_energy[i]);
            }
            if (sea_next && top >= sea_level + kCliffReliefM) {
                toes.push_back(static_cast<std::uint32_t>(i));
            }
        }
    }
    m.cliff_toe_cells = static_cast<int>(toes.size());
    m.coast_cells = static_cast<int>(coast_e.size());
    if (!coast_e.empty()) {
        std::sort(coast_e.begin(), coast_e.end());
        m.coast_energy_p50 = coast_e[coast_e.size() / 2];
        m.coast_energy_p90 = coast_e[coast_e.size() * 9 / 10];
    }

    // Plataforma: mar somero y casi llano unido a un pie (relleno desde los
    // pies, tope kMaxPlatformM). La pendiente se mide solo entre celdas
    // sumergidas: la pared contigua no cuenta como pendiente del fondo.
    const auto under = [&](size_t i) { return h[i] <= sea_level; };
    const auto axis_slope = [&](size_t i, size_t lo, size_t hi) {
        const bool a = under(lo);
        const bool b = under(hi);
        if (a && b) {
            return (h[hi] - h[lo]) * 0.5f / cell;
        }
        if (a) {
            return (h[i] - h[lo]) / cell;
        }
        if (b) {
            return (h[hi] - h[i]) / cell;
        }
        return 0.0f;
    };
    const auto is_platform = [&](size_t i) {
        if (h[i] > sea_level || h[i] <= sea_level - kPlatformMaxDepthM) {
            return false;
        }
        const int x = static_cast<int>(i % static_cast<size_t>(res));
        const int z = static_cast<int>(i / static_cast<size_t>(res));
        if (x < 1 || z < 1 || x >= res - 1 || z >= res - 1) {
            return false;
        }
        const size_t row = static_cast<size_t>(res);
        const float gx = axis_slope(i, i - 1, i + 1);
        const float gz = axis_slope(i, i - row, i + row);
        return std::sqrt(gx * gx + gz * gz) < kPlatformMaxSlope;
    };
    const int max_levels = static_cast<int>(kMaxPlatformM / cell);
    std::vector<std::uint8_t> on_platform(n, 0);
    std::vector<std::uint32_t> cur;
    std::vector<std::uint32_t> nxt;
    for (const std::uint32_t toe : toes) {
        for (const auto& nb : kNeighbors8) {
            const long long sni = neighbor_index(res, toe, nb);
            if (sni >= 0 && !on_platform[static_cast<size_t>(sni)]
                && is_platform(static_cast<size_t>(sni))) {
                on_platform[static_cast<size_t>(sni)] = 1;
                cur.push_back(static_cast<std::uint32_t>(sni));
            }
        }
    }
    for (int level = 1; level <= max_levels && !cur.empty(); ++level) {
        nxt.clear();
        for (const std::uint32_t i : cur) {
            ++m.platform_cells;
            for (const auto& nb : kNeighbors8) {
                const long long sni = neighbor_index(res, i, nb);
                if (sni >= 0 && !on_platform[static_cast<size_t>(sni)]
                    && is_platform(static_cast<size_t>(sni))) {
                    on_platform[static_cast<size_t>(sni)] = 1;
                    nxt.push_back(static_cast<std::uint32_t>(sni));
                }
            }
        }
        std::swap(cur, nxt);
    }
    m.platform_area_m2 = static_cast<float>(m.platform_cells) * cell_m2;

    // Anchura por pie: transecto perpendicular al frente (normal = suma de
    // los desplazamientos a las celdas de mar de la ventana 5×5) que avanza
    // mientras pisa plataforma.
    std::vector<float> width(toes.size(), 0.0f);
    for (size_t t = 0; t < toes.size(); ++t) {
        const int x = static_cast<int>(toes[t] % static_cast<std::uint32_t>(res));
        const int z = static_cast<int>(toes[t] / static_cast<std::uint32_t>(res));
        float nx = 0.0f;
        float nz = 0.0f;
        for (int dz = -kCliffRadius; dz <= kCliffRadius; ++dz) {
            for (int dx = -kCliffRadius; dx <= kCliffRadius; ++dx) {
                if (h[at(x + dx, z + dz)] <= sea_level) {
                    nx += static_cast<float>(dx);
                    nz += static_cast<float>(dz);
                }
            }
        }
        const float len = std::sqrt(nx * nx + nz * nz);
        if (len < 1e-3f) {
            continue;
        }
        nx /= len;
        nz /= len;
        for (int s = 1; s <= max_levels; ++s) {
            const int sx = x + static_cast<int>(std::lround(nx * static_cast<float>(s)));
            const int sz = z + static_cast<int>(std::lround(nz * static_cast<float>(s)));
            if (sx < 0 || sz < 0 || sx >= res || sz >= res
                || !on_platform[at(sx, sz)]) {
                break;
            }
            width[t] = static_cast<float>(s) * cell;
        }
    }

    std::vector<float> toe_e(toes.size());
    double width_sum = 0.0;
    int with_platform = 0;
    for (size_t t = 0; t < toes.size(); ++t) {
        toe_e[t] = wave_energy[toes[t]];
        if (width[t] > 0.0f) {
            width_sum += width[t];
            ++with_platform;
        }
    }
    if (with_platform > 0) {
        m.mean_platform_width_m =
            static_cast<float>(width_sum / static_cast<double>(with_platform));
    }
    m.width_energy_pearson = pearson(width, toe_e);
    m.width_energy_spearman = pearson(ranks(width), ranks(toe_e));
    if (!toe_e.empty()) {
        std::vector<float> sorted = toe_e;
        std::sort(sorted.begin(), sorted.end());
        m.toe_energy_p50 = sorted[sorted.size() / 2];
        m.toe_energy_p90 = sorted[sorted.size() * 9 / 10];
    }

    // Farallones: islotes pequeños a menos de 300 m de un pie de acantilado
    // de una masa de tierra mayor (BFS por saltos desde esos pies).
    std::vector<std::uint8_t> near(n, 0);
    cur.clear();
    for (const std::uint32_t i : toes) {
        if (!stack_sized(label[i])) {
            near[i] = 1;
            cur.push_back(i);
        }
    }
    const int reach = static_cast<int>(kStackReachM / cell);
    for (int level = 1; level <= reach && !cur.empty(); ++level) {
        nxt.clear();
        for (const std::uint32_t i : cur) {
            for (const auto& nb : kNeighbors8) {
                const long long sni = neighbor_index(res, i, nb);
                if (sni >= 0 && !near[static_cast<size_t>(sni)]) {
                    near[static_cast<size_t>(sni)] = 1;
                    nxt.push_back(static_cast<std::uint32_t>(sni));
                }
            }
        }
        std::swap(cur, nxt);
    }
    std::vector<std::uint8_t> is_stack(area.size(), 0);
    std::vector<float> top(area.size(), sea_level);
    for (size_t i = 0; i < n; ++i) {
        const int id = label[i];
        if (id < 0 || !stack_sized(id)) {
            continue;
        }
        top[static_cast<size_t>(id)] = std::max(top[static_cast<size_t>(id)], h[i]);
        if (near[i]) {
            is_stack[static_cast<size_t>(id)] = 1;
        }
    }
    double stack_area = 0.0;
    for (size_t id = 0; id < area.size(); ++id) {
        if (!is_stack[id]) {
            continue;
        }
        const float a = static_cast<float>(area[id]) * cell_m2;
        ++m.stacks;
        m.tall_stacks += top[id] - sea_level > kTallStackM ? 1 : 0;
        stack_area += a;
        m.stack_max_area_m2 = std::max(m.stack_max_area_m2, a);
    }
    if (m.stacks > 0) {
        m.stack_mean_area_m2 = static_cast<float>(stack_area / m.stacks);
    }
    return m;
}

} // namespace terrain
