// Desprendimientos, canchales y erosión térmica de 8 vecinos (auditoría
// 2026-10 relieve #5/#6): contratos físicos sobre relieves sintéticos y sobre
// el mundo de referencia.

#include "terrain/erosion.h"
#include "terrain/generator.h"
#include "terrain/heightfield.h"
#include "terrain/landform_stats.h"
#include "terrain/rockfall.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr int kRes = 96;
constexpr float kSize = 96.0f * 2.4f; // celda ~2.4 m, la del juego
constexpr float kWallTopM = 40.0f;
constexpr int kWallX = 40;            // la pared cae entre x=39 y x=40
constexpr float kNoWater = std::numeric_limits<float>::quiet_NaN();
constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

std::size_t idx(int x, int z) {
    return static_cast<std::size_t>(z) * kRes + static_cast<std::size_t>(x);
}

// Meseta a kWallTopM con pared vertical hacia un llano a 10 m (x >= kWallX).
terrain::Heightfield cliff_over_plain() {
    terrain::Heightfield hf(kRes, kSize);
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            hf.at(x, z) = x < kWallX ? kWallTopM : 10.0f;
        }
    }
    return hf;
}

std::vector<float> uniform(float v) {
    return std::vector<float>(static_cast<std::size_t>(kRes) * kRes, v);
}

float max_neighbour_slope(const terrain::Heightfield& hf, int x, int z) {
    float best = 0.0f;
    for (int k = 0; k < 8; ++k) {
        const float d = hf.cell_size() * ((kDx[k] != 0 && kDz[k] != 0) ? 1.41421356f : 1.0f);
        best = std::max(best, (hf.at(x, z) - hf.at(x + kDx[k], z + kDz[k])) / d);
    }
    return best;
}

terrain::TerrainConfig golden_like_config() {
    terrain::TerrainConfig cfg;
    cfg.seed = 20260709;
    cfg.resolution = 512;
    cfg.size_m = 10000.0f;
    cfg.erosion_droplets = 20000;
    return cfg;
}

} // namespace

TEST(Erosion, TermicaDeOchoVecinosNoDejaParedesDiagonales) {
    // Columna de 30 m sobre un llano: con 4 vecinos la escalera diagonal que
    // queda era estable aunque su pendiente real fuese √2 veces la crítica.
    terrain::Heightfield hf(kRes, kSize);
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            hf.at(x, z) = (std::abs(x - kRes / 2) <= 1 && std::abs(z - kRes / 2) <= 1) ? 30.0f
                                                                                    : 0.0f;
        }
    }
    constexpr float kTalus = 1.0f;
    terrain::thermal_erode(hf, 400, kTalus);
    float worst = 0.0f;
    for (int z = 1; z < kRes - 1; ++z) {
        for (int x = 1; x < kRes - 1; ++x) {
            worst = std::max(worst, max_neighbour_slope(hf, x, z));
        }
    }
    EXPECT_LT(worst, kTalus * 1.05f) << "queda una pendiente de " << worst;
}

TEST(Erosion, TermicaRespetaLaRocaDuraYRelajaLaBlanda) {
    // Misma pared en dos formaciones: la blanda (factor bajo) se tiende más.
    terrain::Heightfield soft = cliff_over_plain();
    terrain::Heightfield hard = cliff_over_plain();
    terrain::thermal_erode(soft, 300, 1.0f, uniform(terrain::rock_strength_factor(1.0f)));
    terrain::thermal_erode(hard, 300, 1.0f, uniform(terrain::rock_strength_factor(0.0f)));
    const int z = kRes / 2;
    EXPECT_LT(max_neighbour_slope(soft, kWallX - 1, z), max_neighbour_slope(hard, kWallX - 1, z));
    EXPECT_GT(terrain::rock_strength_factor(0.0f), terrain::rock_strength_factor(1.0f));
    EXPECT_GT(terrain::debris_repose_tan(0.0f), terrain::debris_repose_tan(1.0f));
}

