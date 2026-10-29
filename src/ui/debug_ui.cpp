#include "ui/debug_ui.h"

#include "player/controller.h"
#include "render/camera.h"
#include "ui/field_maps.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/trigonometric.hpp>

namespace ui {

glm::vec3 DebugState::sun_dir() const {
    const float az = glm::radians(sun_azimuth_deg);
    const float alt = glm::radians(sun_altitude_deg);
    return glm::normalize(glm::vec3{
        glm::cos(alt) * glm::cos(az),
        glm::sin(alt),
        glm::cos(alt) * glm::sin(az),
    });
}

DebugUi::DebugUi(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
}

DebugUi::~DebugUi() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void DebugUi::draw(DebugState& state, render::Camera& camera,
                   player::Controller& controller, float dt,
                   const FieldMaps* fields) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Debug");
    ImGui::Text("%.1f fps (%.2f ms)", 1.0f / dt, dt * 1000.0f);
    if (state.total_chunks > 0) {
        ImGui::Text("chunks: %d/%d", state.visible_chunks, state.total_chunks);
    }
    ImGui::Text("pos: %.0f, %.0f, %.0f  [%s]", camera.position.x, camera.position.y,
                camera.position.z, controller.flying ? "volando (F)" : "caminando (F)");

    if (ImGui::CollapsingHeader("Vista", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("velocidad vuelo", &camera.fly_speed, 5.0f, 300.0f, "%.0f m/s");
        ImGui::SliderFloat("sol: azimut", &state.sun_azimuth_deg, 0.0f, 360.0f, "%.0f deg");
        ImGui::SliderFloat("sol: altura", &state.sun_altitude_deg, 5.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("niebla", &state.fog_density, 0.0f, 0.001f, "%.5f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("dist. vegetacion", &state.veg_draw_distance, 500.0f,
                           6000.0f, "%.0f m");
        ImGui::Checkbox("Texturas de detalle", &state.detail_textures);
        ImGui::BeginDisabled(!state.detail_textures);
        ImGui::SliderFloat("intensidad texturas", &state.detail_texture_intensity,
                           0.0f, 1.5f, "%.2f");
        ImGui::EndDisabled();
        ImGui::Checkbox("Sombreado suave", &state.smooth_shading);
        ImGui::Checkbox("wireframe", &state.wireframe);
    }

    if (ImGui::CollapsingHeader("Generacion", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& cfg = state.pending_config;
        int seed = static_cast<int>(cfg.seed);
        if (ImGui::InputInt("seed", &seed)) {
            cfg.seed = static_cast<std::uint32_t>(seed);
        }
        ImGui::SliderFloat("tamano isla", &cfg.size_m, 2000.0f, 12000.0f, "%.0f m");
        ImGui::SliderFloat("radio tierra", &cfg.island_radius, 0.2f, 0.85f);
        ImGui::SliderFloat("costa irregular", &cfg.coast_warp_m, 0.0f, 1500.0f, "%.0f m");
        ImGui::SliderFloat("transgresion marina", &cfg.coast_flood_m, 0.0f, 20.0f, "%.0f m");
        ImGui::SliderFloat("colinillas", &cfg.base_height_m, 0.0f, 80.0f, "%.0f m");
        ImGui::SliderFloat("altura montanas", &cfg.mountain_height_m, 0.0f, 2500.0f, "%.0f m");
        ImGui::SliderInt("gotas erosion", &cfg.erosion_droplets, 0, 600000);
        ImGui::SliderFloat("erosion fluvial", &cfg.fluvial_strength, 0.0f, 1.0f);
        ImGui::SliderFloat("umbral rios", &cfg.river_flow_threshold, 100.0f, 5000.0f, "%.0f");
        ImGui::SliderFloat("anchura rios", &cfg.river_max_width_m, 4.0f, 30.0f, "%.0f m");
        ImGui::SliderFloat("aplanado costa", &cfg.coast_flatten_strength, 0.0f, 1.0f);
        ImGui::SliderFloat("viento dominante", &cfg.wind_dir_deg, 0.0f, 360.0f, "%.0f grados");
        ImGui::SliderFloat("focalizacion oleaje", &cfg.wind_focus, 0.0f, 1.0f);
        ImGui::SliderFloat("anchura orillas", &cfg.shore_width_scale, 0.3f, 2.5f);
        ImGui::SliderFloat("detalle fino", &cfg.detail_amplitude_m, 0.0f, 3.0f, "%.1f m");
        ImGui::SliderFloat("linea de nieve", &cfg.snow_line_m, 300.0f, 2400.0f, "%.0f m");

        ImGui::BeginDisabled(state.generation_phase != nullptr);
        if (ImGui::Button("Regenerar", ImVec2(-1.0f, 0.0f))) {
            state.regenerate_requested = true;
        }
        ImGui::EndDisabled();
        if (state.last_generation_seconds > 0.0f) {
            ImGui::TextDisabled("ultima generacion: %.1f s", state.last_generation_seconds);
        }

        // Presets (C1): guardar el mundo actual / cargar y regenerar.
        ImGui::Separator();
        ImGui::Text("Presets");
        ImGui::InputText("nombre##preset", state.preset_name,
                         sizeof(state.preset_name));
        ImGui::BeginDisabled(state.generation_phase != nullptr);
        if (ImGui::Button("Guardar preset", ImVec2(-1.0f, 0.0f))) {
            state.save_preset_requested = true;
        }
        for (const auto& name : state.preset_list) {
            if (ImGui::Button(("Cargar '" + name + "'").c_str(),
                              ImVec2(-1.0f, 0.0f))) {
                state.load_preset_name = name;
            }
        }
        ImGui::EndDisabled();
        if (!state.preset_status.empty()) {
            ImGui::TextDisabled("%s", state.preset_status.c_str());
        }
    }

    // Minimapas de campos intermedios (C2). Solo se dibujan con la pestaÃ±a
    // abierta â€” las texturas ya viven en VRAM, el coste cerrado es cero.
    if (fields != nullptr && !fields->maps().empty()
        && ImGui::CollapsingHeader("Campos")) {
        for (const auto& m : fields->maps()) {
            ImGui::Text("%s", m.name.c_str());
            ImGui::Image(static_cast<ImTextureID>(
                             static_cast<intptr_t>(m.texture)),
                         ImVec2(256.0f, 256.0f));
        }
    }

    if (ImGui::CollapsingHeader("Vegetacion")) {
        static const char* kVegNames[6] = {"pinos",    "frondosos", "rocas",
                                           "matas",    "arbustos",  "troncos"};
        size_t total = 0;
        for (int t = 0; t < 6; ++t) {
            ImGui::Text("%-10s %zu", kVegNames[t], state.veg_counts[t]);
            total += state.veg_counts[t];
        }
        ImGui::Separator();
        ImGui::Text("total      %zu", total);
        ImGui::TextDisabled("(rocas=0 por decision 2026-07-08)");
    }

    ImGui::TextDisabled("Esc: soltar/capturar raton Â· F: volar/caminar");
    ImGui::End();

    // Overlay central mientras el hilo de trabajo cuece el mundo: la ventana
    // sigue viva (antes se quedaba congelada ~3 min en Debug al arrancar).
    if (state.generation_phase != nullptr) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##generando", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                         | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav
                         | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("Generando mundo: %s", state.generation_phase);
        ImGui::TextDisabled("%.0f s", state.generation_elapsed);
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace ui
