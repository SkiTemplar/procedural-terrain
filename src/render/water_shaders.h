#pragma once

// Shaders del agua (mar, ríos y lagos). Se incluye SOLO desde
// render/terrain_shaders.h, que define PT_GLSL_SKY_COMMON (cielo analítico +
// niebla) antes de incluirlo: el agua funde con la misma niebla que el resto.
#ifndef PT_GLSL_SKY_COMMON
#error "water_shaders.h se incluye desde render/terrain_shaders.h"
#endif

namespace render {

// Vértice del agua: olas largas por vértice en mar y lagos; los ríos llevan
// su calado y su corriente por vértice (malla a resolución plena) y la normal
// suave de su lámina en a_color. El mar muestrea su calado por píxel de
// u_seabed (r = calado, g = energía de oleaje).
constexpr const char* kWaterVert = R"glsl(
#version 460 core
layout (location = 0) in vec3 a_pos;
layout (location = 1) in vec3 a_color;  // normal suave de la lámina
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
out vec3 v_surf_n; // normal suave de la lámina (sin olas)
out vec3 v_river;  // (velocidad.x, calado, velocidad.z) interpolado
out float v_ww;    // whitewater interpolado
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
    v_surf_n = a_color;
    v_river = a_normal;
    v_ww = a_light.x;
    gl_Position = u_proj * u_view * vec4(p, 1.0);
}
)glsl";

// Fragmento del agua: óptica física de una lámina con fondo.
//  - Superficie: Fresnel de Schlick (F0 = 0.02, n = 1.333) entre el reflejo
//    del cielo analítico y la luz que sale del agua; destello solar GGX cuya
//    rugosidad recoge la varianza de pendiente de las olas no resueltas.
//  - Volumen: Beer-Lambert por canal con el CALADO REAL y el camino refractado
//    (bajada del sol + subida hacia la cámara), más la luz retrodispersada por
//    la columna (reflectancia de columna infinita R∞ ≈ 0.33·b_b/(a + b_b)).
//  - Fondo: el terreno ya pintado detrás. Mezcla de doble fuente
//    (GL_ONE, GL_SRC1_COLOR): destino·T por canal + radiancia añadida, así la
//    arena somera se ve turquesa y el agua honda es opaca, sin alpha de plástico.
constexpr const char* kWaterFrag = R"glsl(
#version 460 core
in vec3 v_world_pos;
in vec3 v_surf_n;
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

