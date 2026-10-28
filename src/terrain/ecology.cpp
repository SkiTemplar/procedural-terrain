#include "terrain/ecology.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <execution>
#include <numeric>
#include <utility>

namespace terrain {

namespace {

constexpr float kPi = 3.14159265f;

// Microclima del árbol (°C equivalentes sobre la isoterma): la solana gana y
// la umbría pierde (Körner: ±100-200 m de límite entre caras); la cara de
// barlovento y el clima nuboso de la fachada húmeda lo bajan.
constexpr float kRadTreeC = 1.2f;   // por unidad de radiación relativa
constexpr float kWindTreeC = 0.6f;  // cara plenamente expuesta al viento
constexpr float kWetCoolC = 0.5f;   // por unidad de lluvia efectiva sobre 1
constexpr float kJitterC = 0.3f;    // perturbación de microclima (±45 m)
// Nieve estacional: la fusión depende sobre todo de la radiación directa.
constexpr float kRadSnowC = 1.6f;
constexpr float kSnowSlopeC = 0.6f;  // retención de nieve que cae con tanβ
// Suelo (Heimsath et al. 1997): producción P0·exp(−H/H0) frente a la
// denudación local; en equilibrio H = H0·ln(P0/E) y roca desnuda si E ≥ P0
// (speed limit de Dixon & von Blanckenburg 2012). Unidades de E: denudación
// del LEM relativa al uplift máximo por paso.
constexpr float kSoilProdMax = 1.1f;
constexpr float kSoilScaleM = 0.5f;  // H0
// En laderas con suelo E ∝ −∇²z (Roering 1999): la convexidad acelera la
// denudación de crestas; la concavidad (vaguadas) acumula coluvión.
constexpr float kCurvGain = 0.6f;
constexpr float kCurvLengthM = 30.0f;  // escala de normalización de ∇²z
constexpr int kCurvStep = 3;           // celdas: curvatura a ~7 m, no por vértice
// Humedad del suelo: TWI de ladera seca → vaguada húmeda (malla de ~2.4 m).
constexpr float kTwiDry = 3.0f;
constexpr float kTwiWet = 9.0f;
constexpr float kEdgeAmp = 0.08f;  // perturbación del borde de bosque (≤ 10 %)
constexpr float kMaxSnowSlope = 1.15f;  // ~49°: la nieve no agarra

float smooth01(float e0, float e1, float x) noexcept {
    const float t = glm::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Lluvia efectiva: el campo del LEM lleva el término de ascenso por celda,
// muy contrastado; la raíz lo comprime al rango climático (0.5 → 0.71).
float effective_precip(float precip) noexcept {
    return std::sqrt(glm::clamp(precip, 0.3f, 3.0f));
}

std::vector<int> iota_rows(int res) {
    std::vector<int> rows(static_cast<std::size_t>(res));
    std::iota(rows.begin(), rows.end(), 0);
    return rows;
}

// Box blur separable de radio r (in situ, clamp en el borde).
void box_blur(std::vector<float>& f, int res, int r, int passes) {
    std::vector<float> tmp(f.size());
    const auto rows = iota_rows(res);
    const auto at = [res](int x, int z) {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
             + static_cast<std::size_t>(x);
    };
    for (int pass = 0; pass < passes; ++pass) {
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            for (int x = 0; x < res; ++x) {
                float acc = 0.0f;
                for (int d = -r; d <= r; ++d) {
                    acc += f[at(std::clamp(x + d, 0, res - 1), z)];
                }
                tmp[at(x, z)] = acc / static_cast<float>(2 * r + 1);
            }
        });
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            for (int x = 0; x < res; ++x) {
                float acc = 0.0f;
                for (int d = -r; d <= r; ++d) {
                    acc += tmp[at(x, std::clamp(z + d, 0, res - 1))];
                }
                f[at(x, z)] = acc / static_cast<float>(2 * r + 1);
            }
        });
    }
}

