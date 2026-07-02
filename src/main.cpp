#include "core/frame_clock.h"
#include "core/input.h"
#include "core/window.h"
#include "render/camera.h"
#include "render/shader.h"
#include "render/terrain_shaders.h"
#include "terrain/grid.h"
#include "ui/debug_ui.h"

#include <GLFW/glfw3.h>

#include <exception>
#include <iostream>

namespace {

constexpr float kGridSizeM = 2000.0f;
constexpr int kGridResolution = 256;
constexpr glm::vec3 kSkyColor{0.53f, 0.71f, 0.92f};

void run() {
    core::Window window(1600, 900, "Procedural Terrain");
    core::Input input(window.handle());
    core::FrameClock clock;

    render::Camera camera;
    render::Shader terrain_shader(render::kTerrainVert, render::kTerrainFrag);
    render::Mesh grid = terrain::make_grid(kGridSizeM, kGridResolution);

    ui::DebugUi debug_ui(window.handle());
    ui::DebugState debug_state;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    while (!window.should_close()) {
        clock.tick();
        input.update();

        if (input.was_pressed(GLFW_KEY_ESCAPE)) {
            window.set_mouse_captured(!window.is_mouse_captured());
        }
        if (window.is_mouse_captured()) {
            camera.update(input, clock.dt);
        }

        glClearColor(kSkyColor.r, kSkyColor.g, kSkyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glPolygonMode(GL_FRONT_AND_BACK, debug_state.wireframe ? GL_LINE : GL_FILL);

        terrain_shader.use();
        terrain_shader.set_mat4("u_view", camera.view());
        terrain_shader.set_mat4("u_proj", camera.proj(window.aspect()));
        terrain_shader.set_vec3("u_sun_dir", debug_state.sun_dir());
        terrain_shader.set_vec3("u_camera_pos", camera.position);
        terrain_shader.set_vec3("u_sky_color", kSkyColor);
        terrain_shader.set_float("u_fog_density", debug_state.fog_density);
        grid.draw();

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        debug_ui.draw(debug_state, camera, clock.dt);

        window.swap_and_poll();
    }
}

} // namespace

int main() {
    try {
        run();
    } catch (const std::exception& e) {
        std::cerr << "Error fatal: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
