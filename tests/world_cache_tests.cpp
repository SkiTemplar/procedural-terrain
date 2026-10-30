#include "terrain/generator.h"
#include "terrain/preset.h"
#include "terrain/world_cache.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <vector>

namespace {

terrain::TerrainConfig small_config() {
    terrain::TerrainConfig cfg;
    cfg.seed = 7;
    cfg.resolution = 128;
    cfg.size_m = 3000.0f;
    cfg.erosion_droplets = 4000;
    cfg.mountain_height_m = 1000.0f;
    cfg.river_flow_threshold = 3840.0f;
    return cfg;
}

// Igualdad bit a bit (los NaN del nivel de agua deben sobrevivir).
template <typename T>
bool same_bits(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size()
        && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

std::string temp_path(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace

TEST(WorldCache, IdaYVueltaConservaElMundo) {
    const terrain::TerrainConfig cfg = small_config();
    const terrain::TerrainData data = terrain::generate(cfg);
    const std::string path = temp_path("pt_world_cache_test.bin");
    ASSERT_TRUE(terrain::save_world_cache(path, data));
    const auto loaded = terrain::load_world_cache(path, cfg);
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(terrain::heightfield_hash(loaded->height), terrain::heightfield_hash(data.height));
    EXPECT_TRUE(same_bits(loaded->hydrology.water_level, data.hydrology.water_level));
    EXPECT_TRUE(same_bits(loaded->hydrology.flow, data.hydrology.flow));
    EXPECT_TRUE(same_bits(loaded->hydrology.lake, data.hydrology.lake));
    EXPECT_TRUE(same_bits(loaded->hydrology.whitewater, data.hydrology.whitewater));
    EXPECT_TRUE(same_bits(loaded->material, data.material));
    EXPECT_TRUE(same_bits(loaded->forest_density, data.forest_density));
    EXPECT_TRUE(same_bits(loaded->climate, data.climate));
    EXPECT_TRUE(same_bits(loaded->wave_energy, data.wave_energy));
    EXPECT_TRUE(same_bits(loaded->lithology, data.lithology));
    EXPECT_TRUE(same_bits(loaded->coast_cliff, data.coast_cliff));
    EXPECT_TRUE(same_bits(loaded->shadow, data.shadow));
    EXPECT_TRUE(same_bits(loaded->sky_ao, data.sky_ao));
    EXPECT_TRUE(same_bits(loaded->precipitation, data.precipitation));
    EXPECT_TRUE(same_bits(loaded->dryness, data.dryness));
    EXPECT_TRUE(same_bits(loaded->lem_stats.alluvium_m, data.lem_stats.alluvium_m));
    EXPECT_EQ(loaded->litho_res, data.litho_res);
    EXPECT_EQ(loaded->light_res, data.light_res);
    EXPECT_EQ(loaded->precip_res, data.precip_res);
    EXPECT_EQ(loaded->max_height_m, data.max_height_m);
    EXPECT_EQ(loaded->snow_line_m, data.snow_line_m);
    EXPECT_EQ(loaded->treeline_m, data.treeline_m);
    EXPECT_EQ(loaded->wind, data.wind);
}

TEST(WorldCache, RechazaOtroConfig) {
    const terrain::TerrainConfig cfg = small_config();
    const terrain::TerrainData data = terrain::generate(cfg);
    const std::string path = temp_path("pt_world_cache_otro.bin");
    ASSERT_TRUE(terrain::save_world_cache(path, data));
    terrain::TerrainConfig other = cfg;
    other.seed = cfg.seed + 1;
    const auto loaded = terrain::load_world_cache(path, other);
    std::filesystem::remove(path);
    EXPECT_FALSE(loaded.has_value());
}

TEST(WorldCache, FicheroInexistenteDaNullopt) {
    EXPECT_FALSE(terrain::load_world_cache(temp_path("pt_no_existe.bin"), small_config()));
}
