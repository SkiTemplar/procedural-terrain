// Métricas de ecología para el dump y los tests (ecology.h): reparto de
// materiales, cuantiles de los campos físicos, correlaciones del bosque,
// límite del bosque por exposición y fragmentación de la roca.

#include "terrain/ecology.h"

#include "terrain/ecology_fields.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace terrain {

namespace {

using eco_detail::bilinear;

// Rangos con empates promediados (Spearman).
std::vector<double> ranks(const std::vector<float>& v) {
    std::vector<std::size_t> order(v.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return v[a] < v[b]; });
    std::vector<double> r(v.size());
    std::size_t i = 0;
    while (i < order.size()) {
        std::size_t j = i;
        while (j + 1 < order.size() && v[order[j + 1]] == v[order[i]]) {
            ++j;
        }
        const double avg = 0.5 * static_cast<double>(i + j);
        for (std::size_t k = i; k <= j; ++k) {
            r[order[k]] = avg;
        }
        i = j + 1;
    }
    return r;
}

double pearson(const std::vector<double>& a, const std::vector<double>& b) {
    const double n = static_cast<double>(a.size());
    if (n < 2.0) {
        return 0.0;
    }
    const double ma = std::accumulate(a.begin(), a.end(), 0.0) / n;
    const double mb = std::accumulate(b.begin(), b.end(), 0.0) / n;
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return (saa > 0.0 && sbb > 0.0) ? sab / std::sqrt(saa * sbb) : 0.0;
}

double spearman(const std::vector<float>& a, const std::vector<float>& b) {
    return pearson(ranks(a), ranks(b));
}

std::array<float, 3> quantiles(std::vector<float> v) {
    if (v.empty()) {
        return {};
    }
    std::array<float, 3> q{};
    const float fr[3] = {0.10f, 0.50f, 0.90f};
    for (int k = 0; k < 3; ++k) {
        const auto nth = static_cast<std::ptrdiff_t>(
            fr[k] * static_cast<float>(v.size() - 1));
        std::nth_element(v.begin(), v.begin() + nth, v.end());
        q[static_cast<std::size_t>(k)] = v[static_cast<std::size_t>(nth)];
    }
    return q;
}

float quantile(std::vector<float> v, float fr) {
    if (v.empty()) {
        return 0.0f;
    }
    const auto nth =
        static_cast<std::ptrdiff_t>(fr * static_cast<float>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + nth, v.end());
    return v[static_cast<std::size_t>(nth)];
}

} // namespace

LogisticFit fit_presence_logistic(const std::vector<std::size_t>& present,
                                  const std::vector<std::size_t>& total, float band_m) {
    constexpr std::size_t kMinBandCells = 20;
    LogisticFit best;
    const std::size_t bands = std::min(present.size(), total.size());
    double best_sse = -1.0;
    const auto centre = [band_m](std::size_t b) {
        return (static_cast<float>(b) + 0.5f) * band_m;
    };
    for (std::size_t c = 0; c < bands; ++c) {
        const float h50 = centre(c);
        for (float s = 0.5f * band_m; s <= 30.0f * band_m; s *= 1.15f) {
            double sgg = 0.0, sfg = 0.0;
            for (std::size_t b = 0; b < bands; ++b) {
                if (total[b] < kMinBandCells) {
                    continue;
                }
                const double w = static_cast<double>(total[b]);
                const double f = static_cast<double>(present[b]) / w;
                const double g = 1.0 / (1.0 + std::exp((centre(b) - h50) / s));
                sgg += w * g * g;
                sfg += w * f * g;
            }
            if (sgg <= 0.0) {
                continue;
            }
            const double a = std::clamp(sfg / sgg, 0.0, 1.0);
            double sse = 0.0;
            for (std::size_t b = 0; b < bands; ++b) {
                if (total[b] < kMinBandCells) {
                    continue;
                }
                const double w = static_cast<double>(total[b]);
                const double f = static_cast<double>(present[b]) / w;
                const double g = 1.0 / (1.0 + std::exp((centre(b) - h50) / s));
                sse += w * (f - a * g) * (f - a * g);
            }
            if (best_sse < 0.0 || sse < best_sse) {
                best_sse = sse;
                best = {h50, s, static_cast<float>(a)};
            }
        }
    }
    return best;
}

