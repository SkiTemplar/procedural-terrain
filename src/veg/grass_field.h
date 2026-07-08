#pragma once

#include "render/instanced_mesh.h"
#include "terrain/detail.h"
#include "terrain/generator.h"
#include "veg/models.h"

#include <glm/glm.hpp>

#include <optional>

namespace veg {

// Césped denso relativo a la cámara (feedback 2026-07-08, "caminar por
// pastos"). Generar césped para toda la isla reventaría memoria (millones de
// matas), así que solo se puebla un disco alrededor del jugador y se
// reconstruye cuando la cámara se desplaza lo suficiente. Determinista por
// posición: el mismo punto siempre lleva las mismas briznas. Se dibuja con el
// shader de vegetación (kVegVert + kTerrainFrag), igual que árboles y matas.
class GrassField {
public:
    GrassField(const terrain::TerrainData& data,
               const terrain::DetailSampler& sampler);

    // Reconstruye el disco si la cámara se movió más del umbral. Barato: unas
    // ~10-15k briznas dentro del radio.
    void update(const glm::vec3& camera_pos);
    void draw() const;

    size_t instance_count() const {
        return mesh_ ? static_cast<size_t>(mesh_->instance_count()) : 0;
    }

private:
    const terrain::TerrainData* data_;
    const terrain::DetailSampler* sampler_;
    ModelData model_;
    std::optional<render::InstancedMesh> mesh_;
    glm::vec2 last_center_{1e9f, 1e9f};

    void rebuild(const glm::vec3& camera_pos);
};

} // namespace veg
