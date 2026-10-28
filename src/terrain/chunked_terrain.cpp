#include "terrain/chunked_terrain.h"

#include "terrain/mesher.h"
#include "render/terrain_shader_constants.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <execution>
#include <numeric>

namespace terrain {

namespace {

constexpr float kSkirtDepth = 5.0f; // m hacia abajo para tapar grietas de LOD

// Jitter determinista de vértice (HP-6 fase 1, feedback 2026-07-08): desplaza
// cada nodo de la rejilla en XZ una fracción del paso, en función SOLO de su
// índice global de nodo — chunks vecinos del mismo nivel comparten nodos de
// borde y por tanto el mismo jitter (sin grietas; entre niveles ya hay
// faldón). Rompe la retícula de cuadrados alineados sin abandonar el low-poly.
glm::vec2 vertex_jitter(int gx, int gz, float step) {
    const float k = 0.30f * step;
    return {(hash01(gx * 3 + 7331, gz * 5 - 911) - 0.5f) * 2.0f * k,
            (hash01(gx * 7 - 4177, gz * 11 + 263) - 0.5f) * 2.0f * k};
}

// Domain warp del muestreo de material (feedback 2026-07-08 "colores en
// cuadrado"): desplaza la posición donde se lee el material unos metros según
// un ruido suave → los bordes hierba↔roca↔arena SERPENTEAN en vez de seguir la
// rejilla de celdas, sin mezclar colores (cada material queda nítido). Solo
// afecta al COLOR del render; la altura, la física y la vegetación usan la
// posición real.
// Las constantes viven en render/terrain_shader_constants.h: el fragment del
// terreno replica este warp (mismo ruido) para que la textura de detalle se
// alinee con el color de paleta.
glm::vec2 material_warp(float wx, float wz) {
    const float k = PT_MATERIAL_WARP_AMP; // amplitud del serpenteo (m)
    const float f = PT_MATERIAL_WARP_FREQ;
    const float nx = value_noise(wx * f + PT_MATERIAL_WARP_X_OFFSET_X,
                                 wz * f + PT_MATERIAL_WARP_X_OFFSET_Z) - 0.5f;
    const float nz = value_noise(wx * f + PT_MATERIAL_WARP_Z_OFFSET_X,
                                 wz * f + PT_MATERIAL_WARP_Z_OFFSET_Z) - 0.5f;
    return {nx * 2.0f * k, nz * 2.0f * k};
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
            // Diagonal alternada por paridad: con la misma diagonal en todos
            // los quads el suelo se lee como retícula (HP-6 "cuadraditos").
            if (((i + j) & 1) == 0) {
                indices.insert(indices.end(), {tl, bl, tr, tr, bl, br});
            } else {
                indices.insert(indices.end(), {tl, bl, br, tl, br, tr});
            }
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
            // Margen XZ de 1 m: el jitter de vértice puede sacar un nodo de
            // borde fuera de la caja exacta del chunk.
            chunk.aabb_min = {hf.world_x(chunk.x0) - 1.0f,
                              y_min - kSkirtDepth - 2.0f,
                              hf.world_z(chunk.z0) - 1.0f};
            chunk.aabb_max = {hf.world_x(x1) + 1.0f, y_max + 3.0f,
                              hf.world_z(z1) + 1.0f};
        }
    }
    // Arranque: todo en el nivel más basto (rápido); update() irá refinando.
    for (auto& chunk : chunks_) {
        rebuild(chunk, 4);
    }
}

