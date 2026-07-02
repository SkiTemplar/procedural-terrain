#pragma once

#include "render/mesh.h"
#include "terrain/detail.h"
#include "terrain/generator.h"

#include <memory>
#include <vector>

namespace terrain {

// Terreno por chunks con LOD por distancia (heightmap 2048, celda ~2.4 m):
//   nivel 0: subdivisión ×4 con ruido de detalle (celda ~0.6 m) — cerca
//   nivel 1: resolución del heightmap (celda ~2.4 m)            — media
//   nivel 2: stride 4 (celda ~9.8 m)                            — lejos
// Cada chunk lleva un faldón (skirt) que oculta las grietas entre niveles.
// Las reconstrucciones se reparten entre frames (sin tirones).
class ChunkedTerrain {
public:
    ChunkedTerrain(const TerrainData& data, const DetailSampler& sampler);

    // Ajusta los LOD deseados a la posición de la cámara y reconstruye
    // como mucho `max_rebuilds` chunks (los más cercanos primero).
    void update(const glm::vec3& camera_pos, int max_rebuilds = 3);

    const std::vector<const render::Mesh*>& visible_meshes() const { return visible_; }

    static constexpr float kFineDistance = 550.0f;   // m → nivel 0
    static constexpr float kMediumDistance = 2200.0f; // m → nivel 1

private:
    struct Chunk {
        int x0 = 0; // celda inicial en el heightfield
        int z0 = 0;
        glm::vec2 center{0.0f};
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
