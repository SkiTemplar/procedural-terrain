#include "render/terrain_normal_map.h"
#include "terrain/coast.h"
#include "terrain/coast_metrics.h"
#include "terrain/erosion.h"
#include "terrain/generator.h"
#include "terrain/heightfield.h"
#include "terrain/hydraulic_geometry.h"
#include "terrain/hydrology.h"
#include "terrain/lem.h"
#include "terrain/noise.h"
#include "terrain/preset.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace {

terrain::TerrainConfig test_config() {
    terrain::TerrainConfig cfg;
    cfg.seed = 42;
    cfg.resolution = 128;
    cfg.size_m = 3000.0f;
    cfg.island_radius = 0.45f;
    cfg.coast_warp_m = 300.0f;
    cfg.erosion_droplets = 8000;
    // Nota: las PENDIENTES del LEM no dependen de mountain_height (en
    // equilibrio S = u·S0·(A0/A)^θ, el objetivo se cancela); este valor solo
    // dimensiona el budget de uplift. Las laderas del mapa de test son las
    // mismas que las del mapa real.
    cfg.mountain_height_m = 1000.0f;
    cfg.base_height_m = 24.0f; // mismo valor efectivo que el antiguo hardcode
    // El umbral se expresa en celdas equivalentes de un mapa 1024:
    // 3840 × (128²/1024²) = 60 celdas efectivas en este mapa de test.
    cfg.river_flow_threshold = 3840.0f;
    return cfg;
}

float effective_river_threshold(const terrain::TerrainConfig& cfg) {
    const float res = static_cast<float>(cfg.resolution);
    return cfg.river_flow_threshold * (res * res) / (1024.0f * 1024.0f);
}

} // namespace

TEST(Noise, MismaSeedMismosValores) {
    // Arrange
    const terrain::Noise a(1234);
    const terrain::Noise b(1234);

    // Act & Assert
    for (float x = -3.0f; x < 3.0f; x += 0.37f) {
        for (float z = -3.0f; z < 3.0f; z += 0.53f) {
            EXPECT_FLOAT_EQ(a.perlin({x, 0.0f, z}), b.perlin({x, 0.0f, z}));
        }
    }
}

TEST(Noise, SeedsDistintasDifieren) {
    const terrain::Noise a(1);
    const terrain::Noise b(2);

    bool any_diff = false;
    for (float x = 0.1f; x < 5.0f && !any_diff; x += 0.61f) {
        any_diff = std::abs(a.perlin({x, 0.0f, x * 0.7f})
                            - b.perlin({x, 0.0f, x * 0.7f})) > 1e-6f;
    }
    EXPECT_TRUE(any_diff);
}

TEST(Noise, RangosAcotados) {
    const terrain::Noise n(7);
    for (float x = -10.0f; x < 10.0f; x += 0.71f) {
        for (float z = -10.0f; z < 10.0f; z += 0.83f) {
            const glm::vec3 p{x, 0.0f, z};
            EXPECT_LE(std::abs(n.perlin(p)), 1.05f);
            EXPECT_LE(std::abs(n.fbm(p, 5)), 1.05f);
            const float r = n.ridged(p, 5);
            EXPECT_GE(r, 0.0f);
            EXPECT_LE(r, 1.05f);
        }
    }
}

TEST(Noise, CeroEnRetriculaEntera) {
    const terrain::Noise n(99);
    EXPECT_FLOAT_EQ(n.perlin({3.0f, 0.0f, -7.0f}), 0.0f);
    EXPECT_FLOAT_EQ(n.perlin({0.0f, 2.0f, 5.0f}), 0.0f);
}

TEST(Heightfield, SampleBilinealInterpolaEntreMuestras) {
    terrain::Heightfield hf(3, 2.0f); // muestras en -1, 0, 1
    hf.at(0, 0) = 0.0f;
    hf.at(1, 0) = 10.0f;
    hf.at(0, 1) = 0.0f;
    hf.at(1, 1) = 10.0f;

    // A mitad de camino entre columna 0 y 1, misma fila.
    EXPECT_NEAR(hf.sample(-0.5f, -1.0f), 5.0f, 1e-4f);
    // Fuera del dominio clampa al borde.
    EXPECT_NEAR(hf.sample(-50.0f, -50.0f), hf.at(0, 0), 1e-4f);
}

TEST(Generator, DeterministaPorSeed) {
    const auto cfg = test_config();
    const auto a = terrain::generate(cfg);
    const auto b = terrain::generate(cfg);

    ASSERT_EQ(a.height.data().size(), b.height.data().size());
    for (size_t i = 0; i < a.height.data().size(); ++i) {
        ASSERT_FLOAT_EQ(a.height.data()[i], b.height.data()[i]) << "índice " << i;
    }
}

TEST(Generator, SeedsDistintasProducenTerrenosDistintos) {
    auto cfg = test_config();
    const auto a = terrain::generate(cfg);
    cfg.seed = 43;
    const auto b = terrain::generate(cfg);

    bool any_diff = false;
    for (size_t i = 0; i < a.height.data().size() && !any_diff; ++i) {
        any_diff = std::abs(a.height.data()[i] - b.height.data()[i]) > 1e-3f;
    }
    EXPECT_TRUE(any_diff);
}

TEST(Generator, EsUnaIslaConMarAlrededorYTierraDentro) {
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);
    const int res = data.height.resolution();

    // El borde es mayoritariamente mar.
    float border_mean = 0.0f;
    int border_count = 0;
    for (int i = 0; i < res; ++i) {
        border_mean += data.height.at(i, 0) + data.height.at(i, res - 1)
                     + data.height.at(0, i) + data.height.at(res - 1, i);
        border_count += 4;
    }
    border_mean /= static_cast<float>(border_count);
    EXPECT_LT(border_mean, cfg.sea_level);

    // Y el interior tiene tierra firme.
    float interior_max = -1e9f;
    for (int z = res / 4; z < 3 * res / 4; ++z) {
        for (int x = res / 4; x < 3 * res / 4; ++x) {
            interior_max = std::max(interior_max, data.height.at(x, z));
        }
    }
    EXPECT_GT(interior_max, cfg.sea_level + 50.0f);
}

TEST(Generator, SinNaNsYConMaterialesCoherentes) {
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);

    for (const float h : data.height.data()) {
        ASSERT_TRUE(std::isfinite(h));
        ASSERT_LT(std::abs(h), 10000.0f);
    }
    ASSERT_EQ(data.material.size(), data.height.data().size());

    // Donde hay lámina de agua, está por encima del suelo tallado.
    const int res = data.height.resolution();
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            const float wl = data.hydrology.water_level[i];
            if (!std::isnan(wl)) {
                EXPECT_GT(wl, data.height.at(x, z) - 1e-3f);
            }
        }
    }
}

TEST(Generator, LosRiosExisten) {
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);

    const float max_flow =
        *std::max_element(data.hydrology.flow.begin(), data.hydrology.flow.end());
    EXPECT_GT(max_flow, effective_river_threshold(cfg));
}

TEST(Generator, SinHierbaNiBosqueBajoElAgua) {
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);
    const int res = data.height.resolution();

    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            const float wl = data.hydrology.water_level[i];
            if (std::isnan(wl) || data.height.at(x, z) >= wl - 0.05f) {
                continue;
            }
            const auto m = data.material[i];
            EXPECT_TRUE(m == terrain::Material::LakeBed
                        || m == terrain::Material::RiverBed
                        || m == terrain::Material::Seabed)
                << "material vegetal sumergido en (" << x << ", " << z << ")";
        }
    }
}