void ChunkedTerrain::bind_detail_textures(const render::Shader& shader,
                                          bool enabled,
                                          float intensity) {
    detail_textures_.bind(shader, enabled, intensity);
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

void ChunkedTerrain::update(const glm::vec3& camera_pos, const glm::mat4& view_proj,
                            int max_rebuilds) {
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

    // Frustum culling (Gribb-Hartmann): 6 planos de view_proj contra el AABB
    // de cada chunk con el test del vértice positivo. Sin esto se dibujaban
    // los 4096 chunks por frame, mirases donde mirases.
    glm::vec4 planes[6];
    const glm::mat4 m = glm::transpose(view_proj); // filas de view_proj
    planes[0] = m[3] + m[0]; // izquierda
    planes[1] = m[3] - m[0]; // derecha
    planes[2] = m[3] + m[1]; // abajo
    planes[3] = m[3] - m[1]; // arriba
    planes[4] = m[3] + m[2]; // cerca
    planes[5] = m[3] - m[2]; // lejos

    visible_.clear();
    visible_.reserve(chunks_.size());
    for (const auto& chunk : chunks_) {
        if (!chunk.mesh) {
            continue;
        }
        bool inside = true;
        for (const glm::vec4& p : planes) {
            const glm::vec3 positive{
                p.x >= 0.0f ? chunk.aabb_max.x : chunk.aabb_min.x,
                p.y >= 0.0f ? chunk.aabb_max.y : chunk.aabb_min.y,
                p.z >= 0.0f ? chunk.aabb_max.z : chunk.aabb_min.z};
            if (glm::dot(glm::vec3{p}, positive) + p.w < 0.0f) {
                inside = false;
                break;
            }
        }
        if (inside) {
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
                const glm::vec2 jit =
                    vertex_jitter(chunk.x0 * sub + i, chunk.z0 * sub + j, step);
                const float wx = wx0 + static_cast<float>(i) * step + jit.x;
                const float wz = wz0 + static_cast<float>(j) * step + jit.y;
                const float wy = sampler_->height(wx, wz);
                // El COLOR se muestrea en una posición warpeada (bordes que
                // serpentean, no cuadros); la altura/normal usan la real.
                const glm::vec2 mw = material_warp(wx, wz);
                const float cx = wx + mw.x;
                const float cz = wz + mw.y;
                const Material m = sampler_->material_near(cx, cz);
                const glm::vec3 color = palette_.ground(
                    m, sampler_->forest_density_near(cx, cz),
                    sampler_->climate_near(cx, cz), wy, wx, wz, cx, cz);
                return render::Vertex{{wx, wy, wz}, color,
                                      sampler_->normal_near(wx, wz),
                                      {sampler_->shadow_at(wx, wz),
                                       sampler_->sky_ao_at(wx, wz)}};
            });
        }
        const int stride = level == 2 ? 1 : (level == 3 ? 4 : 8);
        const int w = cells_x / stride;
        const int h = cells_z / stride;
        return build_grid_mesh(w, h, [&](int i, int j) {
            const int sx = std::min(chunk.x0 + i * stride, res - 1);
            const int sz = std::min(chunk.z0 + j * stride, res - 1);
            Material m = material_at(sx, sz);
            float wx = hf.world_x(sx);
            float wz = hf.world_z(sz);
            float wy = hf.at(sx, sz);
            float cx = wx;
            float cz = wz;
            if (stride == 1) {
                // Nivel medio (celda nativa): mismo jitter anti-retícula que
                // los niveles finos. Los lejanos (stride 4/8) se quedan en
                // rejilla: allí manda el blend a suave del shader.
                const glm::vec2 jit = vertex_jitter(sx, sz, hf.cell_size());
                wx += jit.x;
                wz += jit.y;
                wy = hf.sample(wx, wz);
                // Domain warp del color también en media distancia (el borde de
                // material serpentea, no solo en el LOD cercano).
                const glm::vec2 mw = material_warp(wx, wz);
                cx = wx + mw.x;
                cz = wz + mw.y;
                m = sampler_->material_near(cx, cz);
            }
            return render::Vertex{
                {wx, wy, wz},
                palette_.ground(m, sampler_->forest_density_near(cx, cz),
                                sampler_->climate_near(cx, cz), wy, wx, wz,
                                cx, cz),
                hf.normal_at(sx, sz),
                {sampler_->shadow_at(wx, wz), sampler_->sky_ao_at(wx, wz)}};
        });
    }();

    chunk.mesh = std::make_unique<render::Mesh>(std::move(mesh));
    chunk.level = level;
}

} // namespace terrain
