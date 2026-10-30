#pragma once

#include "render/frustum.h"
#include "render/mesh.h"
#include "render/terrain_textures.h"
#include "terrain/detail.h"
#include "terrain/generator.h"
#include "terrain/palette.h"

#include <array>
#include <memory>
#include <vector>

namespace terrain {

// Terreno por chunks con LOD por distancia (heightmap 4096, celda ~2.4 m,
// isla de 10×10 km): facetas de ~1.2 m bajo los pies (celda subdividida con
// el relieve fino del DetailSampler) que doblan de tamaño con cada nivel hasta
// ~78 m en el horizonte (tabla en chunked_terrain.cpp).
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
    // Añade a `out` las mallas cuyo AABB corta `frustum` (con el LOD que fijó
    // el último update). La usan las cascadas de sombra: un oclusor fuera de
    // la vista pero dentro del volumen del sol también proyecta.
    void collect_meshes(const render::Frustum& frustum,
                        std::vector<const render::Mesh*>& out) const;
    // AABB de todo el terreno (unión de los AABB de chunk).
    const glm::vec3& bounds_min() const { return bounds_min_; }
    const glm::vec3& bounds_max() const { return bounds_max_; }
    int total_chunks() const { return static_cast<int>(chunks_.size()); }
    void bind_detail_textures(const render::Shader& shader, bool enabled,
                              float intensity);

private:
    struct Chunk {
        int x0 = 0; // celda inicial en el heightfield
        int z0 = 0;
        glm::vec2 center{0.0f};
        // AABB en mundo para el frustum culling (y con margen de faldón
        // y ruido de detalle).
        glm::vec3 aabb_min{0.0f};
        glm::vec3 aabb_max{0.0f};
        bool coastal = false; // corta el nivel del mar: LOD acotado (ver .cpp)
        int level = -1; // -1 = sin malla
        // Niveles de los vecinos z0, z1, x0, x1 con que se cosió la malla.
        std::array<int, 4> edge_levels{-1, -1, -1, -1};
        std::unique_ptr<render::Mesh> mesh;
    };

    const TerrainData* data_;
    const DetailSampler* sampler_;
    Palette palette_;
    render::TerrainTextures detail_textures_;
    int chunk_cells_ = 64;
    int chunks_per_side_ = 0;
    std::vector<Chunk> chunks_;
    std::vector<const render::Mesh*> visible_;
    glm::vec3 bounds_min_{1e9f};
    glm::vec3 bounds_max_{-1e9f};

    int desired_level(const Chunk& chunk, const glm::vec3& camera_pos) const;
    std::array<int, 4> neighbour_levels(const Chunk& chunk,
                                        const glm::vec3& camera_pos) const;
    void rebuild(Chunk& chunk, int level, const std::array<int, 4>& edges);
};

} // namespace terrain