namespace {

constexpr float kBandM = 10.0f;
constexpr int kBands = 300;

struct GroupAcc {
    std::vector<std::size_t> present = std::vector<std::size_t>(kBands, 0);
    std::vector<std::size_t> total = std::vector<std::size_t>(kBands, 0);
    std::vector<float> forest_h;
    std::vector<float> snow_h;
    std::size_t cells = 0;
};

bool is_veg(Material m) noexcept {
    return m == Material::Grass || m == Material::Forest;
}

// Fragmentación sobre la malla completa: celdas de roca/nieve sin cuerpo
// (< 3 vecinas iguales en su 3×3) y tiras de hierba de ≤ 3 celdas encerradas
// entre roca en x o en z (los "dedos" de las vaguadas).
void fragmentation(const TerrainData& data, EcologyReport& rep) {
    const int res = data.height.resolution();
    const auto mat = [&](int x, int z) {
        return data.material[static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
                             + static_cast<std::size_t>(x)];
    };
    constexpr int kMaxStrip = 3;
    std::size_t rock = 0, rock_iso = 0, snow = 0, snow_iso = 0;
    std::vector<std::uint8_t> strip(data.material.size(), 0);
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const Material m = mat(x, z);
            if (m != Material::Rock && m != Material::Snow) {
                continue;
            }
            int same = 0;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    same += (dx != 0 || dz != 0) && mat(x + dx, z + dz) == m ? 1 : 0;
                }
            }
            const bool iso = same < 3;
            if (m == Material::Rock) {
                ++rock;
                rock_iso += iso ? 1 : 0;
            } else {
                ++snow;
                snow_iso += iso ? 1 : 0;
            }
        }
    }
    // Tiras: recorridos de vegetación entre dos rocas, en filas y columnas.
    for (int axis = 0; axis < 2; ++axis) {
        for (int a = 0; a < res; ++a) {
            int run_start = -1;
            bool rock_before = false;
            for (int b = 0; b < res; ++b) {
                const int x = axis == 0 ? b : a;
                const int z = axis == 0 ? a : b;
                const Material m = mat(x, z);
                if (is_veg(m)) {
                    if (run_start < 0) {
                        run_start = b;
                    }
                    continue;
                }
                if (run_start >= 0 && rock_before && m == Material::Rock
                    && b - run_start <= kMaxStrip) {
                    for (int k = run_start; k < b; ++k) {
                        const int sx = axis == 0 ? k : a;
                        const int sz = axis == 0 ? a : k;
                        strip[static_cast<std::size_t>(sz) * static_cast<std::size_t>(res)
                              + static_cast<std::size_t>(sx)] = 1;
                    }
                }
                run_start = -1;
                rock_before = m == Material::Rock;
            }
        }
    }
    const auto strips = static_cast<std::size_t>(
        std::count(strip.begin(), strip.end(), std::uint8_t{1}));
    const auto pct = [](std::size_t a, std::size_t b) {
        return b ? 100.0 * static_cast<double>(a) / static_cast<double>(b) : 0.0;
    };
    rep.rock_isolated_pct = pct(rock_iso, rock);
    rep.snow_isolated_pct = pct(snow_iso, snow);
    rep.veg_strips_per_100_rock = pct(strips, rock);
}

void finish_groups(GroupAcc (&acc)[4], EcologyReport& rep) {
    EcologyReport::Group* groups[4] = {&rep.windward, &rep.leeward, &rep.sunny, &rep.shady};
    for (int g = 0; g < 4; ++g) {
        const GroupAcc& a = acc[g];
        EcologyReport::Group& out = *groups[g];
        out.cells = a.cells;
        const LogisticFit fit = fit_presence_logistic(a.present, a.total, kBandM);
        out.forest_line_m = fit.h50_m;
        out.treeline_m = fit.h50_m + fit.width_m * std::log(9.0f);
        out.forest_p95_m = quantile(a.forest_h, 0.95f);
        out.snow_p10_m = quantile(a.snow_h, 0.10f);
    }
}

} // namespace