TEST(Generator, SinMicrolagosDeUnaCelda) {
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);
    const int res = data.height.resolution();
    const auto& wl = data.hydrology.water_level;

    // Toda celda con agua sobre el nivel del mar debe tener al menos un
    // vecino con agua (los charcos de 1 celda se purgan).
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            if (std::isnan(wl[i])) {
                continue;
            }
            bool has_wet_neighbor = false;
            for (int dz = -1; dz <= 1 && !has_wet_neighbor; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) {
                        continue;
                    }
                    if (!std::isnan(wl[static_cast<size_t>(z + dz) * res + (x + dx)])) {
                        has_wet_neighbor = true;
                        break;
                    }
                }
            }
            EXPECT_TRUE(has_wet_neighbor)
                << "celda de agua aislada en (" << x << ", " << z << ")";
        }
    }
}

TEST(Generator, ElRioPrincipalDesembocaEnElMar) {
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);

    // La celda con máxima acumulación es la desembocadura y debe estar a cota
    // de mar. Sin el drenaje continuo (priority-flood+epsilon) el flujo se
    // cortaba en cualquier llanura interior a media altura.
    const auto& flow = data.hydrology.flow;
    const size_t imax = static_cast<size_t>(
        std::max_element(flow.begin(), flow.end()) - flow.begin());
    const int res = data.height.resolution();
    const int x = static_cast<int>(imax % static_cast<size_t>(res));
    const int z = static_cast<int>(imax / static_cast<size_t>(res));
    EXPECT_LT(data.height.at(x, z), cfg.sea_level + 10.0f);
}

TEST(Generator, ElAguaDeLosRiosNoFlota) {
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);
    const int res = data.height.resolution();
    const float threshold = effective_river_threshold(cfg);

    // 1) Profundidad acotada: la lámina va pegada al lecho tallado (antes se
    //    calculaba sobre el terreno sin tallar y quedaba colgada en el aire).
    //    Por construcción el cauce puede tener hasta ~3.5 m de profundidad.
    // 2) Contención: el agua tiene orillas — dentro de un radio corto existe
    //    terreno por encima de la lámina (el agua no es una sábana flotante).
    std::vector<float> gaps;
    int contained = 0;
    int total = 0;
    constexpr int kBankRadius = 4;
    float worst_gap = 0.0f;
    int worst_x = -1, worst_z = -1;

    for (int z = kBankRadius; z < res - kBankRadius; ++z) {
        for (int x = kBankRadius; x < res - kBankRadius; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            const float wl = data.hydrology.water_level[i];
            if (std::isnan(wl) || data.hydrology.lake[i]
                || data.hydrology.flow[i] < threshold
                || data.height.at(x, z) <= cfg.sea_level) {
                continue;
            }
            gaps.push_back(wl - data.height.at(x, z));
            if (gaps.back() > worst_gap) {
                worst_gap = gaps.back();
                worst_x = x;
                worst_z = z;
            }

            ++total;
            bool has_bank = false;
            for (int dz = -kBankRadius; dz <= kBankRadius && !has_bank; ++dz) {
                for (int dx = -kBankRadius; dx <= kBankRadius; ++dx) {
                    if (data.height.at(x + dx, z + dz) >= wl - 0.05f) {
                        has_bank = true;
                        break;
                    }
                }
            }
            contained += has_bank ? 1 : 0;
        }
    }
    ASSERT_FALSE(gaps.empty());
    std::sort(gaps.begin(), gaps.end());
    const float median = gaps[gaps.size() / 2];
    // Límite ligado al calado físico del cauce mayor del mapa (thalweg 1.5·d
    // de su caudal de cauce lleno) más el tope de lámina sobre el lecho que
    // impone la hidrología (2.5 m, rampas de cascada): protege contra
    // flotación (junto al check de contención). A resolución de test (celda
    // ~23 m, pendientes de varios metros por celda) la mediana ya vivía en
    // ese tope antes de la geometría absoluta.
    constexpr float kMaxSheetM = 2.5f;
    const float max_flow_cells = *std::max_element(
        data.hydrology.flow.begin(), data.hydrology.flow.end());
    const float cell = data.height.cell_size();
    const float max_thalweg_m =
        1.5f * terrain::hydraulics::regime_depth_m(
                   terrain::hydraulics::bankfull_discharge_m3s(
                       max_flow_cells * cell * cell));
    EXPECT_LT(median, max_thalweg_m + kMaxSheetM);
    EXPECT_GT(median, 0.0f);
    EXPECT_GT(static_cast<float>(contained) / static_cast<float>(total), 0.97f);

    // Torres de agua (feedback 2026-07-08): la cola del gap también queda
    // acotada — profundidad de diseño + poza + tope de rampa de cascada.
    // A resolución de test la rampa apenas dispara (celda ~24 m); la
    // verificación a escala real (4096², celda 2.4 m) vive en el informe de
    // sanidad de --dump-costa ("agua flotante").
    const float p99_gap = gaps[static_cast<size_t>(
        static_cast<float>(gaps.size()) * 0.99f)];
    if (worst_x >= 0 && (p99_gap >= 6.5f || gaps.back() >= 9.0f)) {
        // Diagnóstico del peor caso: contexto hidrológico completo.
        const size_t wi = static_cast<size_t>(worst_z) * res + worst_x;
        printf("  peor gap %.1f m en (%d,%d): h=%.1f wl=%.1f ww=%d lago=%d "
               "flow=%.0f\n",
               worst_gap, worst_x, worst_z, data.height.at(worst_x, worst_z),
               data.hydrology.water_level[wi],
               static_cast<int>(data.hydrology.whitewater[wi]),
               static_cast<int>(data.hydrology.lake[wi]),
               data.hydrology.flow[wi]);
        for (int dz = -2; dz <= 2; ++dz) {
            printf("   ");
            for (int dx = -2; dx <= 2; ++dx) {
                const size_t ni =
                    static_cast<size_t>(worst_z + dz) * res + (worst_x + dx);
                const float nwl = data.hydrology.water_level[ni];
                printf(" [h=%6.1f wl=%6.1f ww=%d L=%d]",
                       data.height.at(worst_x + dx, worst_z + dz),
                       std::isnan(nwl) ? -1.0f : nwl,
                       static_cast<int>(data.hydrology.whitewater[ni]),
                       static_cast<int>(data.hydrology.lake[ni]));
            }
            printf("\n");
        }
    }
    EXPECT_LT(p99_gap, 6.5f);
    EXPECT_LT(gaps.back(), 9.0f);
}

TEST(Generator, SinMurallasVerticales) {
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);
    const int res = data.height.resolution();
    const float cell = data.height.cell_size();

    // Los bordes de placa no deben producir murallas REGIONALES: el p99 de
    // pendiente queda acotado. El umbral está calibrado para el régimen
    // ALPINO actual (cabeceras a 39°, núcleos de uplift saturados, θ=0.35):
    // p99 < 2.3 (~66°) admite caras de roca en los flancos, no murallas.
    // Los cañones fluviales legítimos son paredes de pocas celdas y viven en
    // la cola extrema — se les da margen (p99.9 < 4.5) pero no barra libre.
    std::vector<float> slopes;
    slopes.reserve(static_cast<size_t>(res) * res);
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res - 1; ++x) {
            slopes.push_back(
                std::abs(data.height.at(x + 1, z) - data.height.at(x, z)) / cell);
        }
    }
    std::sort(slopes.begin(), slopes.end());
    const float p99 = slopes[static_cast<size_t>(slopes.size() * 0.99)];
    const float p999 = slopes[static_cast<size_t>(slopes.size() * 0.999)];

    printf("  pendientes: p99=%.2f p99.9=%.2f\n", p99, p999);

    // Diagnóstico: dónde viven las peores pendientes.
    if (p99 >= 2.3f) {
        int printed = 0;
        for (int z = 0; z < res && printed < 8; ++z) {
            for (int x = 0; x < res - 1 && printed < 8; ++x) {
                const float s =
                    std::abs(data.height.at(x + 1, z) - data.height.at(x, z)) / cell;
                if (s > 2.5f) {
                    const size_t i = static_cast<size_t>(z) * res + x;
                    printf("  pendiente %.1f en (%d,%d) h=%.0f->%.0f agua=%d rio=%.0f\n",
                           s, x, z, data.height.at(x, z), data.height.at(x + 1, z),
                           std::isnan(data.hydrology.water_level[i]) ? 0 : 1,
                           data.hydrology.flow[i]);
                    ++printed;
                }
            }
        }
    }
    EXPECT_LT(p99, 2.3f);
    EXPECT_LT(p999, 4.5f);
}

