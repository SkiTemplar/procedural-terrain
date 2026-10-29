#include "terrain/rockfall.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <execution>
#include <limits>
#include <numeric>

namespace terrain {

namespace {

// Vecinos en orden fila a fila: k y 7-k son opuestos.
constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
constexpr float kDiag = 1.41421356f;
constexpr float kDist[8] = {kDiag, 1.0f, kDiag, 1.0f, 1.0f, kDiag, 1.0f, kDiag};

// Fracción del mayor exceso que cede una celda por pasada (la mitad de la
// mitad: nunca invierte la pendiente con el vecino, estable en Jacobi).
constexpr float kRelaxTransfer = 0.25f;
// Margen de orilla: un desprendimiento nunca deja una celda a menos de esto
// por encima de la lámina de agua de su entorno (no abre agua perchada).
constexpr float kBankFreeboardM = 0.5f;
constexpr int kWaterGuardRadius = 4;
constexpr int kMaxCascadeSteps = 64;
// Por debajo de esto el exceso de pendiente es rugosidad de la celda, no una
// pared que se desmorona: no genera paquete (la térmica ya lo alisa).
constexpr float kMinDetachM = 0.05f;
constexpr float kMinParcelM = 1e-4f;
constexpr float kNoWater = -std::numeric_limits<float>::infinity();

// Ángulos de reposo del derrubio (Statham 1976; canchales alpinos 33-37°) y
// pendiente crítica de la roca relativa a la formación media.
constexpr float kReposeHardDeg = 37.0f;
constexpr float kReposeSoftDeg = 33.0f;
constexpr float kRockHardFactor = 1.20f; // con talus 1.0: ~50° en roca dura
constexpr float kRockSoftFactor = 0.84f; // ~40° en roca blanda
constexpr float kDegToRad = 0.0174532925f;

// Bilineal en coordenadas fraccionarias de un grid cuadrado res² (clamp).
float bilinear(const std::vector<float>& grid, int res, float fx, float fz) {
    fx = std::clamp(fx, 0.0f, static_cast<float>(res - 1));
    fz = std::clamp(fz, 0.0f, static_cast<float>(res - 1));
    const int x0 = std::min(static_cast<int>(fx), res - 2);
    const int z0 = std::min(static_cast<int>(fz), res - 2);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    const auto at = [&](int x, int z) {
        return grid[static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
                    + static_cast<std::size_t>(x)];
    };
    const float top = std::lerp(at(x0, z0), at(x0 + 1, z0), tx);
    const float bot = std::lerp(at(x0, z0 + 1), at(x0 + 1, z0 + 1), tx);
    return std::lerp(top, bot, tz);
}

std::vector<int> interior_rows(int res) {
    std::vector<int> rows(static_cast<std::size_t>(std::max(res - 2, 0)));
    std::iota(rows.begin(), rows.end(), 1);
    return rows;
}

struct Grid {
    int res;
    float cell;

    [[nodiscard]] std::size_t idx(int x, int z) const {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
             + static_cast<std::size_t>(x);
    }
    [[nodiscard]] bool interior(int x, int z) const {
        return x >= 1 && z >= 1 && x < res - 1 && z < res - 1;
    }
};

struct Steepest {
    int k = -1;
    float slope = 0.0f;
};

// Máximo descenso a 8 vecinos interiores (pendiente = caída / distancia real).
Steepest steepest(const std::vector<float>& h, const Grid& g, int x, int z) {
    Steepest best;
    const float hc = h[g.idx(x, z)];
    for (int k = 0; k < 8; ++k) {
        const int nx = x + kDx[k];
        const int nz = z + kDz[k];
        if (!g.interior(nx, nz)) {
            continue;
        }
        const float s = (hc - h[g.idx(nx, nz)]) / (g.cell * kDist[k]);
        if (s > best.slope) {
            best.slope = s;
            best.k = k;
        }
    }
    return best;
}

// Lámina de agua máxima en un radio de kWaterGuardRadius celdas (filtro de
// máximo separable); kNoWater donde no hay agua cerca.
std::vector<float> water_guard(const std::vector<float>& wl, const Grid& g) {
    const std::size_t n = wl.size();
    std::vector<float> tmp(n, kNoWater);
    std::vector<float> out(n, kNoWater);
    std::vector<int> rows(static_cast<std::size_t>(g.res));
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < g.res; ++x) {
            float m = kNoWater;
            for (int d = -kWaterGuardRadius; d <= kWaterGuardRadius; ++d) {
                const float v = wl[g.idx(std::clamp(x + d, 0, g.res - 1), z)];
                m = std::isnan(v) ? m : std::max(m, v);
            }
            tmp[g.idx(x, z)] = m;
        }
    });
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < g.res; ++x) {
            float m = kNoWater;
            for (int d = -kWaterGuardRadius; d <= kWaterGuardRadius; ++d) {
                m = std::max(m, tmp[g.idx(x, std::clamp(z + d, 0, g.res - 1))]);
            }
            out[g.idx(x, z)] = m;
        }
    });
    return out;
}

