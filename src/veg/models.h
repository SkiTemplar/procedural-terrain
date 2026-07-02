#pragma once

#include "render/mesh.h"

#include <cstdint>
#include <vector>

namespace veg {

struct ModelData {
    std::vector<render::Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Modelos low-poly generados por código (sin assets externos). Unidades en
// metros a escala 1; la variación por individuo va en la instancia.
ModelData make_pine();      // conífera: tronco + 3 conos apilados (~9 m)
ModelData make_broadleaf(); // frondoso: tronco + copa icosférica deformada (~7 m)
ModelData make_rock();      // pedrusco: icosfera deformada (~1 m)
ModelData make_tuft();      // mata de hierba: abanico de conos finos (~0.5 m)
ModelData make_bush();      // arbusto: icosfera achatada (~1 m)
ModelData make_log();       // tronco caído: prisma tumbado con conicidad (~4 m)

} // namespace veg
