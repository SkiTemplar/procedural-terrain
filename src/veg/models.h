#pragma once

#include "render/mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace veg {

struct ModelData {
    std::vector<render::Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Follaje base (color lineal) y radio de copa a escala 1 de los dos árboles:
// los comparte el dosel lejano (veg/canopy_layer) con los modelos.
inline constexpr glm::vec3 kPineFoliage{0.19f, 0.38f, 0.20f};
inline constexpr glm::vec3 kLeafFoliage{0.30f, 0.50f, 0.22f};
inline constexpr float kPineCrownRadiusM = 2.4f;
inline constexpr float kLeafCrownRadiusM = 2.6f;

// Modelos low-poly generados por código (sin assets externos). Unidades en
// metros a escala 1; la variación por individuo va en la instancia.
ModelData make_pine();      // conífera: tronco + 3 conos apilados (~9 m)
ModelData make_broadleaf(); // frondoso: tronco + copa icosférica deformada (~7 m)
ModelData make_rock();      // pedrusco: icosfera deformada (~1 m)
ModelData make_tuft();      // mata de hierba: abanico de conos finos (~0.5 m)
ModelData make_grass();     // brizna densa: abanico de hojas finas (~0.35 m, LOD cercano)
ModelData make_bush();      // arbusto: icosfera achatada (~1 m)
ModelData make_log();       // tronco caído: prisma tumbado con conicidad (~4 m)

} // namespace veg
