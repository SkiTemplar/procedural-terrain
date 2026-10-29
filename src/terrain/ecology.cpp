#include "terrain/ecology.h"

#include "terrain/ecology_fields.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <execution>
#include <numeric>
#include <utility>

namespace terrain {

namespace eco_detail {

float smooth01(float e0, float e1, float x) noexcept {
    const float t = glm::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void box_blur(std::vector<float>& f, int res, int r, int passes) {
    if (r < 1 || res < 2) {
        return;
    }
    std::vector<float> tmp(f.size());
    std::vector<int> rows(static_cast<std::size_t>(res));
    std::iota(rows.begin(), rows.end(), 0);
    const auto at = [res](int x, int z) {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
             + static_cast<std::size_t>(x);
    };
    const float inv = 1.0f / static_cast<float>(2 * r + 1);
    for (int pass = 0; pass < passes; ++pass) {
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            for (int x = 0; x < res; ++x) {
                float acc = 0.0f;
                for (int d = -r; d <= r; ++d) {
                    acc += f[at(std::clamp(x + d, 0, res - 1), z)];
                }
                tmp[at(x, z)] = acc * inv;
            }
        });
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            for (int x = 0; x < res; ++x) {
                float acc = 0.0f;
                for (int d = -r; d <= r; ++d) {
                    acc += tmp[at(x, std::clamp(z + d, 0, res - 1))];
                }
                f[at(x, z)] = acc * inv;
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

} // namespace eco_detail

namespace {

using eco_detail::bilinear;
using eco_detail::box_blur;
using eco_detail::smooth01;

constexpr float kPi = 3.14159265f;

// Microclima del árbol (°C equivalentes sobre la isoterma): la solana gana y
// la umbría pierde (Körner: ±100-200 m de límite entre caras); el viento y el
// clima nuboso de la fachada húmeda lo bajan.
constexpr float kRadTreeC = 1.2f;   // por unidad de radiación relativa
constexpr float kWindTreeC = 1.2f;  // sitio plenamente expuesto al viento
constexpr float kWetCoolC = 0.5f;   // por unidad de lluvia efectiva sobre 1
constexpr float kJitterC = 0.3f;    // perturbación de microclima (±45 m)
// Nieve estacional: la fusión depende sobre todo de la radiación directa.
constexpr float kRadSnowC = 1.2f;
constexpr float kSnowSlopeC = 0.6f;  // retención de nieve que cae con tanβ
constexpr float kMaxSnowSlope = 1.15f;  // ~49°: la nieve no agarra
// Suelo (Heimsath et al. 1997): producción P0·exp(−H/H0) frente a la
// denudación local; en equilibrio H = H0·ln(P0/E) y roca desnuda si E ≥ P0
// (speed limit de Dixon & von Blanckenburg 2012). Unidades de E: denudación
// del LEM relativa al uplift máximo por paso; con el LEM convergente la
// mediana en tierra es ~0.4-0.55 y el p90 ~0.75-1.0.
constexpr float kSoilProdMax = 2.0f;
constexpr float kSoilScaleM = 0.5f;  // H0
// En laderas con suelo E ∝ −∇²z (Roering 1999): la convexidad de ladera
// (~50 m, no la de surco) acelera la denudación de crestas y espolones; las
// vaguadas acumulan coluvión.
constexpr float kCurvGain = 0.5f;
// Afloramiento: fracción de roca continua alrededor de kMinSoilM (mitad de
// la superficie) entre un suelo esquelético y uno que ya cubre la roca.
constexpr float kRockSoilLoM = 0.04f;
constexpr float kRockSoilHiM = 0.20f;
// Balance hídrico. La radiación sube la PET (Hargreaves: ∝ radiación
// extraterrestre). El excedente P − ET drena ladera abajo; como en TOPMODEL
// (Beven & Kirkby 1979), la recarga lateral escala con a/tanβ = exp(TWI)
// relativa a la mediana: una cresta cede parte de su excedente, una vaguada
// recibe el de su cuenca.
constexpr float kRadPet = 0.5f;
constexpr float kTwiMid = 3.6f;        // ~mediana del TWI en tierra
constexpr float kLateralShare = 0.5f;  // excedente que redistribuye la ladera
constexpr float kLateralMaxGain = 5.0f;
constexpr float kRiparianShare = 1.0f;
// El bosque sufre por agua solo con déficit relativo alto (sotavento seco,
// solana de suelo fino): 1 − ET/PET de 0.35 (subhúmedo) a 0.65 (semiárido).
constexpr float kDryForestLo = 0.35f;
constexpr float kDryForestHi = 0.65f;
// Encharcamiento: turbera de cobertor donde el agua disponible dobla la PET
// y el relieve converge (TWI alto); el bosque no prospera en ella.
constexpr float kBogAridityLo = 2.0f;
constexpr float kBogAridityHi = 3.5f;
constexpr float kBogTwiLo = 5.0f;
constexpr float kBogTwiHi = 7.0f;
constexpr float kBogForest = 0.7f;
// Viento: en las islas hiperoceánicas el árbol se refugia de la exposición
// (Feroe, Shetland: bosque solo en barrancos abrigados).
constexpr float kExposureForest = 0.7f;
constexpr float kExposureLo = 0.5f;
constexpr float kExposureHi = 0.95f;
constexpr float kEdgeAmp = 0.08f;  // perturbación del borde de bosque (≤ 10 %)
// Thornthwaite: corrección de duración del día (~58° N: los meses cálidos
// tienen ~1.4× la insolación media) y forma de Fu de la curva de Budyko.
constexpr float kDayLengthAmp = 0.4f;
constexpr float kFuOmega = 2.6f;

// Escalas de la sonda (m): relieve suavizado, pendiente y curvatura.
constexpr float kSmoothRadiusM = 8.0f;
constexpr float kSlopeHalfM = 12.0f;
constexpr float kCurvHalfM = 50.0f;
constexpr float kCurvLengthM = 100.0f;  // normalización de ∇²z
// TOPEX (Ruel et al. 2002): ángulo del horizonte hasta ~1 km, a barlovento y
// medio en 8 direcciones. En la isla el índice va de −2° (p10) a +19° (p90),
// mediana +6.6°: plenamente expuesto ≤ −3°, abrigado ≥ +12°.
constexpr int kExposureRes = 512;
constexpr float kTopexRadiusM = 1000.0f;
constexpr float kTopexUpShare = 0.6f;
constexpr float kExposedDeg = -3.0f;
constexpr float kShelterDeg = 12.0f;

// Lluvia efectiva para la nubosidad y la nieve: la raíz comprime el
// contraste del campo relativo (0.5 → 0.71).
float effective_precip(float precip) noexcept {
    return std::sqrt(glm::clamp(precip, 0.3f, 3.0f));
}

std::vector<int> iota_rows(int res) {
    std::vector<int> rows(static_cast<std::size_t>(res));
    std::iota(rows.begin(), rows.end(), 0);
    return rows;
}

std::array<float, 12> month_cosines() {
    std::array<float, 12> c{};
    for (std::size_t m = 0; m < c.size(); ++m) {
        c[m] = std::cos(2.0f * kPi * static_cast<float>(m) / 12.0f);
    }
    return c;
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

float thornthwaite_pet_mm(float warm_month_c) noexcept {
    static const std::array<float, 12> kCos = month_cosines();
    const float mean = warm_month_c - 0.5f * kAnnualRangeC;
    std::array<float, 12> t{};
    float heat = 0.0f;
    for (std::size_t m = 0; m < t.size(); ++m) {
        t[m] = std::min(mean + 0.5f * kAnnualRangeC * kCos[m], 26.5f);
        if (t[m] > 0.0f) {
            heat += std::pow(t[m] / 5.0f, 1.514f);
        }
    }
    if (heat <= 0.0f) {
        return 0.0f;
    }
    const float a = 6.75e-7f * heat * heat * heat - 7.71e-5f * heat * heat
                  + 1.792e-2f * heat + 0.49239f;
    float pet = 0.0f;
    for (std::size_t m = 0; m < t.size(); ++m) {
        if (t[m] > 0.0f) {
            pet += 16.0f * (1.0f + kDayLengthAmp * kCos[m])
                 * std::pow(10.0f * t[m] / heat, a);
        }
    }
    return pet;
}

float budyko_et_mm(float water_mm, float pet_mm) noexcept {
    if (water_mm <= 0.0f || pet_mm <= 0.0f) {
        return 0.0f;
    }
    const float r = pet_mm / water_mm;
    const float ratio =
        1.0f + r - std::pow(1.0f + std::pow(r, kFuOmega), 1.0f / kFuOmega);
    return water_mm * glm::clamp(ratio, 0.0f, 1.0f);
}

namespace {

// Espesor de suelo en equilibrio producción-denudación, retenido por la
// pendiente, más el aluvión.
float soil_thickness(const EcoInputs& in, const EcoClimate& climate,
                     float t_air) noexcept {
    // La pedogénesis se frena con el frío (piso alpino de suelo fino).
    const float p0 =
        kSoilProdMax * (0.45f + 0.55f * smooth01(0.0f, kTreelineIsothermC, t_air));
    const float e = std::max(in.denudation, 0.0f)
                  * glm::clamp(1.0f + kCurvGain * in.convexity, 0.2f, 3.0f);
    const float regolith =
        e < p0 ? kSoilScaleM * std::log(p0 / std::max(e, 0.01f)) : 0.0f;
    const float retention = 1.0f - smooth01(0.75f * climate.rock_slope,
                                            1.05f * climate.rock_slope, in.slope);
    return (regolith + std::max(in.alluvium_m, 0.0f)) * retention;
}

// Balance hídrico anual: lluvia frente a PET (Thornthwaite, modulada por la
// radiación); el excedente P − ET drena ladera abajo y el TWI lo reparte
// (crestas lo ceden, vaguadas lo reciben); la ribera tiene freático.
void water_balance(const EcoInputs& in, float t_air, EcoCell& out) noexcept {
    const float p_mm = kMeanPrecipMm * glm::clamp(in.precip, 0.2f, 4.0f);
    const float pet = thornthwaite_pet_mm(t_air)
                    * glm::clamp(1.0f + kRadPet * (in.radiation - 1.0f), 0.5f, 1.5f);
    const float surplus = std::max(p_mm - budyko_et_mm(p_mm, pet), 0.0f);
    const float lateral =
        std::min(std::exp(in.twi - kTwiMid), kLateralMaxGain) - 1.0f;
    const float water = std::max(
        p_mm + surplus * (kLateralShare * lateral + kRiparianShare * in.riparian),
        0.0f);
    out.pet_mm = pet;
    out.water_mm = water;
    out.dryness = pet > 0.0f
                    ? glm::clamp(1.0f - budyko_et_mm(water, pet) / pet, 0.0f, 1.0f)
                    : 0.0f;
    const float aridity = pet > 0.0f ? water / pet : 10.0f;
    out.waterlog = smooth01(kBogAridityLo, kBogAridityHi, aridity)
                 * smooth01(kBogTwiLo, kBogTwiHi, in.twi)
                 * (1.0f - glm::clamp(in.riparian, 0.0f, 1.0f));
}

} // namespace

EcoCell evaluate_ecology(const EcoInputs& in, const EcoClimate& climate) noexcept {
    EcoCell out;
    const float t_air =
        climate.sea_temp_c - kLapseRateCPerM * std::max(in.height_m, 0.0f);
    const float p_eff = effective_precip(in.precip);

    out.tree_temp_c = t_air + kRadTreeC * (in.radiation - 1.0f)
                    - kWindTreeC * in.wind_exposure - kWetCoolC * (p_eff - 1.0f)
                    + kJitterC * in.jitter;

    out.soil_m = soil_thickness(in, climate, t_air);
    out.rock_frac = 1.0f - smooth01(kRockSoilLoM, kRockSoilHiM, out.soil_m);
    water_balance(in, t_air, out);

    const float f_t = smooth01(kTreelineIsothermC - 0.3f,
                               kTreelineIsothermC + 1.2f, out.tree_temp_c);
    const float f_w = 1.0f - smooth01(kDryForestLo, kDryForestHi, out.dryness);
    const float f_s = smooth01(kMinSoilM, 0.45f, out.soil_m);
    const float f_x =
        1.0f - kExposureForest * smooth01(kExposureLo, kExposureHi, in.wind_exposure);
    const float f_bog = 1.0f - kBogForest * out.waterlog;
    out.forest =
        glm::clamp(f_t * f_w * f_s * f_x * f_bog + kEdgeAmp * in.edge, 0.0f, 1.0f);

    // Nieve estacional: persiste donde la temperatura de la escena (con la
    // radiación que la funde) queda bajo la temperatura de equilibrio que
    // permite la acumulación local (Ohmura): más lluvia → más nieve.
    const float t_snow = t_air + kRadSnowC * (in.radiation - 1.0f)
                       + kJitterC * in.jitter;
    // Acotada: Ohmura es para el balance anual de un glaciar; para la nieve
    // estacional de una isla de 10 km el contraste de lluvia se satura.
    const float t_crit =
        ela_summer_temp_c(kMeanPrecipMm * glm::clamp(p_eff, 0.75f, 1.35f));
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

std::vector<float> orographic_precipitation(const Heightfield& hf, float sea_level,
                                            glm::vec2 wind, int gres) {
    const std::size_t n = static_cast<std::size_t>(gres) * static_cast<std::size_t>(gres);
    std::vector<float> precip(n, 1.0f);
    if (gres < 2 || glm::length(wind) < 0.5f) {
        return precip;
    }
    const float cell = hf.size() / static_cast<float>(gres - 1);
    const float half = hf.size() * 0.5f;
    std::vector<float> h(n);
    const auto rows = iota_rows(gres);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < gres; ++x) {
            h[static_cast<std::size_t>(z) * static_cast<std::size_t>(gres)
              + static_cast<std::size_t>(x)] =
                hf.sample(-half + static_cast<float>(x) * cell,
                          -half + static_cast<float>(z) * cell);
        }
    });

    // Barrido en orden de proyección sobre el viento (8 direcciones, como el
    // LEM): la celda de barlovento ya está resuelta al llegar a cada celda.
    const float a_snap =
        std::round(std::atan2(wind.y, wind.x) / (kPi * 0.25f)) * (kPi * 0.25f);
    const int sdx = static_cast<int>(std::lround(std::cos(a_snap)));
    const int sdz = static_cast<int>(std::lround(std::sin(a_snap)));
    const float step = cell * ((sdx != 0 && sdz != 0) ? 1.4142f : 1.0f);
    std::vector<std::uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        const int ka = static_cast<int>(a) % gres * sdx + static_cast<int>(a) / gres * sdz;
        const int kb = static_cast<int>(b) % gres * sdx + static_cast<int>(b) / gres * sdz;
        return ka < kb;
    });

