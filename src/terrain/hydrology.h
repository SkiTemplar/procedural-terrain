#pragma once

#include "terrain/heightfield.h"
#include "terrain/noise.h"
#include "terrain/terrain_config.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terrain {

// Balance de los depósitos fluviales (deltas y barras) para el dump: cuántos
// rasgos ha construido el proceso y cuánta superficie emerge de cada tipo.
struct DepositStats {
    int deltas = 0;                 // bocas con lóbulo emergido
    int delta_lobes = 0;            // lóbulos (1 por delta + 1 por avulsión)
    std::size_t delta_cells = 0;    // celdas de lóbulo sobre la lámina
    double delta_volume_m3 = 0.0;   // sedimento depositado en los lóbulos
    int bars = 0;                   // barras emergidas
    std::size_t bar_cells = 0;      // celdas de barra sobre la lámina
};

// Resultado hidrológico: por muestra, nivel de agua (NaN si no hay),
// acumulación de flujo y si la celda pertenece a un lago.
struct Hydrology {
    std::vector<float> water_level;  // lagos y ríos; NaN donde no hay agua
    std::vector<float> flow;         // acumulación D8
    std::vector<std::uint8_t> lake;  // 1 = celda de lago (tras purga)
    // 0 = agua tranquila; 1 = rápidos (espuma); 2 = cascada (pared + poza).
    // Lo consume el render (scroll de espuma / ribbon vertical).
    std::vector<std::uint8_t> whitewater;
    DepositStats deposits;
};

// 0) Perturba las llanuras con ruido suave → el D8 serpentea (sinuosidad).
// 1) Rellena depresiones (priority-flood) para que todo drene al borde.
//    Donde el relleno supera el suelo original nace un lago.
// 2) Acumula flujo D8 sobre la superficie rellenada; incisión stream-power
//    dependiente de pendiente (cañones en montaña, no en llanura).
// 3) Marca ríos donde el flujo supera el umbral y talla su cauce en hf.
// 4) Agua v2 (riverscape.h): efluentes de lagos con caudal heredado, lecho
//    monótono aguas abajo, cascadas (rellano + pared + poza) y rápidos.
Hydrology compute_hydrology(Heightfield& hf, const TerrainConfig& cfg,
                            const Noise& noise);

} // namespace terrain
