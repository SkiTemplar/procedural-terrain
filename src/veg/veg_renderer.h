#pragma once

#include "render/frustum.h"
#include "render/instanced_mesh.h"
#include "terrain/detail.h"
#include "terrain/generator.h"

#include <glm/glm.hpp>

#include <vector>

namespace veg {

// Puebla la isla con pinos, frondosos y rocas (instanciados) y los dibuja
// con culling por celdas de 512 m. La colocación es determinista por seed y
// usa el DetailSampler: cada árbol pisa exactamente el suelo visible.
class VegRenderer {
public:
    VegRenderer(const terrain::TerrainData& data, const terrain::DetailSampler& sampler);

    // El shader (kVegVert + kTerrainFrag) debe estar activo con sus uniforms.
    void draw(const glm::vec3& camera_pos, float max_distance) const;

    // Pase de sombra: las celdas que draw() pintaría y cuyo AABB corta el
    // volumen ortográfico de una cascada del sol (kShadowVegVert activo).
    void draw_casters(const render::Frustum& light, const glm::vec3& camera_pos,
                      float max_distance) const;

    size_t total_instances() const { return total_instances_; }

    // Censo por tipo (auditoría Fase B): verificable desde el panel ImGui sin
    // volar el mapa contando árboles. Las rocas están a 0 POR DECISIÓN
    // (feedback 2026-07-08 "quitar los peñascos") — el contador lo hace visible.
    struct TypeCounts {
        size_t pines = 0;
        size_t broadleafs = 0;
        size_t rocks = 0;
        size_t tufts = 0;
        size_t bushes = 0;
        size_t logs = 0;
    };
    const TypeCounts& counts() const { return counts_; }

private:
    struct Cell {
        glm::vec2 center{0.0f};
        glm::vec3 aabb_min{1e9f}; // envolvente de todas las instancias
        glm::vec3 aabb_max{-1e9f};
        std::vector<render::InstancedMesh> batches_far;  // árboles y rocas
        std::vector<render::InstancedMesh> batches_near; // matas y arbustos
    };

    std::vector<Cell> cells_;
    size_t total_instances_ = 0;
    TypeCounts counts_;
};

} // namespace veg
