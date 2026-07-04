#include "terrain/chunked_terrain.h"

#include "terrain/mesher.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <execution>
#include <numeric>

namespace terrain {

namespace {

constexpr float kSkirtDepth = 5.0f; // m hacia abajo para tapar grietas de LOD

// Estratos sutiles en la roca: bandas horizontales de tono (~18 m de período)
// que insinúan capas geológicas en paredes y cañones.
float rock_banding(float y) {
    return 0.93f + 0.07f * std::sin(y * 0.35f);
}

// Color del suelo con gradiente de bosque, clima y altitud: la pradera funde
// hacia bosque por densidad, se seca con la altura, empardece en el sector
// frío; la arena es dorada en frío y clara caribeña en cálido.
glm::vec3 ground_color(Material m, float forest_density, float climate, float y) {
    if (m == Material::Grass || m == Material::Forest) {
        glm::vec3 c = glm::mix(material_color(Material::Grass),
                               material_color(Material::Forest), forest_density);
        const float dryness = glm::clamp((y - 200.0f) / 400.0f, 0.0f, 1.0f) * 0.30f
                            + (1.0f - climate) * 0.18f;
        return glm::mix(c, glm::vec3{0.58f, 0.55f, 0.33f}, dryness);
    }
    if (m == Material::Sand) {
        // Arena común (dorada) por defecto; la blanca caribeña solo en el
        // extremo cálido del clima.
        return glm::mix(glm::vec3{0.84f, 0.76f, 0.55f},
                        glm::vec3{0.94f, 0.90f, 0.74f},
                        climate * climate);
    }
    return material_color(m);
}

// Malla de rejilla + faldón perimetral. get_vertex(i, j) devuelve el vértice
// (posición+color) del nodo (i, j) de una rejilla de (w+1)×(h+1) nodos.
// La generación de vértices va en paralelo (get_vertex debe ser thread-safe);
// las llamadas GL se quedan en el hilo principal.
template <typename GetVertex>
render::Mesh build_grid_mesh(int w, int h, GetVertex&& get_vertex) {
    std::vector<render::Vertex> vertices(
        static_cast<size_t>(w + 1) * (h + 1));

    std::vector<int> rows(static_cast<size_t>(h + 1));
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int j) {
        for (int i = 0; i <= w; ++i) {
            vertices[static_cast<size_t>(j) * (w + 1) + i] = get_vertex(i, j);
        }
    });
    // 2·(w+h) aristas de perímetro × 2 vértices nuevos por arista.
    vertices.reserve(vertices.size() + 4 * static_cast<size_t>(w + h));

    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<size_t>(w) * h * 6 + 2 * static_cast<size_t>(w + h) * 6);
    const auto node = [w](int i, int j) {
        return static_cast<std::uint32_t>(j * (w + 1) + i);
    };
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const auto tl = node(i, j);
            const auto tr = node(i + 1, j);
            const auto bl = node(i, j + 1);
            const auto br = node(i + 1, j + 1);
            indices.insert(indices.end(), {tl, bl, tr, tr, bl, br});
        }
    }

    // Faldón: por cada arista del perímetro, un quad hacia abajo.
    const auto add_skirt_edge = [&](std::uint32_t a, std::uint32_t b) {
        const auto base = static_cast<std::uint32_t>(vertices.size());
        render::Vertex va = vertices[a];
        render::Vertex vb = vertices[b];
        va.position.y -= kSkirtDepth;
        vb.position.y -= kSkirtDepth;
        vertices.push_back(va);
        vertices.push_back(vb);
        indices.insert(indices.end(), {a, base, b, b, base, base + 1});
    };
    for (int i = 0; i < w; ++i) {
        add_skirt_edge(node(i, 0), node(i + 1, 0));
        add_skirt_edge(node(i + 1, h), node(i, h));
    }
    for (int j = 0; j < h; ++j) {
        add_skirt_edge(node(0, j + 1), node(0, j));
        add_skirt_edge(node(w, j), node(w, j + 1));
    }

    return render::Mesh{vertices, indices};
}

} // namespace

ChunkedTerrain::ChunkedTerrain(const TerrainData& data, const DetailSampler& sampler)
    : data_(&data), sampler_(&sampler) {
    const int cells = data.height.resolution() - 1;
    chunks_per_side_ = (cells + chunk_cells_ - 1) / chunk_cells_;
    chunks_.resize(static_cast<size_t>(chunks_per_side_) * chunks_per_side_);

    for (int cz = 0; cz < chunks_per_side_; ++cz) {
        for (int cx = 0; cx < chunks_per_side_; ++cx) {
            Chunk& chunk = chunks_[static_cast<size_t>(cz) * chunks_per_side_ + cx];
            chunk.x0 = cx * chunk_cells_;
            chunk.z0 = cz * chunk_cells_;
            const auto& hf = data_->height;
            const float x_mid = hf.world_x(std::min(chunk.x0 + chunk_cells_ / 2, hf.resolution() - 1));
            const float z_mid = hf.world_z(std::min(chunk.z0 + chunk_cells_ / 2, hf.resolution() - 1));
            chunk.center = {x_mid, z_mid};
        }
    }
    // Arranque: todo en el nivel más basto (rápido); update() irá refinando.
    for (auto& chunk : chunks_) {
        rebuild(chunk, 4);
    }
}

