#pragma once

#include "render/mesh.h"
#include "terrain/generator.h"

#include <vector>

namespace terrain {

struct TerrainMeshes {
    std::vector<render::Mesh> ground_chunks;
    std::vector<render::Mesh> water_chunks; // lagos y ríos (puede estar vacío)
};

// Convierte el TerrainData en mallas por chunks. El color va por vértice y el
// shader lo interpola `flat`, así cada triángulo queda de un color (low-poly).
TerrainMeshes build_meshes(const TerrainData& data, int chunk_cells = 128);

// Plano del mar, bastante mayor que la isla para que se pierda en la niebla.
render::Mesh build_sea_plane(float island_size_m, float sea_level);

} // namespace terrain
