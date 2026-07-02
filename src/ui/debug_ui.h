#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

namespace render {
class Camera;
}

namespace ui {

// Estado tuneable desde el panel de debug.
struct DebugState {
    bool wireframe = false;
    float sun_azimuth_deg = 135.0f;
    float sun_altitude_deg = 40.0f;
    float fog_density = 0.00035f;

    glm::vec3 sun_dir() const;
};

// Overlay ImGui: FPS, cámara, sol, niebla, wireframe. RAII sobre el contexto.
class DebugUi {
public:
    explicit DebugUi(GLFWwindow* window);
    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    void draw(DebugState& state, render::Camera& camera, float dt);
};

} // namespace ui
