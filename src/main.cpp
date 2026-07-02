#include "core/frame_clock.h"
#include "core/input.h"
#include "core/window.h"
#include "player/controller.h"
#include "render/camera.h"
#include "render/shader.h"
#include "render/terrain_shaders.h"
#include "terrain/generator.h"
#include "terrain/mesher.h"
#include "ui/debug_ui.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <exception>
#include <iostream>

namespace {

constexpr glm::vec3 kSkyColor{0.53f, 0.71f, 0.92f};

struct World {
    terrain::TerrainData data;
    terrain::TerrainMeshes meshes;
    render::Mesh sea;

    explicit World(const terrain::TerrainConfig& cfg)
        : data(terrain::generate(cfg)),
          meshes(terrain::build_meshes(data)),
          sea(terrain::build_sea_plane(cfg.size_m, cfg.sea_level)) {}
};

void set_common_uniforms(const render::Shader& shader, const render::Camera& camera,
                         float aspect, const ui::DebugState& state) {
    shader.set_mat4("u_view", camera.view());
    shader.set_mat4("u_proj", camera.proj(aspect));
    shader.set_vec3("u_sun_dir", state.sun_dir());
    shader.set_vec3("u_camera_pos", camera.position);
    shader.set_vec3("u_sky_color", kSkyColor);
    shader.set_float("u_fog_density", state.fog_density);
}

void run() {
    core::Window window(1600, 900, "Procedural Terrain");
    core::Input input(window.handle());
    core::FrameClock clock;

    render::Camera camera;
    player::Controller controller;
    render::Shader terrain_shader(render::kTerrainVert, render::kTerrainFrag);
    render::Shader water_shader(render::kWaterVert, render::kWaterFrag);

    ui::DebugUi debug_ui(window.handle());
    ui::DebugState state;

    const auto generate_world = [&state]() {
        const auto start = std::chrono::steady_clock::now();
        World world(state.pending_config);
        state.last_generation_seconds =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        std::cout << "Terreno generado en " << state.last_generation_seconds << " s\n";
        return world;
    };

    World world = generate_world();

    // Spawn: volando sobre la costa sur mirando al centro de la isla.
    camera.position = {0.0f, 350.0f, world.data.config.size_m * 0.45f};
    camera.yaw = -90.0f;
    camera.pitch = -12.0f;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    while (!window.should_close()) {
        clock.tick();
        input.update();

        if (input.was_pressed(GLFW_KEY_ESCAPE)) {
            window.set_mouse_captured(!window.is_mouse_captured());
        }
        if (window.is_mouse_captured()) {
            controller.update(camera, input, world.data.height, clock.dt);
        }

        if (state.regenerate_requested) {
            state.regenerate_requested = false;
            world = generate_world();
            camera.position.y =
                world.data.height.sample(camera.position.x, camera.position.z) + 350.0f;
            controller.flying = true;
        }

        glClearColor(kSkyColor.r, kSkyColor.g, kSkyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, state.wireframe ? GL_LINE : GL_FILL);

        terrain_shader.use();
        set_common_uniforms(terrain_shader, camera, window.aspect(), state);
        for (const auto& chunk : world.meshes.ground_chunks) {
            chunk.draw();
        }

        // Agua después del terreno, con blending.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        water_shader.use();
        set_common_uniforms(water_shader, camera, window.aspect(), state);
        water_shader.set_float("u_time", clock.total);
        world.sea.draw();
        for (const auto& chunk : world.meshes.water_chunks) {
            chunk.draw();
        }
        glDisable(GL_BLEND);

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        debug_ui.draw(state, camera, controller, clock.dt);

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
