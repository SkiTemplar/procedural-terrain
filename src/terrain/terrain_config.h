#pragma once

#include <cstdint>

namespace terrain {

// Parámetros del pipeline de generación. Pocos y con significado físico;
// todos tuneables desde el panel de ImGui.
struct TerrainConfig {
    std::uint32_t seed = 20260711; // reinicio de semilla (feedback 2026-07-10)

    // Dominio: 10×10 km con celda ~2,4 m (generación ~2 min en Debug,
    // asíncrona: la ventana abre al instante con overlay de progreso).
    float size_m = 10000.0f;
    int resolution = 4096;
    float sea_level = 0.0f;
    float sea_depth = 70.0f; // profundidad del fondo lejos de la costa

    // Continentalidad (N2, costa emergente): ya NO es la forma de la isla —
    // es el PRIOR DÉBIL de uplift (dónde puede haber tierra). La costa real
    // es el contorno h=0 del relieve que el LEM erosiona sobre un fondo
    // marino simulado; island_radius escala el prior, no el radio de un disco.
    float island_radius = 0.55f; // fracción del semiancho donde decae el prior
    float coast_warp_m = 700.0f; // distorsión del prior (bahías/penínsulas)
    // Transgresión marina (mecanismo real de las rías): el mar sube unos
    // metros sobre el nivel de base al que gradó la red fluvial del LEM y
    // AHOGA los valles bajos — bahías donde había valles, cabos en las
    // divisorias. 0 = costa exactamente en el nivel de base (sin rías).
    float coast_flood_m = 6.0f;

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
    // Subido 1200 -> 2000 (feedback 2026-07-08): menos riachuelos y que nazcan
    // más abajo (con más cuenca acumulada), no un río brotando en la cima.
    float river_flow_threshold = 2000.0f;
    float river_max_depth_m = 5.5f;
    // Subido 20 -> 30 (feedback 2026-07-08 "ríos grandes"): con menos
    // tributarios (umbral alto), el río principal cerca de la desembocadura
    // abre un cauce ancho y se lee como un río de verdad.
    float river_max_width_m = 30.0f;

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

    // Clima (ecología física). `snow_line_m`: cota de la nieve estacional de
    // la escena con precipitación media y exposición neutra (calibra la fecha
    // de la escena; la línea real sube en solana y sotavento y baja en umbría
    // y barlovento). Ya no es relativa a la cumbre (antes 0.60·cumbre).
    float snow_line_m = 700.0f;
    // Cota de la isoterma de 10 °C del mes cálido en una ladera neutra: fija
    // la temperatura a nivel del mar con el gradiente de 6.5 °C/km. 500 m es
    // una isla hiperoceánica de veranos frescos (Hébridas, Feroe: límite del
    // bosque natural a 300-500 m); el límite real varía alrededor con la
    // radiación, la exposición al viento y la lluvia.
    float treeline_m = 500.0f;
    float rock_slope = 0.85f; // tanβ crítica de retención de suelo (~40°)
    float beach_band_m = 12.0f; // la arena cubre la mayor parte de la zona plana

    // Costa expuesta (Agua v2 paso 8): rosa de vientos del oleaje REI/fetch.
    // wind_dir_deg = DE dónde sopla el dominante (meteorológico); wind_focus
    // 0 = mar isótropo, 1 = todo el oleaje viene del viento dominante.
    float wind_dir_deg = 315.0f;
    float wind_focus = 0.6f;
    float shore_width_scale = 1.0f; // escala global de anchura de orillas
};

} // namespace terrain
