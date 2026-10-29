#include "terrain/coast.h"

#include "terrain/coast_metrics.h"
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

// Caudal mínimo de cauce en la rejilla de trabajo (el umbral se expresa en
// celdas equivalentes de un mapa 1024²).
float channel_flow_threshold(const TerrainConfig& cfg, int res) {
    return cfg.river_flow_threshold
         * (static_cast<float>(res) * static_cast<float>(res))
         / (1024.0f * 1024.0f);
}

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
    const float channel_flow = channel_flow_threshold(cfg, hf.resolution());
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
    const float threshold_flow = channel_flow_threshold(cfg, res);
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
    float own_force;  // F_0 = √E del tramo de costa original que la ataca
    float resistance; // F_r
    float progress;   // metros de retroceso acumulados en la celda
};

struct Erosion {
    std::uint32_t idx;
    float width; // anchura de plataforma que cruzó la ola (m)
};

// Marcas de force_ durante la herencia de F_0 (valores válidos >= 0).
constexpr float kUnlabelled = -1.0f;
constexpr float kPending = -2.0f;

using grid::kNeighbors8;
using grid::neighbor_index;

bool is_open(std::uint8_t s) {
    return s == kSea || s == kEroded;
}

// Rebaja las celdas erosionadas a la plataforma: cota de marea al pie de la
// pared y pendiente suave mar adentro (distancia en saltos a la tierra que
// queda en pie). min(): la abrasión solo excava; una celda que ya estaba
// más honda que la plataforma (un surco) conserva su cota.
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
            h[i] = std::min(h[i], p.sea_level - d);
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
            h[i] = std::min(h[i], p.sea_level - p.max_depth_m);
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

// Ola que alcanza una celda del frente: la plataforma más corta que ha
// cruzado (por la vecina abierta más cercana) y las caras expuestas (vecinas
// axiales abiertas). La F_0 no viaja con la ola: es la del tramo de costa
// original de la celda, así que el oleaje de un cabo no se filtra de lado a
// las calas abrigadas a través de la plataforma.
struct WaveAtToe {
    float width = std::numeric_limits<float>::infinity();
    int exposed_faces = 0;
};

// Frente de erosión: estado por celda y lista de celdas de tierra atacables.
class CliffFront {
public:
    CliffFront(int res, float cell, const CliffRetreatInput& in)
        : res_(res), cell_(cell), in_(in), state_(in.sea.size()),
          width_(in.sea.size(), 0.0f), force_(in.sea.size(), kUnlabelled) {
        for (size_t i = 0; i < state_.size(); ++i) {
            state_[i] = in.sea[i] ? kSea : kLand;
        }
        std::vector<std::uint32_t> coast;
        for (size_t i = 0; i < state_.size(); ++i) {
            if (state_[i] == kLand && touches(i, kSea)) {
                coast.push_back(static_cast<std::uint32_t>(i));
            }
        }
        inherit_tract_force(coast);
        for (const std::uint32_t i : coast) {
            enlist(i);
        }
    }

    [[nodiscard]] bool empty() const { return front_.empty(); }

    // Un paso: todo el frente se evalúa con el estado del paso previo (orden
    // irrelevante → determinista) y después se aplican los socavamientos.
    void step(const CliffRetreatParams& p, CliffRetreatStats& stats) {
        eroding_.clear();
        for (FrontCell& fc : front_) {
            const WaveAtToe wave = wave_at(fc);
            if (wave.exposed_faces == 0) {
                continue;
            }
            const float fw = fc.own_force * std::exp(-wave.width / p.attenuation_m);
            if (!(fw > fc.resistance)) {
                continue; // Sunamura: sin retroceso si F_w <= F_r
            }
            // Cada cara expuesta es superficie de ataque: un saliente (dos
            // caras) o un pilar aislado (cuatro) retroceden más deprisa que
            // un frente recto. √caras deja el avance de un frente diagonal
            // igual al de uno axial (isótropo).
            fc.progress += p.rate_m * std::log(fw / fc.resistance)
                         * std::sqrt(static_cast<float>(wave.exposed_faces));
            if (fc.progress >= cell_) {
                eroding_.push_back({fc.idx, wave.width});
            }
        }
        if (!eroding_.empty()) {
            apply_erosion(stats);
        }
    }

