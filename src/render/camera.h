#pragma once

#include <glm/glm.hpp>

namespace core {
class Input;
}

namespace render {

// Cámara primera persona. El movimiento de vuelo vive aquí; el modo caminar
// lo gestiona player::Controller usando solo update_look().
class Camera {
public:
    glm::vec3 position{0.0f, 120.0f, 0.0f};
    float yaw = -90.0f;   // grados; -90 mira hacia -Z
    float pitch = -15.0f; // grados
    float fly_speed = 60.0f; // m/s base; Shift multiplica

    void update_look(const core::Input& input);
    void update_fly(const core::Input& input, float dt); // incluye update_look

    glm::mat4 view() const;
    glm::mat4 proj(float aspect) const;
    glm::vec3 forward() const;

    static constexpr float kFovDeg = 70.0f;
    static constexpr float kNear = 0.5f;
    static constexpr float kFar = 20000.0f;
    static constexpr float kMouseSensitivity = 0.1f; // grados por píxel
    static constexpr float kBoostFactor = 5.0f;
};

} // namespace render
