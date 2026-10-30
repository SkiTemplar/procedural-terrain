#pragma once

#include "render/terrain_shaders.h"

#include <array>

namespace render {

struct TerrainShaderProgram {
    const char* name;
    const char* vertex;
    const char* fragment;
};

inline constexpr std::array kTerrainShaderPrograms{
    TerrainShaderProgram{"terrain", kTerrainVert, kTerrainFrag},
    TerrainShaderProgram{"vegetacion", kVegVert, kTerrainFrag},
    TerrainShaderProgram{"sombra_terreno", kShadowTerrainVert, kShadowFrag},
    TerrainShaderProgram{"sombra_vegetacion", kShadowVegVert, kShadowFrag},
    TerrainShaderProgram{"cesped", kGrassVert, kGrassFrag},
    TerrainShaderProgram{"agua", kWaterVert, kWaterFrag},
    TerrainShaderProgram{"cielo", kSkyVert, kSkyFrag},
};

} // namespace render