TEST(Preset, LosPresetsVersionadosUsanElClimaV2) {
    // Revisión de mp/ecologia-fisica: snow_line_m cambió de significado
    // (antes min(valor, 0.60·cumbre)); un preset sin migrar genera otro mundo.
    const std::string dir = PT_PRESET_DIR;
    const auto names = terrain::list_presets(dir);
    ASSERT_FALSE(names.empty());
    for (const auto& name : names) {
        const auto p = terrain::load_preset(dir + "/" + name + ".json");
        ASSERT_TRUE(p.has_value()) << name;
        EXPECT_EQ(p->climate_version, terrain::kPresetClimateVersion) << name;
        std::ifstream in(dir + "/" + name + ".json");
        const auto j = nlohmann::json::parse(in);
        EXPECT_TRUE(j.contains("treeline_m")) << name;
        EXPECT_TRUE(j.contains("snow_line_m")) << name;
    }
    // El mundo canónico es el mismo cargado del preset o del config por
    // defecto.
    const auto canon = terrain::load_preset(dir + "/isla-canonica.json");
    ASSERT_TRUE(canon.has_value());
    const terrain::TerrainConfig def;
    EXPECT_EQ(canon->config.seed, def.seed);
    EXPECT_FLOAT_EQ(canon->config.snow_line_m, def.snow_line_m);
    EXPECT_FLOAT_EQ(canon->config.treeline_m, def.treeline_m);
}

TEST(Preset, RoundTripDeConfigCompleta) {
    // Contrato C1: guardar y cargar un preset conserva TODOS los campos del
    // config (incluida la seed) — un campo olvidado en la serialización
    // significa un mundo distinto al cargar.
    terrain::TerrainConfig cfg = test_config();
    cfg.seed = 777;
    cfg.coast_flood_m = 11.5f;
    cfg.wind_dir_deg = 123.0f;
    cfg.snow_line_m = 640.0f;
    cfg.treeline_m = 430.0f;

    const auto path = std::filesystem::temp_directory_path()
                    / "pt-preset-roundtrip.json";
    ASSERT_TRUE(terrain::save_preset(path.string(), cfg, 0xABCDEFull));
    const auto loaded = terrain::load_preset(path.string());
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(loaded->config.seed, cfg.seed);
    EXPECT_EQ(loaded->height_hash, 0xABCDEFull);
    EXPECT_FLOAT_EQ(loaded->config.size_m, cfg.size_m);
    EXPECT_EQ(loaded->config.resolution, cfg.resolution);
    EXPECT_FLOAT_EQ(loaded->config.coast_flood_m, cfg.coast_flood_m);
    EXPECT_FLOAT_EQ(loaded->config.wind_dir_deg, cfg.wind_dir_deg);
    EXPECT_FLOAT_EQ(loaded->config.snow_line_m, cfg.snow_line_m);
    EXPECT_FLOAT_EQ(loaded->config.treeline_m, cfg.treeline_m);
    EXPECT_EQ(loaded->climate_version, terrain::kPresetClimateVersion);
    // El resto de campos, comparados por su serialización (si un campo nuevo
    // entra en el config y no en el preset, esto NO lo caza — por eso
    // save_preset serializa campo a campo y este test fija los sensibles).
    EXPECT_EQ(terrain::preset_to_json(loaded->config),
              terrain::preset_to_json(cfg));
}

TEST(Preset, CargarYRegenerarReproduceElMundo) {
    // Contrato C1 (criterio §7-C del plan): cargar un preset y regenerar
    // reproduce el mundo — el hash del heightfield coincide con el guardado.
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);
    const std::uint64_t h = terrain::heightfield_hash(data.height);

    const auto path = std::filesystem::temp_directory_path()
                    / "pt-preset-mundo.json";
    ASSERT_TRUE(terrain::save_preset(path.string(), cfg, h));
    const auto loaded = terrain::load_preset(path.string());
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.has_value());

    const auto regen = terrain::generate(loaded->config);
    EXPECT_EQ(terrain::heightfield_hash(regen.height), loaded->height_hash);
}

TEST(Preset, UnPresetViejoConCamposRetiradosSigueCargando) {
    // river_max_width_m y river_max_depth_m se retiraron (la geometría del
    // cauce sale del caudal): un preset guardado antes debe cargar igual,
    // ignorando esos campos y conservando el resto.
    const auto path = std::filesystem::temp_directory_path()
                    / "pt-preset-viejo.json";
    {
        std::ofstream out(path);
        out << R"({"seed": 31337, "river_flow_threshold": 1500.0,
                   "river_max_depth_m": 5.5, "river_max_width_m": 30.0})";
    }
    const auto loaded = terrain::load_preset(path.string());
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->config.seed, 31337u);
    EXPECT_FLOAT_EQ(loaded->config.river_flow_threshold, 1500.0f);
    EXPECT_FALSE(terrain::preset_to_json(loaded->config)
                     .contains("river_max_width_m"));
}

TEST(Preset, CargarFicheroCorruptoNoRevienta) {
    const auto path = std::filesystem::temp_directory_path()
                    / "pt-preset-corrupto.json";
    {
        std::ofstream out(path);
        out << "{ esto no es json valido";
    }
    const auto loaded = terrain::load_preset(path.string());
    std::filesystem::remove(path);
    EXPECT_FALSE(loaded.has_value());
}

TEST(Lem, FondoMarinoRealEIsloteEmergente) {
    // Contrato N2 (costa emergente): en modo batimetría el LEM devuelve el
    // relieve CON SIGNO. El mar es fondo simulado bajo cero (nada lo clava a
    // h=0), la tierra erosiona sobre el nivel del mar, y un lóbulo de uplift
    // submarino EMERGE como islote — sin anillo satélite estampado.
    const int res = 256;
    const float size_m = 10000.0f;
    const float cell = size_m / static_cast<float>(res - 1);
    const float target_m = 2.0f * 1400.0f;

    std::vector<float> uplift(static_cast<size_t>(res) * res, 0.0f);
    std::vector<float> seabed(uplift.size(), 0.0f);
    const auto at = [&](float wx_m, float wz_m) {
        const int x = static_cast<int>((wx_m + 0.5f * size_m) / cell + 0.5f);
        const int z = static_cast<int>((wz_m + 0.5f * size_m) / cell + 0.5f);
        return static_cast<size_t>(z) * res + static_cast<size_t>(x);
    };
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const float wx = (static_cast<float>(x) - 0.5f * (res - 1)) * cell;
            const float wz = (static_cast<float>(z) - 0.5f * (res - 1)) * cell;
            const float r = std::sqrt(wx * wx + wz * wz);
            const size_t i = static_cast<size_t>(z) * res + x;
            // Isla principal: disco r=2 km con rizado determinista.
            if (r < 2000.0f) {
                const float ripple = 0.5f
                    + 0.5f * std::sin(static_cast<float>(x) * 12.9898f
                                      + static_cast<float>(z) * 78.233f);
                uplift[i] = 1.0f - 0.06f * ripple;
            }
            // Lóbulo submarino (futuro islote) a 3.5 km del centro.
            const float dxs = wx - 3500.0f;
            const float rs = std::sqrt(dxs * dxs + wz * wz);
            if (rs < 300.0f) {
                uplift[i] = std::max(uplift[i], 0.30f * (1.0f - rs / 300.0f));
            }
            // Batimetría prior: plataforma que cae a -60 m mar adentro.
            seabed[i] = -60.0f
                * std::clamp((r - 2000.0f) / 500.0f, 0.0f, 1.0f);
        }
    }

    const auto h = terrain::run_lem(uplift, {}, res, cell, target_m, 150,
                                    {0.0f, 0.0f}, seabed);

    // 1) El mar lejos de todo conserva su profundidad (no está clavado a 0).
    EXPECT_LT(h[at(-3500.0f, 0.0f)], -10.0f);
    // 2) El borde del dominio es mar hondo.
    EXPECT_LT(h[0], -30.0f);
    // 3) La isla principal erosiona a cota positiva de verdad.
    const float max_h = *std::max_element(h.begin(), h.end());
    EXPECT_GT(max_h, 0.4f * 1400.0f);
    // 4) El islote EMERGE: algún punto del lóbulo submarino queda sobre el mar.
    float skerry_max = -1e9f;
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const float wx = (static_cast<float>(x) - 0.5f * (res - 1)) * cell;
            const float wz = (static_cast<float>(z) - 0.5f * (res - 1)) * cell;
            const float dxs = wx - 3500.0f;
            if (std::sqrt(dxs * dxs + wz * wz) < 300.0f) {
                skerry_max = std::max(
                    skerry_max, h[static_cast<size_t>(z) * res + x]);
            }
        }
    }
    printf("  islote emergente: cota max %.1f m\n", skerry_max);
    EXPECT_GT(skerry_max, 0.0f);
    // 5) Y sigue siendo un ISLOTE, no una montaña: muy por debajo de la isla.
    EXPECT_LT(skerry_max, 0.25f * max_h);
}

