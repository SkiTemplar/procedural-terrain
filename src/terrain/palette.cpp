#include "terrain/palette.h"

#include "terrain/mesher.h"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

// Anclas RELATIVAS de los gradientes: se escalan con la línea de nieve y la
// cumbre reales de cada mundo (con la config por defecto reproducen las
// cotas de la primera pasada: roca ~210→840, nieve ~840→1230).
constexpr float kRockLowFrac = 0.25f;   // la roca arranca al 25% de la línea de nieve
constexpr float kSnowWhiteFrac = 0.70f; // blanco puro al 70% del snow_line→cumbre
constexpr float kTreeLineDrop = 150.0f; // treeline = snow_line − 150 (generator.cpp)
constexpr float kSeaDepthRange = 25.0f;   // m de calado que saturan el oscurecido marino
constexpr float kFreshDepthRange = 10.0f; // m de calado para lechos de lago/río
constexpr float kShoreRockBand = 8.0f;    // m sobre el mar: roca lamida, gris fregado
constexpr float kSnowTintKeep = 0.35f;    // fracción del moteado que conserva la nieve
constexpr glm::vec3 kScreeFresh{0.66f, 0.64f, 0.60f}; // fractura fresca de canchal
constexpr float kScreeFreshMix = 0.35f;
constexpr float kScreeBrighten = 1.05f;

// Estratos de roca SIN coherencia global (auditoría 2026-07-09): antes un seno
// puro de la cota en fase global pintaba las MISMAS bandas horizontales a las
// mismas alturas en TODA la isla (curvas de nivel, no geología). Ahora la fase
// y la frecuencia se desfasan por zona (~250 m) y se mezclan dos frecuencias
// con una leve inclinación: dos paredes separadas por km ya no riman.
float rock_banding(float y, float wx, float wz) {
    const float phase =
        6.2831853f * value_noise(wx * 0.004f + 2.0f, wz * 0.004f - 7.0f);
    const float tilt =
        0.10f * (value_noise(wx * 0.012f - 5.0f, wz * 0.012f + 3.0f) - 0.5f);
    const float yb = y * (0.35f + tilt) + phase;
    return 0.93f + 0.05f * std::sin(yb) + 0.02f * std::sin(yb * 2.3f + 1.7f);
}

// Tinte de suelo COHERENTE (~3.3 m): el hash por nodo con color flat pintaba
// cada triángulo de un brillo aleatorio distinto — el "tablero de colores
// cuadrados" del feedback. El value noise da manchas continuas entre celdas.
// Variación tonal DENTRO de cada material (feedback 2026-07-08 "color blocky"):
// dos escalas — grano fino (~3 m) + manchas medias (~22 m) — para que el color
// no sea plano por celda. Rango ampliado respecto al 0.94±0.06 anterior.
float ground_tint(float wx, float wz) {
    const float fine = value_noise(wx * 0.30f, wz * 0.30f);
    const float coarse = value_noise(wx * 0.045f + 5.0f, wz * 0.045f - 9.0f);
    return 0.87f + 0.14f * fine + 0.10f * coarse;
}

// Parches de pradera seca (~150-400 m): rompen el verde uniforme con calvas
// pardas de hierba agostada. Umbral alto: manchas definidas, no un degradado.
float dry_patch(float wx, float wz) {
    const float n = 0.65f * value_noise(wx / 380.0f, wz / 380.0f)
                  + 0.35f * value_noise(wx / 160.0f, wz / 160.0f);
    return glm::smoothstep(0.52f, 0.80f, n);
}

} // namespace

Palette::Palette(const TerrainData& data)
    : data_(&data),
      sea_level_(data.config.sea_level),
      snow_line_(data.snow_line_m > 0.0f ? data.snow_line_m
                                         : data.config.snow_line_m) {
    tree_line_ = std::max(snow_line_ - kTreeLineDrop, 1.0f);
    snow_span_ =
        std::max((data.max_height_m - snow_line_) * kSnowWhiteFrac, 50.0f);
    rock_low_ = kRockLowFrac * snow_line_;
    rock_span_ = std::max(snow_line_ - rock_low_, 1.0f);
}

