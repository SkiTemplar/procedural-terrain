#pragma once

#include "terrain/generator.h"

#include <cstdint>
#include <vector>

namespace terrain {

// Geometría de la lámina interior (ríos + lagos) SIN OpenGL: la consumen la
// malla de agua del render, la máscara de recorte del mar y las métricas del
// dump. Una sola fuente de verdad: el mar se recorta exactamente donde esta
// lámina existe, así que nunca queda un hueco entre las dos superficies.
struct WaterSurface {
    int res = 0;
    // Altura del vértice por muestra (res²); NaN = la malla no lo usa.
    std::vector<float> vertex_y;
    // Quad (x, z) con esquinas (x..x+1, z..z+1), (res-1)²: 1 = la malla de
    // agua lo cubre (alguna esquina con lámina).
    std::vector<std::uint8_t> quad_covered;

    [[nodiscard]] std::size_t quad_index(int x, int z) const {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(res - 1)
             + static_cast<std::size_t>(x);
    }
};

// Hundido de la lámina bajo la orilla (el borde de la malla queda enterrado y
// la línea de agua la dibuja la intersección z-buffer contra el talud).
inline constexpr float kWaterSinkM = 0.12f;

[[nodiscard]] WaterSurface compute_water_surface(const TerrainData& data);

} // namespace terrain