int ChunkedTerrain::desired_level(const Chunk& chunk, const glm::vec3& camera_pos) const {
    const float dist = glm::length(glm::vec2{camera_pos.x, camera_pos.z} - chunk.center);
    if (dist < kUltraDistance) {
        return 0;
    }
    if (dist < kFineDistance) {
        return 1;
    }
    if (dist < kMediumDistance) {
        return 2;
    }
    if (dist < kFarDistance) {
        return 3;
    }
    return 4;
}

void ChunkedTerrain::update(const glm::vec3& camera_pos, int max_rebuilds) {
    // Reconstruir primero lo más cercano que esté desactualizado.
    std::vector<std::pair<float, Chunk*>> stale;
    for (auto& chunk : chunks_) {
        if (chunk.level != desired_level(chunk, camera_pos)) {
            const float dist =
                glm::length(glm::vec2{camera_pos.x, camera_pos.z} - chunk.center);
            stale.emplace_back(dist, &chunk);
        }
    }
    std::sort(stale.begin(), stale.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    const int n = std::min(static_cast<int>(stale.size()), max_rebuilds);
    for (int i = 0; i < n; ++i) {
        rebuild(*stale[static_cast<size_t>(i)].second,
                desired_level(*stale[static_cast<size_t>(i)].second, camera_pos));
    }

    visible_.clear();
    visible_.reserve(chunks_.size());
    for (const auto& chunk : chunks_) {
        if (chunk.mesh) {
            visible_.push_back(chunk.mesh.get());
        }
    }
}

void ChunkedTerrain::rebuild(Chunk& chunk, int level) {
    const auto& hf = data_->height;
    const int res = hf.resolution();
    const int x1 = std::min(chunk.x0 + chunk_cells_, res - 1);
    const int z1 = std::min(chunk.z0 + chunk_cells_, res - 1);
    const int cells_x = x1 - chunk.x0;
    const int cells_z = z1 - chunk.z0;

    const auto material_at = [&](int sx, int sz) {
        return data_->material[static_cast<size_t>(sz) * static_cast<size_t>(res)
                               + static_cast<size_t>(sx)];
    };

    render::Mesh mesh = [&] {
        if (level <= 1) {
            // Subdivisión ×8 (nivel 0) o ×4 (nivel 1) con la altura del
            // DetailSampler: exactamente la superficie que pisa el jugador.
            const int sub = level == 0 ? 8 : 4;
            const int w = cells_x * sub;
            const int h = cells_z * sub;
            const float step = hf.cell_size() / static_cast<float>(sub);
            const float wx0 = hf.world_x(chunk.x0);
            const float wz0 = hf.world_z(chunk.z0);
            return build_grid_mesh(w, h, [&](int i, int j) {
                const float wx = wx0 + static_cast<float>(i) * step;
                const float wz = wz0 + static_cast<float>(j) * step;
                const Material m = sampler_->material_near(wx, wz);
                const float wy = sampler_->height(wx, wz);
                float tint =
                    0.94f + 0.12f * hash01(chunk.x0 * sub + i, chunk.z0 * sub + j);
                if (m == Material::Rock) {
                    tint *= rock_banding(wy);
                }
                const glm::vec3 color =
                    ground_color(m, sampler_->forest_density_near(wx, wz),
                                 sampler_->climate_near(wx, wz), wy);
                return render::Vertex{{wx, wy, wz}, color * tint,
                                      sampler_->normal_near(wx, wz)};
            });
        }
        const int stride = level == 2 ? 1 : (level == 3 ? 4 : 8);
        const int w = cells_x / stride;
        const int h = cells_z / stride;
        return build_grid_mesh(w, h, [&](int i, int j) {
            const int sx = std::min(chunk.x0 + i * stride, res - 1);
            const int sz = std::min(chunk.z0 + j * stride, res - 1);
            const Material m = material_at(sx, sz);
            const float wy = hf.at(sx, sz);
            float tint = 0.94f + 0.12f * hash01(sx, sz);
            if (m == Material::Rock) {
                tint *= rock_banding(wy);
            }
            const size_t si = static_cast<size_t>(sz) * static_cast<size_t>(res)
                            + static_cast<size_t>(sx);
            return render::Vertex{
                {hf.world_x(sx), wy, hf.world_z(sz)},
                ground_color(m, data_->forest_density[si], data_->climate[si], wy)
                    * tint,
                hf.normal_at(sx, sz)};
        });
    }();

    chunk.mesh = std::make_unique<render::Mesh>(std::move(mesh));
    chunk.level = level;
}

} // namespace terrain
