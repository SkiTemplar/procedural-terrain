#pragma once

#include "render/mesh.h"
#include "terrain/detail.h"
#include "terrain/generator.h"

#include <memory>
#include <vector>

namespace terrain {

// Terreno por chunks con LOD por distancia (heightmap 4096, celda ~2.4 m,
// isla de 10×10 km):
//   nivel 0: subdivisión ×8 con ruido de detalle (celda ~0.3 m) — bajo los pies
//   nivel 1: subdivisión ×4 con ruido de detalle (celda ~0.6 m) — cerca
//   nivel 2: resolución del heightmap (celda ~2.4 m)            — media
//   nivel 3: stride 4 (celda ~9.8 m)                            — lejos
//   nivel 4: stride 8 (celda ~19.5 m)                           — horizonte
// Cada chunk lleva un faldón (skirt) que oculta las grietas entre niveles.
// Las reconstrucciones se reparten entre frames y la generación de vértices
// va paralelizada (sin tirones).
class ChunkedTerrain {
public:
    ChunkedTerrain(const TerrainData& data, const DetailSampler& sampler);

    // Ajusta los LOD deseados a la posición de la cámara, reconstruye como
    // mucho `max_rebuilds` chunks (los más cercanos primero) y recorta
    // `visible_` al frustum (view_proj): sin culling se emitían los 4096
    // chunks en draw calls cada frame, mirases donde mirases.
    void update(const glm::vec3& camera_pos, const glm::mat4& view_proj,
                int max_rebuilds = 2);

    const std::vector<const render::Mesh*>& visible_meshes() const { return visible_; }
    int total_chunks() const { return static_cast<int>(chunks_.size()); }

    static constexpr float kUltraDistance = 250.0f;  // m → nivel 0
    static constexpr float kFineDistance = 600.0f;   // m → nivel 1
    static constexpr float kMediumDistance = 1800.0f; // m → nivel 2
    static constexpr float kFarDistance = 4200.0f;    // m → nivel 3

private:
    struct Chunk {
        int x0 = 0; // celda inicial en el heightfield
        int z0 = 0;
        glm::vec2 center{0.0f};
        // AABB en mundo para el frustum culling (y con margen de faldón
        // y ruido de detalle).
        glm::vec3 aabb_min{0.0f};
        glm::vec3 aabb_max{0.0f};
        int level = -1; // -1 = sin malla
        std::unique_ptr<render::Mesh> mesh;
    };

    const TerrainData* data_;
    const DetailSampler* sampler_;
    int chunk_cells_ = 64;
    int chunks_per_side_ = 0;
    std::vector<Chunk> chunks_;
    std::vector<const render::Mesh*> visible_;

    int desired_level(const Chunk& chunk, const glm::vec3& camera_pos) const;
    void rebuild(Chunk& chunk, int level);
};

} // namespace terrain
