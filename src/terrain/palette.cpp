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
constexpr float kSnowDriftM = 70.0f;     // m de ondulación del límite por ventisqueros
constexpr float kSnowReliefFrac = 0.66f; // fracción del relieve que ancla los neveros
constexpr float kSnowHollowM = 90.0f;  // m que un canal baja la nieve (una arista la sube)
constexpr float kSnowShedTanLo = 1.00f; // tanβ (45°): la nieve empieza a purgar
constexpr float kSnowShedTanHi = 1.40f; // tanβ (~54°): pared sin nieve
constexpr float kReliefSlopeM = 6.0f;  // semipaso de la pendiente a escala fija
constexpr float kReliefCurvM = 30.0f;  // radio del anillo de concavidad
constexpr float kConcavityFull = 0.08f; // concavidad que satura canal/arista
constexpr float kReliefMinFrac = 0.6f;  // cota (× límite del bosque) desde la que se mide
constexpr float kWetSandBandM = 0.9f; // m sobre el mar que moja el lavado de ola
constexpr float kBankProbeM = 9.0f;   // paso de la sonda de lámina dulce de la ribera

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
    return 0.90f + 0.08f * std::sin(yb) + 0.03f * std::sin(yb * 2.3f + 1.7f);
}

// Estratos buzantes (paleta low-poly): capas de ~45 m de potencia inclinadas
// según un buzamiento que gira por macizos (~1 km), con contactos de escalón
// corto — bancos claros de caliza o arenisca alternan con lutitas oscuras. El
// buzamiento hace que la misma capa corte la ladera en diagonal, no en curva
// de nivel.
float strata(float y, float wx, float wz, float shore_k) {
    const float dip_a = 6.2831853f * value_noise(wx * 0.0010f + 3.0f, wz * 0.0010f - 8.0f);
    const float dip = 0.10f + 0.12f * value_noise(wx * 0.0016f - 5.0f, wz * 0.0016f + 2.0f);
    const float yt = y + dip * (wx * std::cos(dip_a) + wz * std::sin(dip_a));
    const float layer = 0.5f + 0.5f * std::sin(yt * 0.14f + 1.3f * std::sin(yt * 0.037f));
    const float bed = glm::smoothstep(0.35f, 0.62f, layer);
    return glm::mix(1.0f, glm::mix(0.88f, 1.08f, bed), 1.0f - shore_k);
}

// Tinte de suelo COHERENTE (~3.3 m): el hash por nodo con color flat pintaba
// cada triángulo de un brillo aleatorio distinto — el "tablero de colores
// cuadrados" del feedback. El value noise da manchas continuas entre celdas.
// Variación tonal DENTRO de cada material (feedback 2026-07-08 "color blocky"):
// dos escalas — grano fino (~3 m) + manchas medias (~22 m) — para que el color
// no sea plano por celda. Rango ampliado respecto al 0.94±0.06 anterior.
// El grano fino se queda en ±3 %: en las facetas lejanas (10-40 m) un ruido
// de 3 m equivale a un valor aleatorio por cara y pintaba confeti; el peso de
// la variación pasa a las manchas medias, coherentes entre caras vecinas.
float ground_tint(float wx, float wz) {
    const float fine = value_noise(wx * 0.30f, wz * 0.30f);
    const float coarse = value_noise(wx * 0.045f + 5.0f, wz * 0.045f - 9.0f);
    return 0.90f + 0.06f * fine + 0.14f * coarse;
}

// Mancha coherente de faceta en [-0.5, 0.5] (~90 m y ~30 m): el salto de
// valor entre caras vecinas sale de aquí y no de un hash por cara, así que una
// ladera se lee en parches (suelo, humedad, edad del pasto) y no en confeti.
float facet_patch(float wx, float wz) {
    return 0.62f * value_noise(wx * 0.011f - 31.0f, wz * 0.011f + 17.0f)
         + 0.38f * value_noise(wx * 0.034f + 13.0f, wz * 0.034f - 41.0f) - 0.5f;
}