struct Runout {
    std::int32_t foot = -1;    // destino del derrubio fino (-1 = evacuado)
    std::int32_t boulder = -1; // destino de los bloques (-1 = evacuado)
};

// Caída de un paquete desde `src` por máxima pendiente sobre la superficie
// congelada del episodio. El derrubio fino se para en la primera celda cuya
// pendiente ya no supera su reposo (el pie de la pared); los bloques siguen
// hasta agotar la línea de energía h0 − h ≤ μ·L. El agua corta el viaje.
template <typename WetFn>
Runout run_out(const std::vector<float>& h, const std::vector<float>& repose,
               const Grid& g, const RockfallParams& p, int sx, int sz,
               const WetFn& is_wet) {
    constexpr std::int32_t kUnset = -2;
    std::int32_t foot = kUnset;
    std::int32_t boulder = kUnset;
    const float h0 = h[g.idx(sx, sz)];
    int x = sx;
    int z = sz;
    float run = 0.0f;
    for (int step = 0; step < p.max_runout_cells; ++step) {
        const std::size_t c = g.idx(x, z);
        const Steepest s = steepest(h, g, x, z);
        if (step > 0) {
            if (foot == kUnset && s.slope < repose[c]) {
                foot = static_cast<std::int32_t>(c);
            }
            if (boulder == kUnset && h0 - h[c] <= p.runout_mu * run) {
                boulder = static_cast<std::int32_t>(c);
            }
            if (foot != kUnset && boulder != kUnset) {
                break;
            }
        }
        if (s.k < 0) {
            break; // cubeta: todo lo que queda se para aquí
        }
        const int nx = x + kDx[s.k];
        const int nz = z + kDz[s.k];
        run += g.cell * kDist[s.k];
        if (is_wet(g.idx(nx, nz))) {
            foot = foot == kUnset ? -1 : foot;
            boulder = boulder == kUnset ? -1 : boulder;
            break;
        }
        x = nx;
        z = nz;
    }
    const auto here = static_cast<std::int32_t>(g.idx(x, z));
    return {foot == kUnset ? here : foot, boulder == kUnset ? here : boulder};
}

struct Budget {
    double deposited_m = 0.0; // en metros de columna sobre una celda
    double evacuated_m = 0.0;
};

// Depósito granular de un paquete: se queda lo que el reposo permite y el
// resto rueda al vecino más bajo (montón de arena): el cono crece con la
// pendiente de reposo, nunca como una aguja de una celda.
template <typename WetFn>
void deposit_cascade(std::vector<float>& h, std::vector<float>& talus,
                     const std::vector<float>& repose, const Grid& g,
                     std::int32_t start, float v, const WetFn& is_wet,
                     Budget& budget) {
    if (start < 0) {
        budget.evacuated_m += v;
        return;
    }
    auto c = static_cast<std::size_t>(start);
    for (int step = 0; step < kMaxCascadeSteps && v > kMinParcelM; ++step) {
        const int x = static_cast<int>(c % static_cast<std::size_t>(g.res));
        const int z = static_cast<int>(c / static_cast<std::size_t>(g.res));
        int best = -1;
        float best_excess = 0.0f;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + kDx[k];
            const int nz = z + kDz[k];
            if (!g.interior(nx, nz)) {
                continue;
            }
            const float e = h[c] + v - h[g.idx(nx, nz)] - repose[c] * g.cell * kDist[k];
            if (e > best_excess) {
                best_excess = e;
                best = k;
            }
        }
        if (best < 0) {
            break;
        }
        const float move = std::min(v, 0.5f * best_excess);
        const float stay = v - move;
        h[c] += stay;
        talus[c] += stay;
        budget.deposited_m += stay;
        const std::size_t nb = g.idx(x + kDx[best], z + kDz[best]);
        if (is_wet(nb)) {
            budget.evacuated_m += move;
            return;
        }
        c = nb;
        v = move;
    }
    h[c] += v;
    talus[c] += v;
    budget.deposited_m += v;
}

} // namespace

