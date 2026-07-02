#pragma once

#include <string>

struct GLFWwindow;

namespace core {

// Ventana GLFW + contexto OpenGL 4.6 core. RAII: crea en ctor, destruye en dtor.
class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool should_close() const;
    void swap_and_poll();

    void set_mouse_captured(bool captured);
    bool is_mouse_captured() const { return mouse_captured_; }

    float aspect() const;
    GLFWwindow* handle() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
    bool mouse_captured_ = true;
};

} // namespace core
