#include "player/controller.h"

#include "core/input.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace player {

void Controller::update(render::Camera& camera, const core::Input& input,
                        const terrain::DetailSampler& ground, float dt) {
    if (input.was_pressed(GLFW_KEY_F)) {
        flying = !flying;
        vertical_velocity_ = 0.0f;
    }

    if (flying) {
        camera.update_fly(input, dt);
        return;
    }

    camera.update_look(input);

    if (input.was_pressed(GLFW_KEY_LEFT_SHIFT)) {
        sprinting = !sprinting;
    }

    // Dirección de marcha en el plano XZ (el pitch no influye al andar).
    const glm::vec3 fwd3 = camera.forward();
    glm::vec3 fwd{fwd3.x, 0.0f, fwd3.z};
    if (glm::dot(fwd, fwd) < 1e-6f) {
        fwd = {0.0f, 0.0f, -1.0f};
    }
    fwd = glm::normalize(fwd);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, {0.0f, 1.0f, 0.0f}));

    glm::vec3 move{0.0f};
    if (input.is_down(GLFW_KEY_W)) move += fwd;
    if (input.is_down(GLFW_KEY_S)) move -= fwd;
    if (input.is_down(GLFW_KEY_D)) move += right;
    if (input.is_down(GLFW_KEY_A)) move -= right;

    if (glm::dot(move, move) > 0.0f) {
        const float speed = sprinting ? walk_speed * sprint_factor : walk_speed;
        camera.position += glm::normalize(move) * speed * dt;
    }

    if (grounded_ && input.was_pressed(GLFW_KEY_SPACE)) {
        vertical_velocity_ = kJumpSpeed;
        grounded_ = false;
    }

    vertical_velocity_ -= kGravity * dt;
    camera.position.y += vertical_velocity_ * dt;

    const float floor_y = ground.height(camera.position.x, camera.position.z) + kEyeHeight;
    if (camera.position.y <= floor_y) {
        camera.position.y = floor_y;
        vertical_velocity_ = 0.0f;
        grounded_ = true;
    } else {
        grounded_ = false;
    }
}

} // namespace player