    // Plataforma de abrasión frente al oleaje, en el frente final: anchura
    // socavada delante de cada celda en pie (0 si da al mar original) frente
    // a la E de su tramo de costa previa a la etapa. Solo cuenta lo que ha
    // creado el retroceso, no la batimetría somera natural. Se llama antes
    // de rebajar la plataforma (la tierra en pie no cambia de cota).
    void measure_cut(const std::vector<float>& h, float sea_level,
                     CliffRetreatStats& stats) const {
        Samples all;
        Samples cut;
        Samples toe;
        Samples cut_toe;
        for (const FrontCell& fc : front_) {
            if (!std::isfinite(fc.resistance)) {
                continue; // inerosionable por construcción (agua dulce)
            }
            const float w = cut_width(fc.idx);
            if (!std::isfinite(w)) {
                continue;
            }
            const float e = fc.own_force * fc.own_force;
            const bool is_toe = has_wall(h, sea_level, fc.idx);
            all.add(w, e);
            if (is_toe) {
                toe.add(w, e);
            }
            if (w > 0.0f) {
                cut.add(w, e);
                if (is_toe) {
                    cut_toe.add(w, e);
                }
            }
        }
        stats.front = all.correlation();
        stats.abraded = cut.correlation();
        stats.toe = toe.correlation();
        stats.abraded_toe = cut_toe.correlation();
    }

    std::vector<std::uint8_t>& state() { return state_; }
    std::vector<float>& width() { return width_; }

private:
    bool touches(size_t i, std::uint8_t s) const {
        for (int k = 0; k < 4; ++k) {
            const long long ni = neighbor_index(res_, i, kNeighbors8[k]);
            if (ni >= 0 && state_[static_cast<size_t>(ni)] == s) {
                return true;
            }
        }
        return false;
    }

    void enlist(size_t i) {
        state_[i] = kFront;
        front_.push_back({static_cast<std::uint32_t>(i), std::max(force_[i], 0.0f),
                          in_.resistance(i), 0.0f});
    }

    struct Samples {
        std::vector<float> width;
        std::vector<float> energy;
        void add(float w, float e) {
            width.push_back(w);
            energy.push_back(e);
        }
        [[nodiscard]] CutCorrelation correlation() const {
            return {static_cast<int>(width.size()), grid::spearman(width, energy)};
        }
    };

    // Plataforma socavada delante de la celda: la menor anchura que ha
    // cruzado la ola hasta una vecina abierta (0 junto al mar original).
    [[nodiscard]] float cut_width(std::uint32_t i) const {
        float w = std::numeric_limits<float>::infinity();
        for (const auto& nb : kNeighbors8) {
            const long long sni = neighbor_index(res_, i, nb);
            if (sni >= 0 && is_open(state_[static_cast<size_t>(sni)])) {
                w = std::min(w, width_[static_cast<size_t>(sni)]);
            }
        }
        return w;
    }

