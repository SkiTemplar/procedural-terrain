#include "terrain/palette.h"

#include "terrain/mesher.h"
#include "terrain/stand.h"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

// Anclas RELATIVAS de los gradientes: se escalan con la línea de nieve y la
// cumbre reales de cada mundo (con la config por defecto reproducen las
// cotas de la primera pasada: roca ~210→840, nieve ~840→1230).
constexpr float kRockLowFrac = 0.25f;   // la roca arranca al 25% de la línea de nieve
constexpr float kSnowWhiteFrac = 0.70f; // blanco puro al 70% del snow_line→cumbre
constexpr float kSeaDepthRange = 25.0f;   // m de calado que saturan el oscurecido marino
constexpr float kFreshDepthRange = 10.0f; // m de calado para lechos de lago/río
constexpr float kShoreRockBand = 8.0f;    // m sobre el mar: roca lamida, gris fregado
constexpr float kSnowTintKeep = 0.35f;    // fracción del moteado que conserva la nieve
constexpr glm::vec3 kScreeFresh{0.66f, 0.64f, 0.60f}; // fractura fresca de canchal
constexpr float kScreeFreshMix = 0.35f;
constexpr float kScreeBrighten = 1.05f;
constexpr glm::vec3 kBankSilt{0.47f, 0.45f, 0.33f};  // limo de ribera húmedo
constexpr glm::vec3 kWetGravel{0.34f, 0.33f, 0.29f}; // grava de cauce mojada
constexpr float kSnowBelowM = 280.0f;   // m bajo la línea de nieve con neveros
constexpr float kSnowSunShiftM = 110.0f; // m que la solana sube la nieve
constexpr float kSnowReliefFrac = 0.72f; // fracción del relieve que ancla los neveros
constexpr float kWetSandBandM = 0.9f; // m sobre el mar que moja el lavado de ola

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

} // namespace

Palette::Palette(const TerrainData& data)
    : data_(&data),
      sea_level_(data.config.sea_level),
      snow_line_(data.snow_line_m > 0.0f ? data.snow_line_m
                                         : data.config.snow_line_m) {
    tree_line_ = std::max(data.treeline_m > 0.0f ? data.treeline_m
                                                 : data.config.treeline_m,
                          1.0f);
    snow_span_ =
        std::max((data.max_height_m - snow_line_) * kSnowWhiteFrac, 50.0f);
    const float az = kSunAzimuthDeg * 3.14159265f / 180.0f;
    const float alt = kSunAltitudeDeg * 3.14159265f / 180.0f;
    sun_ = {std::cos(az) * std::cos(alt), std::sin(alt), std::sin(az) * std::cos(alt)};
    rock_low_ = kRockLowFrac * snow_line_;
    rock_span_ = std::max(snow_line_ - rock_low_, 1.0f);
}

// Litología bilineal sobre el grid macro del LEM (mismo patrón que la luz
// precomputada). 0.5 (formación neutra) si el campo no existe.
float Palette::lithology_at(float wx, float wz) const {
    return field_at(data_->lithology, data_->litho_res, wx, wz, 0.5f);
}

// Déficit hídrico del suelo de la ecología física (lluvia × TWI × radiación):
// la hierba se agosta donde falta agua, no donde lo dicta un ruido.
float Palette::dryness_at(float wx, float wz) const {
    return field_at(data_->dryness, data_->precip_res, wx, wz, 0.3f);
}