float rock_strength_factor(float lith) {
    return std::lerp(kRockHardFactor, kRockSoftFactor, std::clamp(lith, 0.0f, 1.0f));
}

float debris_repose_tan(float lith) {
    const float deg = std::lerp(kReposeHardDeg, kReposeSoftDeg, std::clamp(lith, 0.0f, 1.0f));
    return std::tan(deg * kDegToRad);
}

std::vector<float> upsample_bilinear(const std::vector<float>& grid, int grid_res,
                                     int res) {
    if (grid_res < 2 || grid.size() != static_cast<std::size_t>(grid_res) * grid_res
        || res < 2) {
        return {};
    }
    std::vector<float> out(static_cast<std::size_t>(res) * static_cast<std::size_t>(res));
    const float k = static_cast<float>(grid_res - 1) / static_cast<float>(res - 1);
    std::vector<int> rows(static_cast<std::size_t>(res));
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < res; ++x) {
            out[static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
                + static_cast<std::size_t>(x)] =
                bilinear(grid, grid_res, static_cast<float>(x) * k, static_cast<float>(z) * k);
        }
    });
    return out;
}

int relax_slopes(Heightfield& hf, const std::vector<float>& crit_tan,
                 int max_iterations, float stable_eps_m, std::vector<float>* movable,
                 const std::vector<float>* water) {
    const Grid g{hf.resolution(), hf.cell_size()};
    std::vector<float>& h = hf.data();
    const std::size_t n = h.size();
    if (crit_tan.size() != n || g.res < 3) {
        return 0;
    }
    std::vector<float> moved(n, 0.0f);
    std::vector<float> total(n, 0.0f);
    std::vector<float> delta(n, 0.0f);
    std::vector<float> row_max(static_cast<std::size_t>(g.res), 0.0f);
    const auto rows = interior_rows(g.res);
    const auto receives = [&](int x, int z) {
        return g.interior(x, z) && (water == nullptr || std::isnan((*water)[g.idx(x, z)]));
    };
    // Exceso de la pendiente i→j sobre la crítica de i (en metros).
    const auto excess = [&](std::size_t i, std::size_t j, int k) {
        return h[i] - h[j] - crit_tan[i] * g.cell * kDist[k];
    };

    for (int it = 0; it < max_iterations; ++it) {
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            for (int x = 1; x < g.res - 1; ++x) {
                const std::size_t i = g.idx(x, z);
                moved[i] = 0.0f;
                total[i] = 0.0f;
                if (movable != nullptr && (*movable)[i] <= 0.0f) {
                    continue;
                }
                float tot = 0.0f;
                float mx = 0.0f;
                for (int k = 0; k < 8; ++k) {
                    if (!receives(x + kDx[k], z + kDz[k])) {
                        continue;
                    }
                    const float e = excess(i, g.idx(x + kDx[k], z + kDz[k]), k);
                    if (e > 0.0f) {
                        tot += e;
                        mx = std::max(mx, e);
                    }
                }
                if (tot <= 0.0f) {
                    continue;
                }
                float m = kRelaxTransfer * mx;
                if (movable != nullptr) {
                    m = std::min(m, (*movable)[i]);
                }
                moved[i] = m;
                total[i] = tot;
            }
        });
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            float rmax = 0.0f;
            for (int x = 1; x < g.res - 1; ++x) {
                const std::size_t j = g.idx(x, z);
                float in = 0.0f;
                if (receives(x, z)) {
                    for (int k = 0; k < 8; ++k) {
                        const std::size_t i = g.idx(x + kDx[k], z + kDz[k]);
                        if (moved[i] <= 0.0f) {
                            continue;
                        }
                        // j está en la dirección 7-k vista desde i (misma distancia).
                        const float e = excess(i, j, 7 - k);
                        if (e > 0.0f) {
                            in += moved[i] * e / total[i];
                        }
                    }
                }
                delta[j] = in - moved[j];
                rmax = std::max(rmax, moved[j]);
            }
            row_max[static_cast<std::size_t>(z)] = rmax;
        });
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            for (int x = 1; x < g.res - 1; ++x) {
                const std::size_t j = g.idx(x, z);
                h[j] += delta[j];
                if (movable != nullptr) {
                    (*movable)[j] = std::max(0.0f, (*movable)[j] + delta[j]);
                }
            }
        });
        if (*std::max_element(row_max.begin(), row_max.end()) < stable_eps_m) {
            return it + 1;
        }
    }
    return max_iterations;
}