EcologyReport ecology_report(const TerrainData& data) {
    EcologyReport rep;
    const Heightfield& hf = data.height;
    const int res = hf.resolution();
    const float sea = data.config.sea_level;

    std::array<std::size_t, kMaterialCount> counts{};
    std::size_t land = 0;
    for (std::size_t i = 0; i < data.material.size(); ++i) {
        if (hf.data()[i] > sea) {
            ++counts[static_cast<std::size_t>(data.material[i])];
            ++land;
        }
    }
    for (std::size_t m = 0; m < kMaterialCount; ++m) {
        rep.land_pct[m] =
            land ? 100.0 * static_cast<double>(counts[m]) / static_cast<double>(land) : 0.0;
    }

    const EcoProbe probe(data, topographic_wetness(hf, data.hydrology.flow));
    const EcoClimate climate = make_eco_climate(data.config);

    constexpr int kStride = 3;
    GroupAcc acc[4];  // barlovento, sotavento, solana, umbría
    std::vector<float> f_forest, f_twi, f_precip, f_exp;
    std::vector<float> q_precip, q_twi, q_den, q_lem, q_arid, q_dry, q_exp, q_cvx;
    std::size_t rock_cvx = 0, n_cvx = 0, rock_ccv = 0, n_ccv = 0;

    for (int z = 1; z < res - 1; z += kStride) {
        for (int x = 1; x < res - 1; x += kStride) {
            const std::size_t i = static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
                                + static_cast<std::size_t>(x);
            const Material m = data.material[i];
            if (hf.data()[i] <= sea
                || !(is_veg(m) || m == Material::Rock || m == Material::Snow)) {
                continue;
            }
            const glm::vec3 n = probe.hillslope_normal_at(x, z);
            const EcoInputs in = probe.inputs_at(x, z);
            const EcoCell eco = evaluate_ecology(in, climate);
            q_precip.push_back(in.precip);
            if (!data.lem_stats.rain.empty() && data.litho_res >= 2) {
                const float k = static_cast<float>(data.litho_res - 1)
                              / static_cast<float>(res - 1);
                q_lem.push_back(bilinear(data.lem_stats.rain, data.litho_res,
                                         static_cast<float>(x) * k,
                                         static_cast<float>(z) * k));
            }
            q_twi.push_back(in.twi);
            q_den.push_back(in.denudation);
            q_arid.push_back(eco.pet_mm > 0.0f ? eco.water_mm / eco.pet_mm : 10.0f);
            q_dry.push_back(eco.dryness);
            q_exp.push_back(in.wind_exposure);
            q_cvx.push_back(in.convexity);
            if (is_veg(m)) {
                f_forest.push_back(data.forest_density[i]);
                f_twi.push_back(in.twi);
                f_precip.push_back(in.precip);
                f_exp.push_back(in.wind_exposure);
            }
            if (in.slope > 0.3f && in.slope < 0.6f) {
                if (in.convexity > 0.3f) {
                    ++n_cvx;
                    rock_cvx += m == Material::Rock ? 1 : 0;
                } else if (in.convexity < -0.3f) {
                    ++n_ccv;
                    rock_ccv += m == Material::Rock ? 1 : 0;
                }
            }
            const float wf = probe.wind_facing(n);
            const float sf = probe.sun_facing(n);
            const bool member[4] = {wf < -0.15f, wf > 0.15f, sf > 0.15f, sf < -0.15f};
            const auto band = static_cast<std::size_t>(
                std::clamp(static_cast<int>(in.height_m / kBandM), 0, kBands - 1));
            for (int g = 0; g < 4; ++g) {
                if (!member[g]) {
                    continue;
                }
                GroupAcc& a = acc[g];
                ++a.cells;
                // La roca no entra en el denominador: su ausencia de bosque
                // la decide el suelo, no la temperatura.
                if (m != Material::Rock) {
                    ++a.total[band];
                }
                if (m == Material::Forest) {
                    ++a.present[band];
                    a.forest_h.push_back(in.height_m);
                } else if (m == Material::Snow) {
                    a.snow_h.push_back(in.height_m);
                }
            }
        }
    }

    rep.precip_q = quantiles(q_precip);
    rep.lem_rain_q = quantiles(q_lem);
    rep.twi_q = quantiles(q_twi);
    rep.denudation_q = quantiles(q_den);
    rep.aridity_q = quantiles(q_arid);
    rep.dryness_q = quantiles(q_dry);
    rep.exposure_q = quantiles(q_exp);
    rep.convexity_q = quantiles(q_cvx);
    rep.spearman_forest_twi = spearman(f_forest, f_twi);
    rep.spearman_forest_precip = spearman(f_forest, f_precip);
    rep.spearman_forest_exposure = spearman(f_forest, f_exp);
    rep.rock_convex_pct = n_cvx ? 100.0 * static_cast<double>(rock_cvx) / static_cast<double>(n_cvx) : 0.0;
    rep.rock_concave_pct = n_ccv ? 100.0 * static_cast<double>(rock_ccv) / static_cast<double>(n_ccv) : 0.0;
    finish_groups(acc, rep);
    fragmentation(data, rep);
    return rep;
}

} // namespace terrain
