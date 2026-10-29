#pragma once

#include "terrain/heightfield.h"
#include "terrain/hydrology.h"

namespace terrain {

// ---- Nivel de base en la costa (pasos finales sobre la lámina 2D) ----

// Knickzone de desembocadura: una lámina de río que llega a la costa colgada
// sobre el mar (vecina sumergida sin lámina) se rebaja hasta la cota del mar
// en la boca y la caída se reparte aguas arriba a lo largo de L = 1.5·Δ/S_k
// (pendiente añadida máxima S_k, Δ = cuelgue), siguiendo la lámina mojada:
// la bajada del nivel de base se difunde como un tramo de rápidos en vez de
// un escalón de una celda (retroceso del knickpoint, Whipple y Tucker 1999).
// El lecho se encaja bajo la lámina nueva (socavón de 0.35 m, igual que el
// tallado del cauce). Devuelve el número de celdas rebajadas.
int grade_coastal_knickzones(Heightfield& hf, Hydrology& out, float sea_level);

// Bolsas sumergidas del estuario: celdas bajo el mar sin lámina, no
// conectadas con el mar abierto (borde del dominio) y en contacto con lámina
// de río en remanso (cota de mar). El agua conectada se iguala: la bolsa
// recibe la lámina del remanso. Sin esto, el eje de un estuario tallado bajo
// el mar quedaba sin agua entre celdas mojadas. Devuelve las celdas llenadas.
int flood_estuary_pockets(const Heightfield& hf, Hydrology& out, float sea_level);

} // namespace terrain