// Hash entero con avalancha completa (finalizador de murmur3) para el pequeño
// salto residual por cara.
float face_hash01(std::uint32_t h) {
    h ^= h >> 16u;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13u;
    h *= 0xC2B2AE35u;
    h ^= h >> 16u;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

// Decisión por cara (estilo low-poly, iter. 8): una faceta es nieve O roca,
// césped O roca. La mezcla fraccionaria de las coberturas pintaba la cumbre
// entera de un beige gris intermedio, sin separación de materiales. El umbral
// se desplaza con un ruido COHERENTE de ~18 m (±kPickNoise/2) y apenas con el
// hash de la cara: el contacto serpentea en manchas de varias caras. Con un
// salto aleatorio por cara la franja de transición (media ladera, porque la
// cobertura varía despacio) salía en confeti de caras blancas sueltas.
constexpr float kPickNoise = 0.45f;
constexpr float kPickNoiseFreq = 0.055f; // 1/m
constexpr float kPickJitter = 0.06f;
constexpr float kPickSoft = 0.05f;

// Matiz de la nieve según la cara mire al sol (sunny en [-1, 1]): la solana
// crema cálida, la umbría azulada (la ilumina el cielo, no el sol). Exagera el
// contraste luz/sombra que el sombreado solo dejaba en un gris lavanda.
glm::vec3 snow_aspect_tint(float sunny) {
    return glm::mix(glm::vec3{0.88f, 0.93f, 1.01f}, glm::vec3{1.04f, 1.01f, 0.95f},
                    glm::smoothstep(-0.6f, 0.6f, sunny));
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
    // La alta no llega al gris neutro: bajo el ambiente azul del cielo un gris
    // frío se leía violeta y lavado junto al bosque saturado. Iter. 7: menos
    // pardo-rosado (la crítica leía la roca como una banda marrón de cota):
    // gris cálido abajo, gris piedra arriba; el matiz lo pone la formación.
    // Iter. 8: la alta, pizarra fría y más oscura (0.42): junto a la nieve
    // (0.9) la roca se separa en valor y no en un beige intermedio.
    glm::vec3 c = glm::mix(glm::vec3{0.52f, 0.48f, 0.42f},
                           glm::vec3{0.41f, 0.41f, 0.42f}, alt_k);
    // El sector cálido tira a ocre; el frío se queda neutro.
    c = glm::mix(c, glm::vec3{0.56f, 0.50f, 0.40f},
                 0.18f * climate * (1.0f - alt_k));
    // Formación dura gris azulada (cristalina), blanda ocre (sedimentaria):
    // la litología del LEM reparte el matiz de la roca por macizos.
    return c * glm::mix(glm::vec3{0.88f, 0.95f, 1.06f},
                        glm::vec3{1.14f, 1.00f, 0.82f}, lithology_at(cx, cz));
}

// Canchal de montaña: derrubio de la pared de encima, con su mismo matiz de
// formación pero más claro (fractura fresca, sin pátina ni líquenes) y sin el
// bandeado de estratos, que en un montón de bloques no existe.
glm::vec3 Palette::scree(float y, float climate, float cx, float cz) const {
    return glm::mix(rock_base(y, climate, cx, cz), kScreeFresh, kScreeFreshMix)
         * kScreeBrighten;
}

// Fracción de una sonda 3×3 (paso kBankProbeM) con lámina de río o lago.
float Palette::fresh_water_fraction(float wx, float wz) const {
    int wet = 0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const float wl = water_level_near(wx + kBankProbeM * static_cast<float>(dx),
                                              wz + kBankProbeM * static_cast<float>(dz));
            wet += std::isnan(wl) ? 0 : 1;
        }
    }
    return static_cast<float>(wet) / 9.0f;
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
    // Iter. 7: capa ferruginosa más tenue (0.75 → 0.30): con el peso
    // anterior las capas pintaban rayas pardas horizontales en cada ladera.
    c = glm::mix(c, c * glm::vec3{1.16f, 0.92f, 0.72f},
                 0.30f * glm::smoothstep(0.50f, 0.90f, band) * (1.0f - shore_k));
    return c * rock_banding(y, wx, wz) * strata(y, wx, wz, shore_k);
}

