#include "app/offscreen.h"
#include "app/scene_renderer.h"
#include "app/world.h"
#include "core/frame_clock.h"
#include "core/input.h"
#include "core/window.h"
#include "player/controller.h"
#include "render/camera.h"
#include "render/shader.h"
#include "render/terrain_shaders.h"
#include "terrain/chunked_terrain.h"
#include "terrain/coast.h"
#include "terrain/coast_metrics.h"
#include "terrain/ecology.h"
#include "terrain/generator.h"
#include "terrain/landform_stats.h"
#include "terrain/hydraulic_geometry.h"
#include "terrain/mesher.h"
#include "terrain/palette.h"
#include "terrain/preset.h"
#include "terrain/water_flow.h"
#include "terrain/water_metrics.h"
#include "terrain/water_surface.h"
#include "ui/debug_ui.h"
#include "ui/field_maps.h"
#include "veg/grass_field.h"
#include "veg/veg_renderer.h"

#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
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

void run(std::optional<std::uint32_t> seed_override,
         std::optional<app::CameraPose> pose_override) {
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
    app::SceneRenderer renderer;

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

    // Generación asíncrona: el generate() (solo CPU) corre en un hilo de
    // trabajo y la ventana sigue viva con un overlay de progreso. El montaje
    // GL (mallas, agua, vegetación) se hace en el hilo principal al recoger
    // el resultado. En regeneración, el mundo viejo sigue dibujándose.
    std::unique_ptr<app::World> world;

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
                if (p->climate_version < terrain::kPresetClimateVersion) {
                    state.preset_status += " (clima v1: revisa snow_line_m y treeline_m)";
                }
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
            world = std::make_unique<app::World>(baking.get());
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
                const app::CameraPose& p = *pose_override;
                camera.position = {p.x, world->pose_height(p), p.z};
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

        renderer.draw(world.get(), camera, window.aspect(), state, clock.total);
        debug_ui.draw(state, camera, controller, clock.dt, &field_maps);

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

// Geometría de los ríos mayores (hallazgo #4 de la auditoría 2026-10): la
// anchura MOJADA se mide sobre el resultado, no se lee de la fórmula. Boca =
// celda de río en tierra cuyo único vecino con más caudal está bajo el mar;
// la sección se toma ~100 m aguas arriba siguiendo el tronco principal
// (mediana de 5 secciones) para no medir el abanico de la desembocadura.
void report_river_mouths(const terrain::TerrainData& data) {
    namespace hyd = terrain::hydraulics;
    constexpr int kTopRivers = 5;
    constexpr int kTraceCells = 60;
    constexpr int kSections[] = {30, 35, 40, 45, 50};
    constexpr int kDirHalfSpan = 5;
    constexpr float kProbeStep = 0.25f;
    constexpr float kMaxProbeCells = 200.0f;

    const auto& cfg = data.config;
    const auto& hf = data.height;
    const auto& h = hf.data();
    const auto& flow = data.hydrology.flow;
    const auto& wl = data.hydrology.water_level;
    const auto& lake = data.hydrology.lake;
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    const float threshold = cfg.river_flow_threshold
                          * (static_cast<float>(res) * static_cast<float>(res))
                          / (1024.0f * 1024.0f);
    const auto at = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res)
             + static_cast<size_t>(x);
    };
    const auto is_wet = [&](size_t i) {
        return !std::isnan(wl[i]) && wl[i] > h[i];
    };

    struct Mouth {
        int x;
        int z;
        float flow;
    };
    std::vector<Mouth> mouths;
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = at(x, z);
            if (flow[i] < threshold || h[i] <= cfg.sea_level || lake[i]) {
                continue;
            }
            bool to_sea = false;
            bool land_downstream = false;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const size_t ni = at(x + dx, z + dz);
                    if ((!dx && !dz) || flow[ni] <= flow[i]) {
                        continue;
                    }
                    if (h[ni] <= cfg.sea_level) {
                        to_sea = true;
                    } else {
                        land_downstream = true;
                    }
                }
            }
            if (to_sea && !land_downstream) {
                mouths.push_back({x, z, flow[i]});
            }
        }
    }
    std::sort(mouths.begin(), mouths.end(),
              [](const Mouth& a, const Mouth& b) { return a.flow > b.flow; });

    std::cout << "--- rios mayores (seccion ~100 m aguas arriba de la boca) ---"
              << std::endl;
    const int count = std::min(kTopRivers, static_cast<int>(mouths.size()));
    for (int r = 0; r < count; ++r) {
        // Tronco principal aguas arriba: el aportador de más caudal.
        std::vector<glm::ivec2> path{{mouths[r].x, mouths[r].z}};
        while (static_cast<int>(path.size()) < kTraceCells) {
            const glm::ivec2 c = path.back();
            const float fc = flow[at(c.x, c.y)];
            glm::ivec2 best{-1, -1};
            float best_flow = 0.0f;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = c.x + dx;
                    const int nz = c.y + dz;
                    if ((!dx && !dz) || nx < 1 || nz < 1 || nx >= res - 1
                        || nz >= res - 1) {
                        continue;
                    }
                    const float fn = flow[at(nx, nz)];
                    if (fn < fc && fn > best_flow) {
                        best_flow = fn;
                        best = {nx, nz};
                    }
                }
            }
            if (best.x < 0 || best_flow < threshold) {
                break;
            }
            path.push_back(best);
        }

        std::vector<float> widths;
        for (const int k : kSections) {
            if (k + kDirHalfSpan >= static_cast<int>(path.size())) {
                break;
            }
            const glm::vec2 down = glm::vec2(path[k - kDirHalfSpan])
                                 - glm::vec2(path[k + kDirHalfSpan]);
            if (glm::length(down) < 1e-3f) {
                continue;
            }
            const glm::vec2 perp = glm::normalize(glm::vec2{-down.y, down.x});
            const glm::vec2 c = glm::vec2(path[k]);
            float total = 0.0f;
            for (const float side : {1.0f, -1.0f}) {
                float t = 0.0f;
                while (t < kMaxProbeCells) {
                    const glm::vec2 p = c + perp * (side * (t + kProbeStep));
                    const int px = static_cast<int>(std::floor(p.x + 0.5f));
                    const int pz = static_cast<int>(std::floor(p.y + 0.5f));
                    if (px < 0 || pz < 0 || px >= res || pz >= res
                        || !is_wet(at(px, pz))) {
                        break;
                    }
                    t += kProbeStep;
                }
                total += t + 0.5f * kProbeStep;
            }
            widths.push_back(is_wet(at(path[k].x, path[k].y)) ? total * cell
                                                               : 0.0f);
        }
        std::sort(widths.begin(), widths.end());
        const float wet_w = widths.empty() ? 0.0f : widths[widths.size() / 2];

        const glm::ivec2 s = path[std::min<size_t>(40, path.size() - 1)];
        const float area_m2 = flow[at(s.x, s.y)] * cell * cell;
        const float q_mean = hyd::mean_discharge_m3s(area_m2);
        const float q_bf = hyd::bankfull_discharge_m3s(area_m2);
        std::cout << "rio " << (r + 1) << ": boca (" << hf.world_x(mouths[r].x)
                  << ", " << hf.world_z(mouths[r].z) << ") cuenca "
                  << area_m2 * 1e-6f << " km2, Qmedio " << q_mean
                  << " m3/s, Qbf " << q_bf << " m3/s, anchura mojada " << wet_w
                  << " m (ley: w_bf " << hyd::regime_width_m(q_bf)
                  << " m, d_bf " << hyd::regime_depth_m(q_bf) << " m)"
                  << std::endl;
        if (r == 0) {
            std::cout << "rio mayor: anchura mojada " << wet_w << " m con Qbf "
                      << q_bf << " m3/s" << std::endl;
        }
    }
}

