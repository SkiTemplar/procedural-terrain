#pragma once

#include "terrain/terrain_config.h"

#include <glm/vec2.hpp>

#include <cstddef>
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
// `wind` = dirección del viento dominante (fija por seed; {0,0} = sin
// precipitación orográfica, lluvia uniforme clásica). Con viento, un pase de
// advección de humedad (recarga sobre el mar, lluvia ∝ pendiente de
// barlovento, SOMBRA de lluvia emergente a sotavento) pondera el caudal del
// LEM cada 10 iteraciones sobre el relieve vivo: los flancos húmedos se
// diseccionan fino y hondo, el sotavento queda liso y seco — segunda vía
// (2026-07-10) contra la disección uniforme.
// `seabed` (modo BATIMETRÍA, N2 costa emergente): prior de fondo marino en
// metros CON SIGNO (≤0 mar adentro, →0 bajo la futura tierra). Si se pasa,
// el LEM devuelve relieve con signo: el mar deja de estar clavado a h=0 —
// arranca en su batimetría prior y solo lo tocan tectónica (uplift) y
// difusión (bajo el agua no hay incisión fluvial); los ríos subaéreos gradan
// al NIVEL DEL MAR (h=0), no al lecho de su receptor sumergido. La costa es
// el contorno h=0 del relieve erosionado, y los lóbulos de uplift submarinos
// que asoman se convierten en islotes reales (erosionados como tierra en
// cuanto emergen). Vacío = comportamiento clásico (mar pinned a 0, clamp ≥0).
// Sedimentación (Yuan et al. 2019, JGR Earth Surface): el material que
// erosionan las cabeceras viaja aguas abajo y se DEPOSITA donde la capacidad
// de transporte cae (frente de montaña, llanura costera): abanicos aluviales
// y fondos de valle planos emergen del balance, no de un estampado.
// Trabaja en UNIDADES FÍSICAS con la pendiente de equilibrio calibrada
// (~35° en cabeceras): la altura Y la pendiente salen coherentes, sin
// normalizaciones que distorsionen las laderas. Devuelve metros.
// Balance de sedimento de la ÚLTIMA iteración (para el dump): volúmenes en
// m³ por paso sobre celdas con incisión. `deposited_m3` suma las celdas que
// suben más que su uplift (agradación neta).
struct LemStats {
    double eroded_m3 = 0.0;
    double deposited_m3 = 0.0;
    std::size_t aggrading_cells = 0;
    // Espesor de aluvión (m) por celda macro al final: crece con la
    // agradación neta y se consume cuando la celda vuelve a erosionar. El
    // generador lo usa para enterrar la rugosidad de detalle bajo el
    // sedimento (un fondo de valle relleno es liso).
    std::vector<float> alluvium_m;
    // Precipitación orográfica relativa por celda macro (la misma `rain_w`
    // que pondera el caudal del LEM en su último recálculo): media 1 sobre
    // tierra, suelo kRainFloor en la sombra de lluvia, 1 sobre el mar. Vacío
    // si no se calculó (sin viento → lluvia uniforme 1).
    std::vector<float> rain;
    // Denudación neta de la última iteración por celda macro (incisión +
    // difusión, antes del relleno de pits), en unidades del uplift máximo
    // por iteración: ~u en equilibrio, >u en transitorio erosivo, <0 donde
    // agrada. Mide dónde el suelo no llega a formarse (roca desnuda).
    std::vector<float> denudation;
};

std::vector<float> run_lem(const std::vector<float>& uplift,
                           const std::vector<float>& lithology, int macro_res,
                           float cell_size_m, float uplift_total_m,
                           int iterations = 150,
                           glm::vec2 wind = {0.0f, 0.0f},
                           const std::vector<float>& seabed = {},
                           LemStats* stats = nullptr);
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
