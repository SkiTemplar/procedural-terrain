#include "core/input.h"

#include <GLFW/glfw3.h>

#include <cstring>

namespace core {

Input::Input(GLFWwindow* window) : window_(window) {
    glfwSetWindowUserPointer(window_, this);
    glfwSetCursorPosCallback(window_, cursor_pos_callback);
}

void Input::cursor_pos_callback(GLFWwindow* window, double x, double y) {
    auto* self = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (!self) {
        return;
    }
    if (self->first_mouse_) {
        self->mouse_pos_ = {x, y};
        self->first_mouse_ = false;
        return;
    }
    self->mouse_accum_ += glm::dvec2{x, y} - self->mouse_pos_;
    self->mouse_pos_ = {x, y};
}

void Input::update() {
    std::memcpy(previous_, current_, sizeof(current_));
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        current_[key] = glfwGetKey(window_, key) == GLFW_PRESS;
    }
    mouse_delta_ = glm::vec2(mouse_accum_);
    mouse_accum_ = {0.0, 0.0};
}

bool Input::is_down(int key) const {
    return current_[key];
}

bool Input::was_pressed(int key) const {
    return current_[key] && !previous_[key];
}

} // namespace core
