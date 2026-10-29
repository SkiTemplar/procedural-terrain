#pragma once

#include "render/terrain_shader_constants.h"

namespace render {

// Trozo GLSL compartido: cielo analítico + niebla con altura (C4-L4, sol fijo).
// Se inserta por concatenación de literales tras las declaraciones de cada
// fragment shader — una sola definición para terreno/vegetación, hierba, agua
// y el pase de cielo, de modo que la niebla de CUALQUIER material funde
// exactamente al color del cielo en esa dirección de vista.
#define PT_GLSL_SKY_COMMON R"glsl(
uniform vec3 u_sun_dir;       // hacia el sol, normalizado
uniform float u_fog_density;  // densidad de bruma a nivel del mar (y=0)

// Cielo de sol fijo: gradiente cenit→horizonte (Rayleigh de juguete) más dos
// lóbulos cálidos alrededor del sol (Mie ancho + halo cercano). Devuelve color
// SIN tonemapear: el terreno mezcla su color ya tonemapeado hacia este valor,
// y el pase de cielo lo pinta tal cual — así la fusión en el horizonte es exacta.
vec3 sky_color(vec3 dir) {
    float up = max(dir.y, 0.0);
    float horizon_k = pow(1.0 - up, 4.0);
    vec3 zenith = vec3(0.25, 0.44, 0.80);
    vec3 horizon = vec3(0.72, 0.80, 0.90);
    vec3 sky = mix(zenith, horizon, horizon_k);
    float s = max(dot(dir, u_sun_dir), 0.0);
    sky += vec3(1.00, 0.72, 0.42) * 0.14 * pow(s, 6.0);
    sky += vec3(1.00, 0.86, 0.62) * 0.30 * pow(s, 48.0);
    return sky;
}

// Niebla exponencial CON ALTURA: la densidad decae exp(-y/H) — el mar y los
// valles guardan bruma, las cumbres respiran aire limpio. La profundidad
// óptica del rayo cámara→fragmento tiene integral analítica; el término
// residual (independiente de la altura) es la perspectiva aérea que nunca
// desaparece por alto que vueles.
float fog_amount(vec3 cam_pos, vec3 frag_pos) {
    const float kInvH = 1.0 / 500.0;   // H = 500 m: altura de escala de la bruma
    const float kResidual = 0.15;      // fracción de densidad sin decaimiento
    vec3 d = frag_pos - cam_pos;
    float dist = length(d);
    float dy = d.y;
    float column;                      // integral de exp(-y/H) sobre el rayo
    if (abs(dy) < 1.0) {
        column = exp(-cam_pos.y * kInvH) * dist;
    } else {
        column = dist * (exp(-cam_pos.y * kInvH) - exp(-frag_pos.y * kInvH))
               / (dy * kInvH);
    }
    float od = u_fog_density * (column + kResidual * dist);
    return 1.0 - exp(-od);
}
)glsl"

// Flat shading sin duplicar vértices: la normal por cara sale de las derivadas
// de pantalla (dFdx/dFdy) y el color va `flat` (un color por triángulo).
constexpr const char* kTerrainVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
layout (location = 1) in vec3 a_color;
layout (location = 2) in vec3 a_normal;
layout (location = 3) in vec2 a_light; // x = sombra arrojada, y = AO de cielo

uniform mat4 u_view;
uniform mat4 u_proj;

out vec3 v_world_pos;
flat out vec3 v_color;
out vec3 v_color_smooth;
out vec3 v_normal_smooth;
out vec2 v_light;

void main() {
    v_world_pos = a_pos;
    v_color = a_color;
    v_color_smooth = a_color;
    v_normal_smooth = a_normal;
    v_light = a_light;
    gl_Position = u_proj * u_view * vec4(a_pos, 1.0);
}
)glsl";

constexpr const char* kTerrainFrag = R"glsl(
#version 460 core
in vec3 v_world_pos;
flat in vec3 v_color;
in vec3 v_color_smooth;
in vec3 v_normal_smooth;
in vec2 v_light;             // x = sombra arrojada (1=al sol), y = AO de cielo

uniform vec3 u_camera_pos;
uniform float u_smooth_blend; // 1 = sombreado suave; 0 = solo flat (vegetacion o comparacion)
uniform int u_smooth_shading; // 1 = sombreado suave con normal map; 0 = flat
uniform int u_detail_enabled;
uniform float u_detail_intensity;
uniform float u_terrain_half;
// Unidades fijas (render/terrain_shader_constants.h). La vegetación comparte
// este fragment: con binding explícito los samplers nunca caen en la
// unidad 0 (fondo marino del agua) ni comparten unidad con otro tipo.
layout(binding = )glsl" PT_GLSL_STR(PT_DETAIL_ALBEDO_UNIT) R"glsl() uniform sampler2DArray u_detail_albedo;
layout(binding = )glsl" PT_GLSL_STR(PT_DETAIL_NORMAL_UNIT) R"glsl() uniform sampler2DArray u_detail_normal;
layout(binding = )glsl" PT_GLSL_STR(PT_MATERIAL_GRID_UNIT) R"glsl() uniform usampler2D u_material_grid;
layout(binding = )glsl" PT_GLSL_STR(PT_TERRAIN_NORMAL_MAP_UNIT) R"glsl() uniform sampler2D u_terrain_normal_map;
uniform float u_layer_size[10];
uniform vec3 u_layer_mean[10];

