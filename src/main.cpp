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
#include "veg/grass_field.h"
#include "veg/veg_renderer.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    veg::GrassField grass;

    explicit World(terrain::TerrainData&& ready)
        : data(std::move(ready)),
          sampler(data),
          ground(data, sampler),
          water(terrain::build_water_mesh(data)),
          sea(terrain::build_sea_plane(data.config.size_m, data.config.sea_level)),
          vegetation(data, sampler),
          grass(data, sampler) {}

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
    render::Shader grass_shader(render::kGrassVert, render::kGrassFrag);
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
            size_t counts[terrain::kMaterialCount] = {};
            for (size_t i = 0; i < data.material.size(); ++i) {
                max_h = std::max(max_h, data.height.data()[i]);
                counts[static_cast<size_t>(data.material[i])]++;
            }
            const float total = static_cast<float>(data.material.size());
            const auto pct = [&](terrain::Material m) {
                return static_cast<int>(
                    100.0f * counts[static_cast<size_t>(m)] / total);
            };
            std::cout << "Terreno generado en " << secs
                      << " s | alt max " << static_cast<int>(max_h) << " m | mar "
                      << pct(terrain::Material::Seabed) << "% hierba "
                      << pct(terrain::Material::Grass) << "% bosque "
                      << pct(terrain::Material::Forest) << "% roca "
                      << pct(terrain::Material::Rock) << "% nieve "
                      << pct(terrain::Material::Snow) << "% | orillas: arena "
                      << counts[static_cast<size_t>(terrain::Material::Sand)]
                      << " grava "
                      << counts[static_cast<size_t>(terrain::Material::Gravel)]
                      << " marisma "
                      << counts[static_cast<size_t>(terrain::Material::Marsh)]
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

            // Césped del LOD cercano: shader propio con viento + desvanecido
            // por distancia (encoge la altura entre 70 y 104 m → sin pop al
            // repoblar el disco alrededor de la cámara).
            grass_shader.use();
            set_common_uniforms(grass_shader, camera, window.aspect(), state);
            grass_shader.set_float("u_time", clock.total);
            grass_shader.set_float("u_grass_fade_start", 70.0f);
            grass_shader.set_float("u_grass_fade_end", 104.0f);
            world->grass.update(camera.position);
            world->grass.draw();

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

// Escritor BMP 24-bit sin dependencias — solo para los dumps de verificación.
void write_bmp(const std::string& path, int w, int h,
               const std::vector<glm::u8vec3>& rgb) {
    const int row_bytes = (w * 3 + 3) & ~3;
    const std::uint32_t data_size = static_cast<std::uint32_t>(row_bytes) * h;
    const std::uint32_t file_size = 54 + data_size;
    std::vector<std::uint8_t> out(file_size, 0);
    const auto put32 = [&](size_t at, std::uint32_t v) {
        out[at] = v & 0xFF; out[at + 1] = (v >> 8) & 0xFF;
        out[at + 2] = (v >> 16) & 0xFF; out[at + 3] = (v >> 24) & 0xFF;
    };
    out[0] = 'B'; out[1] = 'M';
    put32(2, file_size); put32(10, 54); put32(14, 40);
    put32(18, static_cast<std::uint32_t>(w));
    put32(22, static_cast<std::uint32_t>(h));
    out[26] = 1; out[28] = 24;
    put32(34, data_size);
    for (int y = 0; y < h; ++y) {
        // BMP guarda la fila inferior primero: volteamos para que la imagen
        // conserve z creciendo hacia abajo (misma orientación que el array).
        const size_t src_row = static_cast<size_t>(h - 1 - y) * w;
        const size_t dst = 54 + static_cast<size_t>(y) * row_bytes;
        for (int x = 0; x < w; ++x) {
            const auto& c = rgb[src_row + x];
            out[dst + static_cast<size_t>(x) * 3] = c.b;
            out[dst + static_cast<size_t>(x) * 3 + 1] = c.g;
            out[dst + static_cast<size_t>(x) * 3 + 2] = c.r;
        }
    }
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
}

