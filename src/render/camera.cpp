#include "render/camera.h"

#include "core/input.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace render {

void Camera::update_look(const core::Input& input) {
    const glm::vec2 mouse = input.mouse_delta();
    yaw += mouse.x * kMouseSensitivity;
    pitch = std::clamp(pitch - mouse.y * kMouseSensitivity, -89.0f, 89.0f);
}

void Camera::update_fly(const core::Input& input, float dt) {
    update_look(input);

    const glm::vec3 fwd = forward();
    const glm::vec3 right = glm::normalize(glm::cross(fwd, {0.0f, 1.0f, 0.0f}));

    glm::vec3 move{0.0f};
    if (input.is_down(GLFW_KEY_W)) move += fwd;
    if (input.is_down(GLFW_KEY_S)) move -= fwd;
    if (input.is_down(GLFW_KEY_D)) move += right;
    if (input.is_down(GLFW_KEY_A)) move -= right;
    if (input.is_down(GLFW_KEY_SPACE)) move.y += 1.0f;
    if (input.is_down(GLFW_KEY_LEFT_CONTROL)) move.y -= 1.0f;

    if (glm::dot(move, move) > 0.0f) {
        float speed = fly_speed;
        if (input.is_down(GLFW_KEY_LEFT_SHIFT)) {
            speed *= kBoostFactor;
        }
        position += glm::normalize(move) * speed * dt;
    }
}

glm::vec3 Camera::forward() const {
    const float cy = glm::cos(glm::radians(yaw));
    const float sy = glm::sin(glm::radians(yaw));
    const float cp = glm::cos(glm::radians(pitch));
    const float sp = glm::sin(glm::radians(pitch));
    return glm::normalize(glm::vec3{cy * cp, sp, sy * cp});
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position, position + forward(), {0.0f, 1.0f, 0.0f});
}

glm::mat4 Camera::proj(float aspect) const {
    return glm::perspective(glm::radians(kFovDeg), aspect, kNear, kFar);
}

} // namespace render