out vec4 frag_color;
)glsl" PT_GLSL_SKY_COMMON R"glsl(
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

// Réplica exacta de terrain::hash01 / terrain::value_noise (mesher.cpp): la
// aritmética uint de GLSL es módulo 2^32 igual que la de C++, así que el warp
// de material del shader coincide con el de la malla.
float hash01_int(ivec2 c) {
    uint h = uint(c.x) * 374761393u + uint(c.y) * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return float(h & 0xFFFFFFu) / 16777216.0;
}

float value_noise(vec2 p) {
    vec2 fl = floor(p);
    ivec2 i = ivec2(fl);
    vec2 t = p - fl;
    t = t * t * (3.0 - 2.0 * t);
    float a = hash01_int(i);
    float b = hash01_int(i + ivec2(1, 0));
    float c = hash01_int(i + ivec2(0, 1));
    float d = hash01_int(i + ivec2(1, 1));
    return mix(mix(a, b, t.x), mix(c, d, t.x), t.y);
}

// Espejo de terrain::material_warp (chunked_terrain.cpp), mismas constantes.
vec2 material_warp(vec2 w) {
    const float f = )glsl" PT_GLSL_STR(PT_MATERIAL_WARP_FREQ) R"glsl(;
    const float k = )glsl" PT_GLSL_STR(PT_MATERIAL_WARP_AMP) R"glsl(;
    float nx = value_noise(vec2(
        w.x * f + )glsl" PT_GLSL_STR(PT_MATERIAL_WARP_X_OFFSET_X) R"glsl(,
        w.y * f + )glsl" PT_GLSL_STR(PT_MATERIAL_WARP_X_OFFSET_Z) R"glsl()) - 0.5;
    float nz = value_noise(vec2(
        w.x * f + )glsl" PT_GLSL_STR(PT_MATERIAL_WARP_Z_OFFSET_X) R"glsl(,
        w.y * f + )glsl" PT_GLSL_STR(PT_MATERIAL_WARP_Z_OFFSET_Z) R"glsl()) - 0.5;
    return vec2(nx, nz) * 2.0 * k;
}

// Texturas PBR cercanas. Coste en lecturas de textura por fragmento:
// - caso común (un material, planar): 4 texelFetch R8UI + 2 albedo + 2 normal
//   = 8 (donde el peso antitiling es 0 o 1 solo se hace una de las dos
//   lecturas de cada mapa: 6);
// - peor caso (4 materiales distintos, los 4 de roca triplanar):
//   4 + 4 * (3 ejes * 2 mapas * 2 escalas) = 52, solo en roca escarpada dentro
//   del radio de detalle.
// Todas las lecturas condicionales usan textureGrad con gradientes de mundo
// calculados en flujo uniforme al principio de main: dentro de ramas no
// uniformes las derivadas implícitas de texture() están indefinidas.
const float kDetailFadeStartM = 150.0;
const float kDetailFadeEndM = 400.0;
const int kRockLayer = 4;
const int kLastLayer = 9;
const float kTriplanarMaxUp = 0.72;      // |n.y| por debajo => triplanar (roca)

// Antitiling: segunda lectura a otra escala y rotada 35 grados, mezclada por
// ruido.
const mat2 kAntitileRot = mat2(0.819152, 0.573576, -0.573576, 0.819152);
const float kAntitileScale = 0.713;
const vec2 kAntitileOffset = vec2(19.7, 7.3);
const float kAntitileNoiseFreq = 0.17;   // ciclos por tile

struct UvGrad {
    vec2 uv;
    vec2 dx;
    vec2 dy;
};

UvGrad antitile_uv(UvGrad g) {
    return UvGrad(kAntitileRot * (g.uv * kAntitileScale) + kAntitileOffset,
                  kAntitileRot * (g.dx * kAntitileScale),
                  kAntitileRot * (g.dy * kAntitileScale));
}

// Peso de la segunda lectura con contraste: amplias zonas con peso 0 o 1 y
// transiciones cortas, en lugar de un 50 % casi constante.
float antitile_weight(vec2 uv, int layer) {
    float n = vnoise(uv * kAntitileNoiseFreq + vec2(float(layer) * 7.13));
    return smoothstep(0.30, 0.70, n);
}

// Normalización de la mezcla lineal que conserva la varianza (Heitz y Neyret
// 2018, aproximación de primer orden): (a-m)(1-w) + (b-m)w pierde contraste
// en w=0.5; dividir por sqrt((1-w)^2 + w^2) lo restituye.
float variance_norm(float w) {
    return inversesqrt((1.0 - w) * (1.0 - w) + w * w);
}

vec3 sample_albedo(UvGrad g, int layer) {
    float w = antitile_weight(g.uv, layer);
    UvGrad r = antitile_uv(g);
    if (w >= 1.0) {
        return textureGrad(u_detail_albedo, vec3(r.uv, float(layer)),
                           r.dx, r.dy).rgb;
    }
    vec3 a = textureGrad(u_detail_albedo, vec3(g.uv, float(layer)),
                         g.dx, g.dy).rgb;
    if (w <= 0.0) return a;
    vec3 b = textureGrad(u_detail_albedo, vec3(r.uv, float(layer)),
                         r.dx, r.dy).rgb;
    vec3 mean = u_layer_mean[layer];
    vec3 blended = mean + ((a - mean) * (1.0 - w) + (b - mean) * w)
                        * variance_norm(w);
    return max(blended, vec3(0.0));
}

// Normal tangente en el marco (u, v) de la proyección. La imagen se sube sin
// voltear (fila 0 = arriba = v pequeña) y el verde de la convención OpenGL
// apunta hacia arriba de la imagen, es decir, hacia -v: se invierte y.
vec3 decode_normal(vec4 texel) {
    vec3 n = texel.xyz * 2.0 - 1.0;
    return vec3(n.x, -n.y, n.z);
}

vec3 sample_tangent_normal(UvGrad g, int layer) {
    float w = antitile_weight(g.uv, layer);
    UvGrad r = antitile_uv(g);
    // La lectura rotada perturba en su propio marco: se devuelve al marco
    // (u, v) con la rotación inversa antes de mezclar.
    if (w >= 1.0) {
        vec3 b = decode_normal(textureGrad(u_detail_normal,
                                           vec3(r.uv, float(layer)), r.dx, r.dy));
        return normalize(vec3(transpose(kAntitileRot) * b.xy, b.z));
    }
    vec3 a = decode_normal(textureGrad(u_detail_normal,
                                       vec3(g.uv, float(layer)), g.dx, g.dy));
    if (w <= 0.0) return normalize(a);
    vec3 b = decode_normal(textureGrad(u_detail_normal,
                                       vec3(r.uv, float(layer)), r.dx, r.dy));
    b.xy = transpose(kAntitileRot) * b.xy;
    vec2 xy = (a.xy * (1.0 - w) + b.xy * w) * variance_norm(w);
    float z = max(mix(a.z, b.z, w), 1e-3);
    return normalize(vec3(xy, z));
}

UvGrad project_uv(vec2 p, vec2 dpdx, vec2 dpdy, float inv_tile) {
    return UvGrad(p * inv_tile, dpdx * inv_tile, dpdy * inv_tile);
}

// Whiteout por eje (Ben Golus, "Normal Mapping for a Triplanar Shader"): la
// normal base se reordena al marco de cada proyección (u, v, eje), se suman
// las pendientes, la componente del eje se multiplica, y se vuelve al marco
// de mundo con el reordenamiento inverso. abs(t.z) * base.eje toma el signo de
// la cara, así que sirve para las dos caras de cada eje.
vec3 whiteout_y(vec3 t, vec3 base) { // proyección XZ: u = +x, v = +z
    return vec3(t.x + base.x, abs(t.z) * base.y, t.y + base.z);
}
vec3 whiteout_x(vec3 t, vec3 base) { // proyección ZY: u = +z, v = +y
    return vec3(abs(t.z) * base.x, t.y + base.y, t.x + base.z);
}
vec3 whiteout_z(vec3 t, vec3 base) { // proyección XY: u = +x, v = +y
    return vec3(t.x + base.x, t.y + base.y, abs(t.z) * base.z);
}

void sample_layer_detail(int layer, vec3 base_normal, vec3 dpdx, vec3 dpdy,
                         out vec3 ratio, out vec3 detail_normal) {
    float inv_tile = 1.0 / max(u_layer_size[layer], 0.01);
    vec3 inv_mean = 1.0 / max(u_layer_mean[layer], vec3(0.03));
    vec3 p = v_world_pos;
    bool triplanar = layer == kRockLayer && abs(base_normal.y) < kTriplanarMaxUp;
    if (!triplanar) {
        UvGrad gy = project_uv(p.xz, dpdx.xz, dpdy.xz, inv_tile);
        ratio = sample_albedo(gy, layer) * inv_mean;
        detail_normal = normalize(whiteout_y(
            sample_tangent_normal(gy, layer), base_normal));
        return;
    }

    vec3 w = pow(abs(base_normal), vec3(4.0));
    w /= max(w.x + w.y + w.z, 1e-4);
    UvGrad gx = project_uv(p.zy, dpdx.zy, dpdy.zy, inv_tile);
    UvGrad gy = project_uv(p.xz, dpdx.xz, dpdy.xz, inv_tile);
    UvGrad gz = project_uv(p.xy, dpdx.xy, dpdy.xy, inv_tile);
    ratio = (sample_albedo(gx, layer) * w.x
           + sample_albedo(gy, layer) * w.y
           + sample_albedo(gz, layer) * w.z) * inv_mean;
    detail_normal = normalize(
        whiteout_x(sample_tangent_normal(gx, layer), base_normal) * w.x
      + whiteout_y(sample_tangent_normal(gy, layer), base_normal) * w.y
      + whiteout_z(sample_tangent_normal(gz, layer), base_normal) * w.z);
}

// dpdx/dpdy: gradientes de pantalla de la posición de mundo, calculados en
// flujo uniforme. Las UV son lineales en la posición, así que sus gradientes
// salen de proyectarlos igual que la propia posición.
void terrain_detail(vec3 base_normal, float dist_cam, vec3 dpdx, vec3 dpdy,
                    out vec3 ratio, out vec3 detail_normal) {
    ratio = vec3(1.0);
    detail_normal = base_normal;
    float fade = 1.0 - smoothstep(kDetailFadeStartM, kDetailFadeEndM, dist_cam);
    if (u_detail_enabled == 0 || fade <= 0.0) return; // cero fetches lejos/fallback

    vec2 world = v_world_pos.xz + material_warp(v_world_pos.xz);
    ivec2 grid_size = textureSize(u_material_grid, 0);
    vec2 grid = (world / (2.0 * u_terrain_half) + 0.5)
              * vec2(grid_size - 1);
    ivec2 p0 = clamp(ivec2(floor(grid)), ivec2(0), grid_size - 1);
    ivec2 p1 = min(p0 + ivec2(1), grid_size - 1);
    uvec4 layers = uvec4(texelFetch(u_material_grid, p0, 0).r,
                         texelFetch(u_material_grid, ivec2(p1.x, p0.y), 0).r,
                         texelFetch(u_material_grid, ivec2(p0.x, p1.y), 0).r,
                         texelFetch(u_material_grid, p1, 0).r);
    layers = min(layers, uvec4(kLastLayer)); // nunca fuera de los arrays
    vec2 f = clamp(grid - vec2(p0), 0.0, 1.0);
    vec4 weights = vec4((1.0-f.x)*(1.0-f.y), f.x*(1.0-f.y),
                        (1.0-f.x)*f.y, f.x*f.y);

    vec3 r0, n0;
    sample_layer_detail(int(layers.x), base_normal, dpdx, dpdy, r0, n0);
    vec3 mixed_ratio = r0;
    vec3 mixed_normal = n0;
    if (!all(equal(layers, uvec4(layers.x)))) {
        vec3 r1, n1, r2, n2, r3, n3;
        sample_layer_detail(int(layers.y), base_normal, dpdx, dpdy, r1, n1);
        sample_layer_detail(int(layers.z), base_normal, dpdx, dpdy, r2, n2);
        sample_layer_detail(int(layers.w), base_normal, dpdx, dpdy, r3, n3);
        mixed_ratio = r0*weights.x + r1*weights.y
                    + r2*weights.z + r3*weights.w;
        mixed_normal = normalize(n0*weights.x + n1*weights.y
                               + n2*weights.z + n3*weights.w);
    }
    // La intensidad puede pasar de 1 en el albedo (exagera el contraste); la
    // normal se queda en [0, 1] para no extrapolar más allá del mapa.
    float strength = clamp(u_detail_intensity, 0.0, 1.5) * fade;
    ratio = clamp(mix(vec3(1.0), mixed_ratio, strength),
                  vec3(0.25), vec3(4.0));
    detail_normal = normalize(mix(base_normal, mixed_normal,
                                  min(strength, 1.0)));
}

// Tonemapping ACES (Narkowicz): rolloff fílmico del rango alto → contraste y
// color sin quemar. La luz del sol aporta >1 en las caras iluminadas, así que
// hay rango que comprimir (antes el sombreado era plano y lavado).
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14),
                 0.0, 1.0);
}

