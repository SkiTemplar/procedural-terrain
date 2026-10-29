// Ecología física (ecology.h): cada rasgo responde al proceso que lo causa.
// Unitarios sobre evaluate_ecology con entradas controladas + la lluvia
// orográfica sobre una cresta sintética + correlaciones en un mundo real.

#include "terrain/ecology.h"
#include "terrain/generator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <vector>

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
    in.twi = 3.6f;  // mediana en tierra: ni cresta ni vaguada
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
    windward.wind_exposure = 1.0f;
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

TEST(Ecologia, ThornthwaiteYBudykoDanElClimaHiperoceanico) {
    // Mes cálido de 13.25 °C (isoterma de 10 °C a 500 m): PET de una isla
    // atlántica fresca, ~500-700 mm/año (Feroe, Hébridas).
    const float pet = terrain::thornthwaite_pet_mm(13.25f);
    printf("  PET a nivel del mar %.0f mm\n", pet);
    EXPECT_GT(pet, 450.0f);
    EXPECT_LT(pet, 750.0f);
    // Más frío, menos PET (Thornthwaite responde poco: el índice de calor
    // también baja).
    EXPECT_LT(terrain::thornthwaite_pet_mm(6.0f), pet - 100.0f);
    EXPECT_FLOAT_EQ(terrain::thornthwaite_pet_mm(-5.0f), 0.0f);
    // Budyko: límite de agua (ET → P) y de energía (ET → PET).
    EXPECT_NEAR(terrain::budyko_et_mm(100.0f, 2000.0f), 100.0f, 5.0f);
    EXPECT_NEAR(terrain::budyko_et_mm(20000.0f, 600.0f), 600.0f, 5.0f);
    EXPECT_LT(terrain::budyko_et_mm(2000.0f, 600.0f), 600.0f);
}

TEST(Ecologia, ConLaLluviaMediaElAguaNoLimitaElBosque) {
    // Revisión de la rama: con 2000 mm y una PET de ~600 mm la celda mediana
    // (TWI p10-p50, radiación neutra) no puede estar limitada por agua.
    const EcoClimate c = default_climate();
    for (const float twi : {2.4f, 3.4f}) {
        EcoInputs in = neutral_slope();
        in.height_m = 100.0f;
        in.twi = twi;
        const EcoCell e = terrain::evaluate_ecology(in, c);
        printf("  twi %.1f: agua %.0f mm PET %.0f mm deficit %.2f bosque %.2f\n", twi,
               e.water_mm, e.pet_mm, e.dryness, e.forest);
        EXPECT_GT(e.water_mm / e.pet_mm, 2.0f);
        EXPECT_GT(e.forest, 0.8f);
    }
}

TEST(Ecologia, MasAguaMasBosqueDondeElAguaLimita) {
    // Sotavento muy seco en solana: aquí sí limita el agua, y la lluvia o la
    // recarga lateral de una vaguada la levantan.
    const EcoClimate c = default_climate();
    EcoInputs dry = neutral_slope();
    dry.height_m = 100.0f;
    dry.precip = 0.2f;
    dry.radiation = 1.3f;
    dry.twi = 3.0f;
    EcoInputs rainy = dry;
    rainy.precip = 0.8f;
    EcoInputs hollow = dry;
    hollow.twi = 7.0f;
    const float f_dry = terrain::evaluate_ecology(dry, c).forest;
    printf("  bosque seco %.2f lluvioso %.2f vaguada %.2f\n", f_dry,
           terrain::evaluate_ecology(rainy, c).forest,
           terrain::evaluate_ecology(hollow, c).forest);
    EXPECT_GT(terrain::evaluate_ecology(rainy, c).forest, f_dry + 0.2f);
    EXPECT_GT(terrain::evaluate_ecology(hollow, c).forest, f_dry + 0.2f);
    EXPECT_GT(terrain::evaluate_ecology(dry, c).dryness,
              terrain::evaluate_ecology(rainy, c).dryness);
}

TEST(Ecologia, ElVientoExpuestoQuitaElBosqueYLoDejaEnElAbrigo) {
    const EcoClimate c = default_climate();
    EcoInputs sheltered = neutral_slope();
    sheltered.height_m = 150.0f;
    sheltered.wind_exposure = 0.1f;
    EcoInputs exposed = sheltered;
    exposed.wind_exposure = 1.0f;
    EXPECT_GT(terrain::evaluate_ecology(sheltered, c).forest,
              terrain::evaluate_ecology(exposed, c).forest + 0.4f);
}

TEST(Ecologia, LaTurberaAparecePorEncharcamientoNoPorRibera) {
    const EcoClimate c = default_climate();
    EcoInputs flat_wet = neutral_slope();
    flat_wet.height_m = 100.0f;
    flat_wet.precip = 1.6f;
    flat_wet.twi = 8.0f;
    const EcoCell bog = terrain::evaluate_ecology(flat_wet, c);
    EXPECT_GT(bog.waterlog, 0.8f);
    EcoInputs bank = flat_wet;
    bank.riparian = 1.0f;
    EXPECT_FLOAT_EQ(terrain::evaluate_ecology(bank, c).waterlog, 0.0f);
    EXPECT_GT(terrain::evaluate_ecology(bank, c).forest, bog.forest + 0.3f);
}

