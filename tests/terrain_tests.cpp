#include "terrain/erosion.h"
#include "terrain/generator.h"
#include "terrain/heightfield.h"
#include "terrain/noise.h"
#include "terrain/tectonics.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

terrain::TerrainConfig test_config() {
    terrain::TerrainConfig cfg;
    cfg.seed = 42;
    cfg.resolution = 128;
    cfg.size_m = 3000.0f;
    cfg.island_radius = 0.45f;
    cfg.coast_warp_m = 300.0f;
    cfg.erosion_droplets = 8000;
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

TEST(Tectonics, DeterministaYAcotada) {
    const terrain::Tectonics a(42, 5000.0f);
    const terrain::Tectonics b(42, 5000.0f);

    bool any_uplift = false;
    for (float x = -2500.0f; x <= 2500.0f; x += 130.0f) {
        for (float z = -2500.0f; z <= 2500.0f; z += 170.0f) {
            const auto sa = a.sample({x, z});
            const auto sb = b.sample({x, z});
            ASSERT_FLOAT_EQ(sa.uplift, sb.uplift);
            ASSERT_FLOAT_EQ(sa.fault, sb.fault);
            EXPECT_GE(sa.uplift, 0.0f);
            EXPECT_LE(sa.uplift, 1.5f);
            EXPECT_GE(sa.rift, 0.0f);
            EXPECT_GE(sa.fault, 0.0f);
            EXPECT_LE(std::abs(sa.bias), 1.0f);
            any_uplift = any_uplift || sa.uplift > 0.05f;
        }
    }
    // En un dominio con ~14 placas tiene que haber algún borde convergente.
    EXPECT_TRUE(any_uplift);
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