void main() {
    // Sombreado del terreno: el sombreado suave emplea el normal map continuo
    // del heightfield completo (calculado en CPU con Scharr 3x3) y color suave
    // interpolado entre vértices (v_color_smooth).
    // Con u_smooth_blend == 0 (vegetación) o u_smooth_shading == 0 (toggle debug),
    // se mantiene el sombreado flat por triángulo (derivadas dFdx/dFdy) y v_color.
    float dist_cam = length(v_world_pos - u_camera_pos);

    // Gradientes de pantalla en flujo uniforme: sirven a la normal plana, al
    // normal map y a todas las lecturas textureGrad del detalle.
    vec3 dpdx = dFdx(v_world_pos);
    vec3 dpdy = dFdy(v_world_pos);
    vec3 n_flat = normalize(cross(dpdx, dpdy));

    ivec2 norm_size = textureSize(u_terrain_normal_map, 0);
    vec2 norm_grid = (v_world_pos.xz / (2.0 * u_terrain_half) + 0.5) * vec2(norm_size - 1);
    vec2 norm_uv = (norm_grid + 0.5) / vec2(norm_size);
    vec2 norm_dx = dpdx.xz / (2.0 * u_terrain_half);
    vec2 norm_dy = dpdy.xz / (2.0 * u_terrain_half);
    vec2 n_xz = textureGrad(u_terrain_normal_map, norm_uv, norm_dx, norm_dy).rg;
    float n_y = sqrt(max(0.0, 1.0 - dot(n_xz, n_xz)));
    vec3 n_map = normalize(vec3(n_xz.x, n_y, n_xz.y));

    float blend = (u_smooth_shading != 0) ? u_smooth_blend : 0.0;
    vec3 normal = normalize(mix(n_flat, n_map, blend));
    vec3 albedo = mix(v_color, v_color_smooth, blend);

    // Sin detalle (desactivado, lejos o sin assets) devuelve ratio 1 y la
    // normal base: el resultado es idéntico al render solo-paleta.
    vec3 detail_ratio;
    vec3 detail_normal;
    terrain_detail(normal, dist_cam, dpdx, dpdy, detail_ratio, detail_normal);
    albedo *= detail_ratio;
    normal = detail_normal;

    // Iluminación de exterior: sol cálido + ambiente hemisférico (las caras
    // que miran al cielo reciben azul frío; las que miran al suelo, rebote
    // verdoso cálido). Mucho más natural que un ambient constante.
    // Rebalance sol/ambiente (feedback 2026-07-08 "luces"): menos ambiente
    // plano (0.50->0.34) y sol más fuerte (0.72->1.15, supera 1 → lo comprime
    // el ACES) → más contraste y volumen, menos look lavado. El hemisférico
    // (cielo frío arriba, rebote cálido abajo) se mantiene.
    const vec3 sun_color = vec3(1.0, 0.94, 0.82);
    float diffuse = max(dot(normal, u_sun_dir), 0.0);
    float sky_facing = normal.y * 0.5 + 0.5;
    vec3 hemi = mix(vec3(0.28, 0.30, 0.26), vec3(0.52, 0.62, 0.80), sky_facing);
    // Iluminación precomputada (sol fijo): la SOMBRA arrojada (v_light.x) atenúa
    // el sol — una montaña oscurece a su vecina; el AO de cielo (v_light.y)
    // atenúa el ambiente hemisférico — valles y pies de acantilado se asientan.
    // La sombra conserva el ambiente: una ladera en sombra la ilumina el cielo,
    // no queda negra.
    vec3 lit = albedo * (0.34 * hemi * v_light.y
                         + 1.15 * diffuse * sun_color * v_light.x);

    // Textura procedural sutil en dos escalas (~1.5 m y ~9 m): rompe el color
    // plano de cada cara sin perder la estética low-poly. Se atenúa de lejos
    // y a partir de ahí ni se calcula (8 hashes por píxel que no se ven).
    float tex_fade = exp(-dist_cam * 0.0035);
    if (tex_fade > 0.02) {
        float tex = 0.55 * vnoise(v_world_pos.xz * 0.65)
                  + 0.45 * vnoise(v_world_pos.xz * 0.11);
        lit *= 1.0 + (tex - 0.5) * 0.10 * tex_fade;
    }

    lit = aces(lit); // tonemap antes de la niebla (que ya va en rango de pantalla)
    vec3 view_dir = (v_world_pos - u_camera_pos) / max(dist_cam, 1e-4);
    float fog = fog_amount(u_camera_pos, v_world_pos);
    vec3 color = mix(lit, sky_color(view_dir), fog);

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
out vec2 v_light;

void main() {
    float c = cos(i_tint_yaw.w);
    float s = sin(i_tint_yaw.w);
    vec3 p = a_pos * i_pos_scale.w;
    p = vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z) + i_pos_scale.xyz;
    v_world_pos = p;
    v_color = a_color * i_tint_yaw.rgb;
    v_color_smooth = v_color;
    v_normal_smooth = vec3(0.0, 1.0, 0.0); // sin uso: la vegetación va con u_smooth_blend=0
    v_light = vec2(1.0); // la vegetación no recibe sombra/AO precomputados
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

uniform vec3 u_camera_pos;

out vec4 frag_color;
)glsl" PT_GLSL_SKY_COMMON R"glsl(
float hash(float n) { return fract(sin(n) * 43758.5453); }
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14),
                 0.0, 1.0);
}

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
    vec3 lit = aces(col * (0.40 + 1.10 * diffuse)); // mismo rebalance + tonemap
    vec3 view_dir = normalize(v_world_pos - u_camera_pos);
    float fog = fog_amount(u_camera_pos, v_world_pos);
    frag_color = vec4(mix(lit, sky_color(view_dir), fog), 1.0);
}
)glsl";

