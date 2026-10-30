#pragma once

#include <glm/glm.hpp>

#include <array>

// Geometría de las cascadas de sombra del sol (CSM). Pura: sin OpenGL, la
// usan el pase de profundidad (render/shadow_map) y los tests.
namespace render {

inline constexpr int kShadowCascadeCount = 4;

// Parámetros de la cámara y del reparto de cascadas.
struct CascadeSetup {
    float fov_y_rad = 1.2217f;       // apertura vertical de la cámara
    float aspect = 16.0f / 9.0f;
    float near_m = 0.5f;             // plano cercano de la cámara
    float shadow_distance_m = 2500.0f; // profundidad de vista que cubren las cascadas
    float lambda = 0.7f;             // 0 = splits uniformes, 1 = logarítmicos
    int resolution = 2048;           // texeles por lado de cada cascada
};

struct ShadowCascade {
    glm::mat4 light_vp{1.0f};  // mundo -> clip ortográfico del sol
    float split_near = 0.0f;   // profundidad de vista (m) donde empieza
    float split_far = 0.0f;    // profundidad de vista (m) donde acaba
    float texel_m = 0.0f;      // lado de un texel de la cascada en mundo
    float depth_range_m = 0.0f; // recorrido del depth [0,1] en metros
};

using ShadowCascades = std::array<ShadowCascade, kShadowCascadeCount>;

// Reparto práctico de Zhang et al. (2006): mezcla lambda de los splits
// logarítmicos (densidad de texel constante en pantalla) y uniformes (que no
// gastan casi toda la resolución en los primeros metros).
std::array<float, kShadowCascadeCount + 1> practical_splits(float near_m, float far_m,
                                                            float lambda);

// Esfera mínima que envuelve la rodaja [near_m, far_m] del frustum: devuelve
// la distancia del centro sobre el eje de vista (x) y el radio (y). El radio
// solo depende de la rodaja, no de la orientación de la cámara: la huella de
// la cascada no cambia de tamaño al girar (sin parpadeo de texeles).
glm::vec2 slice_bounding_sphere(float near_m, float far_m, float fov_y_rad, float aspect);

// Rotación mundo -> espacio del sol (mira en -sun_dir). Fija para un sol fijo:
// la rejilla de texeles de la sombra no gira con la cámara.
glm::mat4 light_rotation(const glm::vec3& sun_dir);

// Cascadas estables (Valient 2008, "Stable cascaded shadow maps"): esfera
// envolvente de cada rodaja y centro redondeado a la rejilla de texeles en el
// espacio del sol, de modo que al trasladar la cámara la imagen de la sombra
// se desplaza en texeles enteros. El rango de profundidad abarca todo el AABB
// del mundo: cualquier oclusor entre el receptor y el sol queda dentro.
ShadowCascades fit_cascades(const CascadeSetup& setup, const glm::vec3& cam_pos,
                            const glm::vec3& cam_forward, const glm::vec3& sun_dir,
                            const glm::vec3& world_min, const glm::vec3& world_max);

} // namespace render