float bilinear(const std::vector<float>& grid, int gres, float fx, float fz) {
    fx = glm::clamp(fx, 0.0f, static_cast<float>(gres - 1));
    fz = glm::clamp(fz, 0.0f, static_cast<float>(gres - 1));
    const int x0 = std::min(static_cast<int>(fx), gres - 2);
    const int z0 = std::min(static_cast<int>(fz), gres - 2);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    const auto g = [&](int x, int z) {
        return grid[static_cast<std::size_t>(z) * static_cast<std::size_t>(gres)
                    + static_cast<std::size_t>(x)];
    };
    const float top = std::lerp(g(x0, z0), g(x0 + 1, z0), tx);
    const float bot = std::lerp(g(x0, z0 + 1), g(x0 + 1, z0 + 1), tx);
    return std::lerp(top, bot, tz);
}

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

EcoClimate make_eco_climate(const TerrainConfig& cfg) noexcept {
    EcoClimate c;
    c.sea_temp_c = kTreelineIsothermC + kLapseRateCPerM * cfg.treeline_m;
    // La escena se calibra para que, con lluvia media y exposición neutra, la
    // nieve estacional llegue justo a snow_line_m.
    c.season_shift_c = c.sea_temp_c - kLapseRateCPerM * cfg.snow_line_m
                     - ela_summer_temp_c(kMeanPrecipMm);
    c.rock_slope = cfg.rock_slope;
    return c;
}

float ela_summer_temp_c(float precip_mm) noexcept {
    // Raíz positiva de 9T² + 296T + (645 − P) = 0.
    const float disc = 296.0f * 296.0f - 36.0f * (645.0f - precip_mm);
    return (-296.0f + std::sqrt(std::max(disc, 0.0f))) / 18.0f;
}

EcoCell evaluate_ecology(const EcoInputs& in, const EcoClimate& climate) noexcept {
    EcoCell out;
    const float t_air =
        climate.sea_temp_c - kLapseRateCPerM * std::max(in.height_m, 0.0f);
    const float p_eff = effective_precip(in.precip);

    out.tree_temp_c = t_air + kRadTreeC * (in.radiation - 1.0f)
                    - kWindTreeC * in.windward - kWetCoolC * (p_eff - 1.0f)
                    + kJitterC * in.jitter;

    // Suelo: la pedogénesis se frena con el frío (piso alpino de suelo fino).
    const float p0 =
        kSoilProdMax * (0.45f + 0.55f * smooth01(0.0f, kTreelineIsothermC, t_air));
    const float e = std::max(in.denudation, 0.0f)
                  * glm::clamp(1.0f + kCurvGain * in.convexity, 0.2f, 3.0f);
    float soil = e < p0 ? kSoilScaleM * std::log(p0 / std::max(e, 0.01f)) : 0.0f;
    const float retention = 1.0f - smooth01(0.75f * climate.rock_slope,
                                            1.05f * climate.rock_slope, in.slope);
    soil = (soil + std::max(in.alluvium_m, 0.0f)) * retention;
    out.soil_m = soil;

    // Agua disponible: lluvia + posición topográfica + freático de ribera.
    const float twi_n = smooth01(kTwiDry, kTwiWet, in.twi);
    const float moist = 0.5f * p_eff + 0.5f * twi_n + 0.35f * in.riparian;
    out.dryness =
        glm::clamp(1.0f - moist + 0.25f * (in.radiation - 1.0f), 0.0f, 1.0f);

    const float f_t = smooth01(kTreelineIsothermC - 0.3f,
                               kTreelineIsothermC + 1.2f, out.tree_temp_c);
    const float f_w = smooth01(0.35f, 0.75f, moist);
    const float f_s = smooth01(kMinSoilM, 0.45f, soil);
    out.forest = glm::clamp(f_t * f_w * f_s + kEdgeAmp * in.edge, 0.0f, 1.0f);

    // Nieve estacional: persiste donde la temperatura de la escena (con la
    // radiación que la funde) queda bajo la temperatura de equilibrio que
    // permite la acumulación local (Ohmura): más lluvia → más nieve.
    const float t_snow = t_air + kRadSnowC * (in.radiation - 1.0f)
                       + kJitterC * in.jitter;
    const float t_crit = ela_summer_temp_c(kMeanPrecipMm * p_eff);
    out.snow_margin_c = t_crit - (t_snow - climate.season_shift_c)
                      - kSnowSlopeC * in.slope;
    if (in.slope >= kMaxSnowSlope) {
        out.snow_margin_c = std::min(out.snow_margin_c, -0.01f);
    }
    return out;
}