TEST(Lem, LaCotaDeCumbreSigueElObjetivo) {
    // Contrato del LEM: las cumbres deben quedar DEL ORDEN del objetivo
    // pedido (no un 40% de él). En estado estacionario el uplift total se
    // cancela (k_dt ∝ uplift_per_iter), así que la cota solo puede venir de
    // la calibración de la ley de pendientes — esto es lo que se verifica.
    // A ESCALA DE ISLA: con canales de ~3.5 km el área drenada crece tanto
    // que el perfil cóncavo hunde las cumbres si la concavidad no está
    // calibrada (un disco pequeño no reproduce el fallo).
    const int res = 512;
    const float size_m = 10000.0f;
    const float cell = size_m / static_cast<float>(res - 1);
    const float target_m = 1400.0f;

    // Disco de uplift máximo con rizado determinista (el uplift real siempre
    // trae gradientes; sin rizado la semilla inicial es una meseta plana).
    const float radius_m = 0.35f * size_m;
    std::vector<float> uplift(static_cast<size_t>(res) * res, 0.0f);
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const float wx = (static_cast<float>(x) - 0.5f * (res - 1)) * cell;
            const float wz = (static_cast<float>(z) - 0.5f * (res - 1)) * cell;
            if (std::sqrt(wx * wx + wz * wz) >= radius_m) {
                continue;
            }
            const float ripple = 0.5f
                + 0.5f * std::sin(static_cast<float>(x) * 12.9898f
                                  + static_cast<float>(z) * 78.233f);
            uplift[static_cast<size_t>(z) * res + x] = 1.0f - 0.06f * ripple;
        }
    }

    const auto h = terrain::run_lem(uplift, res, cell, target_m);
    const float max_h = *std::max_element(h.begin(), h.end());
    printf("  cumbre LEM: %.0f m (objetivo %.0f m)\n", max_h, target_m);

    EXPECT_GT(max_h, 0.6f * target_m);
    EXPECT_LT(max_h, 1.25f * target_m);
}

TEST(Generator, LasMontanasAlcanzanElObjetivoAEscalaIsla) {
    // Contrato end-to-end de verticalidad: en el mapa REAL (10 km) las
    // cumbres deben quedar al menos al 75% de mountain_height_m tras todo el
    // pipeline (LEM + erosión + costa). El test del disco no basta: aquí
    // entra el campo de uplift de verdad (espinazo ridged + máscara de masa),
    // que es quien decide cuánta montaña ve el LEM.
    terrain::TerrainConfig cfg;
    cfg.seed = 31416;
    // 1024 (macro 256): a 512 la celda macro de 78 m sobreestimaba la cumbre
    // ×1.5 frente al juego (4096) y con sedimentación pasaba del tope.
    cfg.resolution = 1024;
    cfg.size_m = 10000.0f;
    cfg.erosion_droplets = 20000;
    const auto data = terrain::generate(cfg);

    const float max_h =
        *std::max_element(data.height.data().begin(), data.height.data().end());
    printf("  cumbre generate(): %.0f m (objetivo %.0f m)\n", max_h,
           cfg.mountain_height_m);
    EXPECT_GT(max_h, 0.75f * cfg.mountain_height_m);
    EXPECT_LT(max_h, 1.30f * cfg.mountain_height_m);
}

TEST(Hidrologia, UnLagoInteriorDesaguaConRioVisible) {
    // Contrato Agua v2 (efluentes): un cuenco cerrado a media ladera de una
    // isla cónica se llena (priority-flood) y su efluente debe salir como
    // RÍO VISIBLE — caudal sobre el umbral y lámina pintada — desde el labio.
    const auto cfg = test_config();
    terrain::Heightfield hf(cfg.resolution, cfg.size_m);
    const int res = hf.resolution();
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const float wx = hf.world_x(x);
            const float wz = hf.world_z(z);
            const float r = std::sqrt(wx * wx + wz * wz);
            float h = 90.0f - 0.075f * r; // cono isla (costa a r = 1.2 km)
            const float dx = wx;
            const float dz = wz + 400.0f; // cuenco centrado en (0, -400)
            const float d2 = dx * dx + dz * dz;
            constexpr float kBowlR = 250.0f;
            if (d2 < kBowlR * kBowlR) {
                h -= 30.0f * (1.0f - d2 / (kBowlR * kBowlR));
            }
            hf.at(x, z) = h;
        }
    }

    const terrain::Noise noise(7);
    const auto hy = terrain::compute_hydrology(hf, cfg, noise);
    const float threshold = effective_river_threshold(cfg);

    int lake_cells = 0;
    bool effluent_at_lip = false;
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            lake_cells += hy.lake[i] ? 1 : 0;
            if (hy.lake[i] || hy.flow[i] < threshold
                || std::isnan(hy.water_level[i])) {
                continue;
            }
            // ¿Este río toca la orilla del lago? Entonces es el efluente.
            for (int dz = -1; dz <= 1 && !effluent_at_lip; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (hy.lake[static_cast<size_t>(z + dz) * res + (x + dx)]) {
                        effluent_at_lip = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_GT(lake_cells, 50) << "el cuenco no formó lago";
    EXPECT_TRUE(effluent_at_lip)
        << "el lago no desagua con un río visible en el labio";

    // Y el agua del efluente llega hasta la costa (lámina pintada a cota baja).
    bool water_reaches_coast = false;
    for (size_t i = 0; i < hy.water_level.size() && !water_reaches_coast; ++i) {
        water_reaches_coast = !std::isnan(hy.water_level[i]) && !hy.lake[i]
                           && hy.flow[i] >= threshold
                           && hy.water_level[i] < cfg.sea_level + 5.0f;
    }
    EXPECT_TRUE(water_reaches_coast);
}

TEST(Hidrologia, UnCharcoSomeroEnAltoNoSeEmbalsa) {
    // Contrato del breach (feedback 2026-07-10 "río naciendo en la cresta"):
    // una depresión SOMERA de sillín fino en cota alta no se embalsa — el agua
    // rompe el sillín y drena (breaching). Sin lago no hay efluente "nacido
    // río hecho" manando de la cresta. La cubeta HONDA del test anterior (30 m)
    // sí sobrevive: el breach solo toca charcos.
    const auto cfg = test_config();
    terrain::Heightfield hf(cfg.resolution, cfg.size_m);
    const int res = hf.resolution();
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const float wx = hf.world_x(x);
            const float wz = hf.world_z(z);
            const float r = std::sqrt(wx * wx + wz * wz);
            // Cono isla con un ANILLO alrededor de la cumbre: entre la ladera
            // y el anillo queda un foso anular somero (~2.6 m bajo el sillín
            // del anillo) a cota alta — el "charco de cresta".
            float h = 90.0f - 0.075f * r;
            const float ring = (r - 260.0f) / 90.0f;
            if (ring > -1.0f && ring < 1.0f) {
                h += 8.0f * (1.0f - ring * ring);
            }
            hf.at(x, z) = h;
        }
    }

    const terrain::Noise noise(7);
    const auto hy = terrain::compute_hydrology(hf, cfg, noise);

    // Sin breach, el foso anular (~2.6 m, >100 celdas) quedaba como lago
    // colgado a cota ~80 en una isla de 90 m. Con breach no debe quedar
    // NINGÚN lago alto: solo drena.
    int high_lake_cells = 0;
    for (size_t i = 0; i < hy.water_level.size(); ++i) {
        if (hy.lake[i] && hy.water_level[i] > 60.0f) {
            ++high_lake_cells;
        }
    }
    EXPECT_EQ(high_lake_cells, 0)
        << "un charco somero de cresta sigue embalsado (el breach no drenó)";
}