    // Pie de acantilado con la definición del dump (measure_rocky_coast):
    // pared de >= 2.5 m sobre el mar a menos de dos celdas. Solo cuenta la
    // tierra en pie: las celdas socavadas aún conservan su cota original.
    [[nodiscard]] bool has_wall(const std::vector<float>& h, float sea_level,
                                std::uint32_t i) const {
        const int x = static_cast<int>(i % static_cast<std::uint32_t>(res_));
        const int z = static_cast<int>(i / static_cast<std::uint32_t>(res_));
        const int r = rocky_detail::kCliffRadius;
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                const int nx = x + dx;
                const int nz = z + dz;
                if (nx < 0 || nz < 0 || nx >= res_ || nz >= res_) {
                    continue;
                }
                const size_t ni = static_cast<size_t>(nz) * static_cast<size_t>(res_)
                                + static_cast<size_t>(nx);
                const bool standing = state_[ni] == kLand || state_[ni] == kFront;
                if (standing && h[ni] >= sea_level + rocky_detail::kCliffReliefM) {
                    return true;
                }
            }
        }
        return false;
    }

    // F_0 de cada celda de tierra: la del tramo de costa original que la
    // ataca. BFS 4-conexo desde la costa original; cada nivel promedia la
    // F_0 de sus vecinas del nivel anterior. Con la métrica de Manhattan,
    // en una costa recta cada celda hereda de la orilla que tiene enfrente,
    // y el resultado no depende del orden de recorrido.
    void inherit_tract_force(const std::vector<std::uint32_t>& coast) {
        for (const std::uint32_t i : coast) {
            force_[i] = std::sqrt(std::clamp(in_.wave_energy(i), 0.0f, 1.0f));
        }
        std::vector<std::uint32_t> cur = coast;
        std::vector<std::uint32_t> nxt;
        std::vector<float> value;
        while (!cur.empty()) {
            nxt.clear();
            for (const std::uint32_t i : cur) {
                for (int k = 0; k < 4; ++k) {
                    const long long sni = neighbor_index(res_, i, kNeighbors8[k]);
                    if (sni < 0) {
                        continue;
                    }
                    const size_t ni = static_cast<size_t>(sni);
                    if (state_[ni] == kLand && force_[ni] == kUnlabelled) {
                        force_[ni] = kPending;
                        nxt.push_back(static_cast<std::uint32_t>(ni));
                    }
                }
            }
            value.assign(nxt.size(), 0.0f);
            for (size_t j = 0; j < nxt.size(); ++j) {
                value[j] = mean_labelled_neighbors(nxt[j]);
            }
            for (size_t j = 0; j < nxt.size(); ++j) {
                force_[nxt[j]] = value[j];
            }
            std::swap(cur, nxt);
        }
    }

    float mean_labelled_neighbors(std::uint32_t i) const {
        float sum = 0.0f;
        int count = 0;
        for (int k = 0; k < 4; ++k) {
            const long long sni = neighbor_index(res_, i, kNeighbors8[k]);
            if (sni >= 0 && force_[static_cast<size_t>(sni)] >= 0.0f) {
                sum += force_[static_cast<size_t>(sni)];
                ++count;
            }
        }
        return count > 0 ? sum / static_cast<float>(count) : 0.0f;
    }

    WaveAtToe wave_at(const FrontCell& fc) const {
        WaveAtToe wave;
        for (const auto& nb : kNeighbors8) {
            const long long sni = neighbor_index(res_, fc.idx, nb);
            if (sni < 0 || !is_open(state_[static_cast<size_t>(sni)])) {
                continue;
            }
            const size_t ni = static_cast<size_t>(sni);
            wave.exposed_faces += nb.axial ? 1 : 0;
            wave.width = std::min(wave.width, width_[ni] + nb.len * cell_);
        }
        return wave;
    }

    // Las celdas socavadas pasan a mar con la ola que las alcanzó; el nuevo
    // frente es lo que sigue en pie más la tierra recién expuesta.
    void apply_erosion(CliffRetreatStats& stats) {
        for (const Erosion& e : eroding_) {
            state_[e.idx] = kEroded;
            width_[e.idx] = e.width;
            ++stats.eroded_cells;
            stats.max_retreat_m = std::max(stats.max_retreat_m, e.width);
        }
        kept_.clear();
        for (const FrontCell& fc : front_) {
            if (state_[fc.idx] == kFront) {
                kept_.push_back(fc);
            }
        }
        std::swap(front_, kept_);
        for (const Erosion& e : eroding_) {
            for (int k = 0; k < 4; ++k) {
                const long long ni = neighbor_index(res_, e.idx, kNeighbors8[k]);
                if (ni >= 0 && state_[static_cast<size_t>(ni)] == kLand) {
                    enlist(static_cast<size_t>(ni));
                }
            }
        }
    }

    int res_;
    float cell_;
    const CliffRetreatInput& in_;
    std::vector<std::uint8_t> state_;
    // Por celda abierta: plataforma que la ola ya ha cruzado al llegar a ella
    // (el mar inicial es mar abierto, anchura 0). Por celda de tierra: F_0
    // de su tramo de costa original.
    std::vector<float> width_;
    std::vector<float> force_;
    std::vector<FrontCell> front_;
    std::vector<FrontCell> kept_;
    std::vector<Erosion> eroding_;
};

