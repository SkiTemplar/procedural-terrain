#include "app/scene_renderer.h"

#include "app/world.h"
#include "render/camera.h"
#include "render/frustum.h"
#include "render/shadow_shaders.h"
#include "render/terrain_shaders.h"
#include "terrain/coast.h"
#include "terrain/mesher.h"
#include "ui/debug_ui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace app {

namespace {

// Cascadas de sombra del sol (CSM). 2048² por cascada en DEPTH32F (64 MB las
// cuatro); cubren 2.5 km de profundidad de vista, más allá manda la sombra
// precomputada por vértice.
constexpr int kShadowResolution = 2048;
constexpr float kShadowDistanceM = 2500.0f;
constexpr float kShadowLambda = 0.7f;
// Holgura vertical sobre el AABB del terreno: copas de árbol por encima de la
// cumbre más alta (el depth clamp ya cubre el resto).
constexpr float kCasterHeadroomM = 60.0f;
// Sesgos en texeles de cada cascada (escalan con su resolución en mundo):
// normal offset del receptor y sesgo de profundidad constante; el sesgo por
// pendiente lo pone glPolygonOffset en el pase de profundidad.
constexpr float kNormalOffsetTexels = 1.5f;
constexpr float kDepthBiasTexels = 1.0f;
constexpr float kSlopeBiasFactor = 2.0f;

std::string indexed(const char* name, int i) {
    return std::string(name) + "[" + std::to_string(i) + "]";
}

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
      sky_shader_(render::kSkyVert, render::kSkyFrag),
      shadow_terrain_shader_(render::kShadowTerrainVert, render::kShadowFrag),
      shadow_veg_shader_(render::kShadowVegVert, render::kShadowFrag),
      shadow_map_(kShadowResolution) {
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

        if (state.csm_shadows) {
            render_shadows(*world, camera, aspect, state);
        }
        shadow_map_.bind_texture(PT_SHADOW_MAP_UNIT);

        terrain_shader_.use();
        set_common_uniforms(terrain_shader_, camera, aspect, state);
        set_shadow_uniforms(terrain_shader_, camera, state.csm_shadows);
        terrain_shader_.set_float("u_smooth_blend", 1.0f);
        terrain_shader_.set_int("u_smooth_shading", state.smooth_shading ? 1 : 0);
        world->ground.bind_detail_textures(terrain_shader_, state.detail_textures,
                                           state.detail_texture_intensity);
        for (const render::Mesh* chunk : world->ground.visible_meshes()) {
            chunk->draw();
        }

        veg_shader_.use();
        set_common_uniforms(veg_shader_, camera, aspect, state);
        set_shadow_uniforms(veg_shader_, camera, state.csm_shadows);
        veg_shader_.set_float("u_smooth_blend", 0.0f);
        veg_shader_.set_int("u_smooth_shading", 0);
        veg_shader_.set_int("u_detail_enabled", 0);
        world->vegetation.draw(camera.position, state.veg_draw_distance);

        // Césped del LOD cercano: shader propio con viento + desvanecido
        // por distancia (encoge la altura entre 70 y 104 m → sin pop al
        // repoblar el disco alrededor de la cámara).
        grass_shader_.use();
        set_common_uniforms(grass_shader_, camera, aspect, state);
        set_shadow_uniforms(grass_shader_, camera, state.csm_shadows);
        grass_shader_.set_float("u_time", time_s);
        grass_shader_.set_float("u_grass_fade_start", 70.0f);
        grass_shader_.set_float("u_grass_fade_end", 104.0f);
        world->grass.update(camera.position);
        world->grass.draw();

        draw_water(*world, camera, aspect, state, time_s);
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Pase de profundidad del sol, una vez por cascada. Proyectan el terreno (con
// el LOD de la cámara) y la vegetación instanciada, cada uno recortado contra
// el volumen ortográfico de la cascada y no contra el frustum de la cámara:
// una ladera o un árbol fuera de pantalla también sombrea lo visible. El
// depth clamp aplasta al plano cercano lo que quede delante (sombra correcta).
void SceneRenderer::render_shadows(World& world, const render::Camera& camera, float aspect,
                                   const ui::DebugState& state) {
    GLint prev_fbo = 0;
    GLint prev_viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    render::CascadeSetup setup;
    setup.fov_y_rad = glm::radians(render::Camera::kFovDeg);
    setup.aspect = aspect;
    setup.near_m = render::Camera::kNear;
    setup.shadow_distance_m = kShadowDistanceM;
    setup.lambda = kShadowLambda;
    setup.resolution = shadow_map_.resolution();
    const glm::vec3 headroom{0.0f, kCasterHeadroomM, 0.0f};
    cascades_ = render::fit_cascades(setup, camera.position, camera.forward(), state.sun_dir(),
                                     world.ground.bounds_min(),
                                     world.ground.bounds_max() + headroom);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_DEPTH_CLAMP);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(kSlopeBiasFactor, 0.0f);
    for (int i = 0; i < render::kShadowCascadeCount; ++i) {
        const render::ShadowCascade& c = cascades_[static_cast<size_t>(i)];
        const render::Frustum light(c.light_vp);
        shadow_map_.begin_cascade(i);

        shadow_terrain_shader_.use();
        shadow_terrain_shader_.set_mat4("u_light_vp", c.light_vp);
        casters_.clear();
        world.ground.collect_meshes(light, casters_);
        for (const render::Mesh* mesh : casters_) {
            mesh->draw();
        }

        shadow_veg_shader_.use();
        shadow_veg_shader_.set_mat4("u_light_vp", c.light_vp);
        world.vegetation.draw_casters(light, camera.position, state.veg_draw_distance);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_CLAMP);
    glPolygonMode(GL_FRONT_AND_BACK, state.wireframe ? GL_LINE : GL_FILL);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
}

// Uniforms de recepción de la CSM. Los sesgos se pasan ya en las unidades del
// shader: normal offset en metros y sesgo de profundidad en [0,1] del depth
// ortográfico (lineal: metros / recorrido del rango del sol).
void SceneRenderer::set_shadow_uniforms(const render::Shader& shader,
                                        const render::Camera& camera, bool enabled) const {
    shader.set_int("u_csm_enabled", enabled ? 1 : 0);
    shader.set_vec3("u_cam_forward", camera.forward());
    for (int i = 0; i < render::kShadowCascadeCount; ++i) {
        const render::ShadowCascade& c = cascades_[static_cast<size_t>(i)];
        shader.set_mat4(indexed("u_csm_vp", i).c_str(), c.light_vp);
        shader.set_float(indexed("u_csm_split", i).c_str(), c.split_far);
        shader.set_float(indexed("u_csm_offset", i).c_str(), kNormalOffsetTexels * c.texel_m);
        shader.set_float(indexed("u_csm_bias", i).c_str(),
                         kDepthBiasTexels * c.texel_m / std::max(c.depth_range_m, 1.0f));
    }
}

// Pase de cielo (C4-L4): dispersión Rayleigh + Mie y disco solar a pantalla
// completa, sin depth (el mundo se pinta encima). Siempre relleno, aunque el modo wireframe
// esté activo — el cielo no es malla que inspeccionar.
void SceneRenderer::draw_sky(const render::Camera& camera, float aspect,
                             const ui::DebugState& state) {
    glDisable(GL_DEPTH_TEST);
    sky_shader_.use();
    const glm::mat4 view_rot = glm::mat4(glm::mat3(camera.view()));
    sky_shader_.set_mat4("u_inv_vp", glm::inverse(camera.proj(aspect) * view_rot));
    sky_shader_.set_vec3("u_sun_dir", state.sun_dir());
    sky_shader_.set_vec3("u_camera_pos", camera.position);
    sky_shader_.set_float("u_fog_density", state.fog_density);
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
