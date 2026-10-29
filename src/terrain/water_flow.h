#pragma once

#include "terrain/generator.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <limits>
#include <vector>

namespace terrain {

// Campo de corriente de los ríos (C4-W3, auditoría 2026-10 #1). La dirección
// sigue el EJE del cauce: la receptora D8 de cada celda de eje (caudal ≥
// umbral), suavizada a lo largo del propio río para borrar los quiebros de
// 45° de la rejilla, y propagada a la anchura mojada desde el eje más
// cercano. El módulo es la velocidad de Manning v = n⁻¹·R^(2/3)·S^(1/2), con
// R ≈ calado (cauce ancho), S la pendiente de la LÁMINA medida sobre un tramo
// de eje de kSlopeReachM (no celda a celda, que mete las cascadas) y n la
// rugosidad de Jarrett (1984) para cauces de montaña, con suelo de grava de
// llanura: con n constante los torrentes iban todos al techo de velocidad.
// Lagos: v = 0 (lámina de equilibrio). El mar no forma parte del campo.
struct RiverFlowField {
    static constexpr std::uint32_t kDry = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> wet_id; // por muestra: índice en `velocity` o kDry
    std::vector<glm::vec2> velocity;   // m/s en el plano (x, z) del mundo

    [[nodiscard]] glm::vec2 at(std::size_t i) const {
        const std::uint32_t k = wet_id[i];
        return k == kDry ? glm::vec2{0.0f} : velocity[k];
    }
};

// Constantes físicas expuestas para los tests y el dump.
inline constexpr float kManningGravel = 0.035f; // suelo: lecho de grava de llanura
inline constexpr float kManningRapids = 0.045f; // suelo en rápidos y cascadas
inline constexpr float kMaxRiverSpeed = 5.0f;   // m/s, techo de seguridad
inline constexpr float kSlopeReachM = 50.0f;    // tramo de eje para S

// Rugosidad de Manning de un cauce natural: Jarrett (1984), ajustada en ríos
// de montaña de Colorado, n = 0.39·S^0.38·R^-0.16 (S pendiente de la lámina,
// R radio hidráulico en m), con suelo `floor_n`: por debajo de S ≈ 0.0015 la
// ley de Jarrett baja del valor de un lecho de grava y manda el suelo.
[[nodiscard]] float manning_n(float slope, float hydraulic_radius_m,
                              float floor_n = kManningGravel);

// Velocidad media de Manning v = R^(2/3)·S^(1/2)/n con la n de manning_n,
// acotada por kMaxRiverSpeed.
[[nodiscard]] float manning_speed(float slope, float depth_m,
                                  float floor_n = kManningGravel);

[[nodiscard]] RiverFlowField compute_river_flow(const TerrainData& data);

} // namespace terrain