// Boca de río (tierra con caudal de cauce junto al mar) y alcance de la
// arena que reparte a lo largo de la costa: σ ∝ √Q.
struct RiverMouth {
    int x;
    int z;
    float inv_two_sigma2; // en 1/celdas²
    float reach2;         // (3σ)² en celdas²
};

std::vector<RiverMouth> find_river_mouths(const std::vector<std::uint8_t>& sea,
                                          const Hydrology& hydro,
                                          const TerrainConfig& cfg, int res,
                                          float cell, float sigma_m,
                                          float max_sigma_m) {
    const float threshold = channel_flow_threshold(cfg, res);
    std::vector<RiverMouth> mouths;
    for (size_t i = 0; i < sea.size(); ++i) {
        if (sea[i] || hydro.flow[i] < threshold) {
            continue;
        }
        bool at_sea = false;
        for (const auto& nb : kNeighbors8) {
            const long long ni = neighbor_index(res, i, nb);
            at_sea = at_sea || (ni >= 0 && sea[static_cast<size_t>(ni)]);
        }
        if (!at_sea) {
            continue;
        }
        const float sigma =
            std::min(sigma_m * std::sqrt(hydro.flow[i] / threshold), max_sigma_m)
            / cell;
        mouths.push_back({static_cast<int>(i % static_cast<size_t>(res)),
                          static_cast<int>(i / static_cast<size_t>(res)),
                          1.0f / (2.0f * sigma * sigma), 9.0f * sigma * sigma});
    }
    return mouths;
}

// Arena fluvial al pie de la celda i: [0,1], la boca más influyente manda.
float beach_sand(const std::vector<RiverMouth>& mouths, int res, size_t i) {
    const int x = static_cast<int>(i % static_cast<size_t>(res));
    const int z = static_cast<int>(i / static_cast<size_t>(res));
    float sand = 0.0f;
    for (const RiverMouth& m : mouths) {
        const float dx = static_cast<float>(x - m.x);
        const float dz = static_cast<float>(z - m.z);
        const float d2 = dx * dx + dz * dz;
        if (d2 < m.reach2) {
            sand = std::max(sand, std::exp(-d2 * m.inv_two_sigma2));
        }
    }
    return sand;
}

} // namespace

CliffRetreatStats retreat_cliffs(Heightfield& hf, const CliffRetreatInput& in,
                                 const CliffRetreatParams& params) {
    CliffRetreatStats stats;
    if (in.sea.size() != hf.data().size() || !in.wave_energy || !in.resistance
        || params.steps <= 0 || params.attenuation_m <= 0.0f) {
        return stats;
    }
    CliffFront front(hf.resolution(), hf.cell_size(), in);
    for (int step = 0; step < params.steps && !front.empty(); ++step) {
        front.step(params, stats);
    }
    front.measure_cut(hf.data(), params.sea_level, stats);
    if (stats.eroded_cells > 0) {
        // La anchura ya no hace falta: se reutiliza como profundidad.
        lower_to_platform(hf, front.state(), front.width(), params);
        ramp_into_seabed(hf, front.state(), front.width(), params);
    }
    return stats;
}

