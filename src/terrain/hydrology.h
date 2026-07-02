#pragma once

#include "terrain/heightfield.h"
#include "terrain/noise.h"
#include "terrain/terrain_config.h"

#include <cstdint>
#include <vector>

namespace terrain {

// Resultado hidrológico: por muestra, nivel de agua (NaN si no hay),
// acumulación de flujo y si la celda pertenece a un lago.
struct Hydrology {
    std::vector<float> water_level;  // lagos y ríos; NaN donde no hay agua
    std::vector<float> flow;         // acumulación D8
    std::vector<std::uint8_t> lake;  // 1 = celda de lago (tras purga)
};

// 0) Perturba las llanuras con ruido suave → el D8 serpentea (sinuosidad).
// 1) Rellena depresiones (priority-flood) para que todo drene al borde.
//    Donde el relleno supera el suelo original nace un lago.
// 2) Acumula flujo D8 sobre la superficie rellenada; incisión stream-power
//    dependiente de pendiente (cañones en montaña, no en llanura).
// 3) Marca ríos donde el flujo supera el umbral y talla su cauce en hf.
Hydrology compute_hydrology(Heightfield& hf, const TerrainConfig& cfg,
                            const Noise& noise);

} // namespace terrain