// Litología bilineal sobre el grid macro del LEM (mismo patrón que la luz
// precomputada). 0.5 (formación neutra) si el campo no existe.
float Palette::lithology_at(float wx, float wz) const {
    const int lr = data_->litho_res;
    const auto& field = data_->lithology;
    if (lr < 2 || field.empty()) {
        return 0.5f;
    }
    const float size = data_->height.size();
    const float half = size * 0.5f;
    const float fx = glm::clamp((wx + half) / size * static_cast<float>(lr - 1),
                                0.0f, static_cast<float>(lr - 1));
    const float fz = glm::clamp((wz + half) / size * static_cast<float>(lr - 1),
                                0.0f, static_cast<float>(lr - 1));
    const int x0 = std::min(static_cast<int>(fx), lr - 2);
    const int z0 = std::min(static_cast<int>(fz), lr - 2);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    const auto at = [&](int x, int z) {
        return field[static_cast<size_t>(z) * static_cast<size_t>(lr) + x];
    };
    const float top = std::lerp(at(x0, z0), at(x0 + 1, z0), tx);
    const float bot = std::lerp(at(x0, z0 + 1), at(x0 + 1, z0 + 1), tx);
    return std::lerp(top, bot, tz);
}

float Palette::water_level_near(float wx, float wz) const {
    const auto& hf = data_->height;
    const int res = hf.resolution();
    const float half = hf.size() * 0.5f;
    const float inv_cell = 1.0f / hf.cell_size();
    const int x = std::clamp(
        static_cast<int>(std::round((wx + half) * inv_cell)), 0, res - 1);
    const int z = std::clamp(
        static_cast<int>(std::round((wz + half) * inv_cell)), 0, res - 1);
    return data_->hydrology
        .water_level[static_cast<size_t>(z) * static_cast<size_t>(res)
                     + static_cast<size_t>(x)];
}

// Roca con gradiente de altitud, clima y formación (paleta C4, feedback
// 2026-07-11 "montañas muy uniformes"): la roca baja es parda-cálida
// (meteorización, líquenes, suelo residual); la alta, gris-azulada fría y
// notablemente MÁS OSCURA que la nieve — sin ese contraste las cumbres
// fundían en un blanco uniforme. El matiz cálido↔frío por zonas sale de la
// litología del LEM: formación blanda (sedimentaria) cálida, dura fría.
glm::vec3 Palette::rock_base(float y, float climate, float cx, float cz) const {
    const float alt_k =
        glm::clamp((y - sea_level_ - rock_low_) / rock_span_, 0.0f, 1.0f);
    glm::vec3 c = glm::mix(glm::vec3{0.53f, 0.46f, 0.36f},
                           glm::vec3{0.415f, 0.41f, 0.43f}, alt_k);
    // El sector cálido tira a ocre; el frío se queda neutro.
    c = glm::mix(c, glm::vec3{0.56f, 0.48f, 0.36f},
                 0.22f * climate * (1.0f - alt_k));
    return c * glm::mix(glm::vec3{0.93f, 0.98f, 1.08f},
                        glm::vec3{1.10f, 1.00f, 0.88f}, lithology_at(cx, cz));
}

// Canchal de montaña: derrubio de la pared de encima, con su mismo matiz de
// formación pero más claro (fractura fresca, sin pátina ni líquenes) y sin el
// bandeado de estratos, que en un montón de bloques no existe.
glm::vec3 Palette::scree(float y, float climate, float cx, float cz) const {
    return glm::mix(rock_base(y, climate, cx, cz), kScreeFresh, kScreeFreshMix)
         * kScreeBrighten;
}

bool Palette::littoral_near(float wx, float wz) const {
    const auto& hf = data_->height;
    const auto& wave = data_->wave_energy;
    if (wave.size() != hf.data().size()) {
        return false;
    }
    const int res = hf.resolution();
    const float half = hf.size() * 0.5f;
    const float inv_cell = 1.0f / hf.cell_size();
    const int x = std::clamp(
        static_cast<int>(std::round((wx + half) * inv_cell)), 0, res - 1);
    const int z = std::clamp(
        static_cast<int>(std::round((wz + half) * inv_cell)), 0, res - 1);
    return wave[static_cast<size_t>(z) * static_cast<size_t>(res)
                + static_cast<size_t>(x)] > 0.0f;
}