// Métricas de ecología física (track mp/ecologia-fisica): reparto de
// materiales, campos físicos, correlaciones del bosque, límite del bosque y
// nieve por exposición, y fragmentación de la roca.
void print_ecology_report(const terrain::TerrainData& data) {
    const terrain::EcologyReport r = terrain::ecology_report(data);
    const auto pct = [&](terrain::Material m) {
        return r.land_pct[static_cast<std::size_t>(m)];
    };
    std::cout << "--- ecologia ---\n";
    std::cout << "materiales (% tierra): hierba " << pct(terrain::Material::Grass)
              << " bosque " << pct(terrain::Material::Forest) << " roca "
              << pct(terrain::Material::Rock) << " nieve "
              << pct(terrain::Material::Snow) << " arena "
              << pct(terrain::Material::Sand) << " grava "
              << pct(terrain::Material::Gravel) << " marisma "
              << pct(terrain::Material::Marsh) << " lecho rio "
              << pct(terrain::Material::RiverBed) << " lecho lago "
              << pct(terrain::Material::LakeBed) << '\n';
    const auto q3 = [](const std::array<float, 3>& q) {
        return std::to_string(q[0]) + "/" + std::to_string(q[1]) + "/"
             + std::to_string(q[2]);
    };
    std::cout << "campos p10/p50/p90: lluvia " << q3(r.precip_q) << " (LEM "
              << q3(r.lem_rain_q) << ") twi " << q3(r.twi_q) << " denudacion "
              << q3(r.denudation_q) << '\n';
    std::cout << "  agua/PET " << q3(r.aridity_q) << " deficit 1-ET/PET "
              << q3(r.dryness_q) << " exposicion viento " << q3(r.exposure_q)
              << " convexidad " << q3(r.convexity_q) << '\n';
    std::cout << "spearman bosque~twi " << r.spearman_forest_twi << " bosque~lluvia "
              << r.spearman_forest_precip << " bosque~exposicion "
              << r.spearman_forest_exposure << '\n';
    const auto group = [](const char* name, const terrain::EcologyReport::Group& g) {
        std::cout << "  " << name << ": limite bosque (logistica, 10 %) " << g.treeline_m
                  << " m (h50 " << g.forest_line_m << " m), p95 bosque "
                  << g.forest_p95_m << " m, p10 nieve " << g.snow_p10_m << " m ("
                  << g.cells << " muestras)\n";
    };
    std::cout << "exposicion:\n";
    group("barlovento", r.windward);
    group("sotavento ", r.leeward);
    group("solana    ", r.sunny);
    group("umbria    ", r.shady);
    std::cout << "roca a pendiente 0.3-0.6: convexas " << r.rock_convex_pct
              << " % concavas " << r.rock_concave_pct << " %\n";
    std::cout << "fragmentacion: roca aislada (<3 vecinas) " << r.rock_isolated_pct
              << " % nieve aislada " << r.snow_isolated_pct
              << " % tiras de hierba <=3 celdas entre roca "
              << r.veg_strips_per_100_rock << " por 100 celdas de roca" << std::endl;
}

