#pragma once

#include "terrain/generator.h"
#include "terrain/water_flow.h"

#include <vector>

namespace terrain {

// Métricas headless del agua que fluye (auditoría 2026-10, hallazgos #1-#3).
// Las consume el dump --dump-costa y los tests; no tocan el mundo.

struct RiverMouthReport {
    int x = -1;
    int z = -1;
    float flow = 0.0f;
    float step_m = 0.0f; // lámina del río en la boca − nivel del mar
};

struct FlowAngleStats {
    int cells = 0;               // celdas de río con corriente evaluadas
    float pct_over_60deg = 0.0f; // % con > 60° respecto al eje aguas abajo
    float median_jump_deg = 0.0f; // salto angular mediano entre vecinas
};

struct WaterReport {
    // #2 desembocaduras: una por río (supresión de bocas vecinas), por caudal.
    std::vector<RiverMouthReport> mouths;
    float mouth_step_max = 0.0f;
    float mouth_step_p95 = 0.0f;
    // Río seco: caudal ≥ 1.5× umbral sin lámina con el terreno sobre el mar.
    int dry_cells = 0;
    float dry_worst_flow = 0.0f;
    int dry_x = -1;
    int dry_z = -1;
    // Celdas con lámina de río y lecho bajo el plano del mar (el render
    // recorta el mar en ellas) y mar encerrado tierra adentro.
    int river_bed_under_sea_plane = 0;
    int enclosed_sea_cells = 0;
    // #1 corriente: campo nuevo y, como referencia, el descenso del lecho.
    FlowAngleStats flow_field;
    FlowAngleStats bed_descent;
    // #3 rompiente.
    int shallow_sea_cells = 0;        // mar abierto con calado < 2.4 m
    int shallow_sea_with_energy = 0;  // … y energía de oleaje > 0
    int surf_cells = 0;               // calado < H_b = H_s / γ
    int surf_exposed = 0;             // de ellas con E > 0.45
    int surf_sheltered = 0;           // de ellas con E < 0.16
};

[[nodiscard]] WaterReport measure_water(const TerrainData& data,
                                        const RiverFlowField& flow);

} // namespace terrain