TEST(Hidrologia, ElLechoDelRioNoSubeAguasAbajo) {
    // Contrato Agua v2 (perfil monótono): todo cauce maduro tiene salida —
    // algún vecino río/lago/mar con lecho igual o más bajo. Sin la pasada de
    // monotonía, los bultos residuales embalsaban tramos enteros.
    const auto cfg = test_config();
    auto data = terrain::generate(cfg);
    const int res = data.height.resolution();
    const float threshold = effective_river_threshold(cfg);

    int dammed = 0;
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            if (data.hydrology.flow[i] < 3.0f * threshold
                || data.hydrology.lake[i]
                || data.height.at(x, z) <= cfg.sea_level + 1.0f
                || std::isnan(data.hydrology.water_level[i])) {
                continue;
            }
            bool has_exit = false;
            for (int dz = -1; dz <= 1 && !has_exit; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) {
                        continue;
                    }
                    const size_t ni = static_cast<size_t>(z + dz) * res + (x + dx);
                    // Desembocar en lago o mar siempre es salida (la lámina
                    // continúa aunque el lecho del lago quede más alto que el
                    // cauce tallado en la boca). Entre ríos, el lecho vecino
                    // debe ser igual o más bajo.
                    if (data.hydrology.lake[ni]
                        || data.height.at(x + dx, z + dz) <= cfg.sea_level) {
                        has_exit = true;
                        break;
                    }
                    if (data.hydrology.flow[ni] >= threshold
                        && data.height.at(x + dx, z + dz)
                               <= data.height.at(x, z) + 0.05f) {
                        has_exit = true;
                        break;
                    }
                }
            }
            dammed += has_exit ? 0 : 1;
        }
    }
    EXPECT_EQ(dammed, 0) << dammed << " celdas de río sin salida aguas abajo";
}

TEST(Costa, EnergiaDeOleajeAcotadaYSoloLitoral) {
    // Contrato Agua v2 (paso 8): la energía REI/fetch está en [0,1] y solo
    // vive en la banda litoral — toda celda con energía debe tener mar a
    // pocas celdas de distancia.
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);
    const int res = data.height.resolution();
    ASSERT_EQ(data.wave_energy.size(), data.height.data().size());

    constexpr int kMaxShoreDist = 8; // banda máxima holgada en el mapa de test
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            const float e = data.wave_energy[i];
            ASSERT_GE(e, 0.0f);
            ASSERT_LE(e, 1.0f);
            if (e <= 0.0f || data.height.at(x, z) <= cfg.sea_level) {
                continue;
            }
            bool sea_nearby = false;
            for (int dz = -kMaxShoreDist; dz <= kMaxShoreDist && !sea_nearby; ++dz) {
                for (int dx = -kMaxShoreDist; dx <= kMaxShoreDist; ++dx) {
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                        continue;
                    }
                    if (data.height.at(nx, nz) <= cfg.sea_level) {
                        sea_nearby = true;
                        break;
                    }
                }
            }
            EXPECT_TRUE(sea_nearby)
                << "energia de oleaje tierra adentro en (" << x << ", " << z << ")";
        }
    }
}

TEST(Costa, VariedadDeOrillas) {
    // Criterio §7-A del plan maestro: la costa ya no es una banda de arena
    // uniforme — en la banda litoral emergen al menos dos regímenes de orilla
    // distintos (arena disipativa, grava expuesta, marisma o roca abrigada).
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);
    const int res = data.height.resolution();

    int sand = 0;
    int gravel = 0;
    int marsh = 0;
    int shore_rock = 0;
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            if (data.wave_energy[i] <= 0.0f
                || data.height.at(x, z) <= cfg.sea_level) {
                continue;
            }
            switch (data.material[i]) {
                case terrain::Material::Sand: ++sand; break;
                case terrain::Material::Gravel: ++gravel; break;
                case terrain::Material::Marsh: ++marsh; break;
                case terrain::Material::Rock: ++shore_rock; break;
                default: break;
            }
        }
    }
    printf("  orillas: arena=%d grava=%d marisma=%d roca=%d\n",
           sand, gravel, marsh, shore_rock);
    EXPECT_GT(sand, 0) << "ninguna playa de arena en toda la costa";
    const int variety = (sand > 0) + (gravel > 0) + (marsh > 0) + (shore_rock > 0);
    EXPECT_GE(variety, 2) << "la costa sigue siendo monotipo";
}

TEST(Costa, RiberasDeRioSinGravaNiRoca) {
    // Regresión A1: la franja de grava/roca de la costa expuesta se comía la
    // ribera de los últimos metros de los ríos (bloques grises flanqueando el
    // agua en las desembocaduras). Las riberas fluviales dentro de la banda
    // litoral conservan arena (o marisma) — nunca grava/roca en pendiente
    // suave.
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);
    const int res = data.height.resolution();

    int offenders = 0;
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            if (data.wave_energy[i] <= 0.0f) {
                continue;
            }
            const auto m = data.material[i];
            if (m != terrain::Material::Gravel && m != terrain::Material::Rock) {
                continue;
            }
            const glm::vec3 n = data.height.normal_at(x, z);
            const float slope =
                std::sqrt(std::max(0.0f, 1.0f - n.y * n.y)) / std::max(n.y, 1e-3f);
            if (slope > 0.8f) {
                continue; // acantilado legítimo del clasificador general
            }
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const size_t ni = static_cast<size_t>(z + dz) * res + (x + dx);
                    if (!std::isnan(data.hydrology.water_level[ni])) {
                        ++offenders;
                        dz = 2;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_EQ(offenders, 0) << offenders
                            << " celdas de grava/roca pegadas a agua dulce";
}

TEST(Costa, SinHierbaAPieDeAgua) {
    // Regresión A1: la trasplaya abrigada caía al clasificador general y
    // aparecía HIERBA en mitad de la playa. A pie de agua (rel < 3.9 m,
    // pendiente suave, banda litoral) nunca hay hierba ni bosque.
    const auto cfg = test_config();
    const auto data = terrain::generate(cfg);
    const int res = data.height.resolution();

    int green = 0;
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * res + x;
            const float h = data.height.at(x, z);
            if (data.wave_energy[i] <= 0.0f || h <= cfg.sea_level
                || h >= cfg.sea_level + 3.9f) {
                continue;
            }
            // SIN filtro de pendiente (feedback 2026-07-08): el test copiaba
            // el mismo agujero del clasificador (slope >= 0.35 quedaba fuera
            // de la orilla Y del test) — los montículos empinados de la playa
            // salían verdes y el test no los veía.
            const auto m = data.material[i];
            if (m == terrain::Material::Grass || m == terrain::Material::Forest) {
                ++green;
            }
        }
    }
    EXPECT_EQ(green, 0) << green << " celdas verdes a pie de agua";
}

