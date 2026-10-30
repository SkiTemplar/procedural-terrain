#pragma once

#include "render/camera.h"
#include "terrain/detail.h"

namespace core {
class Input;
}

namespace player {

// Andar/volar. F alterna. Caminando: gravedad, suelo del DetailSampler (la
// misma superficie que dibuja la malla fina), Espacio salta, Shift alterna el
// sprint (toggle: una isla de 10 km no se recorre con la tecla pisada).
class Controller {
public:
    bool flying = true;
    bool sprinting = false;
    float walk_speed = 7.0f;   // m/s
    float sprint_factor = 2.0f;

    void update(render::Camera& camera, const core::Input& input,
                const terrain::DetailSampler& ground, float dt);

private:
    float vertical_velocity_ = 0.0f;
    bool grounded_ = false;

    static constexpr float kEyeHeight = 1.7f;
    static constexpr float kGravity = 22.0f;    // algo más que 9.8: mejor game-feel
    static constexpr float kJumpSpeed = 7.5f;
};

} // namespace player
