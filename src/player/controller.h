#pragma once

#include "render/camera.h"
#include "terrain/heightfield.h"

namespace core {
class Input;
}

namespace player {

// Andar/volar. F alterna. Caminando: gravedad, suelo por interpolación
// bilineal del heightfield, Espacio salta, Shift esprinta.
class Controller {
public:
    bool flying = true;
    float walk_speed = 5.0f;   // m/s
    float sprint_factor = 2.0f;

    void update(render::Camera& camera, const core::Input& input,
                const terrain::Heightfield& ground, float dt);

private:
    float vertical_velocity_ = 0.0f;
    bool grounded_ = false;

    static constexpr float kEyeHeight = 1.7f;
    static constexpr float kGravity = 22.0f;    // algo más que 9.8: mejor game-feel
    static constexpr float kJumpSpeed = 7.5f;
};

} // namespace player