std::vector<float> topographic_wetness(const Heightfield& hf,
                                       const std::vector<float>& flow) {
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    std::vector<float> twi(static_cast<std::size_t>(res) * static_cast<std::size_t>(res));
    const auto rows = iota_rows(res);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < res; ++x) {
            const std::size_t i =
                static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
                + static_cast<std::size_t>(x);
            const glm::vec3 n = hf.normal_at(x, z);
            const float tan_b =
                std::sqrt(std::max(0.0f, 1.0f - n.y * n.y)) / std::max(n.y, 1e-3f);
            // Área drenada específica: celdas aguas arriba × celda² / anchura
            // de contorno (una celda).
            const float a = std::max(flow.empty() ? 1.0f : flow[i], 1.0f) * cell;
            twi[i] = std::log(a / std::max(tan_b, 0.01f));
        }
    });
    box_blur(twi, res, 2, 2);
    return twi;
}

EcoProbe::EcoProbe(const TerrainData& data, std::vector<float> twi)
    : data_(&data), twi_(std::move(twi)) {
    const float az = kSunAzimuthDeg * kPi / 180.0f;
    const float alt = kSunAltitudeDeg * kPi / 180.0f;
    sun_ = {std::cos(az) * std::cos(alt), std::sin(alt), std::sin(az) * std::cos(alt)};
    sun_sin_alt_ = std::sin(alt);
    rain_ = data.lem_stats.rain;
    if (!rain_.empty() && data.litho_res >= 2) {
        // Deriva de hidrometeoros: la lluvia cae a sotavento de donde se
        // condensa (Smith & Barstad 2004). Suavizado ~120 m del macro.
        box_blur(rain_, data.litho_res, 6, 2);
    }
}

float EcoProbe::macro(const std::vector<float>& grid, int x, int z) const {
    const int gres = data_->litho_res;
    const int res = data_->height.resolution();
    if (grid.empty() || gres < 2 || res < 2) {
        return 0.0f;
    }
    const float k = static_cast<float>(gres - 1) / static_cast<float>(res - 1);
    return bilinear(grid, gres, static_cast<float>(x) * k, static_cast<float>(z) * k);
}

float EcoProbe::light(const std::vector<float>& grid, int x, int z) const {
    const int lres = data_->light_res;
    const int res = data_->height.resolution();
    if (grid.empty() || lres < 2 || res < 2) {
        return 1.0f;
    }
    const float k = static_cast<float>(lres - 1) / static_cast<float>(res - 1);
    return bilinear(grid, lres, static_cast<float>(x) * k, static_cast<float>(z) * k);
}

float EcoProbe::convexity_at(int x, int z) const {
    const Heightfield& hf = data_->height;
    const int res = hf.resolution();
    const int k = kCurvStep;
    const float h = hf.at(x, z);
    const float sum = hf.at(std::max(x - k, 0), z) + hf.at(std::min(x + k, res - 1), z)
                    + hf.at(x, std::max(z - k, 0)) + hf.at(x, std::min(z + k, res - 1));
    const float d = static_cast<float>(k) * hf.cell_size();
    const float lap = (sum - 4.0f * h) / (d * d);
    return glm::clamp(-lap * kCurvLengthM, -2.0f, 2.0f);
}

float EcoProbe::wind_facing(const glm::vec3& n) const {
    return n.x * data_->wind.x + n.z * data_->wind.y;
}

float EcoProbe::sun_facing(const glm::vec3& n) const {
    const float len = std::sqrt(sun_.x * sun_.x + sun_.z * sun_.z);
    return len > 0.0f ? (n.x * sun_.x + n.z * sun_.z) / len : 0.0f;
}

