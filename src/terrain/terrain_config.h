#pragma once

#include <cstdint>

namespace terrain {

// Parámetros del pipeline de generación. Pocos y con significado físico;
// todos tuneables desde el panel de ImGui.
struct TerrainConfig {
    std::uint32_t seed = 31416;

    // Dominio
    float size_m = 5000.0f; // lado de la isla
    int resolution = 2048;  // muestras por lado del heightmap (celda ~2,4 m)
    float sea_level = 0.0f;
    float sea_depth = 70.0f; // profundidad del fondo lejos de la costa

    // Continentalidad (dónde hay tierra)
    float island_radius = 0.55f; // fracción del semiancho donde empieza la costa
    float coast_warp_m = 700.0f; // distorsión de la línea de costa

    // Relieve
    float base_height_m = 70.0f;       // colinillas (fbm) — ni caos ni meseta
    // Montañas altas (mucha nieve); la máscara de ruido reparte los macizos
    // con laderas de varios km, sin bordes de placa.
    float mountain_height_m = 1300.0f;

    // Erosión hidráulica (gotas)
    int erosion_droplets = 600000;
    float erosion_strength = 0.30f; // erode rate
    float erosion_deposit = 0.30f;  // deposit rate
    float erosion_inertia = 0.10f;

    // Erosión térmica (pedreras, faldas al pie de la montaña). Las pasadas
    // altas son las que imponen el ángulo de reposo en las laderas grandes.
    int thermal_iterations = 32;
    float talus_slope = 0.75f; // tan del ángulo de reposo (~37°)

    // Costa: comprime las alturas bajas → playas anchas y planas. Banda
    // ancha: con 9 m la transición parecía un escalón de rasante.
    float coast_flatten_band_m = 40.0f;
    float coast_flatten_strength = 0.72f; // 0 = nada, 1 = playa totalmente plana

    // Hidrología: pocos ríos pero grandes. El umbral está expresado en celdas
    // equivalentes de un mapa de 1024 (se normaliza por resolución real).
    float river_flow_threshold = 1600.0f;
    float river_max_depth_m = 5.0f;
    float river_max_width_m = 14.0f; // radio máximo del cauce en metros

    // Erosión fluvial (stream power law, Braun & Willett 2013): talla el
    // perfil longitudinal de la red de drenaje → valles en V y cañones donde
    // coinciden caudal y pendiente, perfiles cóncavos sin caídas verticales.
    // UNA pasada: con varias, el esquema implícito re-propaga el nivel del
    // mar aguas arriba y convierte el río principal en un tajo a cota cero.
    float fluvial_strength = 0.5f;
    int fluvial_iterations = 1;

    // Detalle geométrico fino (solo malla cercana + suelo al caminar)
    float detail_amplitude_m = 1.0f;

    // Clasificación
    float snow_line_m = 560.0f;
    float rock_slope = 0.85f; // pendiente (tan) a partir de la cual es roca
    float beach_band_m = 7.0f;
};

} // namespace terrain
