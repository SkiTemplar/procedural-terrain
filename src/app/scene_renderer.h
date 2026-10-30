#pragma once

#include "render/shader.h"
#include "render/shadow_map.h"

#include <glad/glad.h>
#include <glm/vec3.hpp>

#include <vector>

namespace render {
class Camera;
class Mesh;
}
namespace ui {
struct DebugState;
}

namespace app {

struct World;

inline constexpr glm::vec3 kSkyColor{0.53f, 0.71f, 0.92f};

// Dibujo de un frame del mundo (cielo, terreno, vegetación, césped y agua)
// sobre el framebuffer ligado. Lo comparten el bucle interactivo y el modo
// fuera de pantalla (--shots / --record / --bench): mismas pasadas y mismos
// uniforms, así una captura offscreen es exactamente lo que se ve jugando.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // `time_s` alimenta viento del césped y oleaje (paso fijo en offscreen).
    // `max_rebuilds` = chunks de LOD que se pueden reconstruir en este frame.
    // `world` puede ser nullptr (solo cielo, mientras se genera).
    void draw(World* world, const render::Camera& camera, float aspect,
              ui::DebugState& state, float time_s, int max_rebuilds = 2);

private:
    render::Shader terrain_shader_;
    render::Shader veg_shader_;
    render::Shader grass_shader_;
    render::Shader water_shader_;
    render::Shader sky_shader_;
    // VAO vacío para el triángulo fullscreen del cielo (core profile exige un
    // VAO ligado aunque el shader no lea atributos: los vértices salen de
    // gl_VertexID).
    GLuint sky_vao_ = 0;
    // Sombras del sol en cascada: pase de profundidad por cascada antes del
    // terreno; terreno, vegetación y césped las reciben.
    render::Shader shadow_terrain_shader_;
    render::Shader shadow_veg_shader_;
    render::ShadowMap shadow_map_;
    render::ShadowCascades cascades_{};
    std::vector<const render::Mesh*> casters_; // reutilizado entre cascadas

    void render_shadows(World& world, const render::Camera& camera, float aspect,
                        const ui::DebugState& state);
    void set_shadow_uniforms(const render::Shader& shader, const render::Camera& camera,
                             bool enabled) const;
    void draw_sky(const render::Camera& camera, float aspect,
                  const ui::DebugState& state);
    void draw_water(World& world, const render::Camera& camera, float aspect,
                    const ui::DebugState& state, float time_s);
};

} // namespace app
