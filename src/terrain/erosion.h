#pragma once

#include "terrain/heightfield.h"
#include "terrain/terrain_config.h"

#include <cstdint>
#include <vector>

namespace terrain {

// Erosión hidráulica por partículas: cada gota rueda pendiente abajo,
// arranca sedimento donde acelera y lo deposita donde frena.
// Determinista: la seed del config alimenta el RNG de las gotas.
void erode(Heightfield& hf, const TerrainConfig& cfg);

// Erosión térmica de la roca: donde la pendiente a alguno de los 8 vecinos
// supera la crítica (talus_slope × crit_scale[i], vacío = 1), el exceso se
// reparte entre los vecinos más bajos. Iterada hasta estabilidad acotada (se
// corta cuando una pasada mueve < 1 cm). Las pedreras con recorrido de caída
// salen de rockfall_talus (rockfall.h), no de aquí.
void thermal_erode(Heightfield& hf, int iterations, float talus_slope,
                   const std::vector<float>& crit_scale = {});

// Suavizado selectivo de agujas: promedia SOLO las celdas cuya pendiente a
// algún vecino supera el umbral. Elimina picos de un píxel sin tocar el
// resto del relieve.
void smooth_peaks(Heightfield& hf, int passes, float slope_threshold);

// Recorta picos de UN vértice: baja toda celda que sobresalga más de max_over
// sobre TODAS sus 8 vecinas. Elimina las agujas artificiales que quedan tras
// el LEM/sharpen sin tocar crestas ni paredes reales.
void clamp_spikes(Heightfield& hf, float max_over);

} // namespace terrain
