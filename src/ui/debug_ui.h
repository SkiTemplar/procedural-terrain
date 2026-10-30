#pragma once

#include "terrain/terrain_config.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

struct GLFWwindow;

namespace render {
class Camera;
}
namespace player {
class Controller;
}

namespace ui {

class FieldMaps;

// Estado tuneable desde el panel de debug.
struct DebugState {
    bool wireframe = false;
    float sun_azimuth_deg = 135.0f;
    float sun_altitude_deg = 40.0f;
    float fog_density = 3.0e-5f; // bruma de valle: extinción a nivel del mar (1/m), H = 400 m
    float veg_draw_distance = 2600.0f; // m
    bool detail_textures = false; // estilo low-poly: sin texturas fotográficas
    float detail_texture_intensity = 0.75f;
    bool smooth_shading = false;  // estilo low-poly: facetas planas
    bool csm_shadows = true; // sombras del sol en tiempo real (cascadas)

    terrain::TerrainConfig pending_config; // lo que editan los sliders
    bool regenerate_requested = false;     // botón "Regenerar"
    float last_generation_seconds = 0.0f;

    // Generación en curso (hilo de trabajo): fase actual o nullptr si no hay
    // ninguna. Con fase activa se muestra el overlay y se bloquea "Regenerar".
    const char* generation_phase = nullptr;
    float generation_elapsed = 0.0f;

    // Diagnóstico del frustum culling (chunks dibujados / totales).
    int visible_chunks = 0;
    int total_chunks = 0;

    // Censo de vegetación por tipo (auditoría Fase B): pino, frondoso, roca,
    // mata, arbusto, tronco caído. Rellenado por main desde VegRenderer.
    size_t veg_counts[6] = {};

    // Presets JSON (C1): el panel edita el nombre y levanta peticiones; main
    // hace la E/S (guardar con el hash del mundo actual, cargar → regenerar).
    char preset_name[64] = "mi-mundo";
    bool save_preset_requested = false;
    std::string load_preset_name;         // no vacío = cargar este preset
    std::vector<std::string> preset_list; // ficheros del directorio presets/
    std::string preset_status;            // feedback de la última operación

    glm::vec3 sun_dir() const;
};

// Overlay ImGui: FPS, cámara, sol, niebla y parámetros de generación.
class DebugUi {
public:
    explicit DebugUi(GLFWwindow* window);
    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    // `fields` (opcional): minimapas de campos intermedios para la pestaña
    // "Campos" (C2); nullptr = pestaña ausente.
    void draw(DebugState& state, render::Camera& camera,
              player::Controller& controller, float dt,
              const FieldMaps* fields = nullptr);
};

} // namespace ui