    constexpr float kRainoutM = 25000.0f;    // lavado de fondo en llano
    constexpr float kLiftScaleM = 1500.0f;   // ascenso que condensa el 63 %
    constexpr float kRechargeM = 3000.0f;    // recarga sobre el mar
    constexpr float kFrontalShare = 0.35f;   // lluvia no orográfica
    constexpr float kDriftM = 250.0f;        // deriva de hidrometeoros

    std::vector<float> moisture(n, 1.0f);
    std::vector<float> rate(n, 0.0f);
    double rate_sum = 0.0;
    std::size_t land = 0;
    for (const std::uint32_t i : order) {
        const int x = static_cast<int>(i) % gres;
        const int z = static_cast<int>(i) / gres;
        const int ux = x - sdx;
        const int uz = z - sdz;
        const bool edge = ux < 0 || uz < 0 || ux >= gres || uz >= gres;
        const std::size_t ui = edge ? i
                                    : static_cast<std::size_t>(uz) * static_cast<std::size_t>(gres)
                                          + static_cast<std::size_t>(ux);
        const float m_up = edge ? 1.0f : moisture[ui];
        if (h[i] <= sea_level) {
            moisture[i] = m_up + (1.0f - m_up) * (1.0f - std::exp(-step / kRechargeM));
            continue;
        }
        const float lift = edge ? 0.0f : std::max(0.0f, h[i] - std::max(h[ui], sea_level));
        const float frac = 1.0f - std::exp(-(step / kRainoutM + lift / kLiftScaleM));
        const float r = m_up * frac;
        moisture[i] = m_up - r;
        rate[i] = r / step;  // descarga por metro de recorrido
        rate_sum += static_cast<double>(rate[i]);
        ++land;
    }
    if (land == 0 || rate_sum <= 0.0) {
        return precip;
    }
    const float inv_mean = static_cast<float>(static_cast<double>(land) / rate_sum);
    for (std::size_t i = 0; i < n; ++i) {
        precip[i] = kFrontalShare + (1.0f - kFrontalShare) * rate[i] * inv_mean;
    }
    const int drift = std::max(1, static_cast<int>(std::lround(kDriftM / cell)));
    box_blur(precip, gres, drift, 2);
    // Renormaliza a media 1 sobre tierra tras la deriva (el mar se queda con
    // lo que el suavizado le reparte: no cuenta para la media).
    double land_sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        land_sum += h[i] > sea_level ? static_cast<double>(precip[i]) : 0.0;
    }
    const float k = static_cast<float>(static_cast<double>(land) / std::max(land_sum, 1e-9));
    for (float& p : precip) {
        p *= k;
    }
    return precip;
}