EcoInputs EcoProbe::inputs_at(int x, int z, const glm::vec3& n) const {
    const TerrainData& d = *data_;
    const Heightfield& hf = d.height;
    const std::size_t i = static_cast<std::size_t>(z)
                            * static_cast<std::size_t>(hf.resolution())
                        + static_cast<std::size_t>(x);
    EcoInputs in;
    in.height_m = hf.at(x, z) - d.config.sea_level;
    in.slope = std::sqrt(std::max(0.0f, 1.0f - n.y * n.y)) / std::max(n.y, 1e-3f);
    in.precip = rain_.empty() ? 1.0f : macro(rain_, x, z);
    in.twi = twi_.empty() ? 0.0f : twi_[i];
    const float direct =
        std::max(glm::dot(n, sun_), 0.0f) / std::max(sun_sin_alt_, 1e-3f);
    const float shadow = light(d.shadow, x, z);
    in.radiation = 0.65f * direct * (0.5f + 0.5f * shadow)
                 + 0.35f * light(d.sky_ao, x, z);
    in.windward = glm::clamp(-2.0f * wind_facing(n), 0.0f, 1.0f);
    in.convexity = convexity_at(x, z);
    in.denudation = macro(d.lem_stats.denudation, x, z);
    in.alluvium_m = macro(d.lem_stats.alluvium_m, x, z);
    return in;
}

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

    constexpr int kStride = 3;
    constexpr float kBandM = 25.0f;
    constexpr int kBands = 160;
    struct Acc {
        std::array<std::size_t, kBands> veg{};
        std::array<std::size_t, kBands> forest{};
        std::vector<float> forest_h;
        std::vector<float> snow_h;
        std::size_t cells = 0;
    };
    Acc acc[4];  // barlovento, sotavento, solana, umbría
    std::vector<float> f_forest, f_twi, f_precip, q_precip, q_twi, q_den;
    std::size_t rock_cvx = 0, n_cvx = 0, rock_ccv = 0, n_ccv = 0;

    for (int z = 1; z < res - 1; z += kStride) {
        for (int x = 1; x < res - 1; x += kStride) {
            const std::size_t i = static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
                                + static_cast<std::size_t>(x);
            if (hf.data()[i] <= sea) {
                continue;
            }
            const Material m = data.material[i];
            const bool veg_domain = m == Material::Grass || m == Material::Forest
                                 || m == Material::Rock || m == Material::Snow;
            if (!veg_domain) {
                continue;
            }
            const glm::vec3 n = hf.normal_at(x, z);
            const EcoInputs in = probe.inputs_at(x, z, n);
            q_precip.push_back(in.precip);
            q_twi.push_back(in.twi);
            q_den.push_back(in.denudation);
            if (m == Material::Grass || m == Material::Forest) {
                f_forest.push_back(data.forest_density[i]);
                f_twi.push_back(in.twi);
                f_precip.push_back(in.precip);
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
            const int band = std::clamp(static_cast<int>(in.height_m / kBandM), 0, kBands - 1);
            for (int g = 0; g < 4; ++g) {
                if (!member[g]) {
                    continue;
                }
                Acc& a = acc[g];
                ++a.cells;
                ++a.veg[static_cast<std::size_t>(band)];
                if (m == Material::Forest) {
                    ++a.forest[static_cast<std::size_t>(band)];
                    a.forest_h.push_back(in.height_m);
                } else if (m == Material::Snow) {
                    a.snow_h.push_back(in.height_m);
                }
            }
        }
    }

    rep.precip_q = quantiles(q_precip);
    rep.twi_q = quantiles(q_twi);
    rep.denudation_q = quantiles(q_den);
    rep.spearman_forest_twi = spearman(f_forest, f_twi);
    rep.spearman_forest_precip = spearman(f_forest, f_precip);
    rep.rock_convex_pct = n_cvx ? 100.0 * static_cast<double>(rock_cvx) / static_cast<double>(n_cvx) : 0.0;
    rep.rock_concave_pct = n_ccv ? 100.0 * static_cast<double>(rock_ccv) / static_cast<double>(n_ccv) : 0.0;

    EcologyReport::Group* groups[4] = {&rep.windward, &rep.leeward, &rep.sunny, &rep.shady};
    for (int g = 0; g < 4; ++g) {
        const Acc& a = acc[g];
        EcologyReport::Group& out = *groups[g];
        out.cells = a.cells;
        for (int b = kBands - 1; b >= 0; --b) {
            const auto bi = static_cast<std::size_t>(b);
            if (a.veg[bi] >= 50 && 4 * a.forest[bi] >= a.veg[bi]) {
                out.treeline_m = (static_cast<float>(b) + 0.5f) * kBandM;
                break;
            }
        }
        out.forest_p95_m = quantile(a.forest_h, 0.95f);
        out.snow_p10_m = quantile(a.snow_h, 0.10f);
    }
    return rep;
}

} // namespace terrain
