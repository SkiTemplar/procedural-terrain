#include "app/scene_renderer.h"

#include "app/world.h"
#include "render/camera.h"
#include "render/terrain_shaders.h"
#include "terrain/coast.h"
#include "terrain/mesher.h"
#include "ui/debug_ui.h"

#include <glm/mat4x4.hpp>

#include <cmath>

namespace app {

namespace {

void set_common_uniforms(const render::Shader& shader, const render::Camera& camera,
                         float aspect, const ui::DebugState& state) {
    shader.set_mat4("u_view", camera.view());
    shader.set_mat4("u_proj", camera.proj(aspect));
    shader.set_vec3("u_sun_dir", state.sun_dir());
    shader.set_vec3("u_camera_pos", camera.position);
    shader.set_float("u_fog_density", state.fog_density);
}

} // namespace

SceneRenderer::SceneRenderer()
    : terrain_shader_(render::kTerrainVert, render::kTerrainFrag),
      veg_shader_(render::kVegVert, render::kTerrainFrag),
      grass_shader_(render::kGrassVert, render::kGrassFrag),
      water_shader_(render::kWaterVert, render::kWaterFrag),
      sky_shader_(render::kSkyVert, render::kSkyFrag) {
    glGenVertexArrays(1, &sky_vao_);
}

SceneRenderer::~SceneRenderer() {
    glDeleteVertexArrays(1, &sky_vao_);
}

void SceneRenderer::draw(World* world, const render::Camera& camera, float aspect,
                         ui::DebugState& state, float time_s, int max_rebuilds) {
    glClearColor(kSkyColor.r, kSkyColor.g, kSkyColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_sky(camera, aspect, state);

    glPolygonMode(GL_FRONT_AND_BACK, state.wireframe ? GL_LINE : GL_FILL);
    if (world) {
        world->ground.update(camera.position, camera.proj(aspect) * camera.view(),
                             max_rebuilds);
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

        terrain_shader_.use();
        set_common_uniforms(terrain_shader_, camera, aspect, state);
        terrain_shader_.set_float("u_smooth_blend", 1.0f);
        terrain_shader_.set_int("u_smooth_shading", state.smooth_shading ? 1 : 0);
        world->ground.bind_detail_textures(terrain_shader_, state.detail_textures,
                                           state.detail_texture_intensity);
        for (const render::Mesh* chunk : world->ground.visible_meshes()) {
            chunk->draw();
        }

        veg_shader_.use();
        set_common_uniforms(veg_shader_, camera, aspect, state);
        veg_shader_.set_float("u_smooth_blend", 0.0f);
        veg_shader_.set_int("u_smooth_shading", 0);
        veg_shader_.set_int("u_detail_enabled", 0);
        world->vegetation.draw(camera.position, state.veg_draw_distance);

        // Césped del LOD cercano: shader propio con viento + desvanecido
        // por distancia (encoge la altura entre 70 y 104 m → sin pop al
        // repoblar el disco alrededor de la cámara).
        grass_shader_.use();
        set_common_uniforms(grass_shader_, camera, aspect, state);
        grass_shader_.set_float("u_time", time_s);
        grass_shader_.set_float("u_grass_fade_start", 70.0f);
        grass_shader_.set_float("u_grass_fade_end", 104.0f);
        world->grass.update(camera.position);
        world->grass.draw();

        draw_water(*world, camera, aspect, state, time_s);
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Pase de cielo (C4-L4): gradiente + disco solar a pantalla completa, sin
// depth (el mundo se pinta encima). Siempre relleno, aunque el modo wireframe
// esté activo — el cielo no es malla que inspeccionar.
void SceneRenderer::draw_sky(const render::Camera& camera, float aspect,
                             const ui::DebugState& state) {
    glDisable(GL_DEPTH_TEST);
    sky_shader_.use();
    const glm::mat4 view_rot = glm::mat4(glm::mat3(camera.view()));
    sky_shader_.set_mat4("u_inv_vp", glm::inverse(camera.proj(aspect) * view_rot));
    sky_shader_.set_vec3("u_sun_dir", state.sun_dir());
    sky_shader_.set_vec3("u_camera_pos", camera.position);
    glBindVertexArray(sky_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// Agua después del terreno, con blending.
void SceneRenderer::draw_water(World& world, const render::Camera& camera,
                               float aspect, const ui::DebugState& state,
                               float time_s) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    water_shader_.use();
    set_common_uniforms(water_shader_, camera, aspect, state);
    water_shader_.set_float("u_time", time_s);
    water_shader_.set_float("u_domain_half", world.data.config.size_m * 0.5f);
    // C4-W4: el oleaje viaja con la rosa de vientos. wind_dir_deg es DE dónde
    // sopla (brújula: 0 = del norte = -z); las olas van al sentido contrario.
    const float wr = world.data.config.wind_dir_deg * 3.14159265f / 180.0f;
    water_shader_.set_vec3("u_wave_dir", {-std::sin(wr), 0.0f, std::cos(wr)});
    water_shader_.set_float("u_wind_focus", world.data.config.wind_focus);
    water_shader_.set_float("u_wave_hmax", terrain::kOpenSeaWaveHeightM);
    water_shader_.set_float("u_breaker_index", terrain::kBreakerIndex);
    water_shader_.set_float("u_sea_level", world.data.config.sea_level);
    water_shader_.set_float("u_sea_half",
                            terrain::sea_plane_half(world.data.config.size_m));
    water_shader_.set_float("u_sea_grid", static_cast<float>(terrain::kSeaPlaneRes));
    water_shader_.set_int("u_seabed", 0);
    water_shader_.set_int("u_sea_clip", 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, world.seabed.clip_id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, world.seabed.id);
    water_shader_.set_int("u_is_sea", 1);
    world.sea.draw();
    water_shader_.set_int("u_is_sea", 0);
    if (world.water) {
        world.water->draw();
    }
    glDisable(GL_BLEND);
}

} // namespace app
