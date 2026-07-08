#pragma once

#include <cstdint>

namespace terrain {

// Parámetros del pipeline de generación. Pocos y con significado físico;
// todos tuneables desde el panel de ImGui.
struct TerrainConfig {
    std::uint32_t seed = 20260709; // reinicio de semilla (feedback 2026-07-08)

    // Dominio: 10×10 km con celda ~2,4 m (generación ~2 min en Debug,
    // asíncrona: la ventana abre al instante con overlay de progreso).
    float size_m = 10000.0f;
    int resolution = 4096;
    float sea_level = 0.0f;
    float sea_depth = 70.0f; // profundidad del fondo lejos de la costa

    // Continentalidad (dónde hay tierra)
    float island_radius = 0.55f; // fracción del semiancho donde empieza la costa
    float coast_warp_m = 700.0f; // distorsión de la línea de costa

    // Relieve: el LEM (uplift + stream power) genera la forma; mountain_height
    // es el objetivo de cumbre que calibra su ley de pendientes. base_height
    // es la amplitud de las colinillas de pradera (antes un 24 hardcodeado).
    float base_height_m = 24.0f;
    float mountain_height_m = 1400.0f;

    // Erosión hidráulica (gotas): con el LEM ya no esculpe la forma, solo
    // texturiza laderas y conos de deyección.
    int erosion_droplets = 400000;
    float erosion_strength = 0.30f; // erode rate
    float erosion_deposit = 0.30f;  // deposit rate
    float erosion_inertia = 0.10f;

    // Erosión térmica (pedreras, faldas al pie de la montaña). Moderada:
    // la agresiva (32 iter, 37°) planchaba el carácter del relieve original.
    // El talus debe quedar POR ENCIMA de la pendiente de cabecera del LEM
    // (~42°): si no, la térmica desmocha los paredones tipo Matterhorn.
    int thermal_iterations = 3;
    float talus_slope = 1.00f; // tan del ángulo de reposo (45°)

    // Costa: comprime las alturas bajas → playas anchas y planas. 25 m:
    // con 40 la rampa arena→hierba se veía como un degradado artificial.
    float coast_flatten_band_m = 25.0f;
    float coast_flatten_strength = 0.85f; // playas bien planas

    // Hidrología: pocos ríos pero grandes. El umbral está expresado en celdas
    // equivalentes de un mapa de 1024 (se normaliza por resolución real).
    float river_flow_threshold = 1200.0f; // más red tributaria visible
    float river_max_depth_m = 5.0f;
    float river_max_width_m = 20.0f; // los caudales grandes se notan de verdad

    // Erosión fluvial (stream power law, Braun & Willett 2013): talla el
    // perfil longitudinal de la red de drenaje → valles en V y cañones donde
    // coinciden caudal y pendiente, perfiles cóncavos sin caídas verticales.
    // UNA pasada: con varias, el esquema implícito re-propaga el nivel del
    // mar aguas arriba y convierte el río principal en un tajo a cota cero.
    float fluvial_strength = 0.5f;
    int fluvial_iterations = 1;

    // Detalle geométrico fino (solo malla cercana + suelo al caminar). Bajado
    // 1.0 -> 0.55 (feedback 2026-07-08: "picos de vértice no realistas en la
    // superficie"): a plena amplitud, en roca (rough 1.5) el término fino/grano
    // levantaba puntas de casi 2 m sobre celdas de 0.3 m del LOD cercano.
    float detail_amplitude_m = 0.55f;

    // Clasificación. La nieve sigue a las cumbres nuevas (~1.2-1.4 km):
    // con 480 m la mitad de la cordillera quedaba blanca.
    float snow_line_m = 950.0f;
    float rock_slope = 0.85f; // pendiente (tan) a partir de la cual es roca
    float beach_band_m = 12.0f; // la arena cubre la mayor parte de la zona plana

    // Costa expuesta (Agua v2 paso 8): rosa de vientos del oleaje REI/fetch.
    // wind_dir_deg = DE dónde sopla el dominante (meteorológico); wind_focus
    // 0 = mar isótropo, 1 = todo el oleaje viene del viento dominante.
    float wind_dir_deg = 315.0f;
    float wind_focus = 0.6f;
    float shore_width_scale = 1.0f; // escala global de anchura de orillas
};

} // namespace terrain
