#pragma once

#include "terrain/heightfield.h"
#include "terrain/hydrology.h"
#include "terrain/noise.h"
#include "terrain/terrain_config.h"

#include <cstddef>
#include <cstdint>
#include <functional>
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

// --- Erosión marina de la costa rocosa (retroceso de acantilado) ---
//
// Sunamura (1977, 1992): el pie del acantilado retrocede a dx/dt ∝ ln(F_w/F_r)
// cuando la fuerza del oleaje F_w supera la resistencia de la roca F_r, y no
// retrocede en caso contrario. La ola llega al pie tras cruzar la plataforma
// que el propio retroceso ha dejado y pierde fuerza en ella:
// F_w = F_0·exp(−W/L). De ahí EMERGEN la plataforma de abrasión (anchura de
// equilibrio W = L·ln(F_0/F_r): crece con el oleaje, decrece con la dureza),
// los farallones (roca más resistente que se queda en pie cuando su entorno
// ya ha retrocedido) y el frente vertical (la pared conserva la cota del
// terreno original detrás de una plataforma a cota de marea).

// Campo escalar evaluado por índice de celda (z·res + x). Se consulta una vez
// por celda cuando la celda entra en el frente de erosión.
using CellField = std::function<float(std::size_t)>;

struct CliffRetreatInput {
    std::vector<std::uint8_t> sea; // 1 = mar al empezar (por celda)
    CellField wave_energy;         // E ∈ [0,1] que llega al pie; F_0 = √E
    CellField resistance;          // F_r relativa (> 0); infinito = inerosionable
};

struct CliffRetreatParams {
    float sea_level = 0.0f;
    int steps = 160;               // pasos de integración (edad de la costa)
    float rate_m = 1.0f;           // retroceso por paso y unidad de ln(F_w/F_r)
    float attenuation_m = 70.0f;   // L: amortiguación del oleaje en la plataforma
    float toe_depth_m = 0.5f;      // cota de la plataforma al pie, bajo el mar
    float platform_slope = 0.01f;  // pendiente mar adentro (1:100)
    float max_depth_m = 2.0f;      // cota máxima de la plataforma bajo el mar
    int ramp_cells = 8;            // enlace con el fondo previo (solo excava)
};

struct CliffRetreatStats {
    int eroded_cells = 0;
    float max_retreat_m = 0.0f;
};

// Integra el retroceso sobre el heightfield. Determinista: cada paso evalúa
// todo el frente con el estado del paso anterior (orden irrelevante).
CliffRetreatStats retreat_cliffs(Heightfield& hf, const CliffRetreatInput& in,
                                 const CliffRetreatParams& params);

// Etapa del pipeline: oleaje de la exposición (con la cobertura de playa que
// da el aporte fluvial) contra la resistencia litológica modulada por el
// diaclasado (fbm de λ ~40 m: el ruido solo modula la resistencia). Las celdas
// con lámina de agua dulce no se erosionan.
CliffRetreatStats erode_rocky_coast(Heightfield& hf, const Hydrology& hydro,
                                    const CoastExposure& coast,
                                    const std::vector<float>& lithology,
                                    int litho_res, const Noise& noise,
                                    const TerrainConfig& cfg);

// Métricas de costa rocosa para el dump de verificación.
struct RockyCoastMetrics {
    int coast_cells = 0;            // toda la tierra con mar en el vecindario 8
    float coast_energy_p50 = 0.0f;
    float coast_energy_p90 = 0.0f;
    int cliff_toe_cells = 0;       // tierra junto al mar con ≥4 m de pared
    int platform_cells = 0;         // −2 < h ≤ mar, pendiente < 0.05, unida a un pie
    float platform_area_m2 = 0.0f;
    float mean_platform_width_m = 0.0f; // media sobre pies con plataforma
    float width_energy_pearson = 0.0f;  // anchura de plataforma vs E en el pie
    float width_energy_spearman = 0.0f;
    float toe_energy_p50 = 0.0f;
    float toe_energy_p90 = 0.0f;
    int stacks = 0;                 // islotes < kStackMaxM2 a < 300 m de costa rocosa
    int tall_stacks = 0;            // de ellos, con más de 3 m sobre el mar
    float stack_mean_area_m2 = 0.0f;
    float stack_max_area_m2 = 0.0f;
};

RockyCoastMetrics measure_rocky_coast(const Heightfield& hf,
                                      const std::vector<float>& wave_energy,
                                      float sea_level);

} // namespace terrain
