#pragma once

// Trozo GLSL del MAR: oleaje facetado, cabrillas, reflejo y reflejo de la
// isla. Se inserta en kWaterFrag (render/water_shaders.h) tras WaterSurf,
// kMicroAlpha, w_hash12 y w_vnoise, y antes de inland_surface (el remanso
// del estuario hereda sea_waves).
//
// OLEAJE COMO MALLA LOW-POLY (iter. 11): cada cara es el triángulo plano que
// une tres vértices de una retícula equilátera, y la cota de cada vértice es
// la del mar de fondo en ese punto más una cota propia del vértice. Dos caras
// vecinas comparten dos vértices, así que el sombreado sigue las filas del
// oleaje y se arruga de forma coherente; antes cada cara llevaba una
// inclinación propia por hash y el mar se leía como un damero de tintes.
//
// Coste por píxel de mar: 3 vértices × 3 trenes (sin) por retícula, dos
// retículas en la franja de transición; reflejo de la isla con hasta 30
// lecturas de u_seabed solo donde el Fresnel pesa (incidencia rasante).
#define PT_GLSL_SEA R"glsl(
uniform float u_land_top;  // m: cota máxima del terreno (corta la marcha del reflejo)
uniform float u_snow_line; // m: nieve estacional de referencia
uniform float u_tree_line; // m sobre el mar: isoterma del bosque

// Trenes del mar de fondo alrededor de la dirección de viaje de la rosa de
// vientos. Espectro de saturación (amplitud ∝ λ, misma pendiente a·k) con
// los trenes cortos algo más tendidos, y dispersión de aguas profundas.
const float kTrainLambda[3] = float[](96.0, 61.0, 39.0);
const float kTrainAngle[3] = float[](0.0, 0.55, -0.48);
const float kTrainShare[3] = float[](1.0, 0.80, 0.62);
const float kSeaFacetM = 7.0;
// Estilización: con la amplitud física (a = H_s/2) la pendiente a·k ≈ 0.05
// deja todas las caras mirando al cénit; kSwellGain la lleva a ≈ 0.13.
const float kSwellGain = 2.6;
// Cota propia de cada vértice (m): arruga la malla donde los trenes se
// cancelan. Es compartida por las caras del vértice, no un tinte por cara.
const float kVertexJitterM = 0.22;
// Reflectancia del mar respecto al Fresnel físico cerca de la cámara: menos
// espejo, más cuerpo de agua. Hacia el horizonte sube a kSeaGrazingRefl y el
// mar lejano se funde con el cielo brumoso de su dirección.
const float kSeaReflectance = 0.55;
const float kSeaGrazingRefl = 0.90;

// Envolvente de grupos de olas: zonas de mar más rizado y más calmo
// (~250 m y ~90 m) que rompen la periodicidad de los trenes.
float wave_envelope(vec2 xz, vec2 wd) {
    vec2 q = xz - wd * (u_time * 1.5);
    return 0.55 + 0.65 * w_vnoise(q * 0.004) + 0.30 * (w_vnoise(q * 0.011 + 7.7) - 0.5);
}

vec2 w_hash22(vec2 p) {
    return vec2(w_hash12(p * 0.37), w_hash12(p * 0.53 + 3.1));
}

// Coordenadas oblicuas (e1 = (1, 0), e2 = (½, √3/2)) de una retícula
// equilátera de lado facet_m, y su inversa. Las retículas de lado k·facet_m
// (k entero) están anidadas.
vec2 sea_lattice(vec2 xz, float facet_m) {
    return vec2(xz.x - xz.y * 0.5773503, xz.y * 1.1547005) / facet_m;
}
vec2 sea_lattice_world(vec2 c, float facet_m) {
    return vec2(c.x + 0.5 * c.y, 0.8660254 * c.y) * facet_m;
}

// Centroide (xz) de la faceta triangular de lado facet_m que contiene xz.
vec2 sea_facet_centroid(vec2 xz, float facet_m) {
    vec2 b = sea_lattice(xz, facet_m);
    vec2 q = floor(b);
    vec2 f = b - q;
    return sea_lattice_world(q + (f.x + f.y > 1.0 ? vec2(2.0 / 3.0) : vec2(1.0 / 3.0)),
                             facet_m);
}

// Un tren que no cabe en la retícula (< ~3.5 lados por onda) se apaga: en la
// retícula lejana daría aliasing en vez de filas.
float train_weight(float lambda, float facet_m) {
    return smoothstep(2.5, 4.0, lambda / facet_m);
}

