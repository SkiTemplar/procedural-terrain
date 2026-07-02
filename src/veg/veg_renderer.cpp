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

    // Muestreo por celda del heightfield: probabilidad según material.
    // Con celda de ~2.4 m: p=0.03 en bosque ≈ 1 árbol / 200 m² de bosque.
    const std::uint32_t salt = cfg.seed;
    for (int z = 2; z < res - 2; ++z) {
        for (int x = 2; x < res - 2; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(res)
                           + static_cast<size_t>(x);
            const auto m = data.material[i];
            const float roll = hash01(static_cast<std::uint32_t>(x),
                                      static_cast<std::uint32_t>(z), salt);

            float tree_p = 0.0f;
            float rock_p = 0.0f;
            switch (m) {
                case terrain::Material::Forest: tree_p = 0.030f; rock_p = 0.0006f; break;
                case terrain::Material::Grass: tree_p = 0.0012f; rock_p = 0.0010f; break;
                case terrain::Material::Rock: rock_p = 0.0045f; break;
                default: break;
            }
            if (roll >= tree_p + rock_p) {
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
            if (wy < cfg.sea_level + 0.5f) {
                continue; // nada dentro del agua
            }

            const float scale_r = hash01(static_cast<std::uint32_t>(x),
                                         static_cast<std::uint32_t>(z), salt + 3);
            const float yaw = hash01(static_cast<std::uint32_t>(x),
                                     static_cast<std::uint32_t>(z), salt + 4)
                            * 6.2831853f;
            const float tint = 0.85f + 0.30f * hash01(static_cast<std::uint32_t>(x),
                                                      static_cast<std::uint32_t>(z),
                                                      salt + 5);

            Placement& cell = cell_of(wx, wz);
            if (roll < tree_p) {
                // Arriba dominan las coníferas; abajo se mezclan frondosos.
                const float conifer_bias =
                    std::clamp((wy - 120.0f) / 250.0f, 0.15f, 0.95f);
                const bool pine = hash01(static_cast<std::uint32_t>(x),
                                         static_cast<std::uint32_t>(z), salt + 6)
                                < conifer_bias;
                const float scale = 0.75f + 0.7f * scale_r;
                render::Instance inst{{wx, wy - 0.15f, wz, scale},
                                      {tint, tint, tint, yaw}};
                (pine ? cell.pines : cell.broadleafs).push_back(inst);
            } else {
                const float scale = 0.4f + 1.8f * scale_r;
                cell.rocks.push_back(render::Instance{{wx, wy - 0.2f * scale, wz, scale},
                                                      {tint, tint, tint, yaw}});
            }
        }
    }

    // Un lote instanciado por (celda, tipo) con vértices propios: draw call
    // por lote, culling por distancia al centro de la celda.
    const ModelData pine = make_pine();
    const ModelData broadleaf = make_broadleaf();
    const ModelData rock = make_rock();

    for (int cz = 0; cz < cells_per_side; ++cz) {
        for (int cx = 0; cx < cells_per_side; ++cx) {
            Placement& p = placements[static_cast<size_t>(cz) * cells_per_side + cx];
            if (p.pines.empty() && p.broadleafs.empty() && p.rocks.empty()) {
                continue;
            }
            Cell cell;
            cell.center = {(-cfg.size_m * 0.5f) + (static_cast<float>(cx) + 0.5f) * kCellSize,
                           (-cfg.size_m * 0.5f) + (static_cast<float>(cz) + 0.5f) * kCellSize};
            if (!p.pines.empty()) {
                cell.batches.emplace_back(pine.vertices, pine.indices, p.pines);
                total_instances_ += p.pines.size();
            }
            if (!p.broadleafs.empty()) {
                cell.batches.emplace_back(broadleaf.vertices, broadleaf.indices,
                                          p.broadleafs);
                total_instances_ += p.broadleafs.size();
            }
            if (!p.rocks.empty()) {
                cell.batches.emplace_back(rock.vertices, rock.indices, p.rocks);
                total_instances_ += p.rocks.size();
            }
            cells_.push_back(std::move(cell));
        }
    }
}

void VegRenderer::draw(const glm::vec3& camera_pos, float max_distance) const {
    const glm::vec2 cam{camera_pos.x, camera_pos.z};
    // Margen de media diagonal de celda para no cortar lotes por el centro.
    const float cutoff = max_distance + kCellSize * 0.71f;

    for (const Cell& cell : cells_) {
        if (glm::length(cam - cell.center) > cutoff) {
            continue;
        }
        for (const auto& batch : cell.batches) {
            batch.draw();
        }
    }
}

} // namespace veg