EcoProbe::EcoProbe(const TerrainData& data, std::vector<float> twi)
    : data_(&data), twi_(std::move(twi)) {
    const float az = kSunAzimuthDeg * kPi / 180.0f;
    const float alt = kSunAltitudeDeg * kPi / 180.0f;
    sun_ = {std::cos(az) * std::cos(alt), std::sin(alt), std::sin(az) * std::cos(alt)};
    sun_sin_alt_ = std::sin(alt);
    denudation_ = data.lem_stats.denudation;
    if (!denudation_.empty() && data.litho_res >= 2) {
        // Media de ~2 celdas macro: el suelo integra la denudación de su
        // entorno, no la de una celda de 10 m.
        box_blur(denudation_, data.litho_res, 2, 1);
    }

    const Heightfield& hf = data.height;
    const float cell = hf.cell_size();
    smooth_h_ = hf.data();
    box_blur(smooth_h_, hf.resolution(),
             static_cast<int>(std::lround(kSmoothRadiusM / cell)), 2);
    slope_step_ = std::max(1, static_cast<int>(std::lround(kSlopeHalfM / cell)));
    curv_step_ = std::max(1, static_cast<int>(std::lround(kCurvHalfM / cell)));
    compute_wind_exposure();
}

float EcoProbe::grid_at(const std::vector<float>& grid, int gres, int x, int z,
                        float fallback) const {
    const int res = data_->height.resolution();
    if (grid.empty() || gres < 2 || res < 2) {
        return fallback;
    }
    const float k = static_cast<float>(gres - 1) / static_cast<float>(res - 1);
    return bilinear(grid, gres, static_cast<float>(x) * k, static_cast<float>(z) * k);
}

