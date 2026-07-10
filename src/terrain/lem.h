#pragma once

#include "terrain/terrain_config.h"

#include <vector>

namespace terrain {

// Modelo de evolución del paisaje (Cordonnier et al. 2016; solver implícito
// O(n) de Braun & Willett 2013 / FastScape): uplift + stream power + difusión
// de laderas iterados hasta cuasi-estado estacionario sobre un grid macro.
//
// La FORMA de las montañas sale de aquí: red dendrítica de valles con
// crestas conectadas como divisorias de aguas — lo que el ruido nunca da.
// El ruido queda después como mero detalle de laderas.
//
// `uplift` en [0,1] por celda del grid macro (0 = mar, nivel de base fijo).
// `uplift_total_m` = metros de levantamiento acumulado donde uplift = 1.
// `lithology` en [0,1] por celda macro (vacío = 0.5 uniforme, comportamiento
// clásico): 0 = roca DURA (K bajo, área crítica alta → laderas macizas y
// empinadas, barrancos anchos), 1 = BLANDA (K alto, área crítica baja →
// disección fina y tendida). Es lo que rompe la longitud de onda única de
// la disección (feedback 2026-07-10 "U de cabeceras"): cada formación
// erosiona a su escala, como en la Tierra.
// Trabaja en UNIDADES FÍSICAS con la pendiente de equilibrio calibrada
// (~35° en cabeceras): la altura Y la pendiente salen coherentes, sin
// normalizaciones que distorsionen las laderas. Devuelve metros.
std::vector<float> run_lem(const std::vector<float>& uplift,
                           const std::vector<float>& lithology, int macro_res,
                           float cell_size_m, float uplift_total_m,
                           int iterations = 150);
inline std::vector<float> run_lem(const std::vector<float>& uplift,
                                  int macro_res, float cell_size_m,
                                  float uplift_total_m, int iterations = 150) {
    return run_lem(uplift, {}, macro_res, cell_size_m, uplift_total_m,
                   iterations);
}

// Muestreo bicúbico (Catmull-Rom) de un grid cuadrado en coordenadas
// fraccionarias de celda. Para amplificar el macro al heightmap completo.
float sample_bicubic(const std::vector<float>& grid, int res, float fx, float fz);

} // namespace terrain
