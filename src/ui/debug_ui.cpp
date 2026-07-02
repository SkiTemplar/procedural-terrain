#include "ui/debug_ui.h"

#include "player/controller.h"
#include "render/camera.h"

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
                   player::Controller& controller, float dt) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Debug");
    ImGui::Text("%.1f fps (%.2f ms)", 1.0f / dt, dt * 1000.0f);
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
        ImGui::SliderFloat("altura colinas", &cfg.base_height_m, 30.0f, 500.0f, "%.0f m");
        ImGui::SliderFloat("altura montanas", &cfg.mountain_height_m, 0.0f, 2500.0f, "%.0f m");
        ImGui::SliderInt("gotas erosion", &cfg.erosion_droplets, 0, 600000);
        ImGui::SliderFloat("erosion fluvial", &cfg.fluvial_strength, 0.0f, 1.0f);
        ImGui::SliderFloat("umbral rios", &cfg.river_flow_threshold, 100.0f, 5000.0f, "%.0f");
        ImGui::SliderFloat("anchura rios", &cfg.river_max_width_m, 4.0f, 30.0f, "%.0f m");
        ImGui::SliderFloat("aplanado costa", &cfg.coast_flatten_strength, 0.0f, 1.0f);
        ImGui::SliderFloat("detalle fino", &cfg.detail_amplitude_m, 0.0f, 3.0f, "%.1f m");
        ImGui::SliderFloat("linea de nieve", &cfg.snow_line_m, 300.0f, 2400.0f, "%.0f m");

        if (ImGui::Button("Regenerar", ImVec2(-1.0f, 0.0f))) {
            state.regenerate_requested = true;
        }
        if (state.last_generation_seconds > 0.0f) {
            ImGui::TextDisabled("ultima generacion: %.1f s", state.last_generation_seconds);
        }
    }

    ImGui::TextDisabled("Esc: soltar/capturar raton · F: volar/caminar");
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace ui