// Mar de fondo en p: (cota en m, amplitud total en m). sea_k escala el
// oleaje (1 = mar abierto, menos en costa abrigada).
vec2 sea_swell(vec2 p, vec2 wd, float facet_m, float sea_k) {
    vec2 perp = vec2(-wd.y, wd.x);
    float spread = 1.0 - 0.4 * u_wind_focus;
    float amp = 0.5 * u_wave_hmax * kSwellGain * sea_k * wave_envelope(p, wd);
    // Desfase lento (~250 m): las filas se curvan sin romperse en manchas.
    float jit = w_vnoise(p * 0.004 + 3.3) * 3.0;
    float h = 0.0;
    float h_max = 0.0;
    for (int i = 0; i < 3; ++i) {
        float ang = kTrainAngle[i] * spread;
        vec2 d = wd * cos(ang) + perp * sin(ang);
        float k = 6.2831853 / kTrainLambda[i];
        float a = amp * kTrainShare[i] * (kTrainLambda[i] / kTrainLambda[0])
                * train_weight(kTrainLambda[i], facet_m);
        float ph = k * dot(p, d) - sqrt(9.81 * k) * u_time + jit * (0.6 + 0.3 * float(i));
        h += a * sin(ph);
        h_max += a;
    }
    return vec2(h, max(h_max, 1e-3));
}

// Varianza de pendiente del oleaje de una retícula (para α² cuando la cara
// no se resuelve): Σ ½(a·k)² de los trenes más la de la cota propia.
float sea_slope_variance(float facet_m, float sea_k) {
    float amp = 0.5 * u_wave_hmax * kSwellGain * sea_k;
    float v = 0.0;
    for (int i = 0; i < 3; ++i) {
        float ak = amp * kTrainShare[i] * 6.2831853 / kTrainLambda[0]
                 * train_weight(kTrainLambda[i], facet_m);
        v += 0.5 * ak * ak;
    }
    float j = kVertexJitterM * sea_k / facet_m;
    return v + j * j / 3.0;
}