// Agua: olas suaves por vértice, semitransparente, con la misma niebla.
// C4-W1: color y alpha por PROFUNDIDAD REAL (Beer-Lambert) — los ríos llevan
// su calado por vértice (a_normal.y, malla a resolución plena); el mar lo
// muestrea por píxel de u_seabed (r = calado, g = energía de oleaje).
constexpr const char* kWaterVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
layout (location = 1) in vec3 a_color;
layout (location = 2) in vec3 a_normal; // ríos: (v.x m/s, calado m, v.z m/s)
layout (location = 3) in vec2 a_light;  // x = whitewater (0/0.5/1), y = 1 en lagos

uniform mat4 u_view;
uniform mat4 u_proj;
uniform float u_time;
uniform vec3 u_camera_pos;
uniform vec3 u_wave_dir; // xz = dirección de VIAJE del oleaje dominante
uniform int u_is_sea;    // 1 = plano del mar, 0 = malla de ríos/lagos
uniform float u_sea_level; // m
uniform float u_sea_half;  // semiancho del plano del mar (m)
uniform float u_sea_grid;  // quads por lado del plano del mar

out vec3 v_world_pos;
flat out vec3 v_color;
out vec3 v_river; // (velocidad.x, calado, velocidad.z) interpolado
out float v_ww;   // whitewater interpolado
out float v_remanso; // 1 = lámina de río a cota de mar (estuario)