TEST(Erosion, ModificaElTerrenoSinRomperlo) {
    terrain::TerrainConfig cfg = test_config();
    terrain::Heightfield hf(cfg.resolution, cfg.size_m);
    // Cono suave: pendiente hacia fuera para que las gotas trabajen.
    for (int z = 0; z < cfg.resolution; ++z) {
        for (int x = 0; x < cfg.resolution; ++x) {
            const float dx = hf.world_x(x);
            const float dz = hf.world_z(z);
            hf.at(x, z) = 400.0f - 0.2f * std::sqrt(dx * dx + dz * dz);
        }
    }
    const auto before = hf.data();

    terrain::erode(hf, cfg);

    bool changed = false;
    for (size_t i = 0; i < before.size(); ++i) {
        ASSERT_TRUE(std::isfinite(hf.data()[i]));
        if (std::abs(hf.data()[i] - before[i]) > 1e-4f) {
            changed = true;
        }
    }
    EXPECT_TRUE(changed);
}

TEST(ScharrNormal, HeightfieldConstanteDevuelveNormalCenital) {
    constexpr int kRes = 65;
    terrain::Heightfield hf(kRes, 640.0f);
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            hf.at(x, z) = 150.0f;
        }
    }

    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            const glm::vec3 n = render::compute_scharr_normal(hf, x, z);
            EXPECT_NEAR(n.x, 0.0f, 1e-4f) << "x=" << x << " z=" << z;
            EXPECT_NEAR(n.y, 1.0f, 1e-4f) << "x=" << x << " z=" << z;
            EXPECT_NEAR(n.z, 0.0f, 1e-4f) << "x=" << x << " z=" << z;
        }
    }
}

TEST(ScharrNormal, PlanoInclinadoConocidoNormalExacta) {
    constexpr int kRes = 33;
    terrain::Heightfield hf(kRes, 320.0f);
    const float a = 0.5f;   // dh/dx
    const float b = -0.3f;  // dh/dz
    const float c = 42.0f;

    // Normal analítica exacta: normalize(-dh/dx, 1, -dh/dz)
    const glm::vec3 expected = glm::normalize(glm::vec3{-a, 1.0f, -b});

    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            hf.at(x, z) = a * hf.world_x(x) + b * hf.world_z(z) + c;
        }
    }

    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            const glm::vec3 n = render::compute_scharr_normal(hf, x, z);
            EXPECT_NEAR(n.x, expected.x, 1e-3f) << "x=" << x << " z=" << z;
            EXPECT_NEAR(n.y, expected.y, 1e-3f) << "x=" << x << " z=" << z;
            EXPECT_NEAR(n.z, expected.z, 1e-3f) << "x=" << x << " z=" << z;
        }
    }
}

TEST(ScharrNormal, PlanosInclinadosEjesPuros) {
    constexpr int kRes = 17;
    terrain::Heightfield hf_x(kRes, 160.0f);
    const float slope_x = 0.75f;
    const glm::vec3 exp_x = glm::normalize(glm::vec3{-slope_x, 1.0f, 0.0f});
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            hf_x.at(x, z) = slope_x * hf_x.world_x(x) + 10.0f;
        }
    }
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            const glm::vec3 n = render::compute_scharr_normal(hf_x, x, z);
            EXPECT_NEAR(n.x, exp_x.x, 1e-3f);
            EXPECT_NEAR(n.y, exp_x.y, 1e-3f);
            EXPECT_NEAR(n.z, exp_x.z, 1e-3f);
        }
    }

    terrain::Heightfield hf_z(kRes, 160.0f);
    const float slope_z = -0.4f;
    const glm::vec3 exp_z = glm::normalize(glm::vec3{0.0f, 1.0f, -slope_z});
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            hf_z.at(x, z) = slope_z * hf_z.world_z(z) + 10.0f;
        }
    }
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            const glm::vec3 n = render::compute_scharr_normal(hf_z, x, z);
            EXPECT_NEAR(n.x, exp_z.x, 1e-3f);
            EXPECT_NEAR(n.y, exp_z.y, 1e-3f);
            EXPECT_NEAR(n.z, exp_z.z, 1e-3f);
        }
    }
}

TEST(ScharrNormal, NormalMapParaleloYDecodificadoSnorm16) {
    constexpr int kRes = 64;
    terrain::Heightfield hf(kRes, 640.0f);
    const float a = -0.25f;
    const float b = 0.40f;
    const glm::vec3 expected = glm::normalize(glm::vec3{-a, 1.0f, -b});

    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            hf.at(x, z) = a * hf.world_x(x) + b * hf.world_z(z) + 50.0f;
        }
    }

    const auto normal_map = render::compute_scharr_normal_map(hf);
    ASSERT_EQ(normal_map.size(), static_cast<std::size_t>(kRes) * kRes * 2);

    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(z) * kRes + x) * 2;
            const glm::vec3 decoded = render::decode_snorm16_normal(
                normal_map[idx], normal_map[idx + 1]);
            EXPECT_NEAR(decoded.x, expected.x, 1e-3f) << "x=" << x << " z=" << z;
            EXPECT_NEAR(decoded.y, expected.y, 1e-3f) << "x=" << x << " z=" << z;
            EXPECT_NEAR(decoded.z, expected.z, 1e-3f) << "x=" << x << " z=" << z;
        }
    }
}

TEST(GeometriaHidraulica, CuadruplicarElCaudalDuplicaLaAnchura) {
    // Leopold & Maddock (1953): aguas abajo w ∝ Q^0.5. Dos tramos de llanura
    // (misma pendiente tendida, fuera del techo de Finnegan) con Q2 = 4·Q1:
    // el segundo es el doble de ancho.
    namespace hyd = terrain::hydraulics;
    constexpr float kGentleSlope = 0.001f;
    for (const float q1 : {0.5f, 2.0f, 5.0f}) {
        const float w1 = hyd::bankfull_width_m(q1, kGentleSlope);
        const float w2 = hyd::bankfull_width_m(4.0f * q1, kGentleSlope);
        ASSERT_GT(w1, 0.0f);
        EXPECT_NEAR(w2 / w1, 2.0f, 2.0f * 0.15f) << "Q1 = " << q1 << " m3/s";
        // Y el calado crece más despacio que la anchura: 4^0.4 ≈ 1.74.
        EXPECT_NEAR(hyd::regime_depth_m(4.0f * q1) / hyd::regime_depth_m(q1),
                    1.74f, 0.05f);
    }
}

TEST(GeometriaHidraulica, LaAnchuraEsAbsolutaYRealista) {
    // La anchura depende del caudal ABSOLUTO, no del río mayor del mapa: una
    // cuenca atlántica de 20 km² da un cauce de piedemonte de 8-18 m (curvas
    // regionales de cauce lleno), sea o no el río más grande de la isla.
    namespace hyd = terrain::hydraulics;
    const float q_mean = hyd::mean_discharge_m3s(20.0e6f);
    EXPECT_NEAR(q_mean, 0.67f, 0.05f); // 1500 mm/año · 70 % sobre 20 km²
    const float q_bf = hyd::bankfull_discharge_m3s(20.0e6f);
    const float w = hyd::bankfull_width_m(q_bf, 0.003f);
    EXPECT_GT(w, 8.0f);
    EXPECT_LT(w, 18.0f);
    const float d = hyd::regime_depth_m(q_bf);
    EXPECT_GT(w / d, 15.0f); // cauce de grava ancho y somero
    EXPECT_LT(w / d, 40.0f);
}

TEST(GeometriaHidraulica, LaPendienteEstrechaElCauce) {
    // Finnegan et al. (2005): w ∝ Q^(3/8)·S^(-3/16). En garganta el cauce es
    // más estrecho que en llanura con el mismo caudal, y nunca más ancho que
    // la anchura de régimen.
    namespace hyd = terrain::hydraulics;
    constexpr float kQ = 10.0f;
    const float flat = hyd::bankfull_width_m(kQ, 0.001f);
    const float gorge = hyd::bankfull_width_m(kQ, 0.15f);
    EXPECT_FLOAT_EQ(flat, hyd::regime_width_m(kQ));
    EXPECT_LT(gorge, 0.7f * flat);
    EXPECT_GT(gorge, 0.3f * flat);
}