glm::vec3 Palette::rock(float y, float climate, float wx, float wz,
                        float cx, float cz) const {
    glm::vec3 c = rock_base(y, climate, cx, cz);
    // Banda de salpicadura: la roca lamida por el oleaje queda gris fregado,
    // sin tinte cálido ni formación — ahí no se deposita nada (misma regla
    // física que el clasificador de orilla), y el dithering roca↔grava de la
    // costa vuelve a ser gris-sobre-gris, no un moteado marrón.
    const float shore_k =
        1.0f - glm::clamp((y - sea_level_) / kShoreRockBand, 0.0f, 1.0f);
    c = glm::mix(c, glm::vec3{0.50f, 0.475f, 0.45f}, shore_k);
    return c * rock_banding(y, wx, wz);
}

// Nieve con gradiente de cota: sucia y azulada-gris junto al límite (polvo de
// roca, hielo viejo), blanca pura hacia la cumbre real de este mundo.
glm::vec3 Palette::snow(float y) const {
    const float alt_k = glm::clamp((y - snow_line_) / snow_span_, 0.0f, 1.0f);
    return glm::mix(glm::vec3{0.80f, 0.83f, 0.88f},
                    glm::vec3{0.95f, 0.96f, 0.99f}, alt_k);
}

glm::vec3 Palette::ground(Material m, float forest_density, float climate,
                          float y, float wx, float wz, float cx,
                          float cz) const {
    glm::vec3 c;
    if (m == Material::Grass || m == Material::Forest) {
        c = glm::mix(material_color(Material::Grass),
                     material_color(Material::Forest), forest_density);
        // El bosque denso protege del agostado: los parches viven en claros.
        const float patch = dry_patch(cx, cz) * (1.0f - 0.8f * forest_density)
                          * (0.55f + 0.45f * climate);
        // La hierba se agosta subiendo hacia la treeline de ESTE mundo.
        const float alt_dry =
            glm::clamp((y - sea_level_ - 0.30f * tree_line_)
                           / (0.70f * tree_line_),
                       0.0f, 1.0f);
        const float dryness = glm::clamp(
            alt_dry * 0.30f + (1.0f - climate) * 0.18f + 0.55f * patch,
            0.0f, 0.85f);
        c = glm::mix(c, glm::vec3{0.62f, 0.55f, 0.31f}, dryness);
    } else if (m == Material::Sand) {
        // Arena común (dorada) por defecto; la blanca caribeña solo en el
        // extremo cálido del clima.
        c = glm::mix(glm::vec3{0.84f, 0.76f, 0.55f},
                     glm::vec3{0.94f, 0.90f, 0.74f}, climate * climate);
    } else if (m == Material::Rock) {
        c = rock(y, climate, wx, wz, cx, cz);
    } else if (m == Material::Gravel && !littoral_near(wx, wz)) {
        c = scree(y, climate, cx, cz); // la grava de playa conserva su color

    } else if (m == Material::Snow) {
        c = snow(y);
    } else if (m == Material::Seabed) {
        // El fondo oscurece con el calado REAL bajo el nivel del mar de la
        // config: da lectura de profundidad a través del alpha del agua.
        const float depth_k =
            glm::clamp((sea_level_ - y) / kSeaDepthRange, 0.0f, 1.0f);
        c = glm::mix(material_color(Material::Seabed),
                     glm::vec3{0.33f, 0.36f, 0.30f}, depth_k);
    } else if (m == Material::LakeBed || m == Material::RiverBed) {
        // Mismo trato que el mar: el lecho dulce oscurece con el calado bajo
        // su lámina local — un lago profundo ya no lee como agua de rodilla.
        c = material_color(m);
        const float wl = water_level_near(wx, wz);
        if (!std::isnan(wl)) {
            const float depth_k =
                glm::clamp((wl - y) / kFreshDepthRange, 0.0f, 1.0f);
            c = glm::mix(c, c * 0.45f, depth_k);
        }
    } else {
        c = material_color(m);
    }

    float tint = ground_tint(wx, wz);
    if (m == Material::Snow) {
        // El moteado de suelo (±27%) invertía el gradiente de cota de la
        // nieve (±15%) vértice a vértice: la nieve conserva solo una fracción.
        tint = std::lerp(1.0f, tint, kSnowTintKeep);
    }
    return c * tint;
}

} // namespace terrain