// Vértice c (coordenadas enteras de la retícula): (x, cota, z, cresta en
// [-1, 1] del mar de fondo). La cota propia late despacio con su fase.
vec4 sea_vertex(vec2 c, float facet_m, vec2 wd, float sea_k) {
    vec2 p = sea_lattice_world(c, facet_m);
    vec2 s = sea_swell(p, wd, facet_m, sea_k);
    vec2 hh = w_hash22(c * 0.0731 + facet_m);
    float bob = sin(u_time * 0.9 + hh.y * 6.2831853);
    return vec4(p.x, s.x + kVertexJitterM * sea_k * (2.0 * hh.x - 1.0) * bob, p.y, s.x / s.y);
}
)glsl" R"glsl(
// CABRILLAS (whitecaps): la rotura se decide por TRAMO de cresta (la faceta
// madre de 4× de lado, ~28 m) y, dentro de él, la espuma cubre las
// subfacetas (un tercio de lado) que quedan sobre la cresta del mar de fondo
// en su centroide: una mancha alargada a lo largo de la cresta con el borde
// roto. El umbral sube hacia el borde de la madre (la espuma nace en el
// centro del tramo). w = resolución de la faceta: sin resolver se sustituye
// por su media (sin centelleo lejano).
float sea_whitecap(vec2 xz, vec2 c, float facet_m, float crest, float cap_p, float w,
                   float footprint, vec2 wd, float sea_k) {
    float mean_cap = cap_p * 0.18 * 0.5;
    vec2 parent = sea_facet_centroid(c, 4.0 * facet_m);
    float group = step(w_hash12(parent * 0.71 + 9.3), 0.9 * cap_p);
    if (group * w <= 0.0) {
        return mean_cap * (1.0 - w);
    }
    float ws = smoothstep(1.5, 4.5, facet_m / (3.0 * footprint));
    float crest_f = crest;
    vec2 at = c;
    if (ws > 0.0) {
        vec2 cs = sea_facet_centroid(xz, facet_m / 3.0);
        vec2 s = sea_swell(cs, wd, facet_m, sea_k);
        crest_f = mix(crest, s.x / s.y + 0.12 * (w_hash12(cs * 0.29 + 1.7) - 0.5), ws);
        at = mix(c, cs, ws);
    }
    float thr = 0.62 + 0.33 * smoothstep(0.3, 1.6, length(at - parent) / facet_m);
    return mix(mean_cap, step(thr, crest_f), w);
}

// Una retícula de facetas: plano de los tres vértices de la cara.
// footprint = huella del píxel; cap_res = 0 deja las cabrillas de esta
// retícula en su media (la retícula lejana las dibujaría como confeti).
WaterSurf sea_facets(vec2 xz, vec2 wd, float footprint, float facet_m, float sea_k,
                     float cap_p, float cap_res, vec2 res_px) {
    vec2 b = sea_lattice(xz, facet_m);
    vec2 q = floor(b);
    bool upper = (b.x - q.x) + (b.y - q.y) > 1.0;
    vec4 v0 = sea_vertex(upper ? q + vec2(1.0) : q, facet_m, wd, sea_k);
    vec4 v1 = sea_vertex(q + vec2(1.0, 0.0), facet_m, wd, sea_k);
    vec4 v2 = sea_vertex(q + vec2(0.0, 1.0), facet_m, wd, sea_k);
    vec3 n = normalize(cross(v1.xyz - v0.xyz, v2.xyz - v0.xyz));
    n *= sign(n.y);
    // Cara resuelta (≥ res_px.y px) → plano; si no, lámina lisa con su varianza.
    float w = smoothstep(res_px.x, res_px.y, facet_m / footprint);
    vec3 nw = normalize(vec3(n.x * w, n.y, n.z * w));
    float var = sea_slope_variance(facet_m, sea_k);
    float crest = (v0.w + v1.w + v2.w) / 3.0;
    vec2 c = (v0.xz + v1.xz + v2.xz) / 3.0;
    return WaterSurf(nw, kMicroAlpha * kMicroAlpha + (1.0 - w * w) * var,
                     sea_whitecap(xz, c, facet_m, crest, cap_p, w * cap_res, footprint,
                                  wd, sea_k));
}

// Mar adentro las caras de kSeaFacetM bajan de unos pocos píxeles: más allá
// de ~200 m entra una retícula 3× mayor (anidada, sin los trenes que no
// caben en ella). Su resolución se mide con la huella MAYOR (la de
// profundidad a incidencia rasante); más allá las caras se funden en la
// lámina lisa con su varianza en α²: reflejo de Fresnel y camino de destellos.
WaterSurf sea_waves(vec2 xz, vec2 wd, float footprint, float cap_p, float sea_k) {
    WaterSurf near_s = sea_facets(xz, wd, footprint, kSeaFacetM, sea_k, cap_p, 1.0,
                                  vec2(1.5, 4.5));
    float far_k = smoothstep(160.0, 320.0, length(xz - u_camera_pos.xz));
    if (far_k <= 0.0) {
        return near_s;
    }
    WaterSurf far_s = sea_facets(xz, wd, footprint, 3.0 * kSeaFacetM, sea_k, cap_p, 0.0,
                                 vec2(3.0, 7.0));
    return WaterSurf(normalize(mix(near_s.n, far_s.n, far_k)),
                     mix(near_s.alpha2, far_s.alpha2, far_k),
                     mix(near_s.cap, far_s.cap, far_k));
}

// Oleaje del punto: la energía de oleaje (fetch y abrigo de la costa) baja
// la amplitud en las calas; mar adentro (> ~30 m) manda el mar abierto.
float sea_open_energy(vec2 de) {
    return mix(de.y, 1.0, smoothstep(8.0, 30.0, de.x));
}
float sea_amplitude(vec2 de) {
    return mix(0.35, 1.0, smoothstep(0.15, 0.85, sea_open_energy(de)));
}

// Probabilidad de cabrilla por faceta: solo en el bajío expuesto. El
// asomeramiento empina la ola de fondo (H crece y λ se acorta al bajar el
// calado) y su cresta rompe; mar adentro, con H_s de 1.5 m, no rompe (antes
// salían manchas blancas sueltas en mar abierto).
float sea_cap_probability(vec2 de) {
    float shoal = 1.0 - smoothstep(5.0, 14.0, de.x);
    return 0.30 * smoothstep(0.25, 0.90, de.y) * shoal;
}

// Luz propia de la faceta del mar sobre el cuerpo de agua: cada cara se
// separa en valor de sus vecinas según mire al sol (±35 % como máximo).
float sea_facet_light(vec3 n) {
    float lam = dot(n, u_sun_dir) - u_sun_dir.y;
    return clamp(1.0 + 3.0 * lam, 0.65, 1.35);
}

// Reflejo del mar: Fresnel con la normal de faceta a medio camino de la
// vertical (ninguna cara refleja el cielo rasante blanco entero). La
// reflectancia sube de kSeaReflectance junto a la cámara a kSeaGrazingRefl
// en incidencia rasante: degradado continuo del azul hondo a la bruma.
float sea_fresnel(vec3 n, vec3 view_dir, out vec3 refl) {
    // A contraluz el cielo cambia muy deprisa junto al sol (aureola de Mie):
    // con la faceta entera cada cara de 7-28 m reflejaba un trozo distinto y
    // el mar salía en damero de cuadros blancos. Hacia el sol la faceta pesa
    // menos en la dirección del reflejo.
    vec3 r_flat = reflect(-view_dir, vec3(0.0, 1.0, 0.0));
    float toward_sun = smoothstep(0.55, 0.95, max(dot(r_flat, u_sun_dir), 0.0));
    vec3 n_r = normalize(mix(vec3(0.0, 1.0, 0.0), n, mix(0.7, 0.2, toward_sun)));
    refl = reflect(-view_dir, n_r);
    float cos_v = max(dot(view_dir, n_r), 0.0);
    float k = mix(kSeaReflectance, kSeaGrazingRefl, smoothstep(0.80, 0.985, 1.0 - cos_v));
    return max((0.02 + 0.98 * pow(1.0 - cos_v, 5.0)) * k, 0.04);
}
)glsl" R"glsl(
// Cota del terreno (m) en xz a partir del calado con signo de u_seabed
// (negativo en tierra). Fuera del heightfield, fondo abisal.
float land_height(vec2 xz) {
    vec2 uv = (xz + vec2(u_domain_half)) / (2.0 * u_domain_half);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return u_sea_level - 100.0;
    }
    return u_sea_level - texture(u_seabed, uv).r;
}