TEST(Rockfall, LaParedSueltaUnCanchalAlPieEnReposo) {
    terrain::Heightfield hf = cliff_over_plain();
    const auto before = hf.data();
    const float repose = terrain::debris_repose_tan(0.5f);
    const auto res = terrain::rockfall_talus(hf, uniform(1.0f), uniform(repose),
                                             uniform(kNoWater), -100.0f);
    const int z = kRes / 2;

    // La cornisa retrocede: la meseta junto al borde baja.
    EXPECT_LT(hf.at(kWallX - 1, z), before[idx(kWallX - 1, z)] - 0.5f);
    // Hay canchal al pie (primeros ~10 m del llano) y no lejos en el llano.
    float foot = 0.0f;
    for (int x = kWallX; x < kWallX + 4; ++x) {
        foot = std::max(foot, res.talus_m[idx(x, z)]);
    }
    EXPECT_GT(foot, 1.0f);
    EXPECT_FLOAT_EQ(res.talus_m[idx(kRes - 5, z)], 0.0f);
    // El canchal es más grueso contra la pared que en su punta: cuña.
    EXPECT_GT(res.talus_m[idx(kWallX, z)], res.talus_m[idx(kWallX + 6, z)]);
    // Su superficie no supera el reposo (margen: discretización D8).
    for (int x = kWallX + 1; x < kRes - 2; ++x) {
        if (res.talus_m[idx(x, z)] > 0.1f) {
            EXPECT_LT(max_neighbour_slope(hf, x, z), repose * 1.10f) << "x=" << x;
        }
    }
    // Conservación: lo desprendido acaba depositado o evacuado.
    const auto& st = res.stats;
    ASSERT_GT(st.detached_m3, 0.0);
    EXPECT_NEAR(st.deposited_m3 + st.evacuated_m3, st.detached_m3, st.detached_m3 * 1e-3);
    EXPECT_GT(st.source_cells, 0u);
}

TEST(Rockfall, ElAguaSeLlevaElDerrubioYNoQuedaPerchada) {
    // Río a lo largo del pie de la pared: nada se deposita en el agua y la
    // orilla nunca queda por debajo de la lámina (no abre agua perchada).
    terrain::Heightfield hf = cliff_over_plain();
    auto water = uniform(kNoWater);
    constexpr float kWaterLevel = 10.5f;
    for (int z = 0; z < kRes; ++z) {
        for (int x = kWallX; x < kWallX + 3; ++x) {
            water[idx(x, z)] = kWaterLevel;
        }
    }
    const auto res = terrain::rockfall_talus(hf, uniform(1.0f),
                                             uniform(terrain::debris_repose_tan(0.5f)),
                                             water, -100.0f);
    for (int z = 1; z < kRes - 1; ++z) {
        for (int x = 1; x < kRes - 1; ++x) {
            if (!std::isnan(water[idx(x, z)])) {
                EXPECT_FLOAT_EQ(res.talus_m[idx(x, z)], 0.0f);
                EXPECT_FLOAT_EQ(hf.at(x, z), 10.0f);
            } else if (x < kWallX && x >= kWallX - 4) {
                EXPECT_GE(hf.at(x, z), kWaterLevel) << "(" << x << "," << z << ")";
            }
        }
    }
    EXPECT_GT(res.stats.evacuated_m3, 0.0);
}

TEST(Rockfall, Determinista) {
    terrain::Heightfield a = cliff_over_plain();
    terrain::Heightfield b = cliff_over_plain();
    const auto ra = terrain::rockfall_talus(a, uniform(1.0f), uniform(0.7f),
                                            uniform(kNoWater), -100.0f);
    const auto rb = terrain::rockfall_talus(b, uniform(1.0f), uniform(0.7f),
                                            uniform(kNoWater), -100.0f);
    ASSERT_EQ(a.data().size(), b.data().size());
    for (std::size_t i = 0; i < a.data().size(); ++i) {
        ASSERT_EQ(a.data()[i], b.data()[i]) << "índice " << i;
        ASSERT_EQ(ra.talus_m[i], rb.talus_m[i]) << "índice " << i;
    }
}

TEST(Pedreras, ElMundoTieneCanchalesAlPieDeSusParedes) {
    // Mundo a escala real: las pedreras emergen al pie de las paredes, con
    // pendiente de reposo, y solo fuera de la banda litoral.
    const auto data = terrain::generate(golden_like_config());
    const terrain::LandformStats st = terrain::compute_landform_stats(data);
    printf("  pedreras: %zu celdas, moda tan %.3f, pie de pared %zu (canchal %zu)\n",
           st.scree_cells, st.scree_slope_mode_tan, st.wall_foot_cells, st.wall_foot_scree);
    ASSERT_GT(st.scree_cells, 0u);
    ASSERT_GT(st.wall_foot_cells, 0u);
    EXPECT_GT(static_cast<double>(st.wall_foot_scree) / static_cast<double>(st.wall_foot_cells),
              0.25);
    EXPECT_GT(st.scree_slope_mode_tan, 0.55f);
    EXPECT_LT(st.scree_slope_mode_tan, 0.85f);
    EXPECT_GT(data.rockfall_stats.detached_m3, 0.0);
}
