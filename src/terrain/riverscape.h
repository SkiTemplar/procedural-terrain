#pragma once

#include "terrain/heightfield.h"
#include "terrain/hydrology.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace terrain {

// Sentinela del grafo D8: celda sin receptora (borde del mapa).
inline constexpr std::size_t kNoDownstream = std::numeric_limits<std::size_t>::max();

// ---- Agua v2 (pasos sobre la red de drenaje ya calculada) ----
// Operan sobre el grafo D8 (`downstream`) y el orden de proceso por altura
// rellenada descendente (`order`) que construye compute_hydrology.

// Efluentes de lagos: cada lago superviviente desagua por su saddle (la
// receptora exterior con más caudal). El río que sale hereda el caudal del
// lago — se le garantiza rango de río (floor sobre `flow`) y nacimiento
// maduro (`from_lake`), y el labio se rompe con una muesca de 1-2 celdas de
// pendiente monótona para que la lámina salga visiblemente de la cubeta.
void connect_lake_effluents(Heightfield& hf, Hydrology& out,
                            const std::vector<std::size_t>& downstream,
                            float river_threshold, float sea_level,
                            std::vector<std::uint8_t>& from_lake);

// Perfil longitudinal monótono: el lecho de un río nunca sube aguas abajo.
// Los bultos residuales (amplificación, tallado) embalsaban la lámina y
// cortaban el cauce en escalones. Solo talla, nunca eleva; respeta lagos.
void enforce_monotone_beds(Heightfield& hf, const Hydrology& out,
                           const std::vector<std::size_t>& order,
                           const std::vector<std::size_t>& downstream,
                           float river_threshold, float sea_level);

// Cascadas y rápidos (Peytavie 2019): sobre el perfil z(s) suavizado de cada
// río, los tramos con pendiente de knickpoint se colapsan en rellano + pared
// vertical + plunge pool (saltos grandes se parten en varios pisos); los
// tramos de rápido reciben micro-escalones y flag whitewater para el render.
void carve_cascades(Heightfield& hf, Hydrology& out,
                    const std::vector<std::size_t>& order,
                    const std::vector<std::size_t>& downstream,
                    float river_threshold, float max_flow, int max_radius,
                    float sea_level);

} // namespace terrain
