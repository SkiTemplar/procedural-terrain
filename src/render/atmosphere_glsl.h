#pragma once

// Trozo GLSL compartido: atmósfera física (dispersión simple Rayleigh + Mie +
// bruma de valle) y tonemap común. Se inserta por concatenación de literales
// tras las declaraciones de cada fragment shader (terreno/vegetación, hierba,
// agua y cielo); usa u_camera_pos, que todos declaran antes de insertarlo.
//
// Todo se calcula en radiancia HDR lineal: cada material aplica primero
// apply_atmosphere() y luego tonemap(), y el cielo sale de la MISMA función
// de dispersión con el rayo llevado al infinito. Así, a distancia grande,
// cualquier superficie converge exactamente al color del cielo de su
// dirección y el horizonte no tiene costura.
//
// Modelo (Nishita 1993; Hoffman y Preetham 2002; Bruneton y Neyret 2008):
// - Tres capas con densidad exponencial en altura: Rayleigh (β por canal,
//   H = 8 km), aerosol Mie (β = 21e-6 /m, albedo 0.9, H = 1.2 km) y bruma de
//   valle de gotas (u_fog_density, albedo 1, H = 400 m).
// - Tierra plana: la columna de densidad de un segmento tiene integral
//   analítica; la de un rayo al infinito es H·exp(-y/H)/μ.
// - Dispersión simple con la proporción entre capas constante a lo largo del
//   rayo: L_in = E_sol·T_sol·(Σ β_i·col_i·P_i / τ)·(1 − e^-τ). Es exacta en un
//   medio de composición uniforme; la transmitancia hacia el sol se evalúa a
//   la altura de la cámara (el sol está fijo a 40°: varía <10 % en la isla).
// - Las órdenes de dispersión múltiple se aproximan con una fase isótropa
//   añadida (kMultiScatter / 4π): aclara el horizonte y la sombra del cielo.
#define PT_GLSL_SKY_COMMON R"glsl(
uniform vec3 u_sun_dir;       // hacia el sol, normalizado
uniform float u_fog_density;  // bruma de valle: extinción a nivel del mar (1/m)

const vec3 kBetaRayleigh = vec3(5.8e-6, 13.5e-6, 33.1e-6); // 680/550/440 nm
const float kBetaMieScat = 21e-6;
const float kBetaMieExt = kBetaMieScat / 0.9;
// Inversa de la altura de escala de cada capa: x = Rayleigh, y = Mie, z = bruma.
const vec3 kInvScaleHeight = vec3(1.0 / 8000.0, 1.0 / 1200.0, 1.0 / 400.0);
const float kMieG = 0.76;
// Irradiancia solar fuera de la atmósfera en las unidades de radiancia del
// sombreado de materiales (calibrada para que el cenit y la bruma lejana
// casen con la exposición del terreno a mediodía).
const float kSunIrradiance = 30.0;
const float kMultiScatter = 0.4;
const float kExposure = 1.0;
// Longitud con la que se estima la proporción entre capas de un rayo largo:
// en rayos de cientos de km solo los primeros ~20 km (≈ 1/β) aportan luz; con
// la columna completa, mirar justo bajo o sobre el horizonte daría
// proporciones distintas y una línea en el horizonte. Coincide con el far plane.
const float kRatioLength = 20000.0;
const float kSunAngularRadius = 0.00465; // rad (0.27°)

// Columnas de densidad relativa (m) de las tres capas sobre un segmento que
// parte a altura y0 y sube dy en una longitud dist. Integral analítica de
// exp(-y/H); con |dy/H| pequeño la diferencia cancela y se usa el punto medio.
vec3 density_columns(float y0, float dy, float dist) {
    vec3 k = dy * kInvScaleHeight;
    vec3 e0 = exp(-y0 * kInvScaleHeight);
    vec3 e1 = exp(-(y0 + dy) * kInvScaleHeight);
    vec3 mid = exp(-(y0 + 0.5 * dy) * kInvScaleHeight);
    bvec3 flat_ray = lessThan(abs(k), vec3(1e-3));
    vec3 k_safe = mix(k, vec3(1.0), flat_ray); // sin 0/0 en la rama descartada
    return dist * mix((e0 - e1) / k_safe, mid, flat_ray);
}

