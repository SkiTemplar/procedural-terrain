#pragma once

namespace render {

// Flat shading sin duplicar vértices: la normal por cara sale de las derivadas
// de pantalla (dFdx/dFdy) y el color va `flat` (un color por triángulo).
constexpr const char* kTerrainVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
layout (location = 1) in vec3 a_color;
layout (location = 2) in vec3 a_normal;

uniform mat4 u_view;
uniform mat4 u_proj;

out vec3 v_world_pos;
flat out vec3 v_color;
out vec3 v_color_smooth;
out vec3 v_normal_smooth;

void main() {
    v_world_pos = a_pos;
    v_color = a_color;
    v_color_smooth = a_color;
    v_normal_smooth = a_normal;
    gl_Position = u_proj * u_view * vec4(a_pos, 1.0);
}
)glsl";

constexpr const char* kTerrainFrag = R"glsl(
#version 460 core
in vec3 v_world_pos;
flat in vec3 v_color;
in vec3 v_color_smooth;
in vec3 v_normal_smooth;

uniform vec3 u_sun_dir;      // hacia el sol, normalizado
uniform vec3 u_camera_pos;
uniform vec3 u_sky_color;
uniform float u_fog_density;  // exponencial
uniform float u_smooth_blend; // 1 = mezclar a suave en distancia; 0 = solo flat

out vec4 frag_color;

float hash12(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    // Suavizado de sombreado del SUELO (HP-6, feedback 2026-07-08): las
    // facetas subdivididas (~0.3-0.6 m) daban un sombreado a cuadros/pixelado
    // al caminar ("divisiones en el suelo"). La normal y el color mezclan a la
    // versión suave ya en el campo cercano: plano solo en los ~4 m inmediatos
    // (mantiene textura al pie), totalmente suave a 40 m. El low-poly persiste
    // en la SILUETA facetada; el sombreado deja de leerse como rejilla.
    float dist_cam = length(v_world_pos - u_camera_pos);
    float smooth_k = u_smooth_blend * clamp((dist_cam - 4.0) / 36.0, 0.0, 1.0);

    vec3 n_flat = normalize(cross(dFdx(v_world_pos), dFdy(v_world_pos)));
    vec3 n_soft = normalize(v_normal_smooth + vec3(1e-5));
    vec3 normal = normalize(mix(n_flat, n_soft, smooth_k));
    vec3 albedo = mix(v_color, v_color_smooth, smooth_k);

    // Iluminación de exterior: sol cálido + ambiente hemisférico (las caras
    // que miran al cielo reciben azul frío; las que miran al suelo, rebote
    // verdoso cálido). Mucho más natural que un ambient constante.
    const vec3 sun_color = vec3(1.0, 0.95, 0.86);
    float diffuse = max(dot(normal, u_sun_dir), 0.0);
    float sky_facing = normal.y * 0.5 + 0.5;
    vec3 hemi = mix(vec3(0.30, 0.31, 0.26), vec3(0.55, 0.65, 0.82), sky_facing);
    vec3 lit = albedo * (0.50 * hemi + 0.72 * diffuse * sun_color);

    // Textura procedural sutil en dos escalas (~1.5 m y ~9 m): rompe el color
    // plano de cada cara sin perder la estética low-poly. Se atenúa de lejos
    // y a partir de ahí ni se calcula (8 hashes por píxel que no se ven).
    float tex_fade = exp(-dist_cam * 0.0035);
    if (tex_fade > 0.02) {
        float tex = 0.55 * vnoise(v_world_pos.xz * 0.65)
                  + 0.45 * vnoise(v_world_pos.xz * 0.11);
        lit *= 1.0 + (tex - 0.5) * 0.10 * tex_fade;
    }

    float fog = 1.0 - exp(-u_fog_density * dist_cam);
    vec3 color = mix(lit, u_sky_color, fog);

    frag_color = vec4(color, 1.0);
}
)glsl";

// Vegetación instanciada: giro Y + escala + traslación por instancia.
// Comparte fragment shader con el terreno (kTerrainFrag).
constexpr const char* kVegVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
layout (location = 1) in vec3 a_color;
layout (location = 2) in vec4 i_pos_scale; // xyz posición, w escala
layout (location = 3) in vec4 i_tint_yaw;  // rgb tinte, w giro (rad)

