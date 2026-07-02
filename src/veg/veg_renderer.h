#pragma once

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

    size_t total_instances() const { return total_instances_; }

private:
    struct Cell {
        glm::vec2 center{0.0f};
        std::vector<render::InstancedMesh> batches_far;  // árboles y rocas
        std::vector<render::InstancedMesh> batches_near; // matas y arbustos
    };

    std::vector<Cell> cells_;
    size_t total_instances_ = 0;
};

} // namespace veg
