#pragma once

#include "terrain/heightfield.h"
#include "terrain/hydrology.h"
#include "terrain/terrain_config.h"

#include <cstdint>
#include <vector>

namespace terrain {

enum class Material : std::uint8_t {
    Seabed,
    Sand,
    Grass,
    Forest,
    Rock,
    Snow,
    RiverBed,
};

struct TerrainData {
    Heightfield height{2, 1.0f};
    Hydrology hydrology;
    std::vector<Material> material; // por muestra del heightfield
    TerrainConfig config;
};

// Pipeline completo: continentalidad → relieve → erosión → hidrología → materiales.
TerrainData generate(const TerrainConfig& cfg);

} // namespace terrain
