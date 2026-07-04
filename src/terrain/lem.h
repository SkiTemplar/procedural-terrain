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
// Trabaja en UNIDADES FÍSICAS con la pendiente de equilibrio calibrada
// (~35° en cabeceras): la altura Y la pendiente salen coherentes, sin
// normalizaciones que distorsionen las laderas. Devuelve metros.
std::vector<float> run_lem(const std::vector<float>& uplift, int macro_res,
                           float cell_size_m, float uplift_total_m,
                           int iterations = 150);

// Muestreo bicúbico (Catmull-Rom) de un grid cuadrado en coordenadas
// fraccionarias de celda. Para amplificar el macro al heightmap completo.
float sample_bicubic(const std::vector<float>& grid, int res, float fx, float fz);

} // namespace terrain