// Ola de vértice del mar de fondo en un punto (C4-W4: viaja con la rosa de
// vientos, dos trenes, el secundario sesgado ~20°). Se aplana con la
// distancia: a >4 km una ola de 15 cm subtiende <0.5 px y solo aporta ruido
// de normal a incidencia rasante.
float swell_at(vec2 xz) {
    float dist = length(xz - u_camera_pos.xz);
    float wave_k = clamp(1.0 - (dist - 2000.0) / 2000.0, 0.0, 1.0);
    vec2 wd = normalize(u_wave_dir.xz + vec2(1e-5, 0.0));
    vec2 wd2 = normalize(wd + vec2(-wd.y, wd.x) * 0.35);
    return wave_k * (0.15 * sin(dot(xz, wd) * 0.05 - u_time * 1.1)
                     + 0.08 * sin(dot(xz, wd2) * 0.13 - u_time * 1.7));
}

// Altura de ola del PLANO DEL MAR en xz: la que interpola su rejilla de
// u_sea_grid² quads (triángulos tl-bl-tr / tr-bl-br, ver build_sea_plane).
float sea_plane_swell(vec2 xz) {
    float cell = 2.0 * u_sea_half / u_sea_grid;
    vec2 g = (xz + vec2(u_sea_half)) / cell;
    vec2 q = clamp(floor(g), vec2(0.0), vec2(u_sea_grid - 1.0));
    vec2 f = g - q;
    vec2 o = q * cell - vec2(u_sea_half);
    float tl = swell_at(o);
    float tr = swell_at(o + vec2(cell, 0.0));
    float bl = swell_at(o + vec2(0.0, cell));
    float br = swell_at(o + vec2(cell, cell));
    return f.x + f.y <= 1.0 ? tl + f.x * (tr - tl) + f.y * (bl - tl)
                            : br + (1.0 - f.x) * (bl - br) + (1.0 - f.y) * (tr - br);
}

void main() {
    vec3 p = a_pos;
    // Los RÍOS no ondulan por vértice (auditoría 2026-10 #1): la fase
    // dot(pos, dir) con direcciones distintas en vértices vecinos daba alturas
    // sin correlación (la lámina "vibraba"). Su movimiento lo pone el flow map
    // del fragment shader. Mar y lagos (a_light.y = 1) llevan oleaje de viento.
    v_remanso = 0.0;
    if (u_is_sea == 1 || a_light.y > 0.5) {
        p.y += swell_at(a_pos.xz);
    } else {
        // REMANSO DEL ESTUARIO: la lámina a cota de mar sube y baja con el
        // plano del mar (su misma interpolación), así que en la costura las
        // dos superficies coinciden y el recorte del mar no deja grietas.
        v_remanso = 1.0 - smoothstep(0.05, 0.5, a_pos.y - u_sea_level);
        p.y += v_remanso * sea_plane_swell(a_pos.xz);
    }
    v_world_pos = p;
    v_color = a_color;
    v_river = a_normal;
    v_ww = a_light.x;
    gl_Position = u_proj * u_view * vec4(p, 1.0);
}
)glsl";

constexpr const char* kWaterFrag = R"glsl(
#version 460 core
in vec3 v_world_pos;
flat in vec3 v_color;
in vec3 v_river;
in float v_ww;
in float v_remanso;

uniform vec3 u_camera_pos;
uniform int u_is_sea;        // 1 = plano del mar, 0 = malla de ríos/lagos
uniform sampler2D u_seabed;  // r = calado bajo el mar (m), g = energía oleaje
uniform sampler2D u_sea_clip; // 1 = la lámina de río/lago cubre el quad (NEAREST)
uniform float u_domain_half; // semiancho del heightfield (m)
uniform float u_time;
uniform vec3 u_wave_dir;     // xz = dirección de VIAJE del oleaje dominante
uniform float u_wind_focus;  // 0 = mar isótropo, 1 = todo del viento dominante
uniform float u_wave_hmax;     // H_s del mar abierto (m), terrain::kOpenSeaWaveHeightM
uniform float u_breaker_index; // γ de rotura por fondo, terrain::kBreakerIndex