TEST(Ecologia, LaRocaEsUnaFraccionContinuaDelSuelo) {
    const EcoClimate c = default_climate();
    EcoInputs in = neutral_slope();
    in.height_m = 200.0f;
    float prev = -1.0f;
    // Al subir la denudación el suelo adelgaza y la fracción de roca crece
    // sin saltos (antes, un corte binario soil < 0.12 m).
    int intermediate = 0;
    for (float e = 0.2f; e <= 3.0f; e += 0.05f) {
        in.denudation = e;
        const float r = terrain::evaluate_ecology(in, c).rock_frac;
        EXPECT_GE(r, prev - 1e-6f);
        intermediate += (r > 0.1f && r < 0.9f) ? 1 : 0;
        prev = r;
    }
    EXPECT_GE(intermediate, 2);
    EXPECT_NEAR(prev, 1.0f, 1e-6f);
}

TEST(Ecologia, LaLogisticaRecuperaElLimiteDeUnaPresenciaSintetica) {
    // Presencia 0.6 abajo que cae a la mitad (0.3) a 420 m con escala 30 m.
    std::vector<std::size_t> present(150), total(150);
    for (std::size_t b = 0; b < present.size(); ++b) {
        const double h = (static_cast<double>(b) + 0.5) * 10.0;
        total[b] = 1000;
        present[b] = static_cast<std::size_t>(
            1000.0 * 0.6 / (1.0 + std::exp((h - 420.0) / 30.0)));
    }
    const auto fit = terrain::fit_presence_logistic(present, total, 10.0f);
    EXPECT_NEAR(fit.h50_m, 420.0f, 10.0f);
    EXPECT_NEAR(fit.asymptote, 0.6f, 0.05f);
    EXPECT_NEAR(fit.width_m, 30.0f, 8.0f);
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

TEST(Ecologia, ElBosqueSigueElAbrigoYLaExposicionEnLaIsla) {
    terrain::TerrainConfig cfg;
    cfg.seed = 20260709;
    cfg.resolution = 512;
    cfg.size_m = 10000.0f;
    cfg.erosion_droplets = 20000;
    const auto data = terrain::generate(cfg);
    const auto rep = terrain::ecology_report(data);
    printf("  spearman bosque~twi %.3f bosque~lluvia %.3f bosque~exposicion %.3f\n",
           rep.spearman_forest_twi, rep.spearman_forest_precip,
           rep.spearman_forest_exposure);
    printf("  p95 bosque: solana %.0f umbria %.0f barlovento %.0f sotavento %.0f m\n",
           rep.sunny.forest_p95_m, rep.shady.forest_p95_m, rep.windward.forest_p95_m,
           rep.leeward.forest_p95_m);
    printf("  roca aislada %.2f %% tiras de hierba %.2f por 100 de roca\n",
           rep.rock_isolated_pct, rep.veg_strips_per_100_rock);
    // En una isla hiperoceánica el bosque busca el abrigo del viento (antes
    // lo buscaba por una sequía que el clima declarado no tiene).
    EXPECT_LT(rep.spearman_forest_exposure, -0.2);
    // El límite del bosque responde a la exposición (no es una cota fija).
    EXPECT_GT(rep.sunny.forest_p95_m, rep.shady.forest_p95_m + 20.0f);
    EXPECT_GT(rep.leeward.forest_p95_m, rep.windward.forest_p95_m);
}

TEST(Ecologia, SinPeinesDeRocaNiDedosDeHierbaEnLaIsla) {
    // Auditoría de julio #18 y revisión de la rama: la roca no puede salir en
    // rayas de 1-3 celdas que siguen los surcos de las gotas de erosión. A
    // 2048² (celda ~4.9 m) los surcos ya se resuelven: con la pendiente de
    // celdas sueltas y la roca binaria daba 3.6 % de roca aislada, 16 tiras
    // por 100 celdas de roca y 0.25 % de nieve aislada.
    terrain::TerrainConfig cfg;
    cfg.seed = 20260709;
    cfg.resolution = 2048;
    cfg.size_m = 10000.0f;
    cfg.erosion_droplets = 150000;
    const auto data = terrain::generate(cfg);
    const auto rep = terrain::ecology_report(data);
    printf("  roca %.2f %% de la tierra; aislada %.2f %%; tiras de hierba %.2f por 100 "
           "de roca; nieve aislada %.3f %%\n",
           rep.land_pct[static_cast<std::size_t>(terrain::Material::Rock)],
           rep.rock_isolated_pct, rep.veg_strips_per_100_rock, rep.snow_isolated_pct);
    EXPECT_LT(rep.rock_isolated_pct, 1.0);
    EXPECT_LT(rep.veg_strips_per_100_rock, 6.0);
    EXPECT_LT(rep.snow_isolated_pct, 0.15);
}
