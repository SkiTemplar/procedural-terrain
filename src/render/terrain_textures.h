#pragma once

#include "render/terrain_texture_data.h"

#include <glad/glad.h>
#include <glm/vec3.hpp>

#include <array>

namespace render {
class Shader;
}
namespace terrain {
struct TerrainData;
}

namespace render {

class TerrainTextures {
public:
    explicit TerrainTextures(const terrain::TerrainData& data);
    ~TerrainTextures();

    TerrainTextures(const TerrainTextures&) = delete;
    TerrainTextures& operator=(const TerrainTextures&) = delete;

    bool available() const { return available_; }
    void bind(const Shader& shader, bool enabled, float intensity) const;

private:
    GLuint albedo_ = 0;
    GLuint normal_ = 0;
    GLuint material_grid_ = 0;
    bool available_ = false;
    float domain_half_ = 1.0f;
    std::array<float, terrain::kMaterialCount> sizes_{};
    std::array<glm::vec3, terrain::kMaterialCount> means_{};

    void release();
};

} // namespace render