RockfallResult rockfall_talus(Heightfield& hf, const std::vector<float>& crit_tan,
                              const std::vector<float>& repose_tan,
                              const std::vector<float>& water_level, float sea_level,
                              const RockfallParams& params) {
    RockfallResult out;
    const Grid g{hf.resolution(), hf.cell_size()};
    std::vector<float>& h = hf.data();
    const std::size_t n = h.size();
    if (g.res < 3 || crit_tan.size() != n || repose_tan.size() != n
        || water_level.size() != n) {
        return out;
    }
    out.talus_m.assign(n, 0.0f);
    const double cell_area = static_cast<double>(g.cell) * g.cell;
    const std::vector<float> guard = water_guard(water_level, g);
    // Agua que se lleva el derrubio: ríos, lagos y la franja de batida.
    const float swash_top = sea_level + params.swash_m;
    const auto is_wet = [&](std::size_t i) {
        return !std::isnan(water_level[i]) || h[i] < swash_top;
    };
    struct Source {
        std::size_t i;
        float detach_m;
        Runout runout;
    };
    // Fuentes por fila: memoria proporcional a las paredes, no al mapa, y el
    // recorrido fila a fila conserva el orden de índice.
    std::vector<std::vector<Source>> row_sources(static_cast<std::size_t>(g.res));
    const auto rows = interior_rows(g.res);
    Budget budget;

    for (int it = 0; it < params.iterations; ++it) {
        // 1) Paredes de roca por encima de su pendiente crítica: se desprende
        //    una fracción del exceso (retroceso acotado por episodio). La
        //    caída se traza sobre la superficie congelada del episodio.
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            auto& list = row_sources[static_cast<std::size_t>(z)];
            list.clear();
            for (int x = 1; x < g.res - 1; ++x) {
                const std::size_t i = g.idx(x, z);
                if (is_wet(i)) {
                    continue;
                }
                const Steepest s = steepest(h, g, x, z);
                if (s.k < 0 || s.slope <= crit_tan[i]) {
                    continue;
                }
                float d = std::min(params.detach_fraction * (s.slope - crit_tan[i])
                                       * g.cell * kDist[s.k],
                                   params.max_detach_m);
                d = std::min(d, h[i] - (guard[i] + kBankFreeboardM));
                if (d >= kMinDetachM) {
                    list.push_back({i, d, run_out(h, repose_tan, g, params, x, z, is_wet)});
                }
            }
        });
        // 2) La pared retrocede y el derrubio cae paquete a paquete en orden de
        //    índice (serie: determinista aunque los conos de dos paredes se
        //    toquen). Paquete a paquete y no sumado por celda de parada: un
        //    montón sumado cae por una sola línea y levanta un caballón.
        std::size_t sources = 0;
        for (const auto& list : row_sources) {
            for (const Source& s : list) {
                ++sources;
                h[s.i] -= s.detach_m;
                out.stats.detached_m3 += s.detach_m * cell_area;
            }
        }
        for (const auto& list : row_sources) {
            for (const Source& s : list) {
                const float boulders = s.detach_m * params.boulder_fraction;
                deposit_cascade(h, out.talus_m, repose_tan, g, s.runout.foot,
                                s.detach_m - boulders, is_wet, budget);
                deposit_cascade(h, out.talus_m, repose_tan, g, s.runout.boulder,
                                boulders, is_wet, budget);
            }
        }
        if (it == 0) {
            out.stats.source_cells = sources;
        }
        if (sources == 0) {
            break;
        }
    }
    // 3) El canchal se asienta a su ángulo de reposo (solo el derrubio suelto
    //    se mueve; la roca de debajo no) y nunca entra en el agua.
    relax_slopes(hf, repose_tan, params.relax_iterations, 0.005f, &out.talus_m,
                 &water_level);
    out.stats.deposited_m3 = budget.deposited_m * cell_area;
    out.stats.evacuated_m3 = budget.evacuated_m * cell_area;
    return out;
}

} // namespace terrain
