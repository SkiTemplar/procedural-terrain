#pragma once

#include "terrain/heightfield.h"

#include <cstddef>
#include <vector>

namespace terrain {

// Desprendimiento de ladera y canchales (auditoría 2026-10 relieve #5/#6).
//
// Dos materiales con resistencia distinta:
//  - ROCA: aguanta hasta su pendiente crítica, que depende de la formación
//    (la roca dura sostiene paredes de ~50°, la blanda se desmorona a ~40°).
//  - DERRUBIO suelto: solo aguanta su ángulo de reposo (~33-37°; los clastos
//    angulosos de roca dura traban más que las lajas de roca blanda).
//
// La roca que supera su pendiente crítica se desprende, cae ladera abajo por
// máxima pendiente y se acumula al pie formando conos de derrubios con
// pendiente de reposo; una fracción de bloques grandes sigue la línea de
// energía (Heim) y forma la cola cóncava del canchal. Lo que llega a un río,
// a un lago o a la franja de batida del oleaje se lo lleva el agua.

// Los criterios de estabilidad comparan la caída a cada uno de los 8 vecinos,
// que subestima el gradiente real entre un 0 % (gradiente alineado con un
// vecino) y un 7.6 % (a 22.5°). Multiplicar un umbral físico de pendiente por
// la razón media D8/gradiente, ∫cos θ dθ / (π/8) en [0, 22.5°], hace que la
// superficie relajada quede en promedio en el ángulo físico, no ~3-8 % más
// empinada.
inline constexpr float kD8MeanSteepness = 0.9745f;

// Pendiente crítica de la roca relativa a la formación media (lith en [0,1],
// 0 = dura): multiplica a TerrainConfig::talus_slope.
[[nodiscard]] float rock_strength_factor(float lith);

// Tangente del ángulo de reposo del derrubio de una formación.
[[nodiscard]] float debris_repose_tan(float lith);

// Sobremuestreo bilineal de un grid cuadrado macro (grid_res²) al heightmap
// (res²). Vacío si el grid no existe.
[[nodiscard]] std::vector<float> upsample_bilinear(const std::vector<float>& grid,
                                                   int grid_res, int res);

// Relaja toda pendiente por encima de `crit_tan` (por celda) repartiendo el
// exceso entre los 8 vecinos con su distancia real (diagonal √2). Iterado
// hasta que lo movido en una pasada baja de `stable_eps_m` o se agotan las
// iteraciones (estabilidad acotada). Si `movable` no es nulo, cada celda solo
// puede ceder hasta su espesor movible (derrubio suelto: la roca de debajo no
// se relaja) y el espesor viaja con el material. Si `water` no es nulo, las
// celdas con lámina (no NaN) no reciben material. Determinista: cada pasada
// es Jacobi (lee el estado anterior, escribe deltas). Devuelve las pasadas.
int relax_slopes(Heightfield& hf, const std::vector<float>& crit_tan,
                 int max_iterations, float stable_eps_m,
                 std::vector<float>* movable = nullptr,
                 const std::vector<float>* water = nullptr);

struct RockfallParams {
    int iterations = 8;            // episodios de desprendimiento
    float detach_fraction = 0.5f;  // fracción del exceso de pared por episodio
    float max_detach_m = 2.0f;     // tope de retroceso por celda y episodio
    float boulder_fraction = 0.25f; // bloques que siguen la línea de energía
    float runout_mu = 0.625f;      // tan 32°: Fahrböschung de bloques sueltos
    int max_runout_cells = 250;    // ~600 m a 4096²
    float swash_m = 3.0f;          // franja de batida: el oleaje se lo lleva
    int relax_iterations = 24;     // relajación final del derrubio
};

struct RockfallStats {
    double detached_m3 = 0.0;
    double deposited_m3 = 0.0;
    double evacuated_m3 = 0.0;     // arrastrado por ríos, lagos u oleaje
    std::size_t source_cells = 0;  // celdas de pared activas en el 1er episodio
};

struct RockfallResult {
    std::vector<float> talus_m;    // espesor de derrubio depositado por celda
    RockfallStats stats;
};

// `crit_tan` y `repose_tan` por celda del heightmap; `water_level` con NaN
// donde no hay agua (Hydrology::water_level). Nunca baja una celda por debajo
// de la lámina de agua cercana (radio 4) ni deposita sobre agua: la red
// hidrológica ya calculada sigue siendo válida. Lo que cae a la franja de
// batida (< sea_level + swash_m) se lo lleva el mar.
[[nodiscard]] RockfallResult rockfall_talus(Heightfield& hf,
                                            const std::vector<float>& crit_tan,
                                            const std::vector<float>& repose_tan,
                                            const std::vector<float>& water_level,
                                            float sea_level,
                                            const RockfallParams& params = {});

} // namespace terrain
