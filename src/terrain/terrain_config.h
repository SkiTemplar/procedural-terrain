#pragma once

#include <cstdint>

namespace terrain {

// Parámetros del pipeline de generación. Pocos y con significado físico;
// todos tuneables desde el panel de ImGui.
struct TerrainConfig {
    std::uint32_t seed = 1337;

    // Dominio
    float size_m = 5000.0f; // lado de la isla
    int resolution = 1024;  // muestras por lado del heightmap
    float sea_level = 0.0f;
    float sea_depth = 70.0f; // profundidad del fondo lejos de la costa

    // Continentalidad (dónde hay tierra)
    float island_radius = 0.55f; // fracción del semiancho donde empieza la costa
    float coast_warp_m = 700.0f; // distorsión de la línea de costa

    // Relieve
    float base_height_m = 180.0f;      // colinas (fbm)
    float mountain_height_m = 1200.0f; // cordilleras (ridged)

    // Erosión hidráulica (gotas)
    int erosion_droplets = 200000;
    float erosion_strength = 0.30f; // erode rate
    float erosion_deposit = 0.30f;  // deposit rate
    float erosion_inertia = 0.10f;

    // Hidrología
    float river_flow_threshold = 220.0f; // acumulación mínima para ser río
    float river_max_depth_m = 6.0f;

    // Clasificación
    float snow_line_m = 900.0f;
    float rock_slope = 0.85f; // pendiente (tan) a partir de la cual es roca
    float beach_band_m = 5.0f;
};

} // namespace terrain