// Albedo medio del terreno reflejado por cota y pendiente: arena de orilla,
// bosque y prado bajo el límite del bosque, roca y pasto alpino por encima y
// nieve sobre su línea. Es una media de lo que pinta la paleta: el reflejo
// llega roto por las facetas y a incidencia rasante, no se ve el detalle.
vec3 reflected_land_albedo(float y, vec3 n) {
    float rel = y - u_sea_level;
    vec3 c = mix(vec3(0.06, 0.12, 0.035), vec3(0.15, 0.22, 0.07),
                 smoothstep(0.55, 0.95, rel / u_tree_line));
    float alpine = smoothstep(0.95, 1.25, rel / u_tree_line);
    float steep = 1.0 - n.y;
    c = mix(c, vec3(0.22, 0.20, 0.17), max(alpine * smoothstep(0.15, 0.35, steep),
                                           smoothstep(0.45, 0.65, steep)));
    c = mix(c, vec3(0.78, 0.80, 0.84), smoothstep(u_snow_line, u_snow_line + 150.0, y));
    return mix(vec3(0.55, 0.50, 0.38), c, smoothstep(1.5, 4.0, rel));
}

// REFLEJO DE LA ISLA: el rayo reflejado se marcha sobre el relieve (pasos
// que crecen un 22 % hasta ~10 km) y se afina por bisección. El punto de
// corte toma el albedo medio del terreno con su luz (cielo + sol sobre la
// normal del relieve) y la perspectiva aérea del tramo agua → ladera. Sin
// esto el agua bajo la costa reflejaba el cielo pálido del horizonte.
// Devuelve (radiancia, cobertura).
vec4 island_reflection(vec3 p, vec3 r) {
    float t_prev = 0.0;
    float t = 5.0;
    float dt = 5.0;
    bool hit = false;
    for (int i = 0; i < 30; ++i) {
        float y = p.y + r.y * t;
        if (y > u_land_top) {
            return vec4(0.0);
        }
        if (land_height(p.xz + r.xz * t) > y) {
            hit = true;
            break;
        }
        t_prev = t;
        dt *= 1.22;
        t += dt;
    }
    if (!hit) {
        return vec4(0.0);
    }
    for (int i = 0; i < 5; ++i) {
        float tm = 0.5 * (t_prev + t);
        bool below = land_height(p.xz + r.xz * tm) > p.y + r.y * tm;
        t = below ? tm : t;
        t_prev = below ? t_prev : tm;
    }
    vec3 hp = p + r * t;
    const float e = 8.0; // m: paso del gradiente (≈ 2 texeles de u_seabed)
    vec3 n = normalize(vec3(land_height(hp.xz - vec2(e, 0.0)) - land_height(hp.xz + vec2(e, 0.0)),
                            2.0 * e,
                            land_height(hp.xz - vec2(0.0, e)) - land_height(hp.xz + vec2(0.0, e))));
    vec3 lit = reflected_land_albedo(hp.y, n)
             * (0.40 + 1.10 * max(dot(n, u_sun_dir), 0.0));
    return vec4(apply_atmosphere(lit, p, hp), 1.0);
}
)glsl"
