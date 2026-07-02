#pragma once

#include "terrain/terrain_config.h"

#include <glm/glm.hpp>

struct GLFWwindow;

namespace render {
class Camera;
}
namespace player {
class Controller;
}

namespace ui {

// Estado tuneable desde el panel de debug.
struct DebugState {
    bool wireframe = false;
    float sun_azimuth_deg = 135.0f;
    float sun_altitude_deg = 40.0f;
    float fog_density = 0.00015f;

    terrain::TerrainConfig pending_config; // lo que editan los sliders
    bool regenerate_requested = false;     // botón "Regenerar"
    float last_generation_seconds = 0.0f;

    glm::vec3 sun_dir() const;
};

// Overlay ImGui: FPS, cámara, sol, niebla y parámetros de generación.
class DebugUi {
public:
    explicit DebugUi(GLFWwindow* window);
    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    void draw(DebugState& state, render::Camera& camera,
              player::Controller& controller, float dt);
};

} // namespace ui