// Agua que fluye (auditoría 2026-10 #1-#3): desembocaduras, río seco,
// corriente y rompiente, medidos en terrain/water_metrics.
void print_water_report(const terrain::TerrainData& data) {
    const auto& hf = data.height;
    const terrain::WaterReport r =
        terrain::measure_water(data, terrain::compute_river_flow(data));
    std::cout << "--- agua que fluye ---" << std::endl;
    std::cout << "escalon en desembocadura (mayor salto de lamina boca->mar y en los "
              << terrain::kMouthReachM << " m aguas arriba): max "
              << r.mouth_step_max << " m, p95 " << r.mouth_step_p95 << " m"
              << std::endl;
    for (const auto& m : r.mouths) {
        std::cout << "  boca caudal " << m.flow << " en mundo (" << hf.world_x(m.x)
                  << ", " << hf.world_z(m.z) << "): salto " << m.step_m
                  << " m (boca-mar " << m.drop_to_sea_m << " m)" << std::endl;
    }
    std::cout << "celdas de boca: " << r.mouth_cells << "; con salto >0.2 m: "
              << r.mouth_cells_step_over_02 << ", >1 m: " << r.mouth_cells_step_over_1;
    if (r.worst_mouth_cell.x >= 0) {
        std::cout << "; peor " << r.worst_mouth_cell.step_m << " m (caudal "
                  << r.worst_mouth_cell.flow << ") en mundo ("
                  << hf.world_x(r.worst_mouth_cell.x) << ", "
                  << hf.world_z(r.worst_mouth_cell.z) << ")";
    }
    std::cout << std::endl;
    std::cout << "rio seco (caudal>=1.5x umbral sin lamina, fuera del mar abierto): "
              << r.dry_cells << " celdas; peor caudal " << r.dry_worst_flow;
    if (r.dry_x >= 0) {
        std::cout << " en mundo (" << hf.world_x(r.dry_x) << ", "
                  << hf.world_z(r.dry_z) << ")";
    }
    std::cout << std::endl;
    std::cout << "lecho de rio bajo el plano del mar: " << r.river_bed_under_sea_plane
              << " celdas; mar encerrado tierra adentro: " << r.enclosed_sea_cells
              << " celdas" << std::endl;
    std::cout << "mar recortado bajo la lamina interior: " << r.sea_clip_quads
              << " quads; vertices con lecho sumergido por debajo del mar: "
              << r.sea_clip_sunk_vertices << " (peor " << r.sea_clip_worst_sink_m
              << " m)" << std::endl;
    const auto print_angles = [](const char* label, const terrain::FlowAngleStats& s) {
        std::cout << label << ": " << s.cells << " celdas, " << s.pct_over_60deg
                  << " % a >60 grados del eje aguas abajo, salto mediano entre vecinas "
                  << s.median_jump_deg << " grados" << std::endl;
    };
    const auto print_speed = [](const char* label, const terrain::SpeedStats& s) {
        std::cout << label << ": " << s.cells << " celdas, p50 " << s.p50 << " m/s, p95 "
                  << s.p95 << " m/s, max " << s.max << " m/s, en el techo ("
                  << terrain::kMaxRiverSpeed << " m/s) " << s.pct_at_cap << " %"
                  << std::endl;
    };
    print_speed("velocidad de Manning (lamina de rio)", r.speed_wet);
    print_speed("  eje (caudal >= umbral)", r.speed_axis);
    print_angles("corriente (campo de flujo)", r.flow_field);
    print_angles("  referencia: descenso del lecho", r.bed_descent);
    const float pct_e = r.shallow_sea_cells > 0
        ? 100.0f * static_cast<float>(r.shallow_sea_with_energy)
              / static_cast<float>(r.shallow_sea_cells)
        : 0.0f;
    std::cout << "rompiente: mar somero (<2.4 m) con oleaje " << pct_e << " % de "
              << r.shallow_sea_cells << " celdas; zona de rompiente " << r.surf_cells
              << " celdas (expuesta E>0.45: " << r.surf_exposed
              << ", abrigo E<0.16: " << r.surf_sheltered << ")" << std::endl;
}

