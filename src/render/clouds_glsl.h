#pragma once

// Trozo GLSL compartido: capa de cúmulos de buen tiempo a altura fija. Se
// inserta tras PT_GLSL_SKY_COMMON (usa u_sun_dir, u_camera_pos y sky_color).
//
// - El cielo la dibuja en la dirección de vista (cloud_layer), con la
//   perspectiva aérea hacia el horizonte (se funde con el cielo de su
//   dirección: sin costura).
// - El agua la refleja (misma función con el vector reflejado).
// - El terreno, la vegetación y la hierba reciben su sombra (cloud_shadow):
//   el rayo hacia el sol corta el plano de nubes y la cobertura de ese punto
//   atenúa el sol. Es la misma capa, así que la sombra cae donde la nube está.
//
// Coste: fbm de 5 octavas de ruido de valor; cielo 2 fbm por píxel (densidad
// y densidad desplazada hacia el sol), sombra 1 fbm de 4 octavas por píxel.
#define PT_GLSL_CLOUDS R"glsl(
const float kCloudBaseM = 3200.0;         // m: plano de la capa
const float kCloudInvFeatureM = 1.0 / 2400.0; // tamaño de las masas
const float kCloudShadowStrength = 0.55;  // fracción de sol que quita una nube densa
const vec2 kCloudOffset = vec2(13.7, -4.1);

float cl_hash(vec2 p) {
    p = fract(p * vec2(233.34, 851.73));
    p += dot(p, p + 23.45);
    return fract(p.x * p.y);
}

float cl_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(cl_hash(i), cl_hash(i + vec2(1.0, 0.0)), f.x),
               mix(cl_hash(i + vec2(0.0, 1.0)), cl_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

// fbm con rotación entre octavas (sin alineación con los ejes).
float cloud_fbm(vec2 p, int octaves) {
    const mat2 rot = mat2(0.80, 0.60, -0.60, 0.80);
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * cl_noise(p);
        p = rot * p * 2.03 + vec2(1.7, 9.2);
        amp *= 0.5;
    }
    return sum;
}

// Cobertura [0, 1] en un punto de mundo xz del plano de nubes: cúmulos
// sueltos (cobertura media ~25 %).
float cloud_cover_at(vec2 xz, int octaves) {
    float d = cloud_fbm(xz * kCloudInvFeatureM + kCloudOffset, octaves);
    return smoothstep(0.50, 0.68, d);
}

// Radiancia (rgb) y opacidad (a) de la capa en la dirección dir.
vec4 cloud_layer(vec3 dir) {
    float above = kCloudBaseM - u_camera_pos.y;
    if (dir.y <= 0.01 || above <= 0.0) {
        return vec4(0.0);
    }
    float t = above / dir.y;
    vec2 xz = u_camera_pos.xz + dir.xz * t;
    float cover = cloud_cover_at(xz, 5);
    if (cover <= 0.0) {
        return vec4(0.0);
    }
    // Autosombra: más densidad hacia el sol = cara en sombra (base gris
    // azulada); el borde que mira al sol queda blanco cálido.
    float toward = cloud_cover_at(xz + u_sun_dir.xz * 260.0, 5);
    float lit = clamp(0.75 - 1.4 * (toward - cover), 0.0, 1.0);
    vec3 col = mix(vec3(0.95, 1.02, 1.18), vec3(2.25, 2.15, 1.98), lit);
    // Perspectiva aérea: a lo lejos la nube se funde con el cielo.
    float haze = 1.0 - exp(-t / 45000.0);
    col = mix(col, sky_color(dir), haze);
    float fade = smoothstep(0.02, 0.16, dir.y);
    return vec4(col, cover * fade);
}

// Fracción de sol que llega a pos tras la capa de nubes (1 = cielo despejado).
float cloud_shadow(vec3 pos) {
    if (u_sun_dir.y <= 0.05) {
        return 1.0;
    }
    vec2 xz = pos.xz + u_sun_dir.xz * ((kCloudBaseM - pos.y) / u_sun_dir.y);
    return 1.0 - kCloudShadowStrength * cloud_cover_at(xz, 4);
}
)glsl"
