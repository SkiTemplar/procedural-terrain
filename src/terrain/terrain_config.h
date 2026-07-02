#pragma once

#include <cstdint>

namespace terrain {

// Parámetros del pipeline de generación. Pocos y con significado físico;
// todos tuneables desde el panel de ImGui.
struct TerrainConfig {
    std::uint32_t seed = 1337;

    // Dominio
    float size_m = 5000.0f; // lado de la isla
    int resolution = 2048;  // muestras por lado del heightmap (celda ~2,4 m)
    float sea_level = 0.0f;
    float sea_depth = 70.0f; // profundidad del fondo lejos de la costa

    // Continentalidad (dónde hay tierra)
    float island_radius = 0.55f; // fracción del semiancho donde empieza la costa
    float coast_warp_m = 700.0f; // distorsión de la línea de costa

    // Relieve
    float base_height_m = 180.0f;      // colinas (fbm)
    float mountain_height_m = 1200.0f; // cordilleras (ridged)

    // Tectónica: las cordilleras siguen los bordes convergentes de placa.
    float tectonic_strength = 1.0f; // multiplicador del uplift
    float rift_depth_m = 45.0f;     // hundimiento en bordes divergentes
    float fault_depth_m = 5.0f;     // zanja de las fallas transformantes

    // Erosión hidráulica (gotas)
    int erosion_droplets = 600000;
    float erosion_strength = 0.30f; // erode rate
    float erosion_deposit = 0.30f;  // deposit rate
    float erosion_inertia = 0.10f;

    // Erosión térmica (pedreras, faldas al pie de la montaña)
    int thermal_iterations = 6;
    float talus_slope = 0.9f; // tan del ángulo de reposo

    // Costa: comprime las alturas bajas → playas anchas y planas.
    float coast_flatten_band_m = 9.0f;   // banda sobre el mar que se aplana
    float coast_flatten_strength = 0.65f; // 0 = nada, 1 = playa totalmente plana

    // Hidrología: pocos ríos pero grandes. El umbral está expresado en celdas
    // equivalentes de un mapa de 1024 (se normaliza por resolución real).
    float river_flow_threshold = 900.0f;
    float river_max_depth_m = 7.0f;
    float river_max_width_m = 14.0f; // radio máximo del cauce en metros

    // Detalle geométrico fino (solo malla cercana + suelo al caminar)
    float detail_amplitude_m = 1.4f;

    // Clasificación
    float snow_line_m = 900.0f;
    float rock_slope = 0.85f; // pendiente (tan) a partir de la cual es roca
    float beach_band_m = 7.0f;
};

} // namespace terrain