uniform mat4 u_view;
uniform mat4 u_proj;

out vec3 v_world_pos;
flat out vec3 v_color;
out vec3 v_color_smooth;
out vec3 v_normal_smooth;

void main() {
    float c = cos(i_tint_yaw.w);
    float s = sin(i_tint_yaw.w);
    vec3 p = a_pos * i_pos_scale.w;
    p = vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z) + i_pos_scale.xyz;
    v_world_pos = p;
    v_color = a_color * i_tint_yaw.rgb;
    v_color_smooth = v_color;
    v_normal_smooth = vec3(0.0, 1.0, 0.0); // sin uso: la vegetación va con u_smooth_blend=0
    gl_Position = u_proj * u_view * vec4(p, 1.0);
}
)glsl";

// Césped (veg/grass_field): planos de brizna con VIENTO y desvanecido por
// distancia. El fade lo hace el shader cada frame (encoge la altura con la
// lejanía) — así no hay "pop" cuando el campo se reconstruye al moverse: las
// briznas del borde ya están a altura cero. a_pos.y (0 raíz → ~1 punta) manda
// cuánto se dobla con el viento y cuánto encoge.
constexpr const char* kGrassVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;   // esquina del card (x ancho, y 0..1, z)
layout (location = 1) in vec3 a_uv;    // color del vértice = (u, v, 0): UV del card
layout (location = 2) in vec4 i_pos_scale; // xyz raíz en mundo, w altura (m)
layout (location = 3) in vec4 i_tint_yaw;  // rgb tinte, w giro (rad)

uniform mat4 u_view;
uniform mat4 u_proj;
uniform vec3 u_camera_pos;
uniform float u_time;
uniform float u_grass_fade_start; // m: empieza a encoger
uniform float u_grass_fade_end;   // m: altura cero (fin del césped)

out vec3 v_world_pos;
out vec2 v_uv;
flat out vec3 v_tint;

void main() {
    float c = cos(i_tint_yaw.w);
    float s = sin(i_tint_yaw.w);
    vec3 root = i_pos_scale.xyz;

    // Solo la ALTURA escala por zona; el ancho del card queda fijo (~0.5 m).
    vec3 local = vec3(a_pos.x, a_pos.y * i_pos_scale.w, a_pos.z);
    local = vec3(c * local.x - s * local.z, local.y, s * local.x + c * local.z);

    float up = clamp(a_pos.y, 0.0, 1.0);
    float dist = length(root.xz - u_camera_pos.xz);
    float fade = clamp((u_grass_fade_end - dist)
                       / max(u_grass_fade_end - u_grass_fade_start, 1.0), 0.0, 1.0);

    // Viento: ola por el campo + racha lenta; la punta se dobla en cuadrático.
    vec2 wind_dir = vec2(0.80, 0.60);
    float wave = sin(u_time * 1.7 + root.x * 0.16 + root.z * 0.13);
    float gust = 0.5 + 0.5 * sin(u_time * 0.45 + root.x * 0.02 - root.z * 0.015);
    float bend = (0.12 + 0.14 * gust) * up * up * i_pos_scale.w
               * (0.55 + 0.45 * wave);
    vec3 wind = vec3(wind_dir.x, 0.0, wind_dir.y) * bend;

    // El fade encoge el card hasta cero con la distancia: sin "pop" al
    // reconstruir el disco (el borde ya está a tamaño ~0).
    vec3 world = root + (local + wind) * fade;
    v_world_pos = world;
    v_uv = a_uv.xy;
    v_tint = i_tint_yaw.rgb;
    gl_Position = u_proj * u_view * vec4(world, 1.0);
}
)glsl";

constexpr const char* kGrassFrag = R"glsl(
#version 460 core
in vec3 v_world_pos;
in vec2 v_uv;
flat in vec3 v_tint;

uniform vec3 u_sun_dir;
uniform vec3 u_camera_pos;
uniform vec3 u_sky_color;
uniform float u_fog_density;

out vec4 frag_color;