out vec4 frag_color;
)glsl" PT_GLSL_SKY_COMMON R"glsl(
float w_hash12(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float w_vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = w_hash12(i);
    float b = w_hash12(i + vec2(1.0, 0.0));
    float c = w_hash12(i + vec2(0.0, 1.0));
    float d = w_hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Ruido de valor con derivadas analíticas: (valor, d/dx, d/dz).
vec3 w_vnoise_d(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    vec2 du = 6.0 * f * (1.0 - f);
    float a = w_hash12(i);
    float b = w_hash12(i + vec2(1.0, 0.0));
    float c = w_hash12(i + vec2(0.0, 1.0));
    float d = w_hash12(i + vec2(1.0, 1.0));
    float k1 = b - a;
    float k2 = c - a;
    float k3 = a - b - c + d;
    return vec3(a + k1 * u.x + k2 * u.y + k3 * u.x * u.y,
                du.x * (k1 + k3 * u.y),
                du.y * (k2 + k3 * u.x));
}

// Pendiente del rizado de corriente (dos octavas, ~0.8 m y ~0.3 m).
vec2 ripple_slope(vec2 p) {
    vec3 n1 = w_vnoise_d(p * 1.3);
    vec3 n2 = w_vnoise_d(p * 3.1 + vec2(17.3, -9.1));
    return n1.yz * (1.3 * 0.030) + n2.yz * (3.1 * 0.012);
}

// FLOW MAP de dos fases (Vlachos 2010): el patrón se desplaza con la
// corriente −v·φ·T y dos capas desfasadas medio periodo se cruzan con peso
// |1 − 2φ0|, de modo que cada capa se reinicia cuando su peso es nulo. No
// depende de la posición absoluta ni de una dirección interpolada: el rizado
// viaja a la velocidad física, es rápido en los rápidos y nulo en lagos.
//
// El desplazamiento por ciclo se acota a kMaxCycleShift: con |v|·T de varios
// metros frente a un rizado de 0.3-0.8 m, el gradiente lateral de velocidad
// (eje rápido, orilla quieta) cizallaba y estiraba el patrón. Por encima de
// kMaxCycleShift/T (2 m/s) el rizado deja de acelerar; la física completa
// sigue en la rugosidad y en la espuma, que dependen de la velocidad real.
const float kFlowPeriod = 1.0;    // s
const float kMaxCycleShift = 2.0; // m por ciclo
vec2 flow_advection(vec2 vel) {
    float shift = length(vel) * kFlowPeriod;
    return shift > kMaxCycleShift ? vel * (kMaxCycleShift / shift) : vel;
}
vec3 flow_phases(vec2 pos) {
    // Desfase espacial lento: los reinicios no laten en sincronía.
    float t = u_time / kFlowPeriod + w_vnoise(pos * 0.04);
    float ph0 = fract(t);
    return vec3(ph0, fract(t + 0.5), abs(1.0 - 2.0 * ph0));
}

// C4-W4: normal ANALÍTICA del oleaje por píxel (independiente de la rejilla
// de 625 m del plano del mar): tres trenes de ola alrededor de la dirección de
// viaje de la rosa de vientos — con focalización alta van casi alineados, con
// mar isótropo se abren en abanico. El sol-glitter y el fresnel salen por
// píxel, no por parche de malla. La comparten el mar y el remanso del estuario.
vec3 sea_wave_normal(float dist, vec2 wd) {
    vec2 perp = vec2(-wd.y, wd.x);
    float spread = 1.0 - 0.75 * u_wind_focus;
    vec2 d1 = normalize(wd + perp * 0.55 * spread);
    vec2 d2 = normalize(wd - perp * 0.45 * spread);
    vec2 d3 = normalize(wd + perp * 0.18 * spread);
    // FASE MODULADA por ruido lento (feedback 2026-07-13 "patrones raros
    // en los píxeles"): tres senoides puras interferían en un moiré
    // periódico sintético; el jitter espacial rompe la retícula sin
    // cambiar la dirección de viaje.
    float jit = w_vnoise(v_world_pos.xz * 0.012) * 6.2831853;
    float p1 = dot(v_world_pos.xz, d1) * 0.150 - u_time * 1.5 + jit;
    float p2 = dot(v_world_pos.xz, d2) * 0.330 - u_time * 2.3 + jit * 0.7;
    float p3 = dot(v_world_pos.xz, d3) * 0.700 - u_time * 3.4 + jit * 1.4;
    // Las olas se aplanan con la distancia (mismo criterio que las de
    // vértice); el tren CORTO muere mucho antes — a media distancia su
    // longitud de onda cae bajo el píxel y solo aporta aliasing.
    float wave_k = clamp(1.0 - (dist - 2000.0) / 2000.0, 0.0, 1.0);
    float near_k = clamp(1.0 - (dist - 250.0) / 600.0, 0.0, 1.0);
    vec2 slope = d1 * (0.14 * cos(p1)) + d2 * (0.10 * cos(p2))
               + d3 * (0.07 * cos(p3) * near_k);
    slope *= wave_k;
    return normalize(vec3(-slope.x, 1.0, -slope.y));
}

void main() {
    float dist = length(v_world_pos - u_camera_pos);
    vec3 view_dir = normalize(u_camera_pos - v_world_pos);
    vec2 wd = normalize(u_wave_dir.xz + vec2(1e-5, 0.0));

    vec3 normal;
    float wall_k = 0.0; // 1 = "pared de agua" del borde de la cinta del río
    vec2 vel = v_river.xz;
    float speed = length(vel);
    if (u_is_sea == 1) {
        normal = sea_wave_normal(dist, wd);
    } else {
        normal = normalize(cross(dFdx(v_world_pos), dFdy(v_world_pos)));
        // PARED DE AGUA (feedback 2026-07-13 "tensión superficial con muros
        // de un metro"): en el borde de la cinta, los triángulos que caen de
        // la lámina a la orilla se vuelven casi verticales y el fresnel los
        // encendía como cortinas. Se desvanecen — SALVO en cascadas y rápidos
        // declarados (whitewater), donde la cortina es real y a propósito.
        wall_k = (1.0 - smoothstep(0.30, 0.60, normal.y))
               * (1.0 - smoothstep(0.40, 0.75, v_ww));
        // C4-W3: rizado advectado por la corriente real (flow map). La
        // rugosidad crece con la velocidad (agua lenta casi lisa, rápidos
        // picados); con v ≈ 0 (remanso quieto) no hay rizado de corriente.
        if (speed > 0.02) {
            vec3 ph = flow_phases(v_world_pos.xz);
            vec2 adv = flow_advection(vel);
            vec2 s0 = ripple_slope(v_world_pos.xz - adv * (ph.x * kFlowPeriod));
            vec2 s1 = ripple_slope(v_world_pos.xz - adv * (ph.y * kFlowPeriod)
                                   + vec2(31.7, 12.9));
            float rough = mix(0.45, 1.0, smoothstep(0.2, 2.0, speed));
            vec2 slope = mix(s0, s1, ph.z) * rough;
            normal = normalize(normal + vec3(-slope.x, 0.0, -slope.y));
        }
        // REMANSO DEL ESTUARIO: la lámina a cota de mar ES el mar que entra
        // en el cauce. Lleva el mismo oleaje por píxel que el plano del mar,
        // así que la costura entre las dos mallas (escalonada a celda) no se
        // lee como un cambio de superficie.
        if (v_remanso > 0.0) {
            normal = normalize(mix(normal, sea_wave_normal(dist, wd), v_remanso));
        }
    }

    // Calado físico del punto: el color del agua sale de ÉL, no de un tinte.
    float depth;
    float wave_e = 0.0;
    if (u_is_sea == 1) {
        vec2 uv = (v_world_pos.xz + vec2(u_domain_half))
                / (2.0 * u_domain_half);
        // Fuera del heightfield el mar es abierto y hondo (el CLAMP del
        // borde ya devuelve el fondo del perímetro, que es abisal).
        // Cauce fluvial: el mar no se mete en el río. Se recorta en los quads
        // que cubre la malla de agua interior (en el remanso enrasa con el mar
        // y es la única superficie); fuera del dominio la textura devuelve el
        // borde (0 = mar). El remanso ondula con el plano del mar (vértice) y
        // en la costura las dos superficies coinciden; el recorte se encoge
        // kSeaClipMargin para que la rasterización de las dos mallas solape
        // en vez de dejar grietas de un píxel con el fondo a la vista.
        const float kSeaClipMargin = 0.05; // m
        float e = kSeaClipMargin / (2.0 * u_domain_half);
        float clip = min(min(texture(u_sea_clip, uv + vec2(e, e)).r,
                             texture(u_sea_clip, uv + vec2(-e, e)).r),
                         min(texture(u_sea_clip, uv + vec2(e, -e)).r,
                             texture(u_sea_clip, uv + vec2(-e, -e)).r));
        if (clip > 0.5) {
            discard;
        }
        vec2 de = texture(u_seabed, clamp(uv, 0.0, 1.0)).rg;
        depth = max(de.r, 0.0);
        wave_e = de.g;
    } else {
        depth = max(v_river.y, 0.0);
    }

    // Fresnel de Schlick: mirando al agua de frente se ve el fondo (alpha
    // baja, color profundo); en ángulo rasante refleja el cielo (alpha alta).
    float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(view_dir, normal), 0.0), 5.0);

    float diffuse = max(dot(normal, u_sun_dir), 0.0);
    // Beer-Lambert por canal: el rojo se absorbe primero, el azul-verde llega
    // al fondo. Somero → agua clara que enseña la arena; hondo → azul denso.
    // Tono somero contenido (feedback 2026-07-13 "el río parece desbordado"):
    // el turquesa chillón en los márgenes de la cinta leía la lámina de
    // orilla como agua de más — el color claro queda para calados medios.
    vec3 k_abs = vec3(0.42, 0.19, 0.11);
    vec3 trans = exp(-depth * k_abs);
    vec3 deep = vec3(0.06, 0.20, 0.30);
    vec3 shallow = mix(vec3(0.28, 0.44, 0.42), v_color, 0.30);
    vec3 body = mix(deep, shallow, trans) * (0.45 + 0.55 * diffuse);
    // Refleja el CIELO REAL en la dirección especular (no un color plano):
    // el agua de cara al sol coge su calidez; la opuesta, el azul del cenit.
    vec3 refl = reflect(-view_dir, normal);
    refl.y = abs(refl.y); // el espejo del agua nunca mira bajo el horizonte
    vec3 lit = mix(body, sky_color(refl), fresnel * 0.85);

    // Especular del sol, más nítido sobre el agua. Las paredes de borde de
    // cinta no brillan (eran el destello metálico de los "muros de agua").
    vec3 half_dir = normalize(view_dir + u_sun_dir);
    lit += vec3(0.45) * pow(max(dot(normal, half_dir), 0.0), 96.0)
         * (1.0 - wall_k);

    // Espuma. C4-W3 en ríos: vetas blancas que corren con el agua donde la
    // hidrología marcó whitewater (0.5 = rápidos, 1 = caída). C4-W2 en el
    // mar: franja de rompiente donde el fondo roza la superficie, con
    // intensidad ∝ energía de OLEAJE REAL (fetch/REI) — la costa batida
    // rompe blanco, la abrigada apenas espumea. Nada decorativo: ambas salen
    // de campos simulados.
    float foam = 0.0;
    if (u_is_sea == 1) {
        // ROTURA POR FONDO (auditoría 2026-10 #3): H_s = H_abierto·√E (oleaje
        // limitado por fetch) y la ola rompe donde H ≥ γ·h, así que la zona
        // de rompiente acaba en h_b = H_s/γ y su anchura la da la pendiente
        // real del fondo (playa tendida: ancha; acantilado: una línea). La
        // blancura crece con la altura de ola: costa batida blanca, abrigo
        // (H_s < 0.3 m) sin espuma de rompiente.
        float h_s = u_wave_hmax * sqrt(wave_e);
        float h_b = h_s / u_breaker_index;
        float surf = 1.0 - smoothstep(0.35 * h_b, 1.15 * h_b, depth);
        float whiteness = smoothstep(0.3, 1.1, h_s);
        float chop = w_vnoise(v_world_pos.xz * 0.45 - wd * (u_time * 1.4));
        float pulse = 0.5
            + 0.5 * sin(dot(v_world_pos.xz, wd) * 0.30 - u_time * 1.8
                        + chop * 2.6);
        foam = clamp(surf * whiteness * (0.35 + 0.65 * pulse), 0.0, 1.0);
    } else if (v_ww > 0.05) {
        // Vetas de espuma arrastradas por la corriente (mismo flow map).
        vec3 ph = flow_phases(v_world_pos.xz);
        vec2 adv = flow_advection(vel);
        float f0 = w_vnoise((v_world_pos.xz - adv * (ph.x * kFlowPeriod)) * 0.9);
        float f1 = w_vnoise((v_world_pos.xz - adv * (ph.y * kFlowPeriod)) * 0.9
                            + vec2(7.3, -3.1));
        float streak = smoothstep(0.30, 0.75, mix(f0, f1, ph.z));
        foam = clamp(v_ww * (0.40 + 0.60 * streak), 0.0, 1.0);
    }
    lit = mix(lit, vec3(0.88, 0.92, 0.95), foam * 0.55);

    float fog = fog_amount(u_camera_pos, v_world_pos);
    vec3 color = mix(lit, sky_color(-view_dir), fog);

    // Opacidad creciente con la distancia: a >6 km el ángulo es rasante y el
    // camino de luz bajo el agua tan largo que el fondo nunca se ve — sin esto
    // el rectángulo del heightfield se marcaba en el mar (el fondo tras el
    // alpha cambiaba de lecho marino a océano del cielo justo en su borde).
    float opaque_k = clamp((dist - 1500.0) / 4500.0, 0.0, 1.0);
    // Alpha por calado (W1): la lámina de <20 cm es CASI INVISIBLE — los
    // márgenes someros de la cinta del río se funden con la orilla mojada y
    // el río se lee con su anchura de canal real, no "desbordado" (feedback
    // 2026-07-13). El cuerpo con calado manda enseguida (k=0.9). El FRESNEL
    // también escala por calado: una película de 5 cm rota por el fondo no
    // refleja como superficie continua — sin esto, el ángulo rasante volvía
    // a opacar los márgenes que el alpha somero acababa de fundir.
    float depth_k = 1.0 - exp(-depth * 0.9);
    float alpha = mix(mix(0.06, 0.68, depth_k), 0.95, fresnel * depth_k);
    alpha = mix(alpha, 0.97, opaque_k);
    // FLECO de orilla (feedback 2026-07-14, borde dentado): en el agua somera
    // el borde se deshilacha con ruido fino en vez de cortar al escalón de la
    // celda — rompe la percepción de diente de sierra del contorno.
    if (u_is_sea == 0 && depth_k < 0.5) {
        float fringe = w_vnoise(v_world_pos.xz * 1.7);
        alpha *= mix(0.4 + 0.6 * fringe, 1.0, depth_k * 2.0);
    }
    alpha = mix(alpha, 0.96, foam); // la espuma es agua aireada: opaca
    // La pared de borde de cinta se desvanece (la orilla real queda detrás).
    alpha *= 1.0 - 0.85 * wall_k;
    frag_color = vec4(color, alpha);
}
)glsl";

