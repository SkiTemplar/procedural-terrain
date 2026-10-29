#pragma once

#include "terrain/generator.h"

#include <array>
#include <cstddef>

namespace terrain {

// Métricas de laderas y pedreras sobre el mundo generado (para el dump y los
// tests). Solo lee TerrainData: no conoce el pipeline que lo produjo.
//
// Pendiente = |∇h| por diferencias centrales (la misma que usa la
// clasificación de materiales). "Tierra" = celda emergida y sin lámina de
// agua. Litología por terciles del campo macro (0 = dura … 1 = blanda).
// Canchal = Material::Gravel fuera de la banda litoral (wave_energy == 0):
// la grava de costa no cuenta como pedrera.
struct LandformStats {
    // Histograma de pendiente de tierra en grados, cortes en kSlopeEdgesDeg.
    static constexpr std::size_t kSlopeBins = 10;
    static constexpr std::array<float, kSlopeBins - 1> kSlopeEdgesDeg = {
        10.0f, 20.0f, 30.0f, 35.0f, 40.0f, 45.0f, 50.0f, 55.0f, 60.0f};
    std::array<std::size_t, kSlopeBins> slope_bins{};
    std::size_t land_cells = 0;

    // Pendiente > 45° en la franja costera (< kCoastBandM sobre el mar, donde
    // el acantilado de oleaje es legítimo) frente al interior.
    static constexpr float kCoastBandM = 60.0f;
    std::size_t steep45_coast = 0;
    std::size_t steep45_inland = 0;

    // Por formación (0 = dura, 1 = media, 2 = blanda): celdas, > 45°, > 55°
    // y percentil 90 de la pendiente (tan).
    std::array<std::size_t, 3> litho_cells{};
    std::array<std::size_t, 3> litho_steep45{};
    std::array<std::size_t, 3> litho_steep55{};
    std::array<float, 3> litho_p90_tan{};

    // Pedreras: celdas de canchal, su moda de pendiente (tan, bins de 0.05)
    // y cobertura del pie de pared: celdas recorridas al bajar por máxima
    // pendiente ≤ kFootRunM desde una pared (tan > 1.0) con tan < 0.85 —
    // fracción de ellas que es canchal (objetivo de la auditoría > 40 %).
    static constexpr float kFootRunM = 50.0f;
    std::size_t scree_cells = 0;
    float scree_area_km2 = 0.0f;
    float scree_slope_mode_tan = 0.0f;
    std::size_t wall_foot_cells = 0;
    std::size_t wall_foot_scree = 0;
    // Histograma de pendiente del canchal (tan, bins de 0.05 desde 0) y su
    // perfil: cóncavo si la pendiente baja al pasar a la celda de aguas abajo
    // (> kProfileEpsTan), convexo si sube. Un canchal real es cóncavo: los
    // bloques grandes ruedan más lejos y tienden la base.
    static constexpr std::size_t kScreeSlopeBins = 30;
    static constexpr float kScreeSlopeBinTan = 0.05f;
    static constexpr float kProfileEpsTan = 0.02f;
    std::array<std::size_t, kScreeSlopeBins> scree_slope_bins{};
    std::size_t scree_concave = 0;
    std::size_t scree_convex = 0;
    // Celda de canchal con más canchal alrededor (radio kHotspotRadius): el
    // mejor sitio para mirar una pedrera in-game. -1 si no hay canchal.
    static constexpr int kHotspotRadius = 8;
    int scree_hotspot_x = -1;
    int scree_hotspot_z = -1;
};

[[nodiscard]] LandformStats compute_landform_stats(const TerrainData& data);

} // namespace terrain