float hash(float n) { return fract(sin(n) * 43758.5453); }

void main() {
    // Briznas procedurales dentro del card: u se reparte en columnas; cada
    // columna es una brizna que afina hacia arriba y tiene su propia altura.
    // Fuera de la brizna se DESCARTA el fragmento → césped denso sin geometría.
    const float cols = 7.0;
    float x = v_uv.x * cols;
    float cell = floor(x);
    float fx = fract(x);
    float r = hash(cell + 3.17);
    float center = 0.5 + (r - 0.5) * 0.5;
    // Brizna FINA con hueco entre columnas: media anchura ~0.13 en la raíz
    // afina a ~0.02 en la punta (antes 0.32 llenaba el card entero -> se veía
    // como una tarjeta sólida, no como hierba).
    float half_w = 0.13 * (1.0 - v_uv.y * 0.85) + 0.02;
    float top = 0.5 + 0.5 * hash(cell + 9.1);       // altura de cada brizna
    if (abs(fx - center) > half_w || v_uv.y > top) {
        discard;
    }

    vec3 root_c = vec3(0.19, 0.34, 0.13);
    vec3 tip_c = vec3(0.42, 0.58, 0.26);
    vec3 col = mix(root_c, tip_c,
                   clamp(v_uv.y / max(top, 0.05), 0.0, 1.0)) * v_tint;

    vec3 normal = normalize(cross(dFdx(v_world_pos), dFdy(v_world_pos)));
    float diffuse = max(abs(dot(normal, u_sun_dir)), 0.0); // hoja fina: 2 caras
    vec3 lit = col * (0.50 + 0.55 * diffuse);
    float dist = length(v_world_pos - u_camera_pos);
    float fog = 1.0 - exp(-u_fog_density * dist);
    frag_color = vec4(mix(lit, u_sky_color, fog), 1.0);
}
)glsl";

// Agua: olas suaves por vértice, semitransparente, con la misma niebla.
constexpr const char* kWaterVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
layout (location = 1) in vec3 a_color;

uniform mat4 u_view;
uniform mat4 u_proj;
uniform float u_time;

out vec3 v_world_pos;
flat out vec3 v_color;

void main() {
    vec3 p = a_pos;
    p.y += 0.15 * sin(u_time * 1.1 + a_pos.x * 0.05 + a_pos.z * 0.07)
         + 0.08 * sin(u_time * 1.7 + a_pos.z * 0.13);
    v_world_pos = p;
    v_color = a_color;
    gl_Position = u_proj * u_view * vec4(p, 1.0);
}
)glsl";

constexpr const char* kWaterFrag = R"glsl(
#version 460 core
in vec3 v_world_pos;
flat in vec3 v_color;

uniform vec3 u_sun_dir;
uniform vec3 u_camera_pos;
uniform vec3 u_sky_color;
uniform float u_fog_density;

out vec4 frag_color;

void main() {
    vec3 normal = normalize(cross(dFdx(v_world_pos), dFdy(v_world_pos)));
    vec3 view_dir = normalize(u_camera_pos - v_world_pos);

    // Fresnel de Schlick: mirando al agua de frente se ve el fondo (alpha
    // baja, color profundo); en ángulo rasante refleja el cielo (alpha alta).
    float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(view_dir, normal), 0.0), 5.0);

    float diffuse = max(dot(normal, u_sun_dir), 0.0);
    vec3 deep = vec3(0.06, 0.20, 0.30);
    vec3 body = mix(deep, v_color, 0.45) * (0.45 + 0.55 * diffuse);
    vec3 lit = mix(body, u_sky_color, fresnel * 0.85);

    // Especular del sol, más nítido sobre el agua.
    vec3 half_dir = normalize(view_dir + u_sun_dir);
    lit += vec3(0.45) * pow(max(dot(normal, half_dir), 0.0), 96.0);

    float dist = length(v_world_pos - u_camera_pos);
    float fog = 1.0 - exp(-u_fog_density * dist);
    vec3 color = mix(lit, u_sky_color, fog);

    float alpha = mix(0.62, 0.95, fresnel);
    frag_color = vec4(color, alpha);
}
)glsl";

} // namespace render
