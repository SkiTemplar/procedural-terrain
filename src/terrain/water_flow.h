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
// R ≈ calado (cauce ancho) y S la pendiente de la LÁMINA a lo largo del eje.
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
inline constexpr float kManningGravel = 0.035f; // lecho de grava (s/m^(1/3))
inline constexpr float kManningRapids = 0.045f; // rápidos y cascadas
inline constexpr float kMaxRiverSpeed = 5.0f;   // m/s, techo de crecida

[[nodiscard]] RiverFlowField compute_river_flow(const TerrainData& data);

} // namespace terrain
