#pragma once

#include "terrain/heightfield.h"
#include "terrain/hydrology.h"
#include "terrain/terrain_config.h"

#include <cstdint>
#include <vector>

namespace terrain {

// Exposición litoral (Agua v2, paso 8): energía de oleaje por REI/fetch y
// aporte sedimentario de las desembocaduras, ambos suavizados a lo largo del
// contorno de costa y propagados tierra adentro por la banda litoral. La
// clasificación de orillas (grava, disipativa, marisma, roca) sale de aquí.
struct CoastExposure {
    std::vector<float> wave_energy;       // [0,1]; 0 fuera de la banda litoral
    std::vector<float> river_supply;      // [0,1]; sedimento de desembocaduras
    std::vector<std::uint8_t> shore_band; // 1 = celda de la banda litoral
};

// 16 rayos de fetch por celda de costa (paso 4 celdas, cap 2,5 km) ponderados
// por la rosa de vientos (cfg.wind_dir_deg / cfg.wind_focus). Determinista:
// no usa ruido ni RNG — solo el heightfield y la hidrología.
CoastExposure compute_coast_exposure(const Heightfield& hf,
                                     const Hydrology& hydro,
                                     const TerrainConfig& cfg);

} // namespace terrain
