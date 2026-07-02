#pragma once

#include "terrain/heightfield.h"
#include "terrain/terrain_config.h"

#include <cstdint>

namespace terrain {

// Erosión hidráulica por partículas: cada gota rueda pendiente abajo,
// arranca sedimento donde acelera y lo deposita donde frena.
// Determinista: la seed del config alimenta el RNG de las gotas.
void erode(Heightfield& hf, const TerrainConfig& cfg);

} // namespace terrain
