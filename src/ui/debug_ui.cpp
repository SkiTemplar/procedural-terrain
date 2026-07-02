#include "ui/debug_ui.h"

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

void DebugUi::draw(DebugState& state, render::Camera& camera, float dt) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Debug");
    ImGui::Text("%.1f fps (%.2f ms)", 1.0f / dt, dt * 1000.0f);
    ImGui::Text("pos: %.0f, %.0f, %.0f", camera.position.x, camera.position.y,
                camera.position.z);
    ImGui::SliderFloat("velocidad vuelo", &camera.fly_speed, 5.0f, 300.0f, "%.0f m/s");
    ImGui::SliderFloat("sol: azimut", &state.sun_azimuth_deg, 0.0f, 360.0f, "%.0f deg");
    ImGui::SliderFloat("sol: altura", &state.sun_altitude_deg, 5.0f, 90.0f, "%.0f deg");
    ImGui::SliderFloat("niebla", &state.fog_density, 0.0f, 0.002f, "%.5f", ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("wireframe", &state.wireframe);
    ImGui::TextDisabled("Esc: soltar/capturar raton");
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace ui