// Nieve con gradiente de cota: sucia y azulada-gris junto al límite (polvo de
// roca, hielo viejo), más limpia hacia la cumbre real de este mundo. Iter. 10:
// albedo máximo 0.90 (antes 0.98): con el sol a 1.15 la nieve al sol saturaba
// en el ACES y las caras de un nevero perdían su lado de luz y de sombra.
glm::vec3 Palette::snow(float y) const {
    const float alt_k = glm::clamp((y - snow_line_) / snow_span_, 0.0f, 1.0f);
    return glm::mix(glm::vec3{0.80f, 0.82f, 0.85f},
                    glm::vec3{0.90f, 0.90f, 0.88f}, alt_k);
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
    } else if (m == Material::Sand) {
        // Arena de ribera (limo y arena fina húmedos junto a un cauce): parda
        // y oscura, no la arena lavada de playa que leía como raya blanca.
        // Solo donde hay lámina dulce cerca: la trasplaya abrigada (sin
        // energía de ola en su celda) sigue siendo playa, no un parche de limo
        // en mitad de la arena. La mezcla es continua: sin bordes en sierra.
        const float silt_k = littoral_near(wx, wz)
            ? 0.0f
            : glm::smoothstep(0.0f, 0.45f, fresh_water_fraction(wx, wz));
        c = glm::mix(beach_sand(y, climate), kBankSilt, silt_k);
    } else if (m == Material::Rock) {
        c = rock(y, climate, wx, wz, cx, cz);
    } else if (m == Material::Gravel && !littoral_near(wx, wz)) {
        c = scree(y, climate, cx, cz); // la grava de playa conserva su color
    } else if (m == Material::RiverBed && littoral_near(wx, wz)) {
        // Boca del río en la playa: el cauce corta arena mojada (delta
        // somero), no la grava oscura de un torrente de montaña.
        c = beach_sand(sea_level_, climate);
    } else if (m == Material::RiverBed && std::isnan(water_level_near(wx, wz))) {
        // Cauce sin lámina resuelta: grava húmeda medio cubierta de hierba y
        // musgo de la orilla; la grava sola dibujaba una pista gris continua
        // por el fondo de los barrancos secos.
        c = glm::mix(kWetGravel, material_color(Material::Grass) * 0.80f, 0.45f);

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

// Relieve a escala fija en las facetas cercanas (la normal de una cara de
// 1-2 m es rizado, no ladera): tanβ por diferencias centrales a
// ±kReliefSlopeM y concavidad = (media del anillo a kReliefCurvM − h) /
// kReliefCurvM, > 0 en canal o vaguada y < 0 en cresta o espolón. En facetas
// mayores que la sonda (LOD lejano, iter. 9) la sonda crece con la cara: con
// la sonda fija cada cara de 78 m caía al azar en un lado u otro de un
// barranco más estrecho que ella y la ladera se pintaba en columnas.
Palette::Relief Palette::relief_at(float wx, float wz, float facet_m) const {
    const auto& hf = data_->height;
    const float s = std::max(kReliefSlopeM, 0.5f * facet_m);
    const float gx = (hf.sample(wx + s, wz) - hf.sample(wx - s, wz)) / (2.0f * s);
    const float gz = (hf.sample(wx, wz + s) - hf.sample(wx, wz - s)) / (2.0f * s);
    const float r = std::max(kReliefCurvM, facet_m);
    const float d = r * 0.7071068f;
    const float ring = 0.125f * (hf.sample(wx + r, wz) + hf.sample(wx - r, wz)
                                 + hf.sample(wx, wz + r) + hf.sample(wx, wz - r)
                                 + hf.sample(wx + d, wz + d) + hf.sample(wx - d, wz - d)
                                 + hf.sample(wx + d, wz - d) + hf.sample(wx - d, wz + d));
    return {std::sqrt(gx * gx + gz * gz), (ring - hf.sample(wx, wz)) / r,
            sunny_of(glm::normalize(glm::vec3{-gx, 1.0f, -gz}))};
}

float Palette::sunny_of(glm::vec3 n) const {
    return glm::clamp(glm::dot(n, sun_) / std::max(sun_.y, 0.1f) - 1.0f, -1.0f, 1.0f);
}

float Palette::pick(float k, const FaceCtx& f, std::uint32_t salt) const {
    const float o = static_cast<float>(salt) * 1.37f;
    const float coherent =
        value_noise(f.cx * kPickNoiseFreq + o, f.cz * kPickNoiseFreq - o) - 0.5f;
    const float j = kPickNoise * coherent + kPickJitter * (face_hash01(f.id * 5u + salt) - 0.5f);
    return glm::smoothstep(0.5f - kPickSoft, 0.5f + kPickSoft, k + j);
}

// Nieve retenida en la cara: cerca y por encima de la línea de nieve, la
// cara tendida y en umbría la conserva (menos radiación que la funda, menos
// pendiente que la purgue); la solana empinada queda en roca. Es la misma
// regla de balance que la ecología aplica a escala de celda, resuelta aquí a
// escala de faceta. Iter. 7: el viento barre crestas y la acumula en canales
// y hoyas (concavidad). Iter. 8: el límite lo deshilacha pick() en manchas
// coherentes, no un salto de cota por cara.
float Palette::snow_cover(const FaceCtx& f) const {
    // Ancla: la línea de nieve estacional o, si queda por encima, el último
    // cuarto del relieve real (en un mundo de cumbres bajas las caras altas
    // en umbría siguen guardando neveros).
    const float anchor = std::min(
        snow_line_, sea_level_ + kSnowReliefFrac * (data_->max_height_m - sea_level_));
    // Redistribución por el viento: ventisqueros en sotavento y crestas
    // barridas ondulan el límite ±kSnowDriftM a escala de ~400 m; el ruido
    // solo modula la cota efectiva, la regla de balance es la misma.
    const float drift = kSnowDriftM
        * (2.0f * value_noise(f.cx * 0.0025f + 7.0f, f.cz * 0.0025f - 3.0f) - 1.0f);
    const float hollow =
        kSnowHollowM * glm::clamp(f.relief.concavity / kConcavityFull, -1.0f, 1.0f);
    // Orientación y pendiente de la LADERA (relieve a ~12 m), no de la cara:
    // con la normal de una faceta de 1-2 m el rizado fino decidía nieve o
    // roca cara a cara y la cumbre salía en confeti blanco y gris.
    const float eff_y = f.y - kSnowSunShiftM * f.relief.sunny + drift + hollow;
    const float alt = glm::smoothstep(anchor - kSnowBelowM, anchor + 40.0f, eff_y);
    // La nieve purga por aludes y sluffs por encima de ~45-54° (tanβ 1.0-1.4).
    // Antes el corte era en 1 − cosβ 0.55-0.85 (63-81°): las paredes de roca
    // salían nevadas.
    return alt * (1.0f - glm::smoothstep(kSnowShedTanLo, kSnowShedTanHi, f.relief.tan_slope));
}

// Césped alpino de hombro: pasto corto y ralo, verde oliva que amarillea con
// la cota; la solana más pajiza.
glm::vec3 Palette::alpine_turf(float y, float sunny) const {
    const float alt_k = glm::clamp((y - sea_level_ - tree_line_) / rock_span_, 0.0f, 1.0f);
    const glm::vec3 c = glm::mix(glm::vec3{0.40f, 0.50f, 0.25f}, glm::vec3{0.53f, 0.52f, 0.34f}, alt_k);
    return c * glm::vec3{1.0f + 0.06f * sunny, 1.0f + 0.02f * sunny, 1.0f - 0.08f * sunny};
}

// Roca de montaña por FORMA, no por cota (iter. 7): el material Rock de la
// ecología marca dónde el suelo es delgado, pero a escala de faceta la roca
// aflora en paredes, aristas y espolones; los hombros tendidos que no son
// cresta retienen césped alpino y los canales de pendiente media al pie de
// las paredes guardan canchal gris.
// Umbral de retención del césped alpino: en la base del roquedo (junto al
// límite del bosque) aguanta laderas de ~43°, cerca de la nieve solo los
// rellanos; las vaguadas acumulan coluvión (más), las aristas lo pierden
// (menos) y un ruido de ~170 m modula el umbral (no el resultado): el
// contacto roca/césped sube por canales y baja por espolones, no sigue cota.
glm::vec3 Palette::mountain_rock(glm::vec3 c, const FaceCtx& f) const {
    const Relief& r = f.relief;
    const float ridge = glm::smoothstep(0.0f, kConcavityFull, -r.concavity);
    const float hollow = glm::smoothstep(0.0f, kConcavityFull, r.concavity);
    const float alt_k = glm::clamp((f.y - sea_level_ - tree_line_)
                                       / std::max(snow_line_ - tree_line_, 50.0f),
                                   0.0f, 1.0f);
    const float tan_lim = glm::mix(0.95f, 0.45f, alt_k) + 0.25f * hollow - 0.30f * ridge
        + 0.24f * (value_noise(f.cx * 0.006f + 41.0f, f.cz * 0.006f - 13.0f) - 0.5f);
    const float turf = 1.0f - glm::smoothstep(tan_lim - 0.12f, tan_lim + 0.12f, r.tan_slope);
    const float talus = glm::smoothstep(0.32f, 0.55f, r.tan_slope)
                      * (1.0f - glm::smoothstep(0.80f, 1.05f, r.tan_slope)) * hollow;
    const glm::vec3 scree_c = glm::mix(c, kScreeFresh, 0.55f) * kScreeBrighten;
    c = glm::mix(c, scree_c, pick(talus, f, 0x3Bu));
    return glm::mix(c, alpine_turf(f.y, f.sunny), pick(turf, f, 0x5Du));
}

// Afloramiento en la pradera de montaña: paredes por encima de ~45° en
// cualquier cota y, sobre el bosque, espolones y aristas de pendiente media.
glm::vec3 Palette::grass_outcrop(glm::vec3 c, Material m, const FaceCtx& f) const {
    const Relief& r = f.relief;
    const float y = f.y;
    const float alt_k = glm::smoothstep(0.6f * tree_line_, 1.0f * tree_line_, y - sea_level_);
    const float ridge = glm::smoothstep(0.0f, kConcavityFull, -r.concavity);
    const float cliff = glm::smoothstep(0.95f, 1.30f, r.tan_slope);
    // Sobre el bosque la pared aflora antes (suelo alpino delgado): lenguas
    // de roca que bajan por las caras empinadas y no un límite de cota.
    const float crag = alt_k * std::max(ridge * glm::smoothstep(0.55f, 0.85f, r.tan_slope),
                                        glm::smoothstep(0.85f, 1.10f, r.tan_slope));
    const float k = pick(std::max(cliff, crag), f, 0x61u)
                  * (m == Material::Forest ? 0.3f : 1.0f);
    return k > 0.0f ? glm::mix(c, rock(y, 0.3f, f.cx, f.cz, f.cx, f.cz), k) : c;
}

// Hierba y bosque: lluvia, orientación y pendiente de la cara; en montaña,
// afloramientos de pared y arista y nieve retenida.
glm::vec3 Palette::grass_face(glm::vec3 c, Material m, const FaceCtx& f) const {
    // Lluvia relativa (media 1 en tierra): más agua → verde más hondo y
    // saturado; menos → hacia el pajizo del agostado.
    const float rain = field_at(data_->precipitation, data_->precip_res, f.cx, f.cz, 1.0f);
    const float wet = glm::clamp((rain - 1.0f) * 0.8f, -0.4f, 0.4f);
    c = glm::mix(c, c * glm::vec3{0.82f, 1.0f, 0.74f}, std::max(wet, 0.0f));
    c = glm::mix(c, glm::vec3{0.62f, 0.55f, 0.31f}, 0.5f * std::max(-wet, 0.0f));
    // Solana más seca y cálida; umbría más fresca y verde.
    c *= glm::vec3{1.0f + 0.07f * f.sunny, 1.0f + 0.03f * f.sunny, 1.0f - 0.10f * f.sunny};
    // Suelo delgado en las caras empinadas: verde oliva apagado.
    c = glm::mix(c, c * glm::vec3{0.96f, 0.90f, 0.78f}, glm::smoothstep(0.12f, 0.45f, f.slope));
    if (!f.mountain) {
        return c;
    }
    c = grass_outcrop(c, m, f);
    return glm::mix(c, snow(f.y) * snow_aspect_tint(f.sunny),
                    pick(snow_cover(f), f, 0x17u));
}

// Meteorización de la roca de montaña (iter. 9: la cresta era un único gris
// plano). Tres capas físicas, todas por cara:
// - Matiz por macizo (~70 m): óxidos cálidos frente a roca fresca fría; la
//   cota enfría el matiz (menos alteración química con el frío).
// - Líquenes crustáceos: en caras estables (ni pared lavada ni canchal), con
//   lluvia y lejos de la nieve; ocre amarillento, naranja en solana.
// - Pátina oscura de escorrentía en las paredes de umbría.
glm::vec3 Palette::rock_weathering(glm::vec3 c, const FaceCtx& f) const {
    const float alt_k = glm::clamp((f.y - sea_level_ - tree_line_)
                                       / std::max(data_->max_height_m - sea_level_ - tree_line_, 50.0f),
                                   0.0f, 1.0f);
    const float massif = value_noise(f.cx * 0.014f + 23.0f, f.cz * 0.014f - 61.0f)
                       + 0.35f * (face_hash01(f.id * 3u + 0x51u) - 0.5f) - 0.5f;
    const float warm = glm::clamp(massif * 1.8f + 0.15f - 0.25f * alt_k, -1.0f, 1.0f);
    c *= glm::mix(glm::vec3{0.93f, 0.98f, 1.07f}, glm::vec3{1.10f, 1.00f, 0.86f},
                  0.5f + 0.5f * warm);
    const float rain = field_at(data_->precipitation, data_->precip_res, f.cx, f.cz, 1.0f);
    const float stable = glm::smoothstep(0.30f, 0.55f, f.relief.tan_slope)
                       * (1.0f - glm::smoothstep(0.95f, 1.35f, f.relief.tan_slope));
    const float lichen = stable * glm::smoothstep(0.75f, 1.25f, rain) * (1.0f - 0.6f * alt_k)
        * glm::smoothstep(0.50f, 0.70f, value_noise(f.cx * 0.045f - 9.0f, f.cz * 0.045f + 33.0f));
    const glm::vec3 lichen_c = glm::mix(glm::vec3{0.55f, 0.54f, 0.36f},
                                        glm::vec3{0.66f, 0.50f, 0.30f},
                                        glm::smoothstep(0.0f, 0.6f, f.sunny));
    c = glm::mix(c, lichen_c, 0.55f * lichen);
    const float stain = glm::smoothstep(0.9f, 1.4f, f.relief.tan_slope)
                      * glm::smoothstep(0.1f, -0.5f, f.sunny);
    return c * (1.0f - 0.18f * stain);
}

// Roca y canchal: lavado de las caras verticales, pátina de solana, forma de
// la montaña (mountain_rock) y nieve retenida.
glm::vec3 Palette::rock_face(glm::vec3 c, Material m, const FaceCtx& f) const {
    // Las caras tendidas retienen derrubio y líquenes (más oscuras y
    // pardas); las verticales se lavan y quedan con la fractura clara.
    const float steep = glm::smoothstep(0.25f, 0.65f, f.slope);
    c *= glm::mix(glm::vec3{0.90f, 0.89f, 0.86f}, glm::vec3{1.06f, 1.05f, 1.04f}, steep);
    // Solana cálida (pátina de óxido seca), umbría fría.
    c *= glm::vec3{1.0f + 0.12f * f.sunny, 1.0f + 0.05f * f.sunny, 1.0f - 0.08f * f.sunny};
    if (f.mountain) {
        c = rock_weathering(c, f);
    }
    if (m == Material::Rock && f.mountain) {
        c = mountain_rock(c, f);
    }
    return glm::mix(c, snow(f.y) * snow_aspect_tint(f.sunny),
                    pick(snow_cover(f), f, 0x17u));
}

// Nieve: no se sostiene en las paredes ni en la arista que barre el viento;
// asoma la roca de la pared y el límite serpentea en manchas (pick). El resto
// toma el matiz de su cara.
glm::vec3 Palette::snow_face(glm::vec3 c, const FaceCtx& f) const {
    const float ridge = glm::smoothstep(0.0f, kConcavityFull, -f.relief.concavity);
    const float tan_j = f.relief.tan_slope;
    const float bare = std::max(glm::smoothstep(0.80f, 1.15f, tan_j),
                                ridge * glm::smoothstep(0.40f, 0.70f, tan_j)
                                    * glm::smoothstep(-0.4f, 0.4f, f.relief.sunny));
    // La roca que asoma es la de la pared, con sus estratos y su bandeado
    // (con rock_base sola, cada calva era un gris liso sin ninguna cara).
    const glm::vec3 wall = rock_weathering(rock(f.y, 0.3f, f.cx, f.cz, f.cx, f.cz) * 0.85f, f);
    return glm::mix(c * snow_aspect_tint(f.sunny), wall, pick(bare, f, 0x29u));
}

// Insolación de la cara relativa a la de un llano (1 = llano, >1 solana,
// <1 umbría) con el sol fijo del mundo — la misma radiación que la ecología
// usa para el balance hídrico. El relieve a escala fija solo se mide donde
// decide algo: montaña (sobre kReliefMinFrac del límite del bosque) o cara
// empinada.
glm::vec3 Palette::face(glm::vec3 base, Material m, glm::vec3 n, float y, float cx,
                        float cz, std::uint32_t id, float facet_m) const {
    FaceCtx f;
    f.slope = glm::clamp(1.0f - n.y, 0.0f, 1.0f);
    f.sunny = sunny_of(n);
    f.y = y;
    f.cx = cx;
    f.cz = cz;
    f.id = id;
    f.mountain = y - sea_level_ > kReliefMinFrac * tree_line_ || f.slope > 0.25f;
    if (f.mountain) {
        f.relief = relief_at(cx, cz, facet_m);
    }
    glm::vec3 c = base;
    if (m == Material::Grass || m == Material::Forest) {
        c = grass_face(c, m, f);
    } else if (m == Material::Rock || m == Material::Gravel) {
        c = rock_face(c, m, f);
    } else if (m == Material::Snow) {
        c = snow_face(c, f);
    }
    return glm::clamp(face_variation(c, m, cx, cz, id), glm::vec3{0.0f}, glm::vec3{1.0f});
}

// Variación de valor y matiz entre caras: sobre todo por manchas coherentes
// (facet_patch, ±7 % de valor y deriva de matiz por humedad del parche) y solo
// un salto residual por cara (±2-4 %) que separa dos caras vecinas sin pintar
// confeti. Antes el hash por cara (±8-11 %) dominaba y, visto desde el aire,
// cada ladera era ruido de triángulos.
glm::vec3 Palette::face_variation(glm::vec3 c, Material m, float cx, float cz,
                                  std::uint32_t id) const {
    const float patch = facet_patch(cx, cz);
    const float hue_patch = value_noise(cx * 0.016f + 57.0f, cz * 0.016f - 23.0f) - 0.5f;
    const float v = face_hash01(id * 2u + 1u) - 0.5f;
    const float hue = face_hash01(id * 2u + 0x9E37u) - 0.5f;
    if (m == Material::Forest) {
        // Sotobosque en mosaico: hojarasca parda entre las copas por manchas
        // de ~25 m (claros de hojarasca), no cara a cara.
        const float litter = glm::smoothstep(
            0.55f, 0.75f, value_noise(cx * 0.04f - 3.0f, cz * 0.04f + 19.0f));
        c = glm::mix(c, glm::vec3{0.36f, 0.29f, 0.19f}, 0.40f * litter);
    }
    // En la arena el salto por cara se reduce: de cerca dibujaba triángulos.
    // En la roca, algo mayor: las caras de una pared se separan en valor.
    // La nieve hereda el salto de la roca: sus calvas son roca de pared.
    const float jump = (m == Material::Sand) ? 0.04f
                     : (m == Material::Rock || m == Material::Snow ? 0.10f : 0.05f);
    // La roca separa más el matiz entre caras (cálida/fría) que el suelo.
    const float hue_k = (m == Material::Rock) ? 2.0f : 1.0f;
    const float h = hue_k * (0.10f * hue_patch + 0.03f * hue);
    return c * (1.0f + 0.14f * patch + jump * v)
             * glm::vec3{1.0f + 0.5f * h, 1.0f, 1.0f - 0.5f * h};
}

} // namespace terrain
