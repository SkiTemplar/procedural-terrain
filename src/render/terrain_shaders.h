#pragma once

#include "render/atmosphere_glsl.h"
#include "render/clouds_glsl.h"
#include "render/shadow_shaders.h"
#include "render/terrain_shader_constants.h"

namespace render {

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
uniform vec3 u_camera_pos;
uniform float u_terrain_half;
// Distancia horizontal (m) en que los árboles instanciados encogen hasta
// desaparecer (kVegVert); en la misma franja entra el dosel lejano.
uniform float u_veg_fade_start;
uniform float u_veg_fade_end;
layout(binding = )glsl" PT_GLSL_STR(PT_CANOPY_UNIT) R"glsl() uniform sampler2D u_canopy;

out vec3 v_world_pos;
flat out vec3 v_color;
out vec3 v_color_smooth;
out vec3 v_normal_smooth;
out vec2 v_light;
flat out vec4 v_canopy; // rgb follaje del dosel, a = peso de mezcla

// Sombreado propio del dosel visto desde lejos: copas con cara de sombra y
// huecos sombreados entre ellas devuelven menos luz que un plano del mismo
// albedo. La cobertura satura antes del 100 % (las copas se solapan).
const float kCanopySelfShade = 0.76;
// Sotobosque a media distancia: el suelo entre copas está en su sombra y
// toma la mitad del tono del dosel; sin esto la pradera clara entre árboles
// sueltos convertía el bosque en un moteado de alto contraste.
const float kUnderstoryShade = 0.5;

vec4 far_canopy(vec3 p) {
    float d = length(p.xz - u_camera_pos.xz);
    float far_k = max(smoothstep(u_veg_fade_start, u_veg_fade_end, d),
                      kUnderstoryShade * smoothstep(150.0, 700.0, d));
    if (far_k <= 0.0) return vec4(0.0);
    vec2 uv = p.xz / (2.0 * u_terrain_half) + 0.5;
    // Mip a la escala de la faceta (~d/60 m; texel de 8 m): con el mip de la
    // distancia (0.5 a 2.5 km) el vértice provocante muestreaba copas sueltas
    // y cada cara de un bosque claro tomaba cobertura 0 o 1 (confeti verde
    // oscuro/claro). Al nivel de la faceta las caras vecinas casi coinciden y
    // el monte se lee en masas, sin perder el color plano por cara.
    float lod = clamp(log2(d / 480.0) + 1.0, 0.0, 6.0);
    vec4 c = textureLod(u_canopy, uv, lod);
    // Brillo propio de cada faceta (hash entero del vértice que la provoca):
    // el dosel lejano conserva un grano facetado leve (±3 %). El hash con
    // sin() de coordenadas de miles de metros perdía precisión en la GPU y
    // dibujaba rayas; con ±8 % el bosque lejano era confeti.
    uvec2 q = uvec2(ivec2(floor(p.xz * 4.0)) + ivec2(1 << 20));
    uint h = q.x * 374761393u + q.y * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= h >> 16u;
    float jitter = 0.97 + 0.06 * float(h & 0xFFFFu) / 65535.0;
    return vec4(c.rgb * kCanopySelfShade * jitter,
                smoothstep(0.04, 0.60, c.a) * far_k);
}

void main() {
    v_canopy = far_canopy(a_pos);
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
flat in vec4 v_canopy;       // dosel lejano (solo terreno; la vegetación da 0)

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
)glsl" PT_GLSL_SKY_COMMON PT_GLSL_CLOUDS R"glsl(
)glsl" PT_GLSL_SHADOW_COMMON R"glsl(
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
    // Dosel lejano: donde los árboles ya no se dibujan, la faceta toma el color
    // medio de las copas que hay encima (misma población que de cerca).
    albedo = mix(albedo, v_canopy.rgb, v_canopy.a);

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
    vec3 hemi = mix(vec3(0.30, 0.31, 0.26), vec3(0.56, 0.68, 0.90), sky_facing);
    // Iluminación precomputada (sol fijo): la SOMBRA arrojada (v_light.x) atenúa
    // el sol — una montaña oscurece a su vecina; el AO de cielo (v_light.y)
    // atenúa el ambiente hemisférico — valles y pies de acantilado se asientan.
    // La sombra conserva el ambiente: una ladera en sombra la ilumina el cielo,
    // no queda negra.
    // Sombra en tiempo real (CSM): árboles, rocas y relieve fino a escala de
    // texel; se queda con la más oscura de las dos dentro del alcance de las
    // cascadas y, más allá, csm_visibility vale 1 y manda la precomputada
    // (las montañas siguen sombreando los valles lejanos). Solo se consulta
    // donde el sol llega: en las caras de espaldas diffuse ya es 0.
    float sun_vis = v_light.x;
    if (diffuse > 0.0) {
        sun_vis = min(sun_vis, csm_visibility(v_world_pos, n_flat))
                * cloud_shadow(v_world_pos);
    }
    // Ambiente 0.55 (antes 0.34): con sombras en tiempo real la cara en
    // sombra solo recibe cielo, y a 0.34 los sotobosques quedaban casi
    // negros. Sombra azulada y legible, como la luz de exterior de un juego.
    vec3 lit = albedo * (0.55 * hemi * v_light.y
                         + 1.15 * diffuse * sun_color * sun_vis);

    // Textura procedural sutil en dos escalas (~1.5 m y ~9 m), solo en el
    // sombreado suave: con facetas el color por cara ya da la variación y el
    // ruido dentro de la cara la desdibujaría. Se atenúa de lejos y a partir
    // de ahí ni se calcula (8 hashes por píxel que no se ven).
    float tex_fade = exp(-dist_cam * 0.0035) * blend;
    if (tex_fade > 0.02) {
        float tex = 0.55 * vnoise(v_world_pos.xz * 0.65)
                  + 0.45 * vnoise(v_world_pos.xz * 0.11);
        lit *= 1.0 + (tex - 0.5) * 0.10 * tex_fade;
    }

    // Perspectiva aérea en HDR lineal y después el tonemap común (la luz del
    // sol aporta >1 en las caras iluminadas: ACES comprime ese rango).
    vec3 color = tonemap(apply_atmosphere(lit, u_camera_pos, v_world_pos));

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
uniform vec3 u_camera_pos;
uniform float u_veg_fade_start;
uniform float u_veg_fade_end;

out vec3 v_world_pos;
flat out vec3 v_color;
out vec3 v_color_smooth;
out vec3 v_normal_smooth;
out vec2 v_light;
flat out vec4 v_canopy;

// Cámara dentro de la copa: la instancia se pliega a su base. Sin esto, una
// cámara a ras de suelo junto a un tronco ve la cara interior de un cono
// llenando media pantalla. Radio de copa del modelo mayor (~2.6 m) y altura
// (~9.5 m) por la escala de la instancia; fuera de ese cilindro no cambia nada.
const float kCanopyRadiusM = 2.7;
const float kCanopyTopM = 9.5;
float camera_clearance(vec4 pos_scale) {
    float r = kCanopyRadiusM * pos_scale.w;
    float d = length(pos_scale.xz - u_camera_pos.xz);
    float below_top = step(u_camera_pos.y, pos_scale.y + kCanopyTopM * pos_scale.w);
    return mix(1.0, smoothstep(0.9 * r, 1.4 * r, d), below_top);
}

// Desvanecido lejano por instancia (no por celda de culling): el árbol encoge
// hacia su base en la franja en que el terreno pasa al dosel lejano, así que
// el borde del bosque dibujado no forma cuadrados.
float far_fade(vec4 pos_scale) {
    float d = length(pos_scale.xz - u_camera_pos.xz);
    return 1.0 - smoothstep(u_veg_fade_start, u_veg_fade_end, d);
}

void main() {
    float c = cos(i_tint_yaw.w);
    float s = sin(i_tint_yaw.w);
    vec3 p = a_pos * (i_pos_scale.w * camera_clearance(i_pos_scale) * far_fade(i_pos_scale));
    p = vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z) + i_pos_scale.xyz;
    v_world_pos = p;
    v_color = a_color * i_tint_yaw.rgb;
    v_color_smooth = v_color;
    v_normal_smooth = vec3(0.0, 1.0, 0.0); // sin uso: la vegetación va con u_smooth_blend=0
    v_light = vec2(1.0); // la vegetación no recibe sombra/AO precomputados
    v_canopy = vec4(0.0);
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
)glsl" PT_GLSL_SKY_COMMON PT_GLSL_CLOUDS R"glsl(
)glsl" PT_GLSL_SHADOW_COMMON R"glsl(
float hash(float n) { return fract(sin(n) * 43758.5453); }

void main() {
    // Briznas procedurales dentro del card: u se reparte en columnas; cada
    // columna es una brizna que afina hacia arriba y tiene su propia altura.
    // Fuera de la brizna se DESCARTA el fragmento → césped denso sin geometría.
    const float cols = 4.0;
    float x = v_uv.x * cols;
    float cell = floor(x);
    float fx = fract(x);
    float r = hash(cell + 3.17);
    float center = 0.5 + (r - 0.5) * 0.5;
    // Brizna FINA con hueco entre columnas: media anchura ~0.13 en la raíz
    // afina a ~0.02 en la punta (antes 0.32 llenaba el card entero -> se veía
    // como una tarjeta sólida, no como hierba).
    // Estilo low-poly: cada brizna es un triángulo ancho que acaba en punta.
    float top = 0.5 + 0.5 * hash(cell + 9.1);       // altura de cada brizna
    float half_w = 0.32 * (1.0 - v_uv.y / top);
    if (abs(fx - center) > half_w || v_uv.y > top) {
        discard;
    }

    vec3 root_c = vec3(0.19, 0.34, 0.13);
    vec3 tip_c = vec3(0.42, 0.58, 0.26);
    vec3 col = mix(root_c, tip_c,
                   clamp(v_uv.y / max(top, 0.05), 0.0, 1.0)) * v_tint;

    vec3 normal = normalize(cross(dFdx(v_world_pos), dFdy(v_world_pos)));
    float diffuse = max(abs(dot(normal, u_sun_dir)), 0.0); // hoja fina: 2 caras
    // Solo receptora de la CSM (no proyecta): normal offset hacia arriba,
    // la brizna es fina y de doble cara.
    float sun_vis = csm_visibility(v_world_pos, vec3(0.0, 1.0, 0.0))
                  * cloud_shadow(v_world_pos);
    vec3 lit = col * (0.55 + 1.10 * diffuse * sun_vis); // mismo rebalance que el terreno
    frag_color = vec4(tonemap(apply_atmosphere(lit, u_camera_pos, v_world_pos)), 1.0);
}
)glsl";

} // namespace render

