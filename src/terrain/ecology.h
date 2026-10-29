#pragma once

#include "terrain/generator.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace terrain {

// Ecología y suelo desde campos físicos (auditoría 2026-10, relieve #3; julio
// #18, #24, #25). Bosque, pradera, roca desnuda y nieve no los decide un fbm:
// salen de temperatura (cota con gradiente ambiental), precipitación
// orográfica del LEM, humedad topográfica (TWI), radiación horneada y espesor
// de suelo (producción de Heimsath frente a la denudación del LEM). El ruido
// solo perturba los umbrales (≤ 10 %).

inline constexpr float kLapseRateCPerM = 0.0065f;  // gradiente ambiental estándar
// Límite del bosque: isoterma de ~10 °C de media del mes cálido (Köppen;
// Körner & Paulsen 2004 lo sitúan en ~6.4 °C de estación de crecimiento).
inline constexpr float kTreelineIsothermC = 10.0f;
// Precipitación anual media en tierra que representa la lluvia relativa 1 del
// LEM (clima oceánico de fachada atlántica).
inline constexpr float kMeanPrecipMm = 2000.0f;

// Entradas físicas de una celda (todas en unidades declaradas).
struct EcoInputs {
    float height_m = 0.0f;    // cota sobre el nivel del mar
    float slope = 0.0f;       // tanβ
    float precip = 1.0f;      // precipitación relativa (media 1 en tierra)
    float twi = 0.0f;         // ln(a / tanβ), a en m²/m
    float radiation = 1.0f;   // radiación relativa (1 = llano abierto al sol)
    float windward = 0.0f;    // [0,1] cara expuesta al viento dominante
    float convexity = 0.0f;   // −∇²z·L (adimensional, > 0 convexo)
    float denudation = 0.0f;  // denudación del LEM / uplift máximo por paso
    float alluvium_m = 0.0f;  // espesor de aluvión del LEM
    float riparian = 0.0f;    // 1 = junto a agua dulce (freático alto)
    float jitter = 0.0f;      // [-1,1] perturbación de microclima (≤ 10 %)
    float edge = 0.0f;        // [-1,1] perturbación del borde de bosque
};

// Clima de referencia del mundo, derivado de la config.
struct EcoClimate {
    float sea_temp_c = 15.0f;     // T media del mes cálido a nivel del mar
    float season_shift_c = 5.0f;  // la escena es anterior al mes cálido
    float rock_slope = 0.85f;     // tanβ crítica de retención de suelo
};

struct EcoCell {
    float tree_temp_c = 0.0f;    // T del mes cálido con microclima del árbol
    float soil_m = 0.0f;         // espesor de suelo (regolito + aluvión)
    float forest = 0.0f;         // [0,1] idoneidad/densidad de bosque
    float snow_margin_c = 0.0f;  // > 0: la nieve estacional persiste
    float dryness = 0.0f;        // [0,1] déficit hídrico del suelo
};

// Espesor mínimo de suelo para que haya cubierta vegetal (por debajo, roca).
inline constexpr float kMinSoilM = 0.12f;

[[nodiscard]] EcoClimate make_eco_climate(const TerrainConfig& cfg) noexcept;

// Temperatura media de verano en la línea de equilibrio en función de la
// precipitación anual (Ohmura et al. 1992): P = 645 + 296·T + 9·T².
[[nodiscard]] float ela_summer_temp_c(float precip_mm) noexcept;

[[nodiscard]] EcoCell evaluate_ecology(const EcoInputs& in,
                                       const EcoClimate& climate) noexcept;

// Precipitación orográfica relativa sobre el relieve final (grid gres²,
// media 1 en tierra). Misma física que la del LEM — la masa de aire avanza
// con el viento, se recarga sobre el mar y descarga al ascender — pero con
// escalas en metros, independientes de la celda: lavado de fondo de ~25 km y
// condensación e-plegada por 1.5 km de ascenso. La `rain_w` del LEM descarga
// un 5 % POR CELDA (agota la humedad en el primer km de costa a macro 1024),
// así que no sirve como clima. Fondo frontal del 35 % de la media y deriva de
// hidrometeoros de ~250 m (Smith & Barstad 2004). Sin viento → todo 1.
[[nodiscard]] std::vector<float> orographic_precipitation(const Heightfield& hf,
                                                          float sea_level,
                                                          glm::vec2 wind,
                                                          int gres);

// Índice topográfico de humedad (Beven & Kirkby 1979) sobre la malla fina con
// el área drenada específica del D8 de la hidrología, suavizado ~12 m (la
// humedad del suelo se reparte lateralmente; el D8 puro deja rayas de una
// celda en cada vaguada).
[[nodiscard]] std::vector<float> topographic_wetness(const Heightfield& hf,
                                                     const std::vector<float>& flow);

// Lee los campos físicos de un TerrainData en cada celda de la malla fina.
// Requiere hidrología, LemStats y la luz horneada ya calculadas.
class EcoProbe {
public:
    EcoProbe(const TerrainData& data, std::vector<float> twi);

    // Sin jitter ni ribera (los pone quien clasifica).
    [[nodiscard]] EcoInputs inputs_at(int x, int z) const;
    [[nodiscard]] glm::vec3 hillslope_normal_at(int x, int z) const;
    [[nodiscard]] float twi_at(std::size_t i) const { return twi_[i]; }
    // Componente horizontal de la normal hacia el viento / el sol (con signo):
    // wind_facing < 0 = cara de barlovento; sun_facing > 0 = solana.
    [[nodiscard]] float wind_facing(const glm::vec3& n) const;
    [[nodiscard]] float sun_facing(const glm::vec3& n) const;

private:
    [[nodiscard]] float grid_at(const std::vector<float>& grid, int gres, int x,
                                int z, float fallback) const;

    [[nodiscard]] float convexity_at(int x, int z) const;


    const TerrainData* data_;
    std::vector<float> twi_;
    std::vector<float> denudation_;  // denudación del LEM suavizada (~20 m)
    glm::vec3 sun_{0.0f, 1.0f, 0.0f};
    float sun_sin_alt_ = 1.0f;
};

// Métricas de ecología para el dump (antes/después de cada cambio).
struct EcologyReport {
    std::array<double, kMaterialCount> land_pct{};  // % de la tierra emergida
    std::array<float, 3> precip_q{};      // p10/p50/p90 en tierra (ecología)
    std::array<float, 3> lem_rain_q{};    // la rain_w exportada del LEM
    std::array<float, 3> twi_q{};
    std::array<float, 3> denudation_q{};
    double spearman_forest_twi = 0.0;     // sobre tierra con hierba/bosque
    double spearman_forest_precip = 0.0;
    // Igual, pero solo bajo el piso térmico del bosque (cota < 0.6·isoterma):
    // aísla el efecto del agua del de la temperatura.
    double spearman_forest_precip_low = 0.0;
    // Límite del bosque: cota más alta (bandas de 25 m) cuya cobertura de
    // bosque llega al 25 % de la máxima del grupo;
    // y p95 de la cota de las celdas de bosque, por grupo de exposición.
    struct Group {
        float treeline_m = 0.0f;
        float forest_p95_m = 0.0f;
        float snow_p10_m = 0.0f;  // cota baja de la nieve
        std::size_t cells = 0;
    };
    Group windward, leeward, sunny, shady;
    double rock_convex_pct = 0.0;   // roca en convexas a pendiente 0.3-0.6
    double rock_concave_pct = 0.0;  // roca en cóncavas, misma pendiente
};

[[nodiscard]] EcologyReport ecology_report(const TerrainData& data);

} // namespace terrain
