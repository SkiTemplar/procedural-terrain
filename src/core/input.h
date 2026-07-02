#pragma once

#include <glm/vec2.hpp>

struct GLFWwindow;

namespace core {

// Estado de teclado/ratón por frame. update() toma el snapshot; los deltas de
// ratón llegan por callback y se consumen una vez por frame.
class Input {
public:
    explicit Input(GLFWwindow* window);

    // Llamar una vez por frame, después de glfwPollEvents.
    void update();

    bool is_down(int key) const;
    bool was_pressed(int key) const; // solo el frame en que baja

    glm::vec2 mouse_delta() const { return mouse_delta_; }

private:
    static void cursor_pos_callback(GLFWwindow* window, double x, double y);

    GLFWwindow* window_ = nullptr;
    bool current_[512] = {};
    bool previous_[512] = {};

    glm::dvec2 mouse_pos_{0.0};
    glm::dvec2 mouse_accum_{0.0};
    glm::vec2 mouse_delta_{0.0f};
    bool first_mouse_ = true;
};

} // namespace core