// Dump de verificación de costa (criterio §7-A del plan): mapa de materiales
// con lámina de agua y mapa de energía de oleaje, sin abrir ventana.
int dump_costa(const std::string& prefix, std::optional<std::uint32_t> seed) {
    terrain::TerrainConfig cfg;
    if (seed) {
        cfg.seed = *seed;
    }
    const auto data = terrain::generate(cfg, [](const char* phase) {
        std::cout << "  " << phase << std::endl;
    });
    const int res = data.height.resolution();
    const size_t n = data.material.size();

    std::vector<glm::u8vec3> img(n);
    const auto to8 = [](const glm::vec3& c) {
        return glm::u8vec3(glm::clamp(c, 0.0f, 1.0f) * 255.0f);
    };
    for (size_t i = 0; i < n; ++i) {
        glm::vec3 c = terrain::material_color(data.material[i]);
        const float wl = data.hydrology.water_level[i];
        if (!std::isnan(wl) && wl > data.height.data()[i]) {
            c = glm::mix(c, glm::vec3{0.15f, 0.35f, 0.75f}, 0.65f);
        }
        if (data.height.data()[i] <= cfg.sea_level) {
            c = {0.10f, 0.20f, 0.45f};
        }
        img[i] = to8(c);
    }
    write_bmp(prefix + "-materiales.bmp", res, res, img);

    for (size_t i = 0; i < n; ++i) {
        const float e = data.wave_energy[i];
        img[i] = data.height.data()[i] <= cfg.sea_level
                     ? glm::u8vec3{20, 30, 80}
                     : to8(glm::vec3{e, e, e});
    }
    write_bmp(prefix + "-oleaje.bmp", res, res, img);

    // Informe de sanidad N0 (criterios §7-N0 del plan): números que deben ser
    // ~0. Con coordenadas de mundo del peor caso para verificar con la cámara.
    {
        const auto& hfd = data.height.data();
        const auto& wl = data.hydrology.water_level;
        const auto& lake = data.hydrology.lake;
        const auto at = [&](int x, int z) {
            return static_cast<size_t>(z) * static_cast<size_t>(res)
                 + static_cast<size_t>(x);
        };

        // (a) Agua flotante: celdas de río (no lago, no poza de cascada) con
        // lámina >3.5 m sobre el terreno — un cauce normal lleva 0.25-1.2 m
        // + tope de rampa de 2 m; las pozas (whitewater=2) llegan a 3.3 m
        // por diseño y quedan exentas.
        int floating = 0;
        float worst_d = 0.0f;
        int fx = -1, fz = -1;
        // (b) Hierba dentro de la banda litoral (proxy de banda: energía de
        // oleaje > 0, que solo existe en el litoral).
        int grass_beach = 0;
        int gx = -1, gz = -1;
        // (c) Dithering de orilla: celda de material de orilla sin NINGUNA
        // vecina 4-conexa del mismo material dentro de la banda.
        int isolated = 0;
        const auto is_shore_mat = [](terrain::Material m) {
            return m == terrain::Material::Sand || m == terrain::Material::Gravel
                || m == terrain::Material::Marsh || m == terrain::Material::Rock;
        };

        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                const size_t i = at(x, z);
                if (!std::isnan(wl[i]) && !lake[i]
                    && data.hydrology.whitewater[i] != 2) {
                    const float d = wl[i] - hfd[i];
                    if (d > 3.5f) {
                        ++floating;
                        if (d > worst_d) {
                            worst_d = d;
                            fx = x;
                            fz = z;
                        }
                    }
                }
                const bool in_band = data.wave_energy[i] > 0.005f
                                  && hfd[i] > cfg.sea_level;
                if (!in_band) {
                    continue;
                }
                const terrain::Material m = data.material[i];
                // A pie de agua (rel < 3.9 m) y SIN filtro de pendiente: es
                // exactamente el agujero del feedback (verde entre la arena).
                if ((m == terrain::Material::Grass
                     || m == terrain::Material::Forest)
                    && hfd[i] - cfg.sea_level < 3.9f) {
                    ++grass_beach;
                    if (gx < 0) {
                        gx = x;
                        gz = z;
                    }
                }
                if (is_shore_mat(m)) {
                    int same = 0;
                    int band_n = 0;
                    const int dx4[4] = {-1, 1, 0, 0};
                    const int dz4[4] = {0, 0, -1, 1};
                    for (int k = 0; k < 4; ++k) {
                        const size_t ni = at(x + dx4[k], z + dz4[k]);
                        if (data.wave_energy[ni] > 0.005f
                            && hfd[ni] > cfg.sea_level
                            && is_shore_mat(data.material[ni])) {
                            ++band_n;
                            if (data.material[ni] == m) {
                                ++same;
                            }
                        }
                    }
                    if (band_n >= 2 && same == 0) {
                        ++isolated;
                    }
                }
            }
        }

        std::cout << "--- sanidad N0 ---" << std::endl;
        std::cout << "agua flotante (rio, lamina>3m): " << floating
                  << " celdas; peor caso " << worst_d << " m";
        if (fx >= 0) {
            std::cout << " en mundo (" << data.height.world_x(fx) << ", "
                      << data.height.world_z(fz) << ")";
        }
        std::cout << std::endl;
        std::cout << "hierba/bosque en banda litoral: " << grass_beach
                  << " celdas";
        if (gx >= 0) {
            std::cout << "; primera en mundo (" << data.height.world_x(gx)
                      << ", " << data.height.world_z(gz) << ")";
        }
        std::cout << std::endl;
        std::cout << "orilla aislada (dithering 4-conexo): " << isolated
                  << " celdas" << std::endl;

        // Picos del RELIEVE BASE (feedback 2026-07-08): celda que sobresale
        // >3 m por encima de sus 8 vecinas — punta artificial de un vértice.
        // Si es ~0, los picos que ve el jugador vienen del detalle de la malla
        // cercana (detail_amplitude), no del heightfield.
        int base_spikes = 0;
        float worst_spike = 0.0f;
        int sx = -1, sz = -1;
        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                const float h = hfd[at(x, z)];
                if (h <= cfg.sea_level) {
                    continue;
                }
                float maxn = -1e9f;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx || dz) {
                            maxn = std::max(maxn, hfd[at(x + dx, z + dz)]);
                        }
                    }
                }
                const float over = h - maxn;
                if (over > 3.0f) {
                    ++base_spikes;
                    if (over > worst_spike) {
                        worst_spike = over;
                        sx = x;
                        sz = z;
                    }
                }
            }
        }
        std::cout << "picos de relieve base (>3m sobre vecinas): " << base_spikes
                  << " celdas; peor " << worst_spike << " m";
        if (sx >= 0) {
            std::cout << " en mundo (" << data.height.world_x(sx) << ", "
                      << data.height.world_z(sz) << ")";
        }
        std::cout << std::endl;
    }

    std::cout << "dumps escritos: " << prefix << "-materiales.bmp / "
              << prefix << "-oleaje.bmp" << std::endl;
    return 0;
}

// Uso: ProceduralTerrain [seed [x z yaw pitch [alt]]] — sin argumentos usa la
// seed del config y el spawn estándar; la pose opcional sirve al lazo de
// verificación visual por capturas (alt = metros sobre el terreno en x,z).
// Modo dump: ProceduralTerrain --dump-costa <prefijo> [seed] genera el mundo
// sin ventana y escribe los mapas de materiales y oleaje como BMP.
int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "--dump-costa") {
        std::optional<std::uint32_t> dump_seed;
        if (argc > 3) {
            dump_seed = static_cast<std::uint32_t>(std::stoul(argv[3]));
        }
        return dump_costa(argv[2], dump_seed);
    }
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
