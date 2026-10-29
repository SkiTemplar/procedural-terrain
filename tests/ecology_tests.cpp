// Ecología física (ecology.h): cada rasgo responde al proceso que lo causa.
// Unitarios sobre evaluate_ecology con entradas controladas + la lluvia
// orográfica sobre una cresta sintética + correlaciones en un mundo real.

#include "terrain/ecology.h"
#include "terrain/generator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>

namespace {

using terrain::EcoCell;
using terrain::EcoClimate;
using terrain::EcoInputs;

constexpr float kTopM = 3000.0f;

// Ladera neutra, bien regada y con suelo de sobra: solo manda la temperatura.
EcoInputs neutral_slope() {
    EcoInputs in;
    in.slope = 0.25f;
    in.precip = 1.0f;
    in.twi = 6.0f;
    in.radiation = 1.0f;
    in.denudation = 0.2f;
    return in;
}

EcoClimate default_climate() {
    return terrain::make_eco_climate(terrain::TerrainConfig{});
}

// Cota más baja (paso de 5 m) a la que el predicado deja de cumplirse.
float crossing_height(EcoInputs in, const std::function<bool(const EcoCell&)>& holds) {
    const EcoClimate c = default_climate();
    for (float h = 0.0f; h <= kTopM; h += 5.0f) {
        in.height_m = h;
        if (!holds(terrain::evaluate_ecology(in, c))) {
            return h;
        }
    }
    return kTopM;
}

float treeline_of(const EcoInputs& in) {
    return crossing_height(in, [](const EcoCell& e) { return e.forest > 0.5f; });
}

float snowline_of(const EcoInputs& in) {
    // La nieve aparece al SUBIR: se busca dónde deja de no haberla.
    return crossing_height(in, [](const EcoCell& e) { return e.snow_margin_c <= 0.0f; });
}

} // namespace

TEST(Ecologia, OhmuraReproduceSuRelacion) {
    for (const float t : {0.0f, 3.0f, 6.0f}) {
        const float p = 645.0f + 296.0f * t + 9.0f * t * t;
        EXPECT_NEAR(terrain::ela_summer_temp_c(p), t, 1e-3f);
    }
}

TEST(Ecologia, LaTemperaturaBajaConElGradienteAmbiental) {
    const EcoClimate c = default_climate();
    EcoInputs in = neutral_slope();
    in.height_m = 0.0f;
    const float t0 = terrain::evaluate_ecology(in, c).tree_temp_c;
    in.height_m = 1000.0f;
    const float t1 = terrain::evaluate_ecology(in, c).tree_temp_c;
    EXPECT_NEAR(t0 - t1, 6.5f, 1e-3f);
}

TEST(Ecologia, ElLimiteDelBosqueSigueLaIsotermaDeDiezGrados) {
    const terrain::TerrainConfig cfg;
    const float tl = treeline_of(neutral_slope());
    printf("  limite del bosque neutro %.0f m (isoterma %.0f m)\n", tl, cfg.treeline_m);
    EXPECT_NEAR(tl, cfg.treeline_m, 150.0f);
}

TEST(Ecologia, LaSolanaSubeElBosqueYLaUmbriaLoBaja) {
    EcoInputs sunny = neutral_slope();
    sunny.radiation = 1.3f;
    EcoInputs shady = neutral_slope();
    shady.radiation = 0.4f;
    const float diff = treeline_of(sunny) - treeline_of(shady);
    printf("  solana - umbria: %.0f m\n", diff);
    EXPECT_GT(diff, 80.0f);
}

TEST(Ecologia, LaCaraDeBarloventoBajaElLimiteDelBosque) {
    EcoInputs lee = neutral_slope();
    EcoInputs windward = neutral_slope();
    windward.windward = 1.0f;
    windward.precip = 1.8f;
    EXPECT_GT(treeline_of(lee) - treeline_of(windward), 60.0f);
}

TEST(Ecologia, LaNieveBajaEnUmbriaYConMasLluvia) {
    EcoInputs base = neutral_slope();
    EcoInputs shady = base;
    shady.radiation = 0.4f;
    EcoInputs wet = base;
    wet.precip = 1.8f;
    const float s_base = snowline_of(base);
    const float s_shady = snowline_of(shady);
    const float s_wet = snowline_of(wet);
    printf("  nieve: neutra %.0f m, umbria %.0f m, lluviosa %.0f m\n",
           s_base, s_shady, s_wet);
    EXPECT_LT(s_shady, s_base - 50.0f);
    EXPECT_LT(s_wet, s_base - 50.0f);
}