float EcoProbe::smooth_at(int x, int z) const {
    const int res = data_->height.resolution();
    x = std::clamp(x, 0, res - 1);
    z = std::clamp(z, 0, res - 1);
    return smooth_h_[static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
                     + static_cast<std::size_t>(x)];
}

float EcoProbe::convexity_at(int x, int z) const {
    const int k = curv_step_;
    const float sum = smooth_at(x - k, z) + smooth_at(x + k, z) + smooth_at(x, z - k)
                    + smooth_at(x, z + k);
    const float d = static_cast<float>(k) * data_->height.cell_size();
    const float lap = (sum - 4.0f * smooth_at(x, z)) / (d * d);
    return glm::clamp(-lap * kCurvLengthM, -2.0f, 2.0f);
}

// Normal a escala de ladera: retención de suelo y de nieve, radiación y
// exposición las decide la ladera, no un surco de las gotas de erosión (que
// dejaba peines de roca/hierba/nieve de 1-3 celdas).
glm::vec3 EcoProbe::hillslope_normal_at(int x, int z) const {
    const int res = data_->height.resolution();
    const int k = slope_step_;
    const int x0 = std::max(x - k, 0);
    const int x1 = std::min(x + k, res - 1);
    const int z0 = std::max(z - k, 0);
    const int z1 = std::min(z + k, res - 1);
    const float cell = data_->height.cell_size();
    const float gx = (smooth_at(x1, z) - smooth_at(x0, z))
                   / (static_cast<float>(std::max(x1 - x0, 1)) * cell);
    const float gz = (smooth_at(x, z1) - smooth_at(x, z0))
                   / (static_cast<float>(std::max(z1 - z0, 1)) * cell);
    return glm::normalize(glm::vec3{-gx, 1.0f, -gz});
}

