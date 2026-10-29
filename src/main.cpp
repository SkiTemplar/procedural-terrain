#include "core/frame_clock.h"
#include "core/input.h"
#include "core/window.h"
#include "player/controller.h"
#include "render/camera.h"
#include "render/shader.h"
#include "render/terrain_shaders.h"
#include "terrain/chunked_terrain.h"
#include "terrain/generator.h"
#include "terrain/landform_stats.h"
#include "terrain/mesher.h"
#include "terrain/palette.h"
#include "terrain/preset.h"
#include "ui/debug_ui.h"
#include "ui/field_maps.h"
#include "veg/grass_field.h"
#include "veg/veg_renderer.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
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

// C4-W1/W2: textura RG del dominio para el shader del MAR — r = profundidad
// real del fondo bajo el nivel del mar (m), g = energía de oleaje [0,1]. Da
// color por calado (Beer-Lambert) y espuma de orilla POR PÍXEL, cosa que los
// vértices del plano del mar (~625 m) jamás podrían resolver. Los ríos no la
// usan: llevan su calado por vértice (la malla de agua va a resolución plena).
struct SeabedTexture {
    GLuint id = 0;

    ~SeabedTexture() {
        release();
    }
    void release() {
        if (id != 0) {
            glDeleteTextures(1, &id);
            id = 0;
        }
    }
    void rebuild(const terrain::TerrainData& data) {
        release();
        constexpr int kRes = 2048;
        const auto& hf = data.height;
        const int res = hf.resolution();
        const float sea = data.config.sea_level;
        std::vector<float> texels(static_cast<size_t>(kRes) * kRes * 2);
        for (int z = 0; z < kRes; ++z) {
            for (int x = 0; x < kRes; ++x) {
                const int sx = x * (res - 1) / (kRes - 1);
                const int sz = z * (res - 1) / (kRes - 1);
                const size_t si = static_cast<size_t>(sz) * res + sx;
                const size_t ti = (static_cast<size_t>(z) * kRes + x) * 2;
                texels[ti] = std::max(0.0f, sea - hf.data()[si]);
                texels[ti + 1] =
                    data.wave_energy.empty() ? 0.0f : data.wave_energy[si];
            }
        }
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, kRes, kRes, 0, GL_RG,
                     GL_FLOAT, texels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
};

void set_common_uniforms(const render::Shader& shader, const render::Camera& camera,
                         float aspect, const ui::DebugState& state) {
    shader.set_mat4("u_view", camera.view());
    shader.set_mat4("u_proj", camera.proj(aspect));
    shader.set_vec3("u_sun_dir", state.sun_dir());
    shader.set_vec3("u_camera_pos", camera.position);
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
    render::Shader sky_shader(render::kSkyVert, render::kSkyFrag);

    // VAO vacío para el triángulo fullscreen del cielo (core profile exige un
    // VAO ligado aunque el shader no lea atributos: los vértices salen de
    // gl_VertexID).
    GLuint sky_vao = 0;
    glGenVertexArrays(1, &sky_vao);

    ui::DebugUi debug_ui(window.handle());
    ui::DebugState state;
    if (seed_override) {
        state.pending_config.seed = *seed_override;
    }
    // Presets (C1): viven en presets/ relativo al directorio de trabajo (la
    // raíz del repo en el flujo normal). El hash esperado del preset cargado
    // se comprueba al terminar el bake — verificación §7-C en runtime.
    const std::string preset_dir = "presets";
    state.preset_list = terrain::list_presets(preset_dir);
    std::uint64_t preset_expected_hash = 0;
    ui::FieldMaps field_maps; // minimapas C2: se reconstruyen al montar mundo
    SeabedTexture seabed_tex; // C4-W1/W2: calado + oleaje para el mar

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

        if (state.save_preset_requested) {
            state.save_preset_requested = false;
            if (world) {
                std::error_code ec;
                std::filesystem::create_directories(preset_dir, ec);
                const std::string path =
                    preset_dir + "/" + state.preset_name + ".json";
                // Se guarda el config del MUNDO ACTUAL (no el de los sliders
                // a medio editar) con el hash del heightfield que produjo.
                const bool ok = terrain::save_preset(
                    path, world->data.config,
                    terrain::heightfield_hash(world->data.height));
                state.preset_status =
                    ok ? "guardado: " + path : "ERROR al guardar " + path;
                state.preset_list = terrain::list_presets(preset_dir);
            } else {
                state.preset_status = "no hay mundo que guardar todavia";
            }
        }
        if (!state.load_preset_name.empty()) {
            const std::string name = state.load_preset_name;
            state.load_preset_name.clear();
            if (const auto p =
                    terrain::load_preset(preset_dir + "/" + name + ".json")) {
                state.pending_config = p->config;
                preset_expected_hash = p->height_hash;
                state.regenerate_requested = true;
                state.preset_status = "cargando preset '" + name + "'...";
            } else {
                state.preset_status = "ERROR al leer preset '" + name + "'";
            }
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
            {
                // Censo de vegetación (auditoría Fase B): verificable desde el
                // log sin volar el mapa. Espejo del panel ImGui "Vegetacion".
                const auto& vc = world->vegetation.counts();
                std::cout << "vegetacion: pinos=" << vc.pines
                          << " frondosos=" << vc.broadleafs
                          << " rocas=" << vc.rocks << " matas=" << vc.tufts
                          << " arbustos=" << vc.bushes << " troncos=" << vc.logs
                          << std::endl;
            }
            field_maps.rebuild(world->data);
            seabed_tex.rebuild(world->data);
            if (preset_expected_hash != 0) {
                // Verificación §7-C: el mundo regenerado desde el preset debe
                // reproducir exactamente el heightfield guardado.
                const bool match = terrain::heightfield_hash(world->data.height)
                                   == preset_expected_hash;
                preset_expected_hash = 0;
                state.preset_status =
                    match ? "preset verificado: el mundo coincide con el hash"
                          : "AVISO: el mundo NO coincide con el hash del preset";
                std::cout << state.preset_status << std::endl;
            }
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

        // Pase de cielo (C4-L4): gradiente + disco solar a pantalla completa,
        // sin depth (el mundo se pinta encima). Siempre relleno, aunque el
        // modo wireframe esté activo — el cielo no es malla que inspeccionar.
        glDisable(GL_DEPTH_TEST);
        sky_shader.use();
        const glm::mat4 view_rot = glm::mat4(glm::mat3(camera.view()));
        sky_shader.set_mat4(
            "u_inv_vp", glm::inverse(camera.proj(window.aspect()) * view_rot));
        sky_shader.set_vec3("u_sun_dir", state.sun_dir());
        sky_shader.set_vec3("u_camera_pos", camera.position);
        glBindVertexArray(sky_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);

        glPolygonMode(GL_FRONT_AND_BACK, state.wireframe ? GL_LINE : GL_FILL);

        if (world) {
            world->ground.update(camera.position,
                                 camera.proj(window.aspect()) * camera.view());
            state.visible_chunks =
                static_cast<int>(world->ground.visible_meshes().size());
            state.total_chunks = world->ground.total_chunks();
            {
                const auto& vc = world->vegetation.counts();
                state.veg_counts[0] = vc.pines;
                state.veg_counts[1] = vc.broadleafs;
                state.veg_counts[2] = vc.rocks;
                state.veg_counts[3] = vc.tufts;
                state.veg_counts[4] = vc.bushes;
                state.veg_counts[5] = vc.logs;
            }

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
            water_shader.set_float("u_domain_half",
                                   world->data.config.size_m * 0.5f);
            // C4-W4: el oleaje viaja con la rosa de vientos. wind_dir_deg es
            // DE dónde sopla (brújula: 0 = del norte = -z); las olas van al
            // sentido contrario.
            {
                const float wr = world->data.config.wind_dir_deg
                               * 3.14159265f / 180.0f;
                water_shader.set_vec3(
                    "u_wave_dir",
                    {-std::sin(wr), 0.0f, std::cos(wr)});
                water_shader.set_float("u_wind_focus",
                                       world->data.config.wind_focus);
            }
            water_shader.set_int("u_seabed", 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, seabed_tex.id);
            water_shader.set_int("u_is_sea", 1);
            world->sea.draw();
            water_shader.set_int("u_is_sea", 0);
            if (world->water) {
                world->water->draw();
            }
            glDisable(GL_BLEND);
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        debug_ui.draw(state, camera, controller, clock.dt, &field_maps);

        window.swap_and_poll();
    }

    glDeleteVertexArrays(1, &sky_vao);
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

// Laderas y pedreras (auditoría 2026-10 relieve #5/#6): histograma de
// pendiente de tierra, paredes > 45° costeras frente a interiores, reparto por
// formación y cobertura de canchal al pie de pared.
void print_landform_stats(const terrain::TerrainData& data) {
    const terrain::LandformStats st = terrain::compute_landform_stats(data);
    const auto pct = [](std::size_t a, std::size_t b) {
        return b > 0 ? 100.0 * static_cast<double>(a) / static_cast<double>(b) : 0.0;
    };
    std::cout << "pendiente de tierra (grados, % de " << st.land_cells << " celdas):";
    for (std::size_t b = 0; b < terrain::LandformStats::kSlopeBins; ++b) {
        const float lo = b == 0 ? 0.0f : terrain::LandformStats::kSlopeEdgesDeg[b - 1];
        std::cout << ' ' << lo << "+:" << pct(st.slope_bins[b], st.land_cells);
    }
    std::cout << std::endl;
    std::cout << "pendiente >45: interior " << st.steep45_inland << " celdas, costa (<"
              << terrain::LandformStats::kCoastBandM << " m) " << st.steep45_coast
              << " celdas" << std::endl;
    const char* names[3] = {"dura", "media", "blanda"};
    for (std::size_t c = 0; c < 3; ++c) {
        std::cout << "  formacion " << names[c] << ": " << st.litho_cells[c]
                  << " celdas, >45 " << pct(st.litho_steep45[c], st.litho_cells[c])
                  << " %, >55 " << pct(st.litho_steep55[c], st.litho_cells[c])
                  << " %, p90 tan " << st.litho_p90_tan[c] << std::endl;
    }
    std::cout << "pedreras: " << st.scree_cells << " celdas (" << st.scree_area_km2
              << " km2), moda de pendiente tan " << st.scree_slope_mode_tan
              << "; pie de pared " << st.wall_foot_cells << " celdas, canchal "
              << pct(st.wall_foot_scree, st.wall_foot_cells) << " %" << std::endl;
    std::cout << "  pendiente del canchal (tan, % por bin de "
              << terrain::LandformStats::kScreeSlopeBinTan << "):";
    for (std::size_t b = 0; b < terrain::LandformStats::kScreeSlopeBins; ++b) {
        const double p = pct(st.scree_slope_bins[b], st.scree_cells);
        if (p >= 0.5) {
            std::cout << ' ' << static_cast<float>(b) * terrain::LandformStats::kScreeSlopeBinTan
                      << ':' << static_cast<int>(p + 0.5);
        }
    }
    std::cout << std::endl;
    std::cout << "  perfil del canchal: concavo " << pct(st.scree_concave, st.scree_cells)
              << " %, convexo " << pct(st.scree_convex, st.scree_cells) << " %";
    if (st.scree_hotspot_x >= 0) {
        std::cout << "; mayor pedrera en mundo (" << data.height.world_x(st.scree_hotspot_x)
                  << ", " << data.height.world_z(st.scree_hotspot_z) << ")";
    }
    std::cout << std::endl;
    const terrain::RockfallStats& rf = data.rockfall_stats;
    std::cout << "desprendimientos: " << rf.source_cells
              << " celdas de pared en el 1er episodio, desprendido " << rf.detached_m3
              << " m3, depositado " << rf.deposited_m3 << " m3, evacuado por el agua "
              << rf.evacuated_m3 << " m3" << std::endl;
}

// Dump de verificación de costa (criterio §7-A del plan): mapa de materiales
// con lámina de agua y mapa de energía de oleaje, sin abrir ventana.
int dump_costa(const std::string& prefix, std::optional<std::uint32_t> seed) {
    terrain::TerrainConfig cfg;
    if (seed) {
        cfg.seed = *seed;
    }
    // Cada fase con el tiempo transcurrido desde el arranque: el coste de una
    // fase es la diferencia con la siguiente.
    const auto t0 = std::chrono::steady_clock::now();
    const auto data = terrain::generate(cfg, [t0](const char* phase) {
        const std::chrono::duration<double> t = std::chrono::steady_clock::now() - t0;
        std::cout << "  [" << t.count() << " s] " << phase << std::endl;
    });
    const int res = data.height.resolution();
    const size_t n = data.material.size();

    std::vector<glm::u8vec3> img(n);
    const auto to8 = [](const glm::vec3& c) {
        return glm::u8vec3(glm::clamp(c, 0.0f, 1.0f) * 255.0f);
    };
    // La MISMA paleta que el render in-game (review 2026-07-11: el dump
    // llamaba a material_color plano y quedaba ciego a los gradientes de
    // roca/nieve/lecho — verificaba una paleta que el juego ya no usa).
    const terrain::Palette palette(data);
    for (size_t i = 0; i < n; ++i) {
        const float wx = data.height.world_x(static_cast<int>(i % static_cast<size_t>(res)));
        const float wz = data.height.world_z(static_cast<int>(i / static_cast<size_t>(res)));
        const float h = data.height.data()[i];
        glm::vec3 c = palette.ground(data.material[i], data.forest_density[i],
                                     data.climate[i], h, wx, wz, wx, wz);
        const float wl = data.hydrology.water_level[i];
        if (!std::isnan(wl) && wl > h) {
            c = glm::mix(c, glm::vec3{0.15f, 0.35f, 0.75f}, 0.65f);
        }
        if (h <= cfg.sea_level) {
            // Velo azul en vez de azul plano: el gradiente de calado del
            // lecho marino queda verificable a través del "agua".
            c = glm::mix(c, glm::vec3{0.10f, 0.20f, 0.45f}, 0.55f);
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

    // Relieve sombreado (diagnóstico de landforms, feedback 2026-07-10 "U de
    // cabeceras"): hillshade lambertiano con el MISMO sol fijo del juego
    // (az 135°, alt 40°) multiplicado por la sombra arrojada baked — se leen
    // las formas del relieve sin el ruido de color de los materiales, y de
    // paso se verifica la luz precomputada a vista de pájaro.
    {
        const float az = 135.0f * 3.14159265f / 180.0f;
        const float alt = 40.0f * 3.14159265f / 180.0f;
        const glm::vec3 sun{std::cos(az) * std::cos(alt), std::sin(alt),
                            std::sin(az) * std::cos(alt)};
        const int lres = data.light_res;
        for (int z = 0; z < res; ++z) {
            for (int x = 0; x < res; ++x) {
                const size_t i = static_cast<size_t>(z) * res + x;
                if (data.height.data()[i] <= cfg.sea_level) {
                    img[i] = {20, 30, 80};
                    continue;
                }
                const float lambert = glm::clamp(
                    glm::dot(data.height.normal_at(x, z), sun), 0.0f, 1.0f);
                float shadow = 1.0f;
                if (lres >= 2 && !data.shadow.empty()) {
                    const int lx = x * (lres - 1) / (res - 1);
                    const int lz = z * (lres - 1) / (res - 1);
                    shadow = data.shadow[static_cast<size_t>(lz) * lres + lx];
                }
                const float v = 0.12f + 0.88f * lambert * (0.35f + 0.65f * shadow);
                img[i] = to8(glm::vec3{v, v, v});
            }
        }
        write_bmp(prefix + "-relieve.bmp", res, res, img);
    }

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

        // Agua PERCHADA / río elevado (feedback 2026-07-08): celda de agua sin
        // NINGUNA vecina de terreno por encima de su lámina en un radio → flota
        // sin orillas. Mide contención (distinto del pico por celda, que mide
        // altura sobre el propio lecho). Es la métrica del "río elevado".
        constexpr int kR = 4;
        int uncontained = 0;
        int uncontained_drowned = 0;
        int uncontained_lowbed = 0;
        int uncontained_high = 0;
        float worst_perch = 0.0f;
        int px = -1, pz = -1;
        for (int z = kR; z < res - kR; ++z) {
            for (int x = kR; x < res - kR; ++x) {
                const size_t wi = at(x, z);
                const float w = wl[wi];
                // Solo RÍOS: los lagos son cuencas anchas y su "perchado" (la
                // cubeta) es legítimo — falso positivo para este bug.
                if (std::isnan(w) || lake[wi]) {
                    continue;
                }
                bool has_bank = false;
                float local_min = 1e9f;
                for (int dz = -kR; dz <= kR && !has_bank; ++dz) {
                    for (int dx = -kR; dx <= kR; ++dx) {
                        const float th = hfd[at(x + dx, z + dz)];
                        local_min = std::min(local_min, th);
                        if (th >= w - 0.1f) {
                            has_bank = true;
                            break;
                        }
                    }
                }
                if (!has_bank) {
                    ++uncontained;
                    if (local_min < cfg.sea_level) {
                        ++uncontained_drowned;
                    }
                    if (hfd[wi] < cfg.sea_level + 2.0f) {
                        ++uncontained_lowbed;
                    }
                    const float perch = w - local_min;
                    if (perch > 2.0f) {
                        ++uncontained_high;
                    }
                    if (perch > worst_perch) {
                        worst_perch = perch;
                        px = x;
                        pz = z;
                    }
                }
            }
        }
        std::cout << "agua perchada sin orillas (r=4): " << uncontained
                  << " celdas; peor " << worst_perch << " m";
        if (px >= 0) {
            std::cout << " en mundo (" << data.height.world_x(px) << ", "
                      << data.height.world_z(pz) << ")";
        }
        std::cout << std::endl;
        std::cout << "  (desglose: " << uncontained_drowned
                  << " con fondo vecino bajo el mar, " << uncontained_lowbed
                  << " con lecho a <2 m sobre el mar, " << uncontained_high
                  << " con perchado >2 m)" << std::endl;

        // RÍO SECO (feedback 2026-07-13 "ríos bloqueados por terreno"): celda
        // con caudal de río hecho pero SIN lámina pintada y en tierra firme —
        // el cauce existe y lleva agua según el D8, pero no se dibuja nada.
        const float dry_threshold = cfg.river_flow_threshold
                                  * (static_cast<float>(res) * static_cast<float>(res))
                                  / (1024.0f * 1024.0f) * 1.5f;
        int dry = 0;
        float worst_dry_flow = 0.0f;
        int dx1 = -1, dz1 = -1;
        for (int z = 0; z < res; ++z) {
            for (int x = 0; x < res; ++x) {
                const size_t i = at(x, z);
                if (data.hydrology.flow[i] < dry_threshold
                    || !std::isnan(wl[i])
                    || hfd[i] <= cfg.sea_level + 0.3f) {
                    continue;
                }
                ++dry;
                if (data.hydrology.flow[i] > worst_dry_flow) {
                    worst_dry_flow = data.hydrology.flow[i];
                    dx1 = x;
                    dz1 = z;
                }
            }
        }
        std::cout << "rio seco (caudal>=1.5x umbral sin lamina, en tierra): "
                  << dry << " celdas; peor caudal " << worst_dry_flow;
        if (dx1 >= 0) {
            std::cout << " en mundo (" << data.height.world_x(dx1) << ", "
                      << data.height.world_z(dz1) << ")";
        }
        std::cout << std::endl;

        // Sedimentación del LEM (Yuan 2019): cuánto de lo erosionado se
        // redeposita en la última iteración, y fondos de valle llanos en
        // tierra (pendiente < 2 %, 5-600 m: excluye playa y cumbres), que es
        // donde la deposición tiene que notarse.
        {
            const auto& ls = data.lem_stats;
            const double ratio =
                ls.eroded_m3 > 0.0 ? ls.deposited_m3 / ls.eroded_m3 : 0.0;
            std::cout << "sedimento LEM: erosionado " << ls.eroded_m3
                      << " m3, depositado " << ls.deposited_m3 << " m3 ("
                      << ratio * 100.0 << " %), celdas en agradacion "
                      << ls.aggrading_cells << std::endl;
            const int hres = data.height.resolution();
            const float cell = data.height.cell_size();
            std::size_t flat = 0;
            std::size_t land = 0;
            for (int z = 1; z < hres - 1; ++z) {
                for (int x = 1; x < hres - 1; ++x) {
                    const float h = data.height.at(x, z);
                    if (h < 5.0f || h > 600.0f) {
                        continue;
                    }
                    ++land;
                    const float gx = (data.height.at(x + 1, z) - data.height.at(x - 1, z))
                                   / (2.0f * cell);
                    const float gz = (data.height.at(x, z + 1) - data.height.at(x, z - 1))
                                   / (2.0f * cell);
                    if (gx * gx + gz * gz < 0.02f * 0.02f) {
                        ++flat;
                    }
                }
            }
            std::cout << "cumbre: "
                      << *std::max_element(data.height.data().begin(),
                                           data.height.data().end())
                      << " m" << std::endl;
            // Lagos: celdas, profundidad máxima (lámina − terreno) y cuántas
            // están por encima de 600 m (lagos de altura, ver N4 descartado).
            std::size_t lake_cells = 0;
            std::size_t lake_high = 0;
            float lake_depth = 0.0f;
            for (std::size_t i = 0; i < data.hydrology.lake.size(); ++i) {
                if (data.hydrology.lake[i] == 0) {
                    continue;
                }
                ++lake_cells;
                const float d = data.hydrology.water_level[i] - data.height.data()[i];
                lake_depth = std::max(lake_depth, d);
                if (data.height.data()[i] > 600.0f) {
                    ++lake_high;
                }
            }
            std::cout << "lagos: " << lake_cells << " celdas (" << lake_high
                      << " sobre 600 m), profundidad maxima " << lake_depth
                      << " m" << std::endl;
            std::cout << "fondo llano (pendiente<2%, 5-600 m): " << flat
                      << " celdas (" << (land ? 100.0 * flat / land : 0.0)
                      << " % de la tierra media)" << std::endl;
            // Paredes (auditoría 2026-10, mesetas con murallas): pendiente
            // >1.2 en tierra y cuántas forman escarpes de gran desnivel.
            const terrain::WallStats ws =
                terrain::count_walls(data.height, cfg.sea_level);
            std::cout << "paredes (pendiente>1.2): " << ws.walls
                      << " celdas ("
                      << (ws.land ? 100.0 * ws.walls / ws.land : 0.0)
                      << " % de la tierra); escarpe >40 m en r=3: "
                      << ws.scarp40 << "; escarpe >100 m en r=12: "
                      << ws.scarp100 << std::endl;
        }

        // Histograma de regímenes de orilla (espejo del log del modo ventana):
        // la calibración de marisma/arena/grava se mide aquí sin abrir GL.
        // La grava cuenta solo en la banda litoral: tierra adentro es canchal
        // (va en la línea de pedreras).
        size_t n_sand = 0, n_gravel = 0, n_marsh = 0;
        for (size_t i = 0; i < data.material.size(); ++i) {
            const auto mtl = data.material[i];
            n_sand += mtl == terrain::Material::Sand ? 1 : 0;
            n_gravel += mtl == terrain::Material::Gravel && data.wave_energy[i] > 0.0f ? 1 : 0;
            n_marsh += mtl == terrain::Material::Marsh ? 1 : 0;
        }
        std::cout << "orillas: arena " << n_sand << " grava " << n_gravel
                  << " marisma " << n_marsh << std::endl;
    }
    print_landform_stats(data);

    std::cout << "dumps escritos: " << prefix << "-materiales.bmp / "
              << prefix << "-oleaje.bmp / " << prefix << "-relieve.bmp"
              << std::endl;
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
