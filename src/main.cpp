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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

constexpr glm::vec3 kSkyColor{0.53f, 0.71f, 0.92f};

// Pose inicial de cámara por CLI: permite al lazo de verificación visual
// (capturas automatizadas) encuadrar costas o ríos concretos sin volar.
struct CameraPose {
    float x;
    float z;
    float yaw;
    float pitch;
    float alt = 180.0f; // metros sobre el terreno en (x, z)
};

// En heap y no-movible: ChunkedTerrain y DetailSampler guardan punteros a data.
// Recibe la TerrainData YA generada (el generate corre en un hilo de trabajo);
// aquí solo queda el trabajo con GL, que debe vivir en el hilo principal.
struct World {
    terrain::TerrainData data;
    terrain::DetailSampler sampler;
    terrain::ChunkedTerrain ground;
    std::optional<render::Mesh> water;
    render::Mesh sea;
    veg::VegRenderer vegetation;

    explicit World(terrain::TerrainData&& ready)
        : data(std::move(ready)),
          sampler(data),
          ground(data, sampler),
          water(terrain::build_water_mesh(data)),
          sea(terrain::build_sea_plane(data.config.size_m, data.config.sea_level)),
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

void run(std::optional<std::uint32_t> seed_override,
         std::optional<CameraPose> pose_override) {
    // Declarados ANTES que window a propósito: al cerrar durante una
    // generación, el destructor del future bloquea hasta que el worker
    // termina — con este orden la ventana (GL) muere al instante y el
    // proceso remata la generación en background antes de salir.
    std::future<terrain::TerrainData> baking;
    std::atomic<const char*> bake_phase{nullptr};
    std::chrono::steady_clock::time_point bake_start{};

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
    if (seed_override) {
        state.pending_config.seed = *seed_override;
    }

    // Generación asíncrona: el generate() (solo CPU) corre en un hilo de
    // trabajo y la ventana sigue viva con un overlay de progreso. El montaje
    // GL (mallas, agua, vegetación) se hace en el hilo principal al recoger
    // el resultado. En regeneración, el mundo viejo sigue dibujándose.
    std::unique_ptr<World> world;

    const auto start_baking = [&]() {
        bake_start = std::chrono::steady_clock::now();
        bake_phase.store("arrancando");
        const terrain::TerrainConfig cfg = state.pending_config;
        baking = std::async(std::launch::async, [cfg, &bake_phase]() {
            const auto t0 = std::chrono::steady_clock::now();
            terrain::TerrainData data = terrain::generate(
                cfg, [&bake_phase](const char* phase) { bake_phase.store(phase); });
            const float secs = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - t0).count();

            // Informe numérico: permite juzgar el carácter del mundo sin volar.
            float max_h = -1e9f;
            size_t counts[8] = {};
            for (size_t i = 0; i < data.material.size(); ++i) {
                max_h = std::max(max_h, data.height.data()[i]);
                counts[static_cast<size_t>(data.material[i])]++;
            }
            const float total = static_cast<float>(data.material.size());
            std::cout << "Terreno generado en " << secs
                      << " s | alt max " << static_cast<int>(max_h) << " m | mar "
                      << static_cast<int>(100.0f * counts[0] / total) << "% hierba "
                      << static_cast<int>(100.0f * counts[2] / total) << "% bosque "
                      << static_cast<int>(100.0f * counts[3] / total) << "% roca "
                      << static_cast<int>(100.0f * counts[4] / total) << "% nieve "
                      << static_cast<int>(100.0f * counts[5] / total) << "%"
                      << std::endl; // flush: legible aunque stdout vaya a fichero
            return data;
        });
    };

    start_baking();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    while (!window.should_close()) {
        clock.tick();
        input.update();

        if (input.was_pressed(GLFW_KEY_ESCAPE)) {
            window.set_mouse_captured(!window.is_mouse_captured());
        }
        if (window.is_mouse_captured() && world) {
            controller.update(camera, input, world->sampler, clock.dt);
        }

        if (state.regenerate_requested) {
            state.regenerate_requested = false;
            if (!baking.valid()) {
                start_baking();
            }
        }

        // ¿Mundo recién cocido? Montaje GL y colocación de cámara.
        if (baking.valid()
            && baking.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const bool first_world = (world == nullptr);
            world = std::make_unique<World>(baking.get());
            baking = {};
            bake_phase.store(nullptr);
            state.last_generation_seconds = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - bake_start).count();
            if (first_world && pose_override) {
                const CameraPose& p = *pose_override;
                const float ground = std::max(
                    world->data.height.sample(p.x, p.z),
                    world->data.config.sea_level);
                camera.position = {p.x, ground + p.alt, p.z};
                camera.yaw = p.yaw;
                camera.pitch = p.pitch;
            } else if (first_world) {
                // Spawn: volando sobre la costa sur mirando al centro de la isla.
                camera.position = {0.0f, 350.0f, world->data.config.size_m * 0.45f};
                camera.yaw = -90.0f;
                camera.pitch = -12.0f;
            } else {
                camera.position.y =
                    world->data.height.sample(camera.position.x, camera.position.z)
                    + 350.0f;
                controller.flying = true;
            }
        }
        state.generation_phase = bake_phase.load();
        state.generation_elapsed = baking.valid()
            ? std::chrono::duration<float>(
                  std::chrono::steady_clock::now() - bake_start).count()
            : 0.0f;

        glClearColor(kSkyColor.r, kSkyColor.g, kSkyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, state.wireframe ? GL_LINE : GL_FILL);

        if (world) {
            world->ground.update(camera.position,
                                 camera.proj(window.aspect()) * camera.view());
            state.visible_chunks =
                static_cast<int>(world->ground.visible_meshes().size());
            state.total_chunks = world->ground.total_chunks();

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
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        debug_ui.draw(state, camera, controller, clock.dt);

        window.swap_and_poll();
    }
}

} // namespace

// Uso: ProceduralTerrain [seed [x z yaw pitch [alt]]] — sin argumentos usa la
// seed del config y el spawn estándar; la pose opcional sirve al lazo de
// verificación visual por capturas (alt = metros sobre el terreno en x,z).
int main(int argc, char** argv) {
    std::optional<std::uint32_t> seed;
    std::optional<CameraPose> pose;
    try {
        if (argc > 1) {
            seed = static_cast<std::uint32_t>(std::stoul(argv[1]));
        }
        if (argc > 5) {
            CameraPose p;
            p.x = std::stof(argv[2]);
            p.z = std::stof(argv[3]);
            p.yaw = std::stof(argv[4]);
            p.pitch = std::stof(argv[5]);
            if (argc > 6) {
                p.alt = std::stof(argv[6]);
            }
            pose = p;
        }
    } catch (const std::exception&) {
        std::cerr << "Uso: ProceduralTerrain [seed [x z yaw pitch [alt]]]\n";
        return 1;
    }
    try {
        run(seed, pose);
    } catch (const std::exception& e) {
        std::cerr << "Error fatal: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
