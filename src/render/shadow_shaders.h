#pragma once

#include "render/terrain_shader_constants.h"

// Sombras del sol en tiempo real (CSM, 4 cascadas). Bloque GLSL de recepción
// que comparten el fragment de terreno/vegetación y el del césped, y los
// shaders del pase de profundidad.

// Unidad fija del sampler de sombras (layout(binding)): alejada de las de
// detalle (1-4) y del fondo marino (0).
#define PT_SHADOW_MAP_UNIT 7
#define PT_SHADOW_CASCADES 4

namespace render {

// Recepción. Requiere `uniform vec3 u_camera_pos;` declarado antes.
// - Cascada por profundidad de vista; en el último 15 % de cada una se funde
//   con la siguiente (y la última con "al sol"): sin costuras visibles.
// - Normal offset (Holbein 2011): el punto de consulta se separa de la
//   superficie a lo largo de la normal geométrica una fracción del texel de su
//   cascada, más un pequeño sesgo de profundidad. Ambos escalan con el texel,
//   así que el acné desaparece en todas las cascadas sin despegar la sombra
//   (peter-panning) más de ~1 texel.
// - PCF 3x3 de lecturas con comparación hardware: cada lectura ya es un PCF
//   bilineal 2x2, el conjunto es un filtro de 4x4 texeles con caída suave.
//   textureGrad con gradiente nulo: válido dentro de ramas no uniformes.
#define PT_GLSL_SHADOW_COMMON R"glsl(
uniform int u_csm_enabled;
uniform vec3 u_cam_forward;
uniform mat4 u_csm_vp[)glsl" PT_GLSL_STR(PT_SHADOW_CASCADES) R"glsl(];
uniform float u_csm_split[)glsl" PT_GLSL_STR(PT_SHADOW_CASCADES) R"glsl(];  // fin de cada cascada (m de vista)
uniform float u_csm_offset[)glsl" PT_GLSL_STR(PT_SHADOW_CASCADES) R"glsl(]; // normal offset (m)
uniform float u_csm_bias[)glsl" PT_GLSL_STR(PT_SHADOW_CASCADES) R"glsl(];   // sesgo de profundidad [0,1]
layout(binding = )glsl" PT_GLSL_STR(PT_SHADOW_MAP_UNIT) R"glsl() uniform sampler2DArrayShadow u_csm_map;

float csm_pcf(int c, vec3 p, vec3 n) {
    vec4 ls = u_csm_vp[c] * vec4(p + n * u_csm_offset[c], 1.0);
    vec3 uvz = ls.xyz * 0.5 + 0.5; // ortográfica: w = 1
    float texel = 1.0 / float(textureSize(u_csm_map, 0).x);
    float ref = uvz.z - u_csm_bias[c];
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 uv = uvz.xy + vec2(float(x), float(y)) * texel;
            sum += textureGrad(u_csm_map, vec4(uv, float(c), ref), vec2(0.0), vec2(0.0));
        }
    }
    return sum * (1.0 / 9.0);
}

// Visibilidad del sol en [0,1] (1 = al sol). Fuera de la última cascada
// devuelve 1: el llamador la combina con la sombra precomputada.
float csm_visibility(vec3 p, vec3 n) {
    if (u_csm_enabled == 0) return 1.0;
    float depth = dot(p - u_camera_pos, u_cam_forward);
    int c = 0;
    while (c < )glsl" PT_GLSL_STR(PT_SHADOW_CASCADES) R"glsl( && depth > u_csm_split[c]) ++c;
    if (c >= )glsl" PT_GLSL_STR(PT_SHADOW_CASCADES) R"glsl() return 1.0;
    float start = (c == 0) ? 0.0 : u_csm_split[c - 1];
    float fade = smoothstep(0.85, 1.0, (depth - start) / (u_csm_split[c] - start));
    float vis = csm_pcf(c, p, n);
    if (fade > 0.0) {
        float next = (c + 1 < )glsl" PT_GLSL_STR(PT_SHADOW_CASCADES) R"glsl() ? csm_pcf(c + 1, p, n) : 1.0;
        vis = mix(vis, next, fade);
    }
    return vis;
}
)glsl"

// Pase de profundidad: solo posición en el espacio del sol, sin fragment útil.
constexpr const char* kShadowTerrainVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
uniform mat4 u_light_vp;
void main() {
    gl_Position = u_light_vp * vec4(a_pos, 1.0);
}
)glsl";

// Misma transformación por instancia que kVegVert (giro Y + escala + traslación).
constexpr const char* kShadowVegVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
layout (location = 2) in vec4 i_pos_scale; // xyz posición, w escala
layout (location = 3) in vec4 i_tint_yaw;  // rgb tinte, w giro (rad)
uniform mat4 u_light_vp;
uniform vec3 u_camera_pos;
uniform float u_veg_fade_start;
uniform float u_veg_fade_end;
void main() {
    float c = cos(i_tint_yaw.w);
    float s = sin(i_tint_yaw.w);
    // Mismo desvanecido lejano que kVegVert: la sombra encoge con su árbol.
    float d = length(i_pos_scale.xz - u_camera_pos.xz);
    float fade = 1.0 - smoothstep(u_veg_fade_start, u_veg_fade_end, d);
    vec3 p = a_pos * (i_pos_scale.w * fade);
    p = vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z) + i_pos_scale.xyz;
    gl_Position = u_light_vp * vec4(p, 1.0);
}
)glsl";

constexpr const char* kShadowFrag = R"glsl(
#version 460 core
void main() {}
)glsl";

} // namespace render
