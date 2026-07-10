#include "veg/veg_renderer.h"

#include "veg/models.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace veg {

namespace {

constexpr float kCellSize = 512.0f; // m: unidad de culling

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
            switch (m) {
                case terrain::Material::Forest:
                    tree_p = 0.16f * canopy; tuft_p = 0.085f;
                    bush_p = 0.012f; log_p = 0.0030f;
                    break;
                case terrain::Material::Grass:
                    // Árboles dispersos de pradera + cola de la copa que se
                    // adentra en el claro (canopy>0 en la franja de transición).
                    tree_p = 0.011f + 0.15f * canopy;
                    tuft_p = 0.200f; bush_p = 0.010f;
                    break;
                case terrain::Material::Sand:
                    tuft_p = 0.008f; // hierba rala de duna
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
            const float veg_light = (0.45f + 0.55f * sampler.shadow_at(wx, wz))
                                  * (0.70f + 0.30f * sampler.sky_ao_at(wx, wz));
            const float tint = (0.85f + 0.30f * hash01(static_cast<std::uint32_t>(x),
                                                       static_cast<std::uint32_t>(z),
                                                       salt + 5)) * veg_light;

            Placement& cell = cell_of(wx, wz);
            if (roll < tree_p) {
                // Arriba dominan las coníferas; abajo se mezclan frondosos.
                const float conifer_bias =
                    std::clamp((wy - 120.0f) / 250.0f, 0.15f, 0.95f);
                const bool pine = hash01(static_cast<std::uint32_t>(x),
                                         static_cast<std::uint32_t>(z), salt + 6)
                                < conifer_bias;
                // Distribución de EDAD (feedback 2026-07-08 "distintos tamaños"):
                // curva cuadrática → muchos árboles jóvenes pequeños y pocos
                // maduros grandes (0.5–2.0×), como un bosque real, en vez del
                // rango estrecho y uniforme anterior.
                const float scale = 0.5f + 1.5f * scale_r * scale_r;
                // Base bien hundida: en pendiente los troncos no quedan al aire.
                render::Instance inst{{wx, wy - 0.35f * scale, wz, scale},
                                      {tint, tint, tint, yaw}};
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
                cell.bushes.push_back(render::Instance{{wx, wy - 0.08f, wz, scale},
                                                       {tint, tint, tint, yaw}});
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
                    const float scale = 0.8f + 0.6f * scale_r;
                    cell.logs.push_back(render::Instance{{wx, wy - 0.18f, wz, scale},
                                                         {tint, tint, tint, yaw}});
                }
            }
        }
    }

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
            Cell cell;
            cell.center = {(-cfg.size_m * 0.5f) + (static_cast<float>(cx) + 0.5f) * kCellSize,
                           (-cfg.size_m * 0.5f) + (static_cast<float>(cz) + 0.5f) * kCellSize};
            const auto add = [&](std::vector<render::InstancedMesh>& dst,
                                 const ModelData& model,
                                 const std::vector<render::Instance>& instances) {
                if (!instances.empty()) {
                    dst.emplace_back(model.vertices, model.indices, instances);
                    total_instances_ += instances.size();
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

} // namespace veg
