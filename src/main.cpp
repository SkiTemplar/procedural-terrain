#include "core/frame_clock.h"
#include "core/input.h"
#include "core/window.h"
#include "player/controller.h"
#include "render/camera.h"
#include "render/shader.h"
#include "render/terrain_shaders.h"
#include "terrain/chunked_terrain.h"
#include "terrain/generator.h"
#include "terrain/mesher.h"
#include "ui/debug_ui.h"
#include "veg/veg_renderer.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>

namespace {

constexpr glm::vec3 kSkyColor{0.53f, 0.71f, 0.92f};

// En heap y no-movible: ChunkedTerrain y DetailSampler guardan punteros a data.
struct World {
    terrain::TerrainData data;
    terrain::DetailSampler sampler;
    terrain::ChunkedTerrain ground;
    std::optional<render::Mesh> water;
    render::Mesh sea;
    veg::VegRenderer vegetation;

    explicit World(const terrain::TerrainConfig& cfg)
        : data(terrain::generate(cfg)),
          sampler(data),
          ground(data, sampler),
          water(terrain::build_water_mesh(data)),
          sea(terrain::build_sea_plane(cfg.size_m, cfg.sea_level)),
          vegetation(data, sampler) {}

    World(const World&) = delete;
    World& operator=(const World&) = delete;
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
    render::Shader veg_shader(render::kVegVert, render::kTerrainFrag);
    render::Shader water_shader(render::kWaterVert, render::kWaterFrag);

    ui::DebugUi debug_ui(window.handle());
    ui::DebugState state;

    const auto generate_world = [&state]() {
        const auto start = std::chrono::steady_clock::now();
        auto world = std::make_unique<World>(state.pending_config);
        state.last_generation_seconds =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();

        // Informe numérico: permite juzgar el carácter del mundo sin volar.
        const auto& data = world->data;
        float max_h = -1e9f;
        size_t counts[8] = {};
        for (size_t i = 0; i < data.material.size(); ++i) {
            max_h = std::max(max_h, data.height.data()[i]);
            counts[static_cast<size_t>(data.material[i])]++;
        }
        const float total = static_cast<float>(data.material.size());
        std::cout << "Terreno generado en " << state.last_generation_seconds
                  << " s | alt max " << static_cast<int>(max_h) << " m | mar "
                  << static_cast<int>(100.0f * counts[0] / total) << "% hierba "
                  << static_cast<int>(100.0f * counts[2] / total) << "% bosque "
                  << static_cast<int>(100.0f * counts[3] / total) << "% roca "
                  << static_cast<int>(100.0f * counts[4] / total) << "% nieve "
                  << static_cast<int>(100.0f * counts[5] / total) << "%\n";
        return world;
    };

    std::unique_ptr<World> world = generate_world();

    // Spawn: volando sobre la costa sur mirando al centro de la isla.
    camera.position = {0.0f, 350.0f, world->data.config.size_m * 0.45f};
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
            controller.update(camera, input, world->sampler, clock.dt);
        }

        if (state.regenerate_requested) {
            state.regenerate_requested = false;
            world = generate_world();
            camera.position.y =
                world->data.height.sample(camera.position.x, camera.position.z) + 350.0f;
            controller.flying = true;
        }

        world->ground.update(camera.position);

        glClearColor(kSkyColor.r, kSkyColor.g, kSkyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, state.wireframe ? GL_LINE : GL_FILL);

        terrain_shader.use();
        set_common_uniforms(terrain_shader, camera, window.aspect(), state);
        terrain_shader.set_float("u_smooth_blend", 1.0f);
        for (const render::Mesh* chunk : world->ground.visible_meshes()) {
            chunk->draw();
        }

        veg_shader.use();
        set_common_uniforms(veg_shader, camera, window.aspect(), state);
        veg_shader.set_float("u_smooth_blend", 0.0f);
        world->vegetation.draw(camera.position, state.veg_draw_distance);

        // Agua después del terreno, con blending.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        water_shader.use();
        set_common_uniforms(water_shader, camera, window.aspect(), state);
        water_shader.set_float("u_time", clock.total);
        world->sea.draw();
        if (world->water) {
            world->water->draw();
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
