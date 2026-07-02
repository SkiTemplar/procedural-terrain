#pragma once

#include "terrain/heightfield.h"
#include "terrain/terrain_config.h"

#include <vector>

namespace terrain {

// Resultado hidrológico: por muestra, nivel de agua (NaN si no hay) y
// acumulación de flujo (nº de celdas que drenan a través de ella).
struct Hydrology {
    std::vector<float> water_level; // lagos y ríos; NaN donde no hay agua
    std::vector<float> flow;        // acumulación D8
};

// 1) Rellena depresiones (priority-flood) para que todo drene al borde.
//    Donde el relleno supera el suelo original nace un lago.
// 2) Acumula flujo D8 sobre la superficie rellenada.
// 3) Marca ríos donde el flujo supera el umbral y talla su cauce en hf.
Hydrology compute_hydrology(Heightfield& hf, const TerrainConfig& cfg);

} // namespace terrain
