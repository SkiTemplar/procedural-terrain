#include "terrain/coast.h"
#include "terrain/coast_metrics.h"
#include "terrain/erosion.h"
#include "terrain/generator.h"
#include "terrain/heightfield.h"
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
    // Límite ligado a la profundidad de diseño del cauce: protege contra
    // flotación (junto al check de contención), no contra la profundidad
    // legítima que crece con el contraste de caudales.
    EXPECT_LT(median, cfg.river_max_depth_m + 2.0f);
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

TEST(Preset, RoundTripDeConfigCompleta) {
    // Contrato C1: guardar y cargar un preset conserva TODOS los campos del
    // config (incluida la seed) — un campo olvidado en la serialización
    // significa un mundo distinto al cargar.
    terrain::TerrainConfig cfg = test_config();
    cfg.seed = 777;
    cfg.coast_flood_m = 11.5f;
    cfg.wind_dir_deg = 123.0f;
    cfg.snow_line_m = 640.0f;

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
    cfg.resolution = 512; // macro 128: misma física, coste de test razonable
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
