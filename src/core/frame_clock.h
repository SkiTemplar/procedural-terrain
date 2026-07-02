#pragma once

#include <GLFW/glfw3.h>

namespace core {

// Delta de tiempo por frame, clampeado para que un stall (resize, breakpoint)
// no dispare la física/cámara.
struct FrameClock {
    float dt = 0.0f;
    float total = 0.0f;

    void tick() {
        const auto now = static_cast<float>(glfwGetTime());
        dt = now - last_;
        if (dt > 0.1f) {
            dt = 0.1f;
        }
        last_ = now;
        total = now;
    }

private:
    float last_ = 0.0f;
};

} // namespace core
