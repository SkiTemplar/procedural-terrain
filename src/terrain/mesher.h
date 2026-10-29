#pragma once

#include "render/mesh.h"
#include "terrain/generator.h"

#include <optional>

namespace terrain {

// Color base por material (low-poly: color plano por triángulo + jitter).
glm::vec3 material_color(Material m);

// Hash determinista → [0,1): variación sutil de tono por vértice.
float hash01(int x, int z);

// Value noise sobre la retícula de hash01: determinista y compartido entre
// niveles de LOD y la paleta (mismo mundo → mismos parches, sin popping).
float value_noise(float x, float z);

// Lámina de agua interior (lagos + ríos). std::nullopt si no hay agua.
std::optional<render::Mesh> build_water_mesh(const TerrainData& data);

// Plano del mar, bastante mayor que la isla para que se pierda en la niebla.
// Rejilla de kSeaPlaneRes² quads sobre ±sea_plane_half(isla); el shader del
// remanso reproduce su interpolación para enrasar con las olas de vértice.
inline constexpr int kSeaPlaneRes = 64;
[[nodiscard]] constexpr float sea_plane_half(float island_size_m) {
    return island_size_m * 2.0f;
}
render::Mesh build_sea_plane(float island_size_m, float sea_level);

} // namespace terrain
