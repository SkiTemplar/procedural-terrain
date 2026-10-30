#include "terrain/chunked_terrain.h"

#include "terrain/faceted_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <execution>
#include <numeric>

namespace terrain {

namespace {

// Faceta por nivel de LOD como (subdivisión de celda, paso en subceldas):
// ~1.2 m bajo los pies (la celda de 2.44 m partida en dos, con el relieve fino
// del DetailSampler), y 2.4/4.9/9.8/19.5/39/78 m al alejarse. Con las
// distancias de cambio de nivel una faceta ocupa en pantalla un tamaño
// parecido (~9-12 px a 1280) en toda la vista: el estilo low-poly se lee igual
// cerca y lejos, y de cerca el suelo deja de ser un puñado de caras enormes.
struct FacetLevel {
    int sub;
    int stride;
};
constexpr int kLevels = 7;
constexpr FacetLevel kFacetLevel[kLevels] = {{2, 1}, {1, 1}, {1, 2}, {1, 4},
                                             {1, 8}, {1, 16}, {1, 32}};
constexpr float kLevelDistance[kLevels - 1] = {110.0f, 250.0f, 600.0f,
                                               1000.0f, 1800.0f, 4200.0f};
// Nivel más basto de un chunk que corta el nivel del mar (~9.8 m): con
// facetas de 20-40 m una flecha o barra más estrecha que la faceta dejaba
// solo nodos sueltos sobre el agua y se dibujaba como una fila de "tiendas".
// Son pocos chunks (el perímetro de la isla) y la cara sigue siendo grande.
constexpr int kCoastalMaxLevel = 3;
// Márgenes del AABB: faldón de la malla facetada (5 m) y jitter XZ máximo de
// sus nodos (0.25 × 32 celdas de 2.44 m en el nivel más basto).
constexpr float kAabbSkirtM = 5.0f;
constexpr float kAabbJitterM = 20.0f;

} // namespace

ChunkedTerrain::ChunkedTerrain(const TerrainData& data, const DetailSampler& sampler)
    : data_(&data), sampler_(&sampler), palette_(data), detail_textures_(data) {
    const int cells = data.height.resolution() - 1;
    chunks_per_side_ = (cells + chunk_cells_ - 1) / chunk_cells_;
    chunks_.resize(static_cast<size_t>(chunks_per_side_) * chunks_per_side_);

    for (int cz = 0; cz < chunks_per_side_; ++cz) {
        for (int cx = 0; cx < chunks_per_side_; ++cx) {
            Chunk& chunk = chunks_[static_cast<size_t>(cz) * chunks_per_side_ + cx];
            chunk.x0 = cx * chunk_cells_;
            chunk.z0 = cz * chunk_cells_;
            const auto& hf = data_->height;
            const int res = hf.resolution();
            const float x_mid = hf.world_x(std::min(chunk.x0 + chunk_cells_ / 2, res - 1));
            const float z_mid = hf.world_z(std::min(chunk.z0 + chunk_cells_ / 2, res - 1));
            chunk.center = {x_mid, z_mid};

            // AABB: extremos de altura del chunk + margen (faldón hacia
            // abajo, ruido de detalle hacia arriba).
            const int x1 = std::min(chunk.x0 + chunk_cells_, res - 1);
            const int z1 = std::min(chunk.z0 + chunk_cells_, res - 1);
            float y_min = 1e9f;
            float y_max = -1e9f;
            for (int z = chunk.z0; z <= z1; ++z) {
                for (int x = chunk.x0; x <= x1; ++x) {
                    const float h = hf.at(x, z);
                    y_min = std::min(y_min, h);
                    y_max = std::max(y_max, h);
                }
            }
            // AABB: extremos de altura del chunk + margen (faldón hacia
            // abajo, ruido de detalle hacia arriba) y, en XZ, el jitter
            // máximo de los nodos del nivel más basto.
            const float jitter = kAabbJitterM;
            chunk.aabb_min = {hf.world_x(chunk.x0) - jitter,
                              y_min - kAabbSkirtM - 2.0f,
                              hf.world_z(chunk.z0) - jitter};
            chunk.aabb_max = {hf.world_x(x1) + jitter, y_max + 3.0f,
                              hf.world_z(z1) + jitter};
            const float sea = data.config.sea_level;
            chunk.coastal = y_min < sea + 0.5f && y_max > sea + 0.5f;
            bounds_min_ = glm::min(bounds_min_, chunk.aabb_min);
            bounds_max_ = glm::max(bounds_max_, chunk.aabb_max);
        }
    }
    // Arranque: todo en el nivel más basto (rápido); update() irá refinando.
    for (auto& chunk : chunks_) {
        rebuild(chunk, kLevels - 1, {kLevels - 1, kLevels - 1, kLevels - 1, kLevels - 1});
    }
}

void ChunkedTerrain::bind_detail_textures(const render::Shader& shader,
                                          bool enabled,
                                          float intensity) {
    detail_textures_.bind(shader, enabled, intensity);
}

int ChunkedTerrain::desired_level(const Chunk& chunk, const glm::vec3& camera_pos) const {
    const float dist = glm::length(glm::vec2{camera_pos.x, camera_pos.z} - chunk.center);
    const int cap = chunk.coastal ? kCoastalMaxLevel : kLevels - 1;
    for (int level = 0; level < cap; ++level) {
        if (dist < kLevelDistance[level]) {
            return level;
        }
    }
    return cap;
}

std::array<int, 4> ChunkedTerrain::neighbour_levels(const Chunk& chunk,
                                                    const glm::vec3& camera_pos) const {
    const int cx = chunk.x0 / chunk_cells_;
    const int cz = chunk.z0 / chunk_cells_;
    constexpr int kOffsets[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}}; // z0, z1, x0, x1
    std::array<int, 4> out{};
    for (size_t e = 0; e < 4; ++e) {
        const int nx = cx + kOffsets[e][0];
        const int nz = cz + kOffsets[e][1];
        const bool inside = nx >= 0 && nz >= 0 && nx < chunks_per_side_ && nz < chunks_per_side_;
        out[e] = inside ? desired_level(chunks_[static_cast<size_t>(nz) * chunks_per_side_ + nx],
                                        camera_pos)
                        : -1;
    }
    return out;
}