// Dump de verificación de costa (criterio §7-A del plan): mapa de materiales
// con lámina de agua y mapa de energía de oleaje, sin abrir ventana.
int dump_costa(const std::string& prefix, const terrain::TerrainConfig& cfg) {
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
        // En el mar, la energía propagada mar adentro (rompiente) en cian.
        img[i] = data.height.data()[i] <= cfg.sea_level
                     ? to8(glm::mix(glm::vec3{0.08f, 0.12f, 0.31f},
                                    glm::vec3{0.35f, 0.85f, 1.0f}, e))
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

        // Litoral: el mismo relieve con el fondo marino somero coloreado por
        // calado (0-2 m claro → plataforma de abrasión legible; >8 m oscuro).
        for (size_t i = 0; i < n; ++i) {
            const float depth = cfg.sea_level - data.height.data()[i];
            if (depth < 0.0f) {
                continue;
            }
            const float t = std::sqrt(glm::clamp(depth / 8.0f, 0.0f, 1.0f));
            img[i] = to8(glm::mix(glm::vec3{0.62f, 0.92f, 0.86f},
                                  glm::vec3{0.06f, 0.12f, 0.36f}, t));
        }
        write_bmp(prefix + "-litoral.bmp", res, res, img);
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

        print_water_report(data);

        // Depósito fluvial: deltas (lóbulos sobre el mar o el lago) y barras
        // (celdas de cauce que pasan de agua a barra emergida).
        {
            const auto& dep = data.hydrology.deposits;
            const double cell_m2 = static_cast<double>(data.height.cell_size())
                                 * data.height.cell_size();
            std::cout << "deltas: " << dep.deltas << " (" << dep.delta_lobes
                      << " lobulos), area emergida "
                      << dep.delta_cells * cell_m2 / 1.0e6 << " km2, volumen "
                      << dep.delta_volume_m3 << " m3" << std::endl;
            std::cout << "barras: " << dep.bars << ", area emergida "
                      << dep.bar_cells * cell_m2 << " m2" << std::endl;
        }

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

        // Costa rocosa (erosión marina, Sunamura): plataforma de abrasión al
        // pie de los acantilados, farallones y su relación con el oleaje.
        const terrain::RockyCoastMetrics rc = terrain::measure_rocky_coast(
            data.height, data.wave_energy, cfg.sea_level);
        std::cout << "costa: " << rc.coast_cells << " celdas (E p50 "
                  << rc.coast_energy_p50 << ", p90 " << rc.coast_energy_p90
                  << ")" << std::endl;
        std::cout << "costa rocosa: " << rc.cliff_toe_cells
                  << " pies de acantilado (E p50 " << rc.toe_energy_p50
                  << ", p90 " << rc.toe_energy_p90 << "); frente p50 "
                  << rc.toe_face_deg_p50 << " grados" << std::endl;
        std::cout << "  plataforma de abrasion: " << rc.platform_cells
                  << " celdas (" << rc.platform_area_m2 / 10000.0f
                  << " ha); anchura media " << rc.mean_platform_width_m
                  << " m" << std::endl;
        // Fondo somero total al pie (natural + abrasión); la firma de
        // Sunamura (anchura SOCAVADA frente a E) sale en el log de la etapa
        // "abrasion costera" de la generación.
        std::cout << "  correlacion fondo somero al pie-E (natural + abrasion): pearson "
                  << rc.width_energy_pearson << " spearman "
                  << rc.width_energy_spearman << std::endl;
        std::cout << "  farallones (<0.5 ha a <300 m de acantilado): "
                  << rc.stacks << " (" << rc.tall_stacks
                  << " con >3 m); area media " << rc.stack_mean_area_m2
                  << " m2, max " << rc.stack_max_area_m2 << " m2" << std::endl;
    }
    print_landform_stats(data);
    report_river_mouths(data);

    print_ecology_report(data);
    // Precipitación de la ecología (gris, 0 → 2.5× la media; mar en azul).
    if (data.precip_res >= 2) {
        const int pr = data.precip_res;
        std::vector<glm::u8vec3> pimg(static_cast<size_t>(pr) * static_cast<size_t>(pr));
        for (int z = 0; z < pr; ++z) {
            for (int x = 0; x < pr; ++x) {
                const size_t pi = static_cast<size_t>(z) * static_cast<size_t>(pr)
                                + static_cast<size_t>(x);
                const float wx = -cfg.size_m * 0.5f
                               + cfg.size_m * static_cast<float>(x) / static_cast<float>(pr - 1);
                const float wz = -cfg.size_m * 0.5f
                               + cfg.size_m * static_cast<float>(z) / static_cast<float>(pr - 1);
                if (data.height.sample(wx, wz) <= cfg.sea_level) {
                    pimg[pi] = {20, 30, 80};
                    continue;
                }
                const float v = glm::clamp(data.precipitation[pi] / 2.5f, 0.0f, 1.0f);
                pimg[pi] = to8(glm::vec3{v, v, v});
            }
        }
        write_bmp(prefix + "-lluvia.bmp", pr, pr, pimg);
    }

    std::cout << "dumps escritos: " << prefix << "-materiales.bmp / "
              << prefix << "-oleaje.bmp / " << prefix << "-relieve.bmp / "
              << prefix << "-litoral.bmp"
              << std::endl;
    return 0;
}

