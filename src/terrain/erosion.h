#pragma once

#include "terrain/heightfield.h"
#include "terrain/terrain_config.h"

#include <cstdint>

namespace terrain {

// Erosión hidráulica por partículas: cada gota rueda pendiente abajo,
// arranca sedimento donde acelera y lo deposita donde frena.
// Determinista: la seed del config alimenta el RNG de las gotas.
void erode(Heightfield& hf, const TerrainConfig& cfg);

// Erosión térmica: donde la pendiente supera el ángulo de reposo (talus),
// el material se desprende hacia el vecino más bajo. Suaviza agujas y crea
// pedreras/faldas naturales al pie de las montañas.
void thermal_erode(Heightfield& hf, int iterations, float talus_slope);

} // namespace terrain
