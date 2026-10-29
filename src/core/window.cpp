#include "core/window.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace core {

namespace {

void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

} // namespace

Window::Window(int width, int height, const std::string& title, bool visible) {
    if (!glfwInit()) {
        throw std::runtime_error("GLFW: fallo en glfwInit");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    // Oculta = render fuera de pantalla (--shots/--record/--bench): la ventana
    // solo aporta el contexto GL, nunca se muestra ni toma el foco.
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, visible ? GLFW_TRUE : GLFW_FALSE);

    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("GLFW: no se pudo crear la ventana (¿driver GL 4.6?)");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(visible ? 1 : 0); // vsync solo en interactivo

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window_);
        glfwTerminate();
        throw std::runtime_error("GLAD: no se pudieron cargar las funciones de OpenGL");
    }

    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    if (visible) {
        set_mouse_captured(true);
    }
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
}

bool Window::should_close() const {
    return glfwWindowShouldClose(window_);
}

void Window::swap_and_poll() {
    glfwSwapBuffers(window_);
    glfwPollEvents();
}

void Window::set_mouse_captured(bool captured) {
    mouse_captured_ = captured;
    glfwSetInputMode(window_, GLFW_CURSOR,
                     captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured && glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
}

float Window::aspect() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
}

} // namespace core