CliffRetreatStats erode_rocky_coast(Heightfield& hf, const Hydrology& hydro,
                                    const CoastExposure& coast,
                                    const std::vector<float>& lithology,
                                    int litho_res, const Noise& noise,
                                    const TerrainConfig& cfg) {
    // Calibración (W_eq = L·ln(√E/F_r), L = 150 m): la roca más blanda
    // aguanta hasta E ≈ 0.15 (rías y ensenadas abrigadas no se socavan) y
    // con E = 0.64 (p90 de la costa) deja ~110 m de plataforma; la
    // formación media (×1.6) ~40 m y la más dura (×2.5) solo retrocede con
    // E ≈ 1. Edad: 240 pasos de 1.5 m → ~90 % del equilibrio.
    constexpr float kSoftResistance = 0.39f;
    constexpr float kHardRatio = 2.5f;
    constexpr float kAttenuationM = 150.0f;
    constexpr int kSteps = 240;
    constexpr float kRateM = 1.5f;
    // Diaclasado: densidad de fracturas a escala de decenas de metros. Es el
    // ÚNICO papel del ruido aquí — modula la resistencia, no pinta formas.
    constexpr float kJointFreqPerKm = 25.0f; // λ ~40 m
    constexpr float kJointSigma = 0.6f;
    // Cobertura de playa: la arena que trae cada río se reparte junto a su
    // boca y la ola rompe sobre ella en vez de golpear la roca. El alcance
    // escala con el caudal (σ ∝ √Q, tope 400 m como el penacho de la
    // exposición): un arroyo cubre su cala, el río principal su ensenada.
    // No se usa river_supply: suma penachos de σ fijo y satura a 1 en casi
    // toda la costa (auditoría 2026-10, #7), así que no distingue nada.
    constexpr float kBeachCover = 0.85f;
    constexpr float kPlumeSigmaM = 40.0f; // río en el umbral de cauce
    constexpr float kPlumeMaxSigmaM = 400.0f;

    if (lithology.empty() || litho_res < 2) {
        return {};
    }
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    const float to_macro =
        static_cast<float>(litho_res - 1) / static_cast<float>(res - 1);

    CliffRetreatInput in;
    in.sea = sea_mask(hf, hydro, cfg);
    const std::vector<RiverMouth> mouths =
        find_river_mouths(in.sea, hydro, cfg, res, cell, kPlumeSigmaM,
                          kPlumeMaxSigmaM);
    in.wave_energy = [&coast, &mouths, res](std::size_t i) {
        const float sand = beach_sand(mouths, res, i);
        const float pass = 1.0f - kBeachCover * sand;
        return coast.wave_energy[i] * pass * pass;
    };
    const float channel_flow = channel_flow_threshold(cfg, res);
    in.resistance = [&](std::size_t i) {
        // El agua dulce (cauces y lagos) no se socava: su lámina ya está
        // calculada y el mar no debe abrir el cauce desde el lado. Tampoco
        // el surco de la desembocadura (caudal de cauce sin lámina, bajo el
        // nivel del mar): su lecho es el perfil monótono de la hidrología.
        if (!std::isnan(hydro.water_level[i]) || hydro.flow[i] >= channel_flow) {
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
    params.attenuation_m = kAttenuationM;
    params.steps = kSteps;
    params.rate_m = kRateM;
    const CliffRetreatStats stats = retreat_cliffs(hf, in, params);
    const auto report = [](const char* label, const CutCorrelation& c) {
        std::cout << "    " << label << ": " << c.cells << " celdas, spearman "
                  << c.spearman << std::endl;
    };
    std::cout << "  abrasion costera: " << stats.eroded_cells
              << " celdas socavadas, retroceso max " << stats.max_retreat_m
              << " m; anchura socavada-E previa en el frente final:" << std::endl;
    report("todo el frente", stats.front);
    report("tramos socavados", stats.abraded);
    report("pies de acantilado", stats.toe);
    report("pies con plataforma socavada", stats.abraded_toe);
    return stats;
}

} // namespace terrain