// Pase de cielo (C4-L4): triángulo fullscreen sin VBO (gl_VertexID) que pinta
// el gradiente atmosférico y el disco solar. Se dibuja con el depth test
// desactivado antes del mundo; también hace de fondo durante la generación.
constexpr const char* kSkyVert = R"glsl(
#version 460 core
out vec2 v_ndc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    v_ndc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
)glsl";

constexpr const char* kSkyFrag = R"glsl(
#version 460 core
in vec2 v_ndc;

uniform mat4 u_inv_vp; // inversa de proj * rotación de view: NDC → dir de mundo
uniform vec3 u_camera_pos;

out vec4 frag_color;
)glsl" PT_GLSL_SKY_COMMON R"glsl(
void main() {
    vec4 p = u_inv_vp * vec4(v_ndc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w);

    vec3 col;
    if (dir.y >= 0.0) {
        // Disco solar (~1° de radio aparente, borde suave) sobre el gradiente.
        // Supera 1.0 a propósito: es el sol, satura a blanco en pantalla.
        float s = dot(dir, u_sun_dir);
        float disc = smoothstep(0.99985, 0.99996, s);
        col = sky_color(dir) + disc * vec3(1.0, 0.96, 0.88) * 2.2;
    } else {
        // OCÉANO INFINITO bajo el horizonte: la malla del mar acaba en el far
        // plane (o en su borde a ±20 km) y a gran altitud la niebla ya no
        // satura ahí — sin esto, el corte geométrico se ve contra el cielo.
        // Mismo modelo que kWaterFrag a distancia (cuerpo + reflejo rasante
        // del cielo + niebla), sin olas: a >20 km no se resuelven.
        float dist = max(u_camera_pos.y, 1.0) / max(-dir.y, 1e-4);
        vec3 hit = u_camera_pos + dir * dist;
        float fresnel = 0.04 + 0.96 * pow(1.0 + dir.y, 5.0);
        const vec3 deep = vec3(0.06, 0.20, 0.30);
        const vec3 sea_col = vec3(0.13, 0.32, 0.50);
        vec3 body = mix(deep, sea_col, 0.45)
                  * (0.45 + 0.55 * max(u_sun_dir.y, 0.0));
        vec3 refl = vec3(dir.x, -dir.y, dir.z);
        vec3 lit = mix(body, sky_color(refl), fresnel * 0.85);
        col = mix(lit, sky_color(dir), fog_amount(u_camera_pos, hit));
    }

    frag_color = vec4(col, 1.0);
}
)glsl";

#undef PT_GLSL_SKY_COMMON

} // namespace render