TEST(Ecologia, SinSueloSobreLaPendienteCriticaNiConDenudacionAlta) {
    const EcoClimate c = default_climate();
    EcoInputs in = neutral_slope();
    in.height_m = 200.0f;
    EXPECT_GT(terrain::evaluate_ecology(in, c).soil_m, 0.3f);
    EcoInputs steep = in;
    steep.slope = 1.2f * c.rock_slope;
    EXPECT_LT(terrain::evaluate_ecology(steep, c).soil_m, terrain::kMinSoilM);
    EcoInputs eroding = in;
    eroding.denudation = 2.0f;
    EXPECT_LT(terrain::evaluate_ecology(eroding, c).soil_m, terrain::kMinSoilM);
    // A igual denudación, la cresta convexa pierde suelo y la vaguada lo gana.
    EcoInputs crest = in;
    crest.denudation = 0.6f;
    crest.convexity = 1.5f;
    EcoInputs hollow = crest;
    hollow.convexity = -1.0f;
    EXPECT_LT(terrain::evaluate_ecology(crest, c).soil_m,
              terrain::evaluate_ecology(hollow, c).soil_m);
}

TEST(Ecologia, MasAguaMasBosqueDondeElAguaLimita) {
    const EcoClimate c = default_climate();
    EcoInputs dry = neutral_slope();
    dry.height_m = 100.0f;
    dry.precip = 0.5f;
    dry.twi = 3.0f;
    EcoInputs rainy = dry;
    rainy.precip = 1.6f;
    EcoInputs hollow = dry;
    hollow.twi = 7.0f;
    const float f_dry = terrain::evaluate_ecology(dry, c).forest;
    EXPECT_GT(terrain::evaluate_ecology(rainy, c).forest, f_dry + 0.2f);
    EXPECT_GT(terrain::evaluate_ecology(hollow, c).forest, f_dry + 0.2f);
    EXPECT_GT(terrain::evaluate_ecology(dry, c).dryness,
              terrain::evaluate_ecology(rainy, c).dryness);
}

TEST(Ecologia, LaLluviaOrograficaCargaElBarlovento) {
    // Cresta N-S de 800 m en mitad de un mar de 6 km; viento hacia +x.
    constexpr int kRes = 256;
    terrain::Heightfield hf(kRes, 6000.0f);
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            const float wx = hf.world_x(x);
            const float land = std::abs(wx) < 2200.0f ? 1.0f : 0.0f;
            hf.at(x, z) = land * (20.0f + 800.0f * std::exp(-wx * wx / (900.0f * 900.0f)))
                        - (1.0f - land) * 30.0f;
        }
    }
    const auto p = terrain::orographic_precipitation(hf, 0.0f, {1.0f, 0.0f}, kRes);
    double west = 0.0, east = 0.0, land_sum = 0.0;
    int nw = 0, ne = 0, nl = 0;
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            const float wx = hf.world_x(x);
            const float v = p[static_cast<size_t>(z) * kRes + x];
            if (hf.at(x, z) > 0.0f) {
                land_sum += v;
                ++nl;
            }
            if (wx > -1500.0f && wx < -300.0f) {
                west += v;
                ++nw;
            } else if (wx > 300.0f && wx < 1500.0f) {
                east += v;
                ++ne;
            }
        }
    }
    const double w = west / nw;
    const double e = east / ne;
    printf("  lluvia barlovento %.2f sotavento %.2f\n", w, e);
    EXPECT_GT(w, 1.5 * e);
    EXPECT_NEAR(land_sum / nl, 1.0, 0.02);
}

TEST(Ecologia, ElBosqueSigueLaHumedadYLaExposicionEnLaIsla) {
    terrain::TerrainConfig cfg;
    cfg.seed = 20260709;
    cfg.resolution = 512;
    cfg.size_m = 10000.0f;
    cfg.erosion_droplets = 20000;
    const auto data = terrain::generate(cfg);
    const auto rep = terrain::ecology_report(data);
    printf("  spearman bosque~twi %.3f bosque~lluvia %.3f (piso bajo %.3f)\n",
           rep.spearman_forest_twi, rep.spearman_forest_precip,
           rep.spearman_forest_precip_low);
    // El bosque sigue la humedad topográfica (antes, con el fbm: 0.18). La
    // correlación con la lluvia solo se imprime: a esta resolución (macro
    // 128) las crestas de barlovento son a la vez lo más lluvioso, lo más
    // frío y lo más rocoso, y la cruda mezcla los tres efectos; el efecto
    // aislado del agua lo fija MasAguaMasBosqueDondeElAguaLimita.
    EXPECT_GT(rep.spearman_forest_twi, 0.3);
    // El límite del bosque responde a la exposición (no es una cota fija).
    EXPECT_GT(rep.sunny.forest_p95_m, rep.shady.forest_p95_m + 20.0f);
    EXPECT_GT(rep.leeward.forest_p95_m, rep.windward.forest_p95_m);
}