// Uso: ProceduralTerrain [seed [x z yaw pitch [alt]]] — sin argumentos usa la
// seed del config y el spawn estándar; la pose opcional sirve al lazo de
// verificación visual por capturas (alt = metros sobre el terreno en x,z).
// Modo dump: ProceduralTerrain --dump-costa <prefijo> [seed | preset.json]
// genera el mundo sin ventana y escribe los mapas de materiales y oleaje.
// Modos fuera de pantalla (ventana oculta, ver app/offscreen.h):
// ProceduralTerrain --shots|--record|--bench <entrada> <salida> [opciones]
int main(int argc, char** argv) {
    try {
        if (const auto offscreen = app::parse_offscreen_args(argc, argv)) {
            return app::run_offscreen(*offscreen);
        }
    } catch (const std::exception& e) {
        std::cerr << "offscreen: " << e.what() << std::endl;
        return 1;
    }
    if (argc > 2 && std::string(argv[1]) == "--dump-costa") {
        terrain::TerrainConfig cfg;
        if (argc > 3) {
            const std::string arg = argv[3];
            if (arg.size() > 5 && arg.ends_with(".json")) {
                const auto preset = terrain::load_preset(arg);
                if (!preset) {
                    std::cerr << "no se pudo cargar el preset " << arg << std::endl;
                    return 1;
                }
                if (preset->climate_version < terrain::kPresetClimateVersion) {
                    std::cerr << "aviso: preset con clima v1 (nieve relativa a la cumbre); "
                                 "snow_line_m y treeline_m se leen con la semantica v2\n";
                }
                cfg = preset->config;
            } else {
                cfg.seed = static_cast<std::uint32_t>(std::stoul(arg));
            }
        }
        return dump_costa(argv[2], cfg);
    }
    std::optional<std::uint32_t> seed;
    std::optional<app::CameraPose> pose;
    try {
        if (argc > 1) {
            seed = static_cast<std::uint32_t>(std::stoul(argv[1]));
        }
        if (argc > 5) {
            app::CameraPose p;
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