// Doble fuente: frag_color = radiancia que añade el agua, frag_trans =
// transmitancia por canal de lo que hay detrás (lecho ya pintado).
layout (location = 0, index = 0) out vec4 frag_color;
layout (location = 0, index = 1) out vec4 frag_trans;
)glsl" PT_GLSL_SKY_COMMON PT_GLSL_CLOUDS R"glsl(
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

// Pendiente del rizado (dos octavas, ~0.8 m y ~0.3 m).
vec2 ripple_slope(vec2 p) {
    vec3 n1 = w_vnoise_d(p * 1.3);
    vec3 n2 = w_vnoise_d(p * 3.1 + vec2(17.3, -9.1));
    return n1.yz * (1.3 * 0.030) + n2.yz * (3.1 * 0.012);
}

// FLOW MAP de dos fases (Vlachos 2010): el patrón se desplaza con la
// corriente −v·φ·T y dos capas desfasadas medio periodo se cruzan con peso
// |1 − 2φ0|, de modo que cada capa se reinicia cuando su peso es nulo. El
// desplazamiento por ciclo se acota a kMaxCycleShift (por encima de 2 m/s el
// gradiente lateral de velocidad cizallaba el patrón); la física completa
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

// Superficie microfacetada: normal resuelta + α² de GGX (rugosidad al
// cuadrado) con la varianza de pendiente que el píxel no resuelve.
struct WaterSurf {
    vec3 n;
    float alpha2;
};

// Rugosidad del rizado capilar no resuelto (α de GGX) en mar y lagos.
const float kMicroAlpha = 0.07;

// OLEAJE FACETADO (dirección de arte low-poly 2026-10): el mar se lee como
// una malla de triángulos planos, no como un espejo con rizado fotográfico.
// Tres trenes de Gerstner largos alrededor de la dirección de viaje de la
// rosa de vientos (abanico más cerrado cuanto más focalizado el viento),
// espectro de saturación (amplitud ∝ λ, misma pendiente a·k por banda, el
// dominante con a = H_s/2) y dispersión de aguas profundas ω = √(g·k). La
// pendiente se evalúa UNA vez por faceta, en el centroide de una retícula
// triangular de kSeaFacetM de lado, así que cada cara refleja un solo trozo de
// cielo. ANTIALIASING: cuando la faceta baja de ~4 px se funde con la lámina
// plana y su varianza de pendiente (s²/2) pasa a α² de GGX — a lo lejos el
// mar es un degradado limpio con el camino de destellos ensanchado, sin moiré.
const float kTrainLambda[3] = float[](70.0, 41.0, 26.0);
const float kTrainAngle[3] = float[](0.0, 0.55, -0.48);
const float kSeaFacetM = 7.0;
// Estilización: menos pendiente que el espectro físico (con la completa, el
// contraste cielo/agua entre caras vecinas era de rayado blanco).
const float kWaveSlopeScale = 0.75;

// Envolvente de grupos de olas: zonas de mar más rizado y más calmo
// (~250 m y ~90 m) que rompen la periodicidad de los trenes.
float wave_envelope(vec2 xz, vec2 wd) {
    vec2 q = xz - wd * (u_time * 1.5);
    return 0.45 + 0.75 * w_vnoise(q * 0.004) + 0.35 * (w_vnoise(q * 0.011 + 7.7) - 0.5);
}

// Centroide (xz) de la faceta triangular que contiene xz: retícula
// equilátera en coordenadas oblicuas (e1 = (1, 0), e2 = (½, √3/2)).
vec2 sea_facet_centroid(vec2 xz) {
    vec2 b = vec2(xz.x - xz.y * 0.5773503, xz.y * 1.1547005) / kSeaFacetM;
    vec2 q = floor(b);
    vec2 f = b - q;
    vec2 c = q + (f.x + f.y > 1.0 ? vec2(2.0 / 3.0) : vec2(1.0 / 3.0));
    return vec2(c.x + 0.5 * c.y, 0.8660254 * c.y) * kSeaFacetM;
}

// Pendiente del oleaje en un punto (x, z) y su varianza (s²/2 sumada).
vec3 swell_slope(vec2 xz, vec2 wd) {
    vec2 perp = vec2(-wd.y, wd.x);
    float slope_k = kWaveSlopeScale * wave_envelope(xz, wd);
    float spread = 1.0 - 0.4 * u_wind_focus;
    // Fase con ruido lento (~85 m): las crestas de un tren se desalinean.
    float jit = w_vnoise(xz * 0.012) * 6.2831853;
    float a0 = 0.5 * u_wave_hmax / kTrainLambda[0];
    vec3 acc = vec3(0.0); // (pendiente x, pendiente z, varianza)
    for (int i = 0; i < 3; ++i) {
        float ang = kTrainAngle[i] * spread;
        vec2 d = wd * cos(ang) + perp * sin(ang);
        float k = 6.2831853 / kTrainLambda[i];
        float s = a0 * kTrainLambda[i] * k * slope_k; // pendiente a·k del tren
        float ph = k * dot(xz, d) - sqrt(9.81 * k) * u_time + jit * (0.7 + 0.4 * float(i));
        acc += vec3(d * (s * cos(ph)), 0.5 * s * s);
    }
    return acc;
}

WaterSurf sea_waves(vec2 xz, vec2 wd, float footprint) {
    vec2 c = sea_facet_centroid(xz);
    vec3 sl = swell_slope(c, wd);
    // Leve inclinación propia de cada cara (hash del centroide): el mosaico
    // se lee aun donde los trenes se cancelan.
    vec2 tilt = vec2(w_hash12(c * 0.37), w_hash12(c * 0.53 + 3.1)) - 0.5;
    vec2 slope = sl.xy + tilt * 0.035;
    // Faceta resuelta (≥ ~4 px) → plano; si no, lámina lisa con su varianza.
    float w = smoothstep(1.5, 4.5, kSeaFacetM / footprint);
    vec3 n = normalize(vec3(-slope.x * w, 1.0, -slope.y * w));
    float alpha = kMicroAlpha;
    return WaterSurf(n, alpha * alpha + (1.0 - w * w) * sl.z);
}

// Normal con que la faceta del mar REFLEJA: la de la faceta acercada a la
// vertical. El facetado se lee en el Fresnel (valor de cada cara), pero el
// reflejo de cielo y nubes sale de una normal suavizada: con la normal
// completa cada triángulo reflejaba un trozo distinto de nube y el mar se
// leía como un mosaico blanco/negro de aliasing. Con la distancia la faceta
// pesa menos (más allá de ~250 m el reflejo es el del mar en calma).
vec3 sea_reflect_normal(vec3 n, float dist, float facet_k) {
    float calm = smoothstep(60.0, 260.0, dist);
    return normalize(mix(vec3(0.0, 1.0, 0.0), n, facet_k * mix(1.0, 0.30, calm)));
}

// Lámina interior: normal suave de la malla + rizado. Ríos: rizado advectado
// por la corriente real (flow map), más picado cuanto más rápida. Lagos y
// remansos quietos: rizado de viento. El remanso del estuario hereda el
// oleaje del mar para que la costura entre las dos mallas no se lea.
WaterSurf inland_surface(vec2 xz, vec2 wd, float footprint) {
    vec3 n = normalize(v_surf_n);
    vec2 vel = v_river.xz;
    float speed = length(vel);
    float wr = smoothstep(2.0, 6.0, 0.6 / footprint); // rizado resuelto
    vec2 slope;
    float rough = mix(0.45, 1.0, smoothstep(0.2, 2.0, speed));
    if (speed > 0.02) {
        vec3 ph = flow_phases(xz);
        vec2 adv = flow_advection(vel);
        vec2 s0 = ripple_slope(xz - adv * (ph.x * kFlowPeriod));
        vec2 s1 = ripple_slope(xz - adv * (ph.y * kFlowPeriod) + vec2(31.7, 12.9));
        slope = mix(s0, s1, ph.z) * rough;
    } else {
        slope = ripple_slope(xz * 0.7 - wd * (u_time * 0.5)) * 0.5;
    }
    n = normalize(n + vec3(-slope.x, 0.0, -slope.y) * wr);
    float alpha = mix(0.04, 0.14, rough) + 0.04 * (1.0 - wr);
    WaterSurf s = WaterSurf(n, alpha * alpha);
    if (v_remanso > 0.0) {
        WaterSurf sea = sea_waves(xz, wd, footprint);
        s.n = normalize(mix(s.n, sea.n, v_remanso));
        s.alpha2 = mix(s.alpha2, sea.alpha2, v_remanso);
    }
    return s;
}
)glsl" R"glsl(
// Calado del mar en el píxel (y energía de oleaje). Descarta donde la lámina
// interior cubre el quad: el mar no se mete en el cauce (en el remanso enrasa
// con el mar y es la única superficie). El recorte se encoge kSeaClipMargin
// para que las dos mallas solapen en vez de dejar grietas de un píxel. Fuera
// del heightfield el CLAMP devuelve el fondo del perímetro, que es abisal.
vec2 sea_depth_energy() {
    vec2 uv = (v_world_pos.xz + vec2(u_domain_half)) / (2.0 * u_domain_half);
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
    return vec2(max(de.r, 0.0), de.g);
}

// Coeficientes ópticos (1/m) a ~650/550/450 nm. Absorción del agua pura
// (Pope y Fry 1997); los ríos y lagos suman materia orgánica disuelta, que
// absorbe en azul (agua verdosa). Dispersión total b (extinción) y
// retrodispersión b_b de partículas: el mar costero es claro, el río turbio.
const vec3 kSigmaA = vec3(0.45, 0.065, 0.02);
const vec3 kCdomA = vec3(0.01, 0.04, 0.13);
const float kWaterIor = 1.333;

// Radiancia que sale del agua (sin fondo) y transmitancia del camino
// sol → lecho → cámara para un calado h. Ángulos refractados por Snell sobre
// la lámina en reposo (el camino no depende del rizado fino).
vec3 water_column(float h, vec3 view_dir, out vec3 trans) {
    bool sea = u_is_sea == 1;
    vec3 sigma_a = sea ? kSigmaA : kSigmaA + kCdomA;
    float b = sea ? 0.06 : 0.25;
    // Mar costero con algo de partícula fina (cocolitos, arena en
    // suspensión): azul turquesa profundo en vez de un marino casi negro.
    vec3 b_b = sea ? vec3(0.020, 0.030, 0.034) : vec3(0.022);
    float inv_n2 = 1.0 / (kWaterIor * kWaterIor);
    float cv = sqrt(1.0 - (1.0 - view_dir.y * view_dir.y) * inv_n2);
    float sy = max(u_sun_dir.y, 0.0);
    float cs = sqrt(1.0 - (1.0 - sy * sy) * inv_n2);
    trans = exp(-(sigma_a + b) * h * (1.0 / cv + 1.0 / cs));
    // Irradiancia bajo la superficie en las unidades del terreno
    // (cielo 0.40 + sol 1.10·cos), menos lo que refleja la lámina al sol.
    float e_w = 0.40 + 1.10 * sy * 0.97;
    vec3 r_inf = 0.33 * b_b / (sigma_a + b_b);
    return e_w * r_inf * (1.0 - trans);
}

// Destello solar: BRDF de microfacetas GGX + visibilidad de Smith correlada
// + Fresnel de Schlick en el semivector. Irradiancia solar = 1.10·π (la del
// terreno lambertiano en sus unidades).
vec3 sun_glint(vec3 n, vec3 v, float alpha2) {
    vec3 l = u_sun_dir;
    vec3 h = normalize(v + l);
    float nl = max(dot(n, l), 0.0);
    float nv = max(dot(n, v), 1e-3);
    float nh = max(dot(n, h), 0.0);
    float dd = nh * nh * (alpha2 - 1.0) + 1.0;
    float d = alpha2 / (3.14159265 * dd * dd);
    float vis = 0.5 / (nl * sqrt(nv * nv * (1.0 - alpha2) + alpha2)
                     + nv * sqrt(nl * nl * (1.0 - alpha2) + alpha2) + 1e-5);
    float f = 0.02 + 0.98 * pow(1.0 - max(dot(v, h), 0.0), 5.0);
    return vec3(1.0, 0.95, 0.86) * (3.4557 * d * vis * f * nl);
}
)glsl" R"glsl(
// ESPUMA DE ROMPIENTE (auditoría 2026-10 #3): H_s = H_abierto·√E (oleaje
// limitado por fetch) y la ola rompe donde H ≥ γ·h, así que la zona de
// rompiente acaba en h_b = H_s/γ. Los frentes de ola viajan hacia la orilla
// paralelos a las isóbatas (la refracción los alinea con el fondo): su fase
// avanza hacia calado menor con el tiempo. La espuma es un encaje de ruido
// cuya cobertura crece con la rotura, no una banda dura; a lo lejos el ruido
// se sustituye por su media (sin aliasing). Costa abrigada: sin espuma.
float sea_foam(float depth, float wave_e, vec2 wd, float footprint) {
    float h_s = u_wave_hmax * sqrt(wave_e);
    float h_b = max(h_s / u_breaker_index, 0.05);
    float zone = 1.0 - smoothstep(0.25 * h_b, 1.3 * h_b, depth);
    float whiteness = smoothstep(0.3, 1.1, h_s);
    vec2 p = v_world_pos.xz;
    float warp = w_vnoise(p * 0.08 + wd * (u_time * 0.05));
    float front = 0.5 + 0.5 * sin(depth / h_b * 8.0 + u_time * 1.6 + warp * 5.0);
    vec2 drift = wd * (u_time * 0.35);
    float lace = 0.6 * w_vnoise((p - drift) * 0.45)
               + 0.4 * w_vnoise((p - drift * 1.6) * 1.7 + vec2(5.2, 1.3));
    lace = mix(lace, 0.5, smoothstep(0.4, 2.5, footprint));
    float cover = zone * whiteness * (0.30 + 0.70 * front);
    // Lámina de lavado: el agua que sube por la playa deja espuma fina.
    float swash = (1.0 - smoothstep(0.0, 0.18 * h_b + 0.04, depth)) * whiteness;
    cover = max(cover, swash * (0.55 + 0.45 * front));
    float lace_foam = smoothstep(1.0 - cover, 1.45 - cover, lace)
                    * smoothstep(0.0, 0.15, cover);
    // Línea de orilla continua: el borde del lavado deja siempre un cordón
    // de espuma, ondulado por el mismo warp; en costa abrigada es más fino.
    float line_w = (0.06 + 0.10 * warp) * (0.5 + 0.5 * whiteness) + 0.03;
    float line = 1.0 - smoothstep(0.35 * line_w, line_w, depth);
    return max(lace_foam, line * (0.55 + 0.35 * whiteness));
}

// Vetas de espuma de rápidos y cascadas (whitewater de la hidrología, 0.5 =
// rápidos, 1 = caída) arrastradas por la corriente con el mismo flow map.
float river_foam(vec2 vel) {
    if (v_ww <= 0.05) {
        return 0.0;
    }
    vec3 ph = flow_phases(v_world_pos.xz);
    vec2 adv = flow_advection(vel);
    float f0 = w_vnoise((v_world_pos.xz - adv * (ph.x * kFlowPeriod)) * 0.9);
    float f1 = w_vnoise((v_world_pos.xz - adv * (ph.y * kFlowPeriod)) * 0.9
                        + vec2(7.3, -3.1));
    float streak = smoothstep(0.30, 0.75, mix(f0, f1, ph.z));
    return clamp(v_ww * (0.40 + 0.60 * streak), 0.0, 1.0);
}
)glsl" R"glsl(
void main() {
    // Huella del píxel en el plano del agua (m): decide qué olas se resuelven.
    vec3 dpx = dFdx(v_world_pos);
    vec3 dpy = dFdy(v_world_pos);
    float footprint = max(length(dpx.xz), length(dpy.xz)) + 1e-3;
    vec3 face_n = normalize(cross(dpx, dpy));
    vec3 view_dir = normalize(u_camera_pos - v_world_pos);
    vec2 wd = normalize(u_wave_dir.xz + vec2(1e-5, 0.0));

    bool sea = u_is_sea == 1;
    vec2 de = sea ? sea_depth_energy() : vec2(max(v_river.y, 0.0), 0.0);
    float depth = de.x;
    WaterSurf surf = sea ? sea_waves(v_world_pos.xz, wd, footprint)
                         : inland_surface(v_world_pos.xz, wd, footprint);
    // PARED DE AGUA: en el borde de la cinta del río los triángulos que caen
    // a la orilla son casi verticales; se desvanecen salvo en cascadas y
    // rápidos declarados (whitewater), donde la cortina es real.
    float wall_k = sea ? 0.0 : (1.0 - smoothstep(0.30, 0.60, face_n.y))
                             * (1.0 - smoothstep(0.40, 0.75, v_ww));

    // Una película de pocos cm rota por el lecho no forma un espejo continuo:
    // la reflexión especular escala con el calado (sin esto el ángulo rasante
    // opaca los márgenes someros y el río se lee "desbordado").
    float film_k = sea ? 1.0 : 1.0 - exp(-depth * 4.0);
    float dist = length(u_camera_pos - v_world_pos);
    vec3 n_fresnel = sea ? sea_reflect_normal(surf.n, dist, 0.70) : surf.n;
    vec3 n_mirror = sea ? sea_reflect_normal(surf.n, dist, 0.50) : surf.n;
    float cos_v = max(dot(view_dir, n_fresnel), 0.0);
    // El mar refleja como el océano infinito del pase de cielo (0.85 del
    // Fresnel): en el horizonte las dos superficies casan sin costura.
    float fresnel = (0.02 + 0.98 * pow(1.0 - cos_v, 5.0)) * film_k * (sea ? 0.85 : 1.0);
    vec3 refl = reflect(-view_dir, n_mirror);
    refl.y = abs(refl.y); // el espejo del agua nunca mira bajo el horizonte

    vec3 trans;
    // Estilización del mar: la columna óptica se acorta (kSeaOpticalScale)
    // para que el degradado arena → turquesa → azul marino se extienda más
    // mar adentro y se lea la batimetría desde lejos.
    const float kSeaOpticalScale = 0.6;
    vec3 body = water_column(sea ? depth * kSeaOpticalScale : depth, view_dir, trans);
    vec4 cloud = cloud_layer(refl);
    // Nube reflejada algo más transparente en el mar: el agua rizada la
    // desdibuja y su contraste con el cielo es menor que mirando arriba.
    vec3 sky_refl = mix(sky_color(refl), cloud.rgb, cloud.a * (sea ? 0.55 : 1.0));
    vec3 radiance = fresnel * sky_refl + (1.0 - fresnel) * body
                  + sun_glint(surf.n, view_dir, surf.alpha2) * film_k * (1.0 - wall_k);
    trans *= 1.0 - fresnel;

    float foam = sea ? sea_foam(depth, de.y, wd, footprint) : river_foam(v_river.xz);
    // Espuma = agua aireada: difusor blanco opaco en la superficie.
    vec3 foam_col = vec3(0.86, 0.89, 0.90) * (0.40 + 1.10 * max(u_sun_dir.y, 0.0));
    radiance = mix(radiance, foam_col, foam * 0.85);
    trans *= 1.0 - foam * 0.85;

    // FLECO de orilla: en el agua de pocos cm el borde se deshilacha con
    // ruido fino en vez de cortar al escalón de la celda. Junto con la pared
    // de cinta, devuelve la transmitancia hacia 1 (solo queda la orilla).
    float keep = 1.0 - 0.85 * wall_k;
    if (!sea && depth < 0.15) {
        keep *= mix(0.4 + 0.6 * w_vnoise(v_world_pos.xz * 1.7), 1.0, depth / 0.15);
    }
    radiance *= keep;
    trans = mix(vec3(1.0), trans, keep);

    // La perspectiva aérea actúa sobre el conjunto agua + lecho: el lecho ya
    // lleva la suya, así que basta con aplicarla a la parte opaca (1 − T) del
    // agua. lit es la radiancia HDR por unidad de opacidad; se tonemapea como
    // cualquier otro material antes de la mezcla de doble fuente.
    vec3 opacity = max(vec3(1.0) - trans, vec3(1e-3));
    vec3 lit = radiance / opacity;
    vec3 color = tonemap(apply_atmosphere(lit, u_camera_pos, v_world_pos));
    frag_color = vec4(color * opacity, 1.0);
    frag_trans = vec4(trans, 1.0);
}
)glsl";

} // namespace render