float Palette::field_at(const std::vector<float>& field, int lr, float wx,
                        float wz, float fallback) const {
    if (lr < 2 || field.size() != static_cast<size_t>(lr) * static_cast<size_t>(lr)) {
        return fallback;
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
    // Formación dura gris azulada (cristalina), blanda ocre (sedimentaria):
    // la litología del LEM reparte el matiz de la roca por macizos.
    return c * glm::mix(glm::vec3{0.90f, 0.97f, 1.10f},
                        glm::vec3{1.18f, 0.99f, 0.76f}, lithology_at(cx, cz));
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
    // Estratos con matiz además de valor: capas ferruginosas (pardo rojizo)
    // alternan con las grises cada ~70 m de cota, con fase propia por zona
    // (~330 m) para que dos laderas lejanas no rimen.
    const float phase = 6.2831853f * value_noise(wx * 0.003f - 17.0f, wz * 0.003f + 4.0f);
    const float band = 0.5f + 0.5f * std::sin(y * 0.09f + phase);
    c = glm::mix(c, c * glm::vec3{1.12f, 0.93f, 0.76f},
                 0.55f * glm::smoothstep(0.55f, 0.95f, band) * (1.0f - shore_k));
    return c * rock_banding(y, wx, wz);
}

// Nieve con gradiente de cota: sucia y azulada-gris junto al límite (polvo de
// roca, hielo viejo), blanca pura hacia la cumbre real de este mundo.
glm::vec3 Palette::snow(float y) const {
    const float alt_k = glm::clamp((y - snow_line_) / snow_span_, 0.0f, 1.0f);
    return glm::mix(glm::vec3{0.80f, 0.83f, 0.88f},
                    glm::vec3{0.95f, 0.96f, 0.99f}, alt_k);
}

// Arena de playa cálida (cuarzo con feldespato y conchas): dorada en clima
// frío, más clara en el cálido, nunca el gris casi blanco que satura con el
// sol. La franja que barre el lavado de la ola queda mojada: más oscura y
// algo más saturada.
glm::vec3 Palette::beach_sand(float y, float climate) const {
    const glm::vec3 dry = glm::mix(glm::vec3{0.84f, 0.69f, 0.45f},
                                   glm::vec3{0.89f, 0.80f, 0.61f}, climate * climate);
    const float wet = 1.0f - glm::clamp((y - sea_level_) / kWetSandBandM, 0.0f, 1.0f);
    return glm::mix(dry, dry * glm::vec3{0.66f, 0.62f, 0.56f}, wet);
}

glm::vec3 Palette::ground(Material m, float forest_density, float climate,
                          float y, float wx, float wz, float cx,
                          float cz) const {
    glm::vec3 c;
    if (m == Material::Grass || m == Material::Forest) {
        // Los claros del mosaico de rodales (terrain/stand.h) son pradera
        // también en el suelo, no solo ausencia de árboles.
        const float stand_density =
            forest_density * (1.0f - stand_gap(cx, cz, forest_density));
        c = glm::mix(material_color(Material::Grass),
                     material_color(Material::Forest), stand_density);
        // Calvas agostadas donde el balance hídrico deja déficit (1 − ET/PET:
        // solanas de cresta, sombra de lluvia); en el clima hiperoceánico la
        // mediana es ~0.05, así que la escala arranca en 0.12. El bosque
        // denso protege: viven en los claros.
        const float deficit = dryness_at(cx, cz);
        const float patch =
            glm::smoothstep(0.12f, 0.35f, deficit) * (1.0f - 0.8f * forest_density);
        // La hierba se agosta subiendo hacia el límite del bosque (frío).
        const float alt_dry =
            glm::clamp((y - sea_level_ - 0.30f * tree_line_)
                           / (0.70f * tree_line_),
                       0.0f, 1.0f);
        const float dryness = glm::clamp(
            alt_dry * 0.30f + 0.30f * deficit + 0.55f * patch, 0.0f, 0.85f);
        c = glm::mix(c, glm::vec3{0.62f, 0.55f, 0.31f}, dryness);
    } else if (m == Material::Sand && !littoral_near(wx, wz)) {
        // Arena de ribera (limo y arena fina húmedos junto a un cauce): parda
        // y oscura, no la arena lavada de playa que leía como raya blanca.
        c = kBankSilt;
    } else if (m == Material::Sand) {
        c = beach_sand(y, climate);
    } else if (m == Material::Rock) {
        c = rock(y, climate, wx, wz, cx, cz);
    } else if (m == Material::Gravel && !littoral_near(wx, wz)) {
        c = scree(y, climate, cx, cz); // la grava de playa conserva su color
    } else if (m == Material::RiverBed && std::isnan(water_level_near(wx, wz))) {
        c = kWetGravel; // cauce sin lámina resuelta: grava húmeda, no raya clara

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

// Nieve retenida en la cara: cerca y por encima de la línea de nieve, la
// cara tendida y en umbría la conserva (menos radiación que la funda, menos
// pendiente que la purgue); la solana empinada queda en roca. Es la misma
// regla de balance que la ecología aplica a escala de celda, resuelta aquí a
// escala de faceta.
float Palette::snow_cover(float y, float slope, float sunny) const {
    // Ancla: la línea de nieve estacional o, si queda por encima, el último
    // cuarto del relieve real (en un mundo de cumbres bajas las caras altas
    // en umbría siguen guardando neveros).
    const float anchor = std::min(
        snow_line_, sea_level_ + kSnowReliefFrac * (data_->max_height_m - sea_level_));
    const float eff_y = y - kSnowSunShiftM * sunny;
    const float alt = glm::smoothstep(anchor - kSnowBelowM, anchor + 40.0f, eff_y);
    return alt * (1.0f - glm::smoothstep(0.45f, 0.75f, slope));
}

// Insolación de la cara relativa a la de un llano (1 = llano, >1 solana,
// <1 umbría) con el sol fijo del mundo — la misma radiación que la ecología
// usa para el balance hídrico.
glm::vec3 Palette::face(glm::vec3 base, Material m, glm::vec3 n, float y, float cx,
                        float cz, std::uint32_t id) const {
    glm::vec3 c = base;
    const float slope = glm::clamp(1.0f - n.y, 0.0f, 1.0f);
    const float sunny = glm::clamp(
        glm::dot(n, sun_) / std::max(sun_.y, 0.1f) - 1.0f, -1.0f, 1.0f);
    if (m == Material::Grass || m == Material::Forest) {
        // Lluvia relativa (media 1 en tierra): más agua → verde más hondo y
        // saturado; menos → hacia el pajizo del agostado.
        const float rain = field_at(data_->precipitation, data_->precip_res, cx, cz, 1.0f);
        const float wet = glm::clamp((rain - 1.0f) * 0.8f, -0.4f, 0.4f);
        c = glm::mix(c, c * glm::vec3{0.82f, 1.0f, 0.74f}, std::max(wet, 0.0f));
        c = glm::mix(c, glm::vec3{0.62f, 0.55f, 0.31f}, 0.5f * std::max(-wet, 0.0f));
        // Solana más seca y cálida; umbría más fresca y verde.
        c *= glm::vec3{1.0f + 0.07f * sunny, 1.0f + 0.03f * sunny, 1.0f - 0.10f * sunny};
        // Suelo delgado en las caras empinadas: verde oliva apagado.
        c = glm::mix(c, c * glm::vec3{0.96f, 0.90f, 0.78f},
                     glm::smoothstep(0.12f, 0.45f, slope));
    } else if (m == Material::Rock || m == Material::Gravel) {
        // Las caras tendidas retienen derrubio y líquenes (más oscuras y
        // pardas); las verticales se lavan y quedan con la fractura clara.
        const float steep = glm::smoothstep(0.25f, 0.65f, slope);
        c *= glm::mix(glm::vec3{0.90f, 0.89f, 0.86f}, glm::vec3{1.06f, 1.05f, 1.04f}, steep);
        // Solana cálida (pátina de óxido seca), umbría fría.
        c *= glm::vec3{1.0f + 0.08f * sunny, 1.0f + 0.04f * sunny, 1.0f - 0.05f * sunny};
        c = glm::mix(c, snow(y), snow_cover(y, slope, sunny));
    }
    // Salto de valor por cara (±8 %) y leve de matiz: el mosaico facetado
    // que hace legible la geometría low-poly incluso a contraluz.
    const int ix = static_cast<int>(id & 0xFFFFu);
    const int iz = static_cast<int>(id >> 16u);
    const float v = hash01(ix * 31 + 7, iz * 17 + 3) - 0.5f;
    const float hue = hash01(ix * 11 + 19, iz * 29 + 5) - 0.5f;
    if (m == Material::Forest) {
        // Sotobosque en mosaico: hojarasca parda y musgo entre las copas;
        // de cerca el suelo del bosque deja de ser un oliva uniforme.
        const float litter = glm::smoothstep(0.15f, 0.45f, hue);
        c = glm::mix(c, glm::vec3{0.36f, 0.29f, 0.19f}, 0.45f * litter);
    }
    // En la arena el salto por cara se reduce: de cerca dibujaba triángulos.
    const float jump = (m == Material::Sand) ? 0.07f : 0.16f;
    c *= (1.0f + jump * v) * glm::vec3{1.0f + 0.03f * hue, 1.0f, 1.0f - 0.03f * hue};
    return glm::clamp(c, glm::vec3{0.0f}, glm::vec3{1.0f});
}

} // namespace terrain