TEST(GeometriaHidraulica, LaLlanuraAluvialDependeDelCaudalAbsoluto) {
    // La puerta de la llanura aluvial solo mira el caudal de cauce lleno del
    // propio tramo y su pendiente: una cuenca de 1 km² la abre en llano tanto
    // si el río mayor de la isla tiene 3 km² como 30 km² (la puerta relativa
    // antigua, (A/A_max)^0.4 > 0.35, la cerraba en la isla grande).
    namespace hyd = terrain::hydraulics;
    const float q_one_km2 = hyd::bankfull_discharge_m3s(1.0e6f);
    const float q_tenth_km2 = hyd::bankfull_discharge_m3s(0.1e6f);
    EXPECT_TRUE(hyd::opens_floodplain(q_one_km2, 0.01f));
    EXPECT_FALSE(hyd::opens_floodplain(q_tenth_km2, 0.01f));
    // En la caída no hay valle ancho, por mucho caudal que lleve.
    EXPECT_FALSE(hyd::opens_floodplain(10.0f * q_one_km2, 0.2f));
    // El umbral está en el caudal, no en una fracción: justo por encima abre
    // y justo por debajo no.
    EXPECT_TRUE(hyd::opens_floodplain(hyd::kFloodplainMinBankfullQ, 0.0f));
    EXPECT_FALSE(
        hyd::opens_floodplain(0.99f * hyd::kFloodplainMinBankfullQ, 0.0f));
}

// --- Erosión marina de la costa rocosa (Sunamura) ---

namespace {

// Costa recta sintética: mar a −5 m en x < kShoreX y acantilado de 10 m que
// sube suave tierra adentro. Celda de 2.5 m (escala del mundo real).
constexpr int kCoastRes = 160;
constexpr int kShoreX = 20;
constexpr float kCoastCell = 2.5f;

terrain::Heightfield straight_cliff_coast() {
    terrain::Heightfield hf(kCoastRes, kCoastCell * (kCoastRes - 1));
    for (int z = 0; z < kCoastRes; ++z) {
        for (int x = 0; x < kCoastRes; ++x) {
            hf.at(x, z) = x < kShoreX
                              ? -5.0f
                              : 10.0f + 0.02f * static_cast<float>(x - kShoreX)
                                            * kCoastCell;
        }
    }
    return hf;
}

std::vector<std::uint8_t> sea_of(const terrain::Heightfield& hf) {
    std::vector<std::uint8_t> sea(hf.data().size());
    for (size_t i = 0; i < sea.size(); ++i) {
        sea[i] = hf.data()[i] <= 0.0f ? 1 : 0;
    }
    return sea;
}

// Retroceso de la fila z: distancia de la costa original a la primera celda
// que sigue en pie.
float retreat_of_row(const terrain::Heightfield& hf, int z) {
    for (int x = kShoreX; x < kCoastRes; ++x) {
        if (hf.at(x, z) > 0.0f) {
            return static_cast<float>(x - kShoreX) * kCoastCell;
        }
    }
    return static_cast<float>(kCoastRes - kShoreX) * kCoastCell;
}

float mean_retreat(const terrain::Heightfield& hf, int z0, int z1) {
    float sum = 0.0f;
    for (int z = z0; z < z1; ++z) {
        sum += retreat_of_row(hf, z);
    }
    return sum / static_cast<float>(z1 - z0);
}

} // namespace

TEST(Costa, LaRocaDuraRetrocedeMenosQueLaBlanda) {
    // Energía de oleaje uniforme y una franja de roca dura (z ∈ [60, 100)):
    // Sunamura dx/dt ∝ ln(F_w/F_r) → la franja dura retrocede menos, deja un
    // cabo, y toda la costa socavada queda como plataforma a cota de marea
    // con el frente vertical (la pared conserva su cota original).
    terrain::Heightfield hf = straight_cliff_coast();
    const auto before = hf.data();
    terrain::CliffRetreatInput in;
    in.sea = sea_of(hf);
    in.wave_energy = [](std::size_t) { return 0.8f; };
    in.resistance = [](std::size_t i) {
        const int z = static_cast<int>(i / kCoastRes);
        return (z >= 60 && z < 100) ? 0.5f : 0.12f;
    };
    const terrain::CliffRetreatStats stats =
        terrain::retreat_cliffs(hf, in, terrain::CliffRetreatParams{});

    const float soft =
        0.5f * (mean_retreat(hf, 15, 45) + mean_retreat(hf, 115, 145));
    const float hard = mean_retreat(hf, 70, 90);
    printf("  retroceso: blanda %.1f m, dura %.1f m (%d celdas, max %.1f m)\n",
           soft, hard, stats.eroded_cells, stats.max_retreat_m);
    EXPECT_GT(hard, 0.0f) << "con E=0.8 la roca dura también debe retroceder algo";
    EXPECT_GT(soft, hard + 40.0f);
    EXPECT_GT(soft, 60.0f);
    EXPECT_LT(soft, 250.0f);

    for (size_t i = 0; i < before.size(); ++i) {
        const float h = hf.data()[i];
        if (before[i] > 0.0f && h <= 0.0f) {
            // Plataforma de abrasión: casi horizontal, ligeramente bajo el mar.
            EXPECT_GE(h, -2.0f - 1e-4f);
            EXPECT_LE(h, -0.5f + 1e-4f);
        }
        if (before[i] > 0.0f && h > 0.0f) {
            EXPECT_FLOAT_EQ(h, before[i]) << "la tierra en pie no se toca";
        }
        EXPECT_LE(h, before[i] + 1e-4f) << "la abrasión solo excava";
    }
    // Frente vertical: en la roca blanda, la primera celda en pie conserva
    // su pared (>= 10 m) sobre una plataforma bajo el mar.
    const int z = 30;
    const int x0 =
        kShoreX + static_cast<int>(retreat_of_row(hf, z) / kCoastCell + 0.5f);
    ASSERT_LT(x0, kCoastRes);
    EXPECT_GE(hf.at(x0, z), 10.0f);
    EXPECT_LE(hf.at(x0 - 1, z), -0.5f + 1e-4f);
}

TEST(Costa, UnNucleoDuroQuedaComoFarallon) {
    // Un núcleo de roca mucho más resistente dentro de roca blanda: su
    // entorno retrocede y él queda en pie, aislado sobre la plataforma. No
    // se estampa nada: solo cambia F_r en esas celdas.
    terrain::Heightfield hf = straight_cliff_coast();
    terrain::CliffRetreatInput in;
    in.sea = sea_of(hf);
    in.wave_energy = [](std::size_t) { return 0.8f; };
    in.resistance = [](std::size_t i) {
        const int x = static_cast<int>(i % kCoastRes);
        const int z = static_cast<int>(i / kCoastRes);
        const bool core = x >= 30 && x < 35 && z >= 78 && z < 83;
        return core ? 5.0f : 0.12f;
    };
    terrain::retreat_cliffs(hf, in, terrain::CliffRetreatParams{});

    EXPECT_GT(hf.at(32, 80), 0.0f) << "el núcleo duro debe seguir en pie";
    EXPECT_LE(hf.at(40, 80), 0.0f) << "la roca blanda tras el núcleo retrocede";
    EXPECT_LE(hf.at(28, 80), 0.0f) << "y la de delante también";

    const std::vector<float> energy(hf.data().size(), 0.8f);
    const terrain::RockyCoastMetrics m =
        terrain::measure_rocky_coast(hf, energy, 0.0f);
    printf("  farallones %d (area max %.0f m2), plataforma %d celdas\n",
           m.stacks, m.stack_max_area_m2, m.platform_cells);
    EXPECT_EQ(m.stacks, 1);
    EXPECT_GT(m.platform_cells, 0);
}