// Exposición al viento por TOPEX: ángulo máximo del horizonte hasta ~1 km en
// 8 direcciones (el mar cuenta a su nivel). La dirección de barlovento pesa
// más: una ladera de sotavento tiene la cresta encima, una de barlovento
// mira al vacío. Grid propio de ≤ 512² sobre el relieve suavizado.
void EcoProbe::compute_wind_exposure() {
    const Heightfield& hf = data_->height;
    const int res = hf.resolution();
    const int g = std::min(res, kExposureRes);
    exposure_res_ = g;
    if (g < 2) {
        return;
    }
    const float sea = data_->config.sea_level;
    const float cell = hf.size() / static_cast<float>(g - 1);
    const float to_fine = static_cast<float>(res - 1) / static_cast<float>(g - 1);
    const std::size_t n = static_cast<std::size_t>(g) * static_cast<std::size_t>(g);
    std::vector<float> h(n);
    const auto rows = iota_rows(g);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < g; ++x) {
            h[static_cast<std::size_t>(z) * static_cast<std::size_t>(g)
              + static_cast<std::size_t>(x)] =
                std::max(bilinear(smooth_h_, res, static_cast<float>(x) * to_fine,
                                  static_cast<float>(z) * to_fine),
                         sea);
        }
    });

    constexpr int kDirs = 8;
    constexpr int kDx[kDirs] = {1, 1, 0, -1, -1, -1, 0, 1};
    constexpr int kDz[kDirs] = {0, 1, 1, 1, 0, -1, -1, -1};
    const glm::vec2 wind = data_->wind;
    const bool has_wind = glm::length(wind) > 0.5f;
    std::array<float, kDirs> up_w{};
    if (has_wind) {
        const float up = std::atan2(-wind.y, -wind.x);
        float sum = 0.0f;
        for (int d = 0; d < kDirs; ++d) {
            const float a = static_cast<float>(d) * kPi * 0.25f;
            const float diff = std::abs(std::remainder(a - up, 2.0f * kPi));
            up_w[static_cast<std::size_t>(d)] = std::max(0.0f, 1.0f - diff / (kPi * 0.25f));
            sum += up_w[static_cast<std::size_t>(d)];
        }
        for (float& w : up_w) {
            w /= std::max(sum, 1e-6f);
        }
    }
    const int steps = std::max(1, static_cast<int>(std::lround(kTopexRadiusM / cell)));

    exposure_.assign(n, 0.5f);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < g; ++x) {
            const std::size_t i = static_cast<std::size_t>(z) * static_cast<std::size_t>(g)
                                + static_cast<std::size_t>(x);
            float mean_deg = 0.0f;
            float up_deg = 0.0f;
            for (int d = 0; d < kDirs; ++d) {
                const float step_m = cell * ((kDx[d] != 0 && kDz[d] != 0) ? 1.4142f : 1.0f);
                float best = -1e9f;
                for (int s = 1; s <= steps; ++s) {
                    const int xx = x + kDx[d] * s;
                    const int zz = z + kDz[d] * s;
                    if (xx < 0 || zz < 0 || xx >= g || zz >= g) {
                        break;
                    }
                    const float dh = h[static_cast<std::size_t>(zz) * static_cast<std::size_t>(g)
                                       + static_cast<std::size_t>(xx)]
                                   - h[i];
                    best = std::max(best, dh / (static_cast<float>(s) * step_m));
                }
                const float deg = best > -1e8f ? std::atan(best) * 180.0f / kPi : 0.0f;
                mean_deg += deg / static_cast<float>(kDirs);
                up_deg += up_w[static_cast<std::size_t>(d)] * deg;
            }
            const float index = has_wind ? kTopexUpShare * up_deg
                                               + (1.0f - kTopexUpShare) * mean_deg
                                         : mean_deg;
            exposure_[i] = 1.0f - smooth01(kExposedDeg, kShelterDeg, index);
        }
    });
}

