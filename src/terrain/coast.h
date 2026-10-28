#pragma once

#include "terrain/heightfield.h"
#include "terrain/hydrology.h"
#include "terrain/terrain_config.h"

#include <cstdint>
#include <vector>

namespace terrain {

// Oleaje limitado por fetch (E ∝ F, H_s ∝ F^0.5): H_s = H_abierto·√E, con
// H_abierto la altura significativa del mar abierto que rodea la isla. La ola
// rompe por fondo cuando H ≥ γ·h (índice de rotura de McCowan, γ ≈ 0.78): la
// zona de rompiente acaba en h_b = H_s / γ y su anchura sale sola de la
// pendiente real del fondo. Lo comparten el shader del mar y el dump.
inline constexpr float kOpenSeaWaveHeightM = 1.5f;
inline constexpr float kBreakerIndex = 0.78f;

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