// Espesor óptico por canal de unas columnas (R, M, bruma).
vec3 optical_depth(vec3 col) {
    return kBetaRayleigh * col.x + vec3(kBetaMieExt * col.y + u_fog_density * col.z);
}

// Transmitancia de la luz del sol hasta la altura y (rayo al infinito).
vec3 sun_transmittance(float y) {
    vec3 col = exp(-y * kInvScaleHeight) / kInvScaleHeight;
    return exp(-optical_depth(col) / max(u_sun_dir.y, 0.05));
}

float phase_rayleigh(float mu) {
    return 0.0596831 * (1.0 + mu * mu); // 3/(16π)
}

// Cornette-Shanks: Henyey-Greenstein con el factor (1 + μ²) de Rayleigh.
float phase_mie(float mu) {
    const float g2 = kMieG * kMieG;
    float denom = 1.0 + g2 - 2.0 * kMieG * mu;
    return 0.1193662 * (1.0 - g2) * (1.0 + mu * mu)
         / ((2.0 + g2) * denom * sqrt(denom)); // 3/(8π)
}

// Radiancia dispersada hacia la cámara por un rayo de opacidad (1 − T),
// con la proporción entre capas de col_ratio y coseno mu respecto del sol.
vec3 inscattered(vec3 col_ratio, vec3 opacity, float mu, float y_cam) {
    float iso = kMultiScatter * 0.0795775; // 1/(4π)
    float scat_mie = kBetaMieScat * col_ratio.y + u_fog_density * col_ratio.z;
    vec3 scat = kBetaRayleigh * col_ratio.x * (phase_rayleigh(mu) + iso)
              + scat_mie * (phase_mie(mu) + iso);
    vec3 albedo = scat / max(optical_depth(col_ratio), vec3(1e-12));
    return kSunIrradiance * sun_transmittance(y_cam) * albedo * opacity;
}

// Perspectiva aérea de un tramo de longitud dist desde cam en la dirección
// unitaria dir: atenúa la radiancia del fondo y suma la dispersión del aire.
vec3 atmosphere_segment(vec3 radiance, vec3 cam, vec3 dir, float dist) {
    vec3 col = density_columns(cam.y, dir.y * dist, dist);
    vec3 col_ratio = dist > kRatioLength
        ? density_columns(cam.y, dir.y * kRatioLength, kRatioLength) : col;
    vec3 trans = exp(-optical_depth(col));
    return radiance * trans
         + inscattered(col_ratio, 1.0 - trans, dot(dir, u_sun_dir), cam.y);
}

vec3 apply_atmosphere(vec3 radiance, vec3 cam, vec3 pos) {
    vec3 d = pos - cam;
    float dist = length(d);
    return atmosphere_segment(radiance, cam, d / max(dist, 1e-4), dist);
}

// Radiancia del cielo (HDR, sin tonemap) vista desde la cámara: el mismo
// modelo con el rayo al infinito. Horizonte pálido (τ → ∞, solo queda el
// albedo de dispersión), cenit azul (τ pequeño, domina Rayleigh) y aureola
// de Mie hacia el sol. Bajo el horizonte se usa la dirección rasante.
vec3 sky_color(vec3 dir) {
    float y = u_camera_pos.y;
    float mu_up = max(dir.y, 1e-3);
    vec3 col = exp(-y * kInvScaleHeight) / (kInvScaleHeight * mu_up);
    vec3 col_ratio = density_columns(y, mu_up * kRatioLength, kRatioLength);
    vec3 opacity = 1.0 - exp(-optical_depth(col));
    return inscattered(col_ratio, opacity, dot(dir, u_sun_dir), y);
}

// Peso escalar de la bruma (1 − transmitancia media) para quien necesite una
// fracción y no una radiancia.
float fog_amount(vec3 cam_pos, vec3 frag_pos) {
    vec3 d = frag_pos - cam_pos;
    vec3 col = density_columns(cam_pos.y, d.y, length(d));
    return 1.0 - dot(exp(-optical_depth(col)), vec3(1.0 / 3.0));
}

// Tonemapping ACES (Narkowicz): rolloff fílmico del rango alto → contraste y
// color sin quemar.
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14),
                 0.0, 1.0);
}

// Exposición + ACES comunes a todos los pases: la radiancia de cualquier
// material y la del cielo pasan por la misma curva.
vec3 tonemap(vec3 radiance) {
    return aces(radiance * kExposure);
}
)glsl"
