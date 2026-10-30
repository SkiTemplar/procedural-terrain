#include "veg/veg_renderer.h"

#include "terrain/stand.h"
#include "veg/canopy_layer.h"
#include "veg/models.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace veg {

namespace {

constexpr float kCellSize = 512.0f; // m: unidad de culling
// Texel del dosel lejano (m): del orden de un rodal pequeño, por debajo de
// la faceta del LOD lejano (paso >= 4, ~10 m).
constexpr float kCanopyTexelM = 8.0f;
// Brillo medio de las tres capas de conos del pino (1.00, 1.06, 1.12).
constexpr float kPineFoliageMean = 1.06f;

float hash01(std::uint32_t x, std::uint32_t z, std::uint32_t salt) {
    std::uint32_t h = x * 374761393u + z * 668265263u + salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

struct Placement {
    std::vector<render::Instance> pines;
    std::vector<render::Instance> broadleafs;
    std::vector<render::Instance> rocks;
    std::vector<render::Instance> tufts;
    std::vector<render::Instance> bushes;
    std::vector<render::Instance> logs;
};

// Tintes de flor para una fracción de las matas (multiplican el verde base).
constexpr glm::vec3 kFlowerTints[] = {
    {1.55f, 1.45f, 0.85f}, // amarillo
    {1.65f, 1.15f, 1.55f}, // rosa
    {1.45f, 1.45f, 1.70f}, // blanco azulado
};

// Amplía el AABB de la celda con las instancias de un modelo: radio
// horizontal del modelo (cualquier giro en Y) y su altura, por la escala.
template <typename CellT>
void grow_bounds(CellT& cell, const ModelData& model,
                 const std::vector<render::Instance>& instances) {
    float radius = 0.0f;
    float y_lo = 0.0f;
    float y_hi = 0.0f;
    for (const render::Vertex& v : model.vertices) {
        radius = std::max(radius, glm::length(glm::vec2{v.position.x, v.position.z}));
        y_lo = std::min(y_lo, v.position.y);
        y_hi = std::max(y_hi, v.position.y);
    }
    for (const render::Instance& inst : instances) {
        const glm::vec3 p{inst.pos_scale};
        const float s = inst.pos_scale.w;
        cell.aabb_min = glm::min(cell.aabb_min, p + glm::vec3{-radius, y_lo, -radius} * s);
        cell.aabb_max = glm::max(cell.aabb_max, p + glm::vec3{radius, y_hi, radius} * s);
    }
}

// Valor del grid cuadrado fres² que cubre el mapa en la celda más cercana.
float field_nearest(const std::vector<float>& field, int fres, float size_m,
                    float wx, float wz, float fallback) {
    if (fres < 2 || field.size() != static_cast<size_t>(fres) * static_cast<size_t>(fres)) {
        return fallback;
    }
    const float scale = static_cast<float>(fres - 1) / size_m;
    const int x = std::clamp(static_cast<int>(std::lround((wx + size_m * 0.5f) * scale)),
                             0, fres - 1);
    const int z = std::clamp(static_cast<int>(std::lround((wz + size_m * 0.5f) * scale)),
                             0, fres - 1);
    return field[static_cast<size_t>(z) * static_cast<size_t>(fres) + x];
}

// Tinte de copa por instancia (rgb que multiplica el color del modelo, tronco
// incluido). Un dosel real visto de lejos no es de un solo verde: cada árbol
// difiere en vigor y fenología (valor ±12 %, matiz entre verde azulado y
// verde amarillento) y la frondosa varía más que la conífera. El clima fija
// la media: el estrés hídrico (déficit 1 − ET/PET, el mismo campo que agosta
// la hierba en la paleta) amarillea y aclara la copa; el frío la azulea. Los
// hashes solo reparten la variación individual alrededor de esa media.
// vigor: tinte común del rodal (terrain::stand_vigor), que desplaza el matiz
// de toda la mancha: los rodales jóvenes amarillean, los viejos se oscurecen.
// Lectura por grupo funcional (dirección de arte): conífera perenne oscura y
// azulada, frondosa clara y amarillenta; vistos de lejos, los rodales de una
// y otra se distinguen por valor además de por silueta.
constexpr glm::vec3 kConiferCast{0.78f, 0.86f, 0.92f};
constexpr glm::vec3 kBroadleafCast{1.10f, 1.08f, 0.88f};
// Senescencia temprana: en los rodales de menos vigor (edad, suelo pobre) las
// frondosas viran a ocre o a cobre según el rodal. El vigor efectivo baja con
// la cota (frío) y en la solana (estrés hídrico), y la fracción de copas que
// viran crece de forma gradual a lo largo de kSenescentRamp: con un umbral
// neto los rodales otoñales se leían como manchas estampadas.
constexpr glm::vec3 kSenescentCast[2] = {{1.38f, 1.12f, 0.56f}, {1.52f, 0.98f, 0.54f}};
constexpr float kSenescentVigorLo = -0.18f; // vigor bajo el que el rodal amarillea
constexpr float kSenescentRamp = 0.30f;     // anchura de la transición en vigor
constexpr float kSenescentShare = 0.85f;    // fracción máxima de frondosas que viran
// Dirección horizontal hacia el sol canónico (terrain::kSunAzimuthDeg = 135°).
const glm::vec2 kSunXZ{std::cos(terrain::kSunAzimuthDeg * 0.01745329f),
                       std::sin(terrain::kSunAzimuthDeg * 0.01745329f)};

// Variación individual pequeña frente a la del rodal: a media distancia un
// jitter fuerte árbol a árbol se convertía en un moteado de alta frecuencia.
glm::vec3 canopy_tint(float hue_r, float value_r, float dryness, float climate,
                      bool conifer, float vigor, float stand_cast, float autumn) {
    const float hue = (hue_r - 0.5f) * (conifer ? 0.06f : 0.10f) + 0.30f * vigor;
    glm::vec3 t{1.0f + hue, 1.0f + 0.10f * vigor, 1.0f - 1.6f * hue};
    t *= conifer ? kConiferCast : kBroadleafCast;
    const float stress = std::clamp((dryness - 0.08f) / 0.30f, 0.0f, 1.0f);
    t *= glm::mix(glm::vec3{1.0f}, glm::vec3{1.12f, 1.05f, 0.80f}, 0.6f * stress);
    t *= glm::mix(glm::vec3{1.0f}, glm::vec3{0.92f, 0.96f, 1.05f},
                  0.5f * (1.0f - climate));
    const float turn = std::clamp(
        (kSenescentVigorLo + 0.5f * kSenescentRamp - (vigor - autumn)) / kSenescentRamp,
        0.0f, 1.0f);
    if (!conifer && value_r < kSenescentShare * turn) {
        t *= glm::mix(glm::vec3{1.0f}, kSenescentCast[stand_cast < 0.6f ? 0 : 1],
                      0.55f + 0.45f * turn);
    }
    return t * (0.92f + 0.16f * value_r) * (1.0f + 0.30f * vigor);
}

// Probabilidades del sitio para los árboles y arbustos según el rodal:
// claros del régimen de perturbación, espesura del rodal, aclarado en
// pendiente fuerte (suelo somero, deslizamientos) y ecotono bajo el límite
// del bosque (árboles achaparrados y matorral).
struct StandSite {
    float tree_mult = 1.0f;  // multiplica tree_p
    float bush_add = 0.0f;   // matorral extra (orla de claro, ecotono)
    float tuft_add = 0.0f;   // herbáceas extra en el claro
    float scale_mult = 1.0f; // achaparramiento cerca del límite del bosque
};

StandSite stand_site(float wx, float wz, float fd, float slope_tan, float height_above_sea,
                     float tree_line) {
    StandSite s;
    const float gap = terrain::stand_gap(wx, wz, fd);
    const float steep = std::clamp((slope_tan - 0.55f) / 0.45f, 0.0f, 1.0f);
    const float ecotone = std::clamp(
        (height_above_sea - 0.65f * tree_line) / (0.35f * tree_line), 0.0f, 1.0f);
    // Bosquetes (~12 m): los árboles se agrupan con huecos entre grupos en
    // vez de repartirse uno por celda con la misma separación.
    const float grove = 0.55f + 0.90f * terrain::value_noise(wx * 0.085f + 3.0f,
                                                             wz * 0.085f - 8.0f);
    s.tree_mult = (1.0f - gap) * terrain::stand_thickness(wx, wz) * grove
                * (1.0f - 0.6f * steep) * (1.0f - 0.45f * ecotone);
    s.bush_add = 0.06f * gap * (1.0f - gap) + 0.025f * ecotone;
    s.tuft_add = 0.10f * gap;
    s.scale_mult = 1.0f - 0.45f * ecotone;
    return s;
}

} // namespace

VegRenderer::VegRenderer(const terrain::TerrainData& data,
                         const terrain::DetailSampler& sampler) {
    const auto& hf = data.height;
    const int res = hf.resolution();
    const auto& cfg = data.config;

    const int cells_per_side =
        std::max(1, static_cast<int>(std::ceil(cfg.size_m / kCellSize)));
    std::vector<Placement> placements(
        static_cast<size_t>(cells_per_side) * cells_per_side);

    const auto cell_of = [&](float wx, float wz) -> Placement& {
        const int cx = std::clamp(
            static_cast<int>((wx + cfg.size_m * 0.5f) / kCellSize), 0, cells_per_side - 1);
        const int cz = std::clamp(
            static_cast<int>((wz + cfg.size_m * 0.5f) / kCellSize), 0, cells_per_side - 1);
        return placements[static_cast<size_t>(cz) * cells_per_side + cx];
    };

    // Muestreo por celda del heightfield (~2.4 m): probabilidades acumuladas
    // por material. Con p_tuft=0.05 sale ~1 mata / 120 m² de pradera.
    const std::uint32_t salt = cfg.seed;
    const float tree_line =
        std::max(data.treeline_m > 0.0f ? data.treeline_m : cfg.treeline_m, 1.0f);
    CanopyAccumulator crowns(
        cfg.size_m, std::clamp(static_cast<int>(cfg.size_m / kCanopyTexelM), 64, 2048));
    for (int z = 2; z < res - 2; ++z) {
        for (int x = 2; x < res - 2; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(res)
                           + static_cast<size_t>(x);
            if (!std::isnan(data.hydrology.water_level[i])) {
                continue; // celda con lámina de agua: nada de vegetación
            }
            const auto m = data.material[i];

            float tree_p = 0.0f;
            float rock_p = 0.0f;
            float tuft_p = 0.0f;
            float bush_p = 0.0f;
            float boulder_p = 0.0f;
            float log_p = 0.0f;
            // Rocas sueltas (rock/boulder) ELIMINADAS (feedback 2026-07-08:
            // "quitar los peñascos, no parecen realistas"). La roca real
            // queda como material del terreno (acantilados, canchales); ya no
            // se salpican modelos de pedrusco por la hierba ni la montaña.
            // Árboles: densidad muy subida (feedback: "muchísimos más árboles
            // en la montaña"); la treeline extendida en el generador los sube
            // en cota.
            // Copa GRADUADA por forest_density (auditoría 2026-07-09): antes el
            // bosque paraba en seco al cruzar la frontera de material (muro de
            // coníferas, density 0.155→0.011 en una celda), mientras el COLOR
            // del suelo ya verdeaba gradual con forest_density → el desajuste
            // cantaba. Ahora la probabilidad de árbol decae suave hacia el
            // claro con la misma señal, así árbol y verdor se apagan juntos.
            const float fd = data.forest_density[i];
            const float canopy = std::clamp((fd - 0.12f) / 0.45f, 0.0f, 1.0f);
            StandSite site;
            if (m == terrain::Material::Forest || m == terrain::Material::Grass) {
                const float cwx = hf.world_x(x);
                const float cwz = hf.world_z(z);
                const glm::vec3 cn = sampler.normal_near(cwx, cwz);
                site = stand_site(
                    cwx, cwz, fd,
                    std::sqrt(std::max(0.0f, 1.0f - cn.y * cn.y)) / std::max(cn.y, 1e-3f),
                    hf.at(x, z) - cfg.sea_level, tree_line);
            }
            switch (m) {
                case terrain::Material::Forest:
                    tree_p = 0.16f * canopy * site.tree_mult;
                    tuft_p = 0.085f + site.tuft_add;
                    bush_p = 0.012f + site.bush_add; log_p = 0.0030f;
                    break;
                case terrain::Material::Grass:
                    // Árboles dispersos de pradera + cola de la copa que se
                    // adentra en el claro (canopy>0 en la franja de transición).
                    tree_p = (0.011f + 0.15f * canopy) * site.tree_mult;
                    tuft_p = 0.200f; bush_p = 0.010f + site.bush_add;
                    break;
                case terrain::Material::Sand:
                    if (data.wave_energy.size() == data.material.size()
                        && data.wave_energy[i] <= 0.0f) {
                        // Ribera de cauce (no playa): juncos y sauces bajos.
                        tuft_p = 0.15f; bush_p = 0.03f;
                    } else {
                        tuft_p = 0.008f; // hierba rala de duna
                    }
                    break;
                case terrain::Material::Marsh:
                    // Marisma: pura vegetación herbácea en la realidad —
                    // estaba estéril (auditoría Fase B). Matas densas de
                    // junco y algún arbusto; sin árboles (suelo anegado).
                    tuft_p = 0.120f;
                    bush_p = 0.006f;
                    break;
                default:
                    break;
            }
            const float total_p = tree_p + rock_p + tuft_p + bush_p + boulder_p + log_p;
            const float roll = hash01(static_cast<std::uint32_t>(x),
                                      static_cast<std::uint32_t>(z), salt);
            if (roll >= total_p) {
                continue;
            }

            // Jitter subcelda para romper la rejilla.
            const float jx = (hash01(static_cast<std::uint32_t>(x),
                                     static_cast<std::uint32_t>(z), salt + 1) - 0.5f)
                           * hf.cell_size();
            const float jz = (hash01(static_cast<std::uint32_t>(x),
                                     static_cast<std::uint32_t>(z), salt + 2) - 0.5f)
                           * hf.cell_size();
            const float wx = hf.world_x(x) + jx;
            const float wz = hf.world_z(z) + jz;
            const float wy = sampler.height(wx, wz);
            if (wy < cfg.sea_level + 0.4f) {
                continue;
            }

            const float scale_r = hash01(static_cast<std::uint32_t>(x),
                                         static_cast<std::uint32_t>(z), salt + 3);
            const float yaw = hash01(static_cast<std::uint32_t>(x),
                                     static_cast<std::uint32_t>(z), salt + 4)
                            * 6.2831853f;
            // Luz baked en los modelos (feedback 2026-07-10 "que los modelos
            // tengan luz"): la instancia recibe la sombra arrojada y el AO del
            // terreno en su base → los árboles/matas en la sombra de una montaña
            // se oscurecen como el suelo. Va en el tinte (no hay v_light por
            // instancia); un árbol al sol queda a plena luz.
            // Suave: dentro del alcance de la CSM la sombra real ya oscurece
            // la copa; esto solo asienta los árboles lejanos en la umbría.
            const float veg_light = (0.70f + 0.30f * sampler.shadow_at(wx, wz))
                                  * (0.75f + 0.25f * sampler.sky_ao_at(wx, wz));
            const float value_r = hash01(static_cast<std::uint32_t>(x),
                                         static_cast<std::uint32_t>(z), salt + 5);
            const float tint = (0.85f + 0.30f * value_r) * veg_light;
            const float hue_r = hash01(static_cast<std::uint32_t>(x),
                                       static_cast<std::uint32_t>(z), salt + 12);
            const float site_dry = field_nearest(data.dryness, data.precip_res,
                                                 cfg.size_m, wx, wz, 0.1f);
            const float site_climate = data.climate[i];
            const float vigor = terrain::stand_vigor(wx, wz);
            // Otoño adelantado con la cota (frío) y en la solana (sequía).
            const glm::vec3 sn = sampler.normal_near(wx, wz);
            const float autumn =
                0.22f * std::clamp((wy - cfg.sea_level) / tree_line, 0.0f, 1.0f)
                + 0.25f * (sn.x * kSunXZ.x + sn.z * kSunXZ.y);
            // Ocre o cobre por rodal (~110 m), no por árbol.
            const float stand_cast = terrain::value_noise(wx * 0.009f - 23.0f,
                                                          wz * 0.009f + 11.0f);

            Placement& cell = cell_of(wx, wz);
            if (roll < tree_p) {
                // Arriba dominan las coníferas; abajo se mezclan frondosos.
                // La composición va por rodales (manchas de ~250 m), no
                // árbol a árbol: a lo lejos se leen bosques distintos. La
                // rampa es empinada (x2.2 alrededor del 50 %): casi todos los
                // rodales son puros y la mezcla queda en sus bordes, en vez
                // del salpicado oscuro/claro de copas sueltas.
                const float conifer_bias = std::clamp(
                    0.5f + 2.2f * ((wy - 245.0f) / 250.0f
                                   + 1.3f * terrain::stand_composition(wx, wz)),
                    0.03f, 0.97f);
                const bool pine = hash01(static_cast<std::uint32_t>(x),
                                         static_cast<std::uint32_t>(z), salt + 6)
                                < conifer_bias;
                // Distribución de EDAD (feedback 2026-07-08 "distintos tamaños"):
                // curva cuadrática → muchos árboles jóvenes pequeños y pocos
                // maduros grandes (0.5–2.0×), como un bosque real, en vez del
                // rango estrecho y uniforme anterior.
                // Edad común del rodal (~80 m) por encima del reparto
                // individual: manchas de regenerado bajo junto a rodales
                // maduros, y no un tapiz de copas iguales.
                const float stand_age = terrain::value_noise(wx * 0.0125f + 9.0f,
                                                             wz * 0.0125f - 51.0f);
                const float scale = (0.55f + 1.05f * scale_r * std::sqrt(scale_r))
                                  * (0.70f + 0.60f * stand_age) * site.scale_mult;
                // Base bien hundida: en pendiente los troncos no quedan al aire.
                const glm::vec3 crown = canopy_tint(hue_r, value_r, site_dry, site_climate,
                                                    pine, vigor, stand_cast, autumn);
                const glm::vec3 t = crown * veg_light;
                render::Instance inst{{wx, wy - 0.35f * scale, wz, scale},
                                      {t.r, t.g, t.b, yaw}};
                // El dosel lejano recibe el color sin la luz horneada: el
                // terreno ya aplica la suya (sombra y AO) a la faceta.
                crowns.add(wx, wz,
                           (pine ? kPineCrownRadiusM : kLeafCrownRadiusM) * scale,
                           crown * (pine ? kPineFoliage * kPineFoliageMean : kLeafFoliage));
                (pine ? cell.pines : cell.broadleafs).push_back(inst);
            } else if (roll < tree_p + rock_p) {
                const float scale = 0.4f + 1.8f * scale_r;
                cell.rocks.push_back(render::Instance{{wx, wy - 0.3f * scale, wz, scale},
                                                      {tint, tint, tint, yaw}});
            } else if (roll < tree_p + rock_p + tuft_p) {
                const float scale = 0.6f + 0.7f * scale_r;
                glm::vec3 t{tint, tint, tint};
                // Una fracción de las matas florece.
                const float flower = hash01(static_cast<std::uint32_t>(x),
                                            static_cast<std::uint32_t>(z), salt + 7);
                if (flower < 0.16f && m == terrain::Material::Grass) {
                    t = kFlowerTints[static_cast<size_t>(flower * 100.0f) % 3] * tint;
                }
                cell.tufts.push_back(
                    render::Instance{{wx, wy - 0.04f, wz, scale}, {t.r, t.g, t.b, yaw}});
            } else if (roll < tree_p + rock_p + tuft_p + bush_p) {
                const float scale = 0.6f + 0.9f * scale_r;
                const glm::vec3 t = canopy_tint(hue_r, value_r, site_dry, site_climate,
                                                false, vigor, stand_cast, autumn) * veg_light;
                cell.bushes.push_back(render::Instance{{wx, wy - 0.08f, wz, scale},
                                                       {t.r, t.g, t.b, yaw}});
            } else if (roll < tree_p + rock_p + tuft_p + bush_p + boulder_p) {
                // Roca grande con séquito: 2-3 rocas medianas alrededor.
                const float scale = 2.6f + 3.0f * scale_r;
                cell.rocks.push_back(render::Instance{{wx, wy - 0.25f * scale, wz, scale},
                                                      {tint, tint, tint, yaw}});
                const int companions = 2 + static_cast<int>(
                    hash01(static_cast<std::uint32_t>(x),
                           static_cast<std::uint32_t>(z), salt + 8) * 2.0f);
                for (int c = 0; c < companions; ++c) {
                    const float ca = hash01(static_cast<std::uint32_t>(x + c),
                                            static_cast<std::uint32_t>(z), salt + 9)
                                   * 6.2831853f;
                    const float cd = scale * (0.8f + 0.6f * hash01(
                        static_cast<std::uint32_t>(x),
                        static_cast<std::uint32_t>(z + c), salt + 10));
                    const float cwx = wx + cd * std::cos(ca);
                    const float cwz = wz + cd * std::sin(ca);
                    const float cwy = sampler.height(cwx, cwz);
                    const float cscale = 0.6f + 1.1f * hash01(
                        static_cast<std::uint32_t>(x + c),
                        static_cast<std::uint32_t>(z + c), salt + 11);
                    cell.rocks.push_back(
                        render::Instance{{cwx, cwy - 0.2f * cscale, cwz, cscale},
                                         {tint, tint, tint, ca}});
                }
            } else {
                // Troncos SOLO en suelo casi llano (feedback 2026-07-08): la
                // instancia solo gira en yaw (no se inclina), así que en
                // pendiente un extremo del prisma quedaba flotando. Se hunden
                // un poco más para asentarse.
                const glm::vec3 ln = sampler.normal_near(wx, wz);
                const float lslope = std::sqrt(std::max(0.0f, 1.0f - ln.y * ln.y))
                                   / std::max(ln.y, 1e-3f);
                if (lslope < 0.13f) {
                    // Troncos finos (0.5-0.8×, ~0.4-0.6 m de diámetro): a 1.4×
                    // el prisma medía un metro de diámetro, desproporcionado
                    // junto a las facetas de 1.2 m del suelo cercano.
                    const float scale = 0.5f + 0.3f * scale_r;
                    cell.logs.push_back(render::Instance{{wx, wy - 0.12f, wz, scale},
                                                         {tint, tint, tint, yaw}});
                }
            }
        }
    }

    upload_canopy(crowns);

    // Un lote instanciado por (celda, tipo): draw call por lote, culling por
    // distancia al centro de la celda. Las matas/arbustos van en lotes "near"
    // con distancia de dibujo corta (son miles y de cerca es donde importan).
    const ModelData pine = make_pine();
    const ModelData broadleaf = make_broadleaf();
    const ModelData rock = make_rock();
    const ModelData tuft = make_tuft();
    const ModelData bush = make_bush();
    const ModelData log_model = make_log();

    for (int cz = 0; cz < cells_per_side; ++cz) {
        for (int cx = 0; cx < cells_per_side; ++cx) {
            Placement& p = placements[static_cast<size_t>(cz) * cells_per_side + cx];
            if (p.pines.empty() && p.broadleafs.empty() && p.rocks.empty()
                && p.tufts.empty() && p.bushes.empty() && p.logs.empty()) {
                continue;
            }
            counts_.pines += p.pines.size();
            counts_.broadleafs += p.broadleafs.size();
            counts_.rocks += p.rocks.size();
            counts_.tufts += p.tufts.size();
            counts_.bushes += p.bushes.size();
            counts_.logs += p.logs.size();
            Cell cell;
            cell.center = {(-cfg.size_m * 0.5f) + (static_cast<float>(cx) + 0.5f) * kCellSize,
                           (-cfg.size_m * 0.5f) + (static_cast<float>(cz) + 0.5f) * kCellSize};
            const auto add = [&](std::vector<render::InstancedMesh>& dst,
                                 const ModelData& model,
                                 const std::vector<render::Instance>& instances) {
                if (!instances.empty()) {
                    dst.emplace_back(model.vertices, model.indices, instances);
                    total_instances_ += instances.size();
                    grow_bounds(cell, model, instances);
                }
            };
            add(cell.batches_far, pine, p.pines);
            add(cell.batches_far, broadleaf, p.broadleafs);
            add(cell.batches_far, rock, p.rocks);
            add(cell.batches_far, log_model, p.logs);
            add(cell.batches_near, tuft, p.tufts);
            add(cell.batches_near, bush, p.bushes);
            cells_.push_back(std::move(cell));
        }
    }
}

VegRenderer::~VegRenderer() {
    if (canopy_texture_ != 0) {
        glDeleteTextures(1, &canopy_texture_);
    }
}

// RGBA16F con mipmaps: el terreno lejano lee el dosel por vértice con un LOD
// que crece con la distancia (sin centelleo de texeles sueltos).
void VegRenderer::upload_canopy(const CanopyAccumulator& canopy) {
    const std::vector<glm::vec4> texels = canopy.resolve();
    const int res = canopy.resolution();
    glGenTextures(1, &canopy_texture_);
    glBindTexture(GL_TEXTURE_2D, canopy_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, res, res, 0, GL_RGBA, GL_FLOAT,
                 texels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void VegRenderer::bind_canopy(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, canopy_texture_);
    glActiveTexture(GL_TEXTURE0);
}

void VegRenderer::draw(const glm::vec3& camera_pos, float max_distance) const {
    const glm::vec2 cam{camera_pos.x, camera_pos.z};
    // Margen de media diagonal de celda para no cortar lotes por el centro.
    const float far_cutoff = max_distance + kCellSize * 0.71f;
    // Matas/arbustos (~0.5 m) dejan de dibujarse a >~400 m: allí su tamaño
    // angular cae bajo ~1 px y solo aportan centelleo/aliasing — el "confeti"
    // de vegetación (auditoría 2026-07-09). Antes se dibujaban hasta 900 m.
    const float near_cutoff = std::min(400.0f, max_distance) + kCellSize * 0.71f;

    for (const Cell& cell : cells_) {
        const float dist = glm::length(cam - cell.center);
        if (dist <= far_cutoff) {
            for (const auto& batch : cell.batches_far) {
                batch.draw();
            }
        }
        if (dist <= near_cutoff) {
            for (const auto& batch : cell.batches_near) {
                batch.draw();
            }
        }
    }
}

void VegRenderer::draw_casters(const render::Frustum& light, const glm::vec3& camera_pos,
                               float max_distance) const {
    // Mismos cortes por distancia que draw(): solo proyecta lo que existe en
    // pantalla. Además, celda a celda contra el volumen del sol de la cascada.
    const glm::vec2 cam{camera_pos.x, camera_pos.z};
    const float far_cutoff = max_distance + kCellSize * 0.71f;
    const float near_cutoff = std::min(400.0f, max_distance) + kCellSize * 0.71f;
    for (const Cell& cell : cells_) {
        const float dist = glm::length(cam - cell.center);
        if (dist > far_cutoff || !light.intersects(cell.aabb_min, cell.aabb_max)) {
            continue;
        }
        for (const auto& batch : cell.batches_far) {
            batch.draw();
        }
        if (dist <= near_cutoff) {
            for (const auto& batch : cell.batches_near) {
                batch.draw();
            }
        }
    }
}

} // namespace veg