void ChunkedTerrain::update(const glm::vec3& camera_pos, const glm::mat4& view_proj,
                            int max_rebuilds) {
    // Reconstruir primero lo más cercano que esté desactualizado.
    std::vector<std::pair<float, Chunk*>> stale;
    for (auto& chunk : chunks_) {
        if (chunk.level != desired_level(chunk, camera_pos)
            || chunk.edge_levels != neighbour_levels(chunk, camera_pos)) {
            const float dist =
                glm::length(glm::vec2{camera_pos.x, camera_pos.z} - chunk.center);
            stale.emplace_back(dist, &chunk);
        }
    }
    std::sort(stale.begin(), stale.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    const int n = std::min(static_cast<int>(stale.size()), max_rebuilds);
    for (int i = 0; i < n; ++i) {
        Chunk& chunk = *stale[static_cast<size_t>(i)].second;
        rebuild(chunk, desired_level(chunk, camera_pos), neighbour_levels(chunk, camera_pos));
    }

    // Frustum culling (Gribb-Hartmann, render::Frustum): sin esto se
    // dibujaban los 4096 chunks por frame, mirases donde mirases.
    visible_.clear();
    collect_meshes(render::Frustum(view_proj), visible_);
}

void ChunkedTerrain::collect_meshes(const render::Frustum& frustum,
                                    std::vector<const render::Mesh*>& out) const {
    out.reserve(out.size() + chunks_.size());
    for (const auto& chunk : chunks_) {
        if (chunk.mesh && frustum.intersects(chunk.aabb_min, chunk.aabb_max)) {
            out.push_back(chunk.mesh.get());
        }
    }
}

void ChunkedTerrain::rebuild(Chunk& chunk, int level, const std::array<int, 4>& edges) {
    const auto& hf = data_->height;
    const int res = hf.resolution();
    const int x1 = std::min(chunk.x0 + chunk_cells_, res - 1);
    const int z1 = std::min(chunk.z0 + chunk_cells_, res - 1);
    const int cells_x = x1 - chunk.x0;
    const int cells_z = z1 - chunk.z0;

    const FacetLevel& lv = kFacetLevel[std::clamp(level, 0, kLevels - 1)];
    FacetRegion region{chunk.x0, chunk.z0, cells_x, cells_z, lv.stride, lv.sub};
    for (size_t e = 0; e < 4; ++e) {
        if (edges[e] > level) {
            const FacetLevel& nb = kFacetLevel[std::clamp(edges[e], 0, kLevels - 1)];
            region.edge_cells[e] = static_cast<float>(nb.stride) / static_cast<float>(nb.sub);
        }
    }
    render::Mesh mesh = build_faceted_mesh(*data_, *sampler_, palette_, region);

    chunk.mesh = std::make_unique<render::Mesh>(std::move(mesh));
    chunk.level = level;
    chunk.edge_levels = edges;
}

} // namespace terrain