float EcoProbe::wind_exposure_at(int x, int z) const {
    return grid_at(exposure_, exposure_res_, x, z, 0.5f);
}

float EcoProbe::wind_facing(const glm::vec3& n) const {
    return n.x * data_->wind.x + n.z * data_->wind.y;
}

float EcoProbe::sun_facing(const glm::vec3& n) const {
    const float len = std::sqrt(sun_.x * sun_.x + sun_.z * sun_.z);
    return len > 0.0f ? (n.x * sun_.x + n.z * sun_.z) / len : 0.0f;
}

EcoInputs EcoProbe::inputs_at(int x, int z) const {
    const TerrainData& d = *data_;
    const Heightfield& hf = d.height;
    const std::size_t i = static_cast<std::size_t>(z)
                            * static_cast<std::size_t>(hf.resolution())
                        + static_cast<std::size_t>(x);
    const glm::vec3 n = hillslope_normal_at(x, z);
    EcoInputs in;
    in.height_m = hf.at(x, z) - d.config.sea_level;
    in.slope = std::sqrt(std::max(0.0f, 1.0f - n.y * n.y)) / std::max(n.y, 1e-3f);
    in.precip = grid_at(d.precipitation, d.precip_res, x, z, 1.0f);
    in.twi = twi_.empty() ? 0.0f : twi_[i];
    const float direct =
        std::max(glm::dot(n, sun_), 0.0f) / std::max(sun_sin_alt_, 1e-3f);
    const float shadow = grid_at(d.shadow, d.light_res, x, z, 1.0f);
    in.radiation = 0.65f * direct * (0.5f + 0.5f * shadow)
                 + 0.35f * grid_at(d.sky_ao, d.light_res, x, z, 1.0f);
    in.wind_exposure = wind_exposure_at(x, z);
    in.convexity = convexity_at(x, z);
    in.denudation = grid_at(denudation_, d.litho_res, x, z, 0.0f);
    in.alluvium_m = grid_at(d.lem_stats.alluvium_m, d.litho_res, x, z, 0.0f);
    return in;
}

} // namespace terrain