TEST(Costa, LaPlataformaEsMasAnchaConMasOleaje) {
    // Roca uniforme y energía creciente a lo largo de la costa: la anchura
    // de la plataforma de equilibrio (L·ln(F_0/F_r)) crece con el oleaje.
    terrain::Heightfield hf = straight_cliff_coast();
    const auto energy_at = [](std::size_t i) {
        const int z = static_cast<int>(i / kCoastRes);
        return 0.1f + 0.8f * static_cast<float>(z) / (kCoastRes - 1);
    };
    terrain::CliffRetreatInput in;
    in.sea = sea_of(hf);
    in.wave_energy = energy_at;
    in.resistance = [](std::size_t) { return 0.2f; };
    terrain::retreat_cliffs(hf, in, terrain::CliffRetreatParams{});

    EXPECT_GT(mean_retreat(hf, 120, 150), mean_retreat(hf, 10, 40) + 20.0f);
    std::vector<float> energy(hf.data().size());
    for (size_t i = 0; i < energy.size(); ++i) {
        energy[i] = energy_at(i);
    }
    const terrain::RockyCoastMetrics m =
        terrain::measure_rocky_coast(hf, energy, 0.0f);
    printf("  correlacion anchura-E: pearson %.2f spearman %.2f\n",
           m.width_energy_pearson, m.width_energy_spearman);
    EXPECT_GT(m.width_energy_spearman, 0.5f);
}

namespace {

// Parámetros de la etapa del pipeline (erode_rocky_coast).
terrain::CliffRetreatParams pipeline_retreat_params() {
    terrain::CliffRetreatParams p;
    p.attenuation_m = 150.0f;
    p.steps = 240;
    p.rate_m = 1.5f;
    return p;
}

} // namespace

TEST(Costa, ElOleajeDeUnTramoExpuestoNoSeFiltraAlAbrigado) {
    // Escalón de energía a lo largo de la costa: E = 0.8 en z < 80 y E = 0.01
    // en z >= 80, roca uniforme F_r = 0.39. Con F_0 = 0.1 < F_r, Sunamura da
    // retroceso nulo en el tramo abrigado aunque el expuesto, al retroceder,
    // le abra un flanco: la F_0 es la de su tramo de costa, no la de la ola
    // que socava la celda de al lado.
    constexpr int kStepZ = 80;
    terrain::Heightfield hf = straight_cliff_coast();
    const auto before = hf.data();
    terrain::CliffRetreatInput in;
    in.sea = sea_of(hf);
    in.wave_energy = [](std::size_t i) {
        return static_cast<int>(i / kCoastRes) < kStepZ ? 0.8f : 0.01f;
    };
    in.resistance = [](std::size_t) { return 0.39f; };
    const terrain::CliffRetreatStats stats =
        terrain::retreat_cliffs(hf, in, pipeline_retreat_params());

    const float exposed = mean_retreat(hf, 10, kStepZ - 10);
    printf("  retroceso expuesto %.1f m (%d celdas)\n", exposed, stats.eroded_cells);
    EXPECT_GT(exposed, 50.0f) << "el tramo expuesto debe socavarse";
    for (int z = kStepZ; z < kCoastRes; ++z) {
        EXPECT_EQ(retreat_of_row(hf, z), 0.0f) << "fila abrigada z=" << z;
        for (int x = kShoreX; x < kCoastRes; ++x) {
            const size_t i = static_cast<size_t>(z) * kCoastRes + x;
            ASSERT_EQ(hf.data()[i], before[i]) << "tierra abrigada tocada en ("
                                               << x << ", " << z << ")";
        }
    }
}

TEST(Costa, LaAbrasionNoSubeUnaHondonadaQueYaEstabaBajoLaPlataforma) {
    // Tierra a -1.5 m (más honda que la plataforma, -0.5 m al pie) detrás
    // del acantilado: el frente la alcanza y la socava, pero la etapa solo
    // excava — ninguna celda acaba más alta que antes.
    terrain::Heightfield hf = straight_cliff_coast();
    for (int z = 75; z < 85; ++z) {
        for (int x = kShoreX + 4; x < kShoreX + 10; ++x) {
            hf.at(x, z) = -1.5f;
        }
    }
    const auto before = hf.data();
    terrain::CliffRetreatInput in;
    in.sea.assign(before.size(), 0);
    for (size_t i = 0; i < before.size(); ++i) {
        in.sea[i] = static_cast<int>(i % kCoastRes) < kShoreX ? 1 : 0;
    }
    in.wave_energy = [](std::size_t) { return 0.8f; };
    in.resistance = [](std::size_t) { return 0.12f; };
    const terrain::CliffRetreatStats stats =
        terrain::retreat_cliffs(hf, in, terrain::CliffRetreatParams{});

    ASSERT_GT(stats.eroded_cells, 0);
    ASSERT_GT(retreat_of_row(hf, 80), static_cast<float>(10 * kCoastCell))
        << "el frente debe haber pasado por la hondonada";
    for (size_t i = 0; i < before.size(); ++i) {
        ASSERT_LE(hf.data()[i], before[i] + 1e-4f)
            << "la abrasión subió la celda (" << i % kCoastRes << ", "
            << i / kCoastRes << ")";
    }
}

TEST(Costa, LaAbrasionNoSocavaElSurcoDeUnCauce) {
    // Surco fluvial sin lámina, con el lecho bajo el nivel del mar y caudal
    // de cauce, tras el acantilado (la hidrología lo deja así en el tramo
    // final de algunos ríos). La etapa del pipeline no debe tocarlo: su
    // lecho es el perfil monótono de la hidrología, y la plataforma no debe
    // subirlo ni hacerlo remontar aguas abajo.
    constexpr int kGrooveZ = 80;
    constexpr int kGrooveX0 = kShoreX + 2;
    constexpr int kGrooveX1 = kShoreX + 40;
    terrain::Heightfield hf = straight_cliff_coast();
    for (int x = kGrooveX0; x < kGrooveX1; ++x) {
        // Baja hacia el mar: -0.6 m tierra adentro, -1.4 m junto a la costa.
        const float t = static_cast<float>(x - kGrooveX0)
                      / static_cast<float>(kGrooveX1 - 1 - kGrooveX0);
        hf.at(x, kGrooveZ) = -1.4f + 0.8f * t;
    }
    const auto before = hf.data();
    const size_t n = before.size();

    terrain::TerrainConfig cfg;
    cfg.sea_level = 0.0f;
    cfg.river_flow_threshold = 3840.0f;
    const float channel = cfg.river_flow_threshold
                        * static_cast<float>(kCoastRes * kCoastRes)
                        / (1024.0f * 1024.0f);
    terrain::Hydrology hydro;
    hydro.water_level.assign(n, std::numeric_limits<float>::quiet_NaN());
    hydro.flow.assign(n, 0.0f);
    hydro.lake.assign(n, 0);
    for (int x = kGrooveX0; x < kGrooveX1; ++x) {
        hydro.flow[static_cast<size_t>(kGrooveZ) * kCoastRes + x] = 4.0f * channel;
    }
    terrain::CoastExposure coast;
    coast.wave_energy.assign(n, 0.8f);
    const std::vector<float> soft_rock(4, 1.0f);
    const terrain::Noise noise(7u);
    const terrain::CliffRetreatStats stats = terrain::erode_rocky_coast(
        hf, hydro, coast, soft_rock, 2, noise, cfg);

    ASSERT_GT(stats.eroded_cells, 0);
    ASSERT_GT(retreat_of_row(hf, kGrooveZ - 1), static_cast<float>(10 * kCoastCell))
        << "el acantilado junto al surco debe retroceder";
    for (size_t i = 0; i < n; ++i) {
        ASSERT_LE(hf.data()[i], before[i] + 1e-4f) << "la abrasión subió una celda";
    }
    for (int x = kGrooveX0; x < kGrooveX1; ++x) {
        EXPECT_EQ(hf.at(x, kGrooveZ), before[static_cast<size_t>(kGrooveZ) * kCoastRes + x])
            << "surco tocado en x=" << x;
    }
}