// Agua (mar, ríos y lagos): bloque propio, usa PT_GLSL_SKY_COMMON.
#include "render/water_shaders.h"

namespace render {

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
)glsl" PT_GLSL_SKY_COMMON PT_GLSL_CLOUDS R"glsl(
// Disco solar de radio aparente real (0.27°) con oscurecimiento del limbo
// (ley lineal, u = 0.6): radiancia E_sol / Ω atenuada por la misma
// transmitancia que ilumina la escena. Tras ACES satura a blanco; el borde se
// suaviza en el último 15 % del radio (≈ medio píxel a 1080p).
vec3 sun_disc(vec3 dir) {
    float r = length(cross(dir, u_sun_dir)) / kSunAngularRadius;
    if (r >= 1.0 || dot(dir, u_sun_dir) <= 0.0) {
        return vec3(0.0);
    }
    float limb = 1.0 - 0.6 * (1.0 - sqrt(1.0 - r * r));
    float edge = 1.0 - smoothstep(0.85, 1.0, r);
    const float solid_angle = 3.14159265 * kSunAngularRadius * kSunAngularRadius;
    return (kSunIrradiance / solid_angle) * sun_transmittance(u_camera_pos.y)
         * (limb * edge);
}

void main() {
    vec4 p = u_inv_vp * vec4(v_ndc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w);

    vec3 radiance;
    if (dir.y >= 0.0) {
        vec4 cloud = cloud_layer(dir);
        radiance = mix(sky_color(dir) + sun_disc(dir), cloud.rgb, cloud.a);
    } else {
        // OCÉANO INFINITO bajo el horizonte: la malla del mar acaba en el far
        // plane (o en su borde a ±20 km) — sin esto, el corte geométrico se
        // ve contra el cielo. Mismo modelo que kWaterFrag a distancia: fondo
        // abisal (Beer-Lambert ya saturado → color profundo), normal vertical
        // (difusa = altura del sol) y reflejo rasante del cielo, con la misma
        // perspectiva aérea hasta el punto de corte con el mar.
        float dist = max(u_camera_pos.y, 1.0) / max(-dir.y, 1e-4);
        // Reflectancia y nube reflejada como el mar (kSeaReflectance = 0.55,
        // kSeaCloudRefl = 0.12 en water_shaders.h): el horizonte casa.
        float fresnel = 0.04 + 0.96 * pow(1.0 + dir.y, 5.0);
        const vec3 deep = vec3(0.015, 0.19, 0.34);
        vec3 body = deep * (0.45 + 0.55 * max(u_sun_dir.y, 0.0));
        vec3 refl = vec3(dir.x, -dir.y, dir.z);
        vec4 cloud = cloud_layer(refl);
        vec3 sky_refl = mix(sky_color(refl), cloud.rgb, cloud.a * 0.12);
        vec3 lit = mix(body, sky_refl, fresnel * 0.55);
        radiance = atmosphere_segment(lit, u_camera_pos, dir, dist);
    }

    frag_color = vec4(tonemap(radiance), 1.0);
}
)glsl";

#undef PT_GLSL_SKY_COMMON
#undef PT_GLSL_CLOUDS

} // namespace render
