#pragma once

#include "render/mesh.h"

namespace terrain {

// Grid placeholder del Hito 1: colinas suaves seno/coseno para validar el
// flat shading y la cámara. Se sustituye por el pipeline real en el Hito 2.
render::Mesh make_grid(float size_m, int resolution);

} // namespace terrain
