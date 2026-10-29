#pragma once

#include "render/terrain_texture_data.h"

#include <glad/glad.h>
#include <glm/vec3.hpp>

#include <array>
#include <memory>

namespace render {
class Shader;
}
namespace terrain {
struct TerrainData;
}

namespace render {

// Arrays GL de albedo (sRGB) y normal de las 10 capas. No dependen del mundo:
// se comparten entre regeneraciones mientras los ficheros no cambien (ver
// acquire en terrain_textures.cpp).
struct DetailTextureArrays {
    GLuint albedo = 0;
    GLuint normal = 0;
    std::array<float, terrain::kMaterialCount> sizes{};
    std::array<glm::vec3, terrain::kMaterialCount> means{}; // lineales
    TextureSetStamp stamp;

    DetailTextureArrays() = default;
    ~DetailTextureArrays();
    DetailTextureArrays(const DetailTextureArrays&) = delete;
    DetailTextureArrays& operator=(const DetailTextureArrays&) = delete;
};

// Texturas de detalle del terreno de un mundo: arrays compartidos + rejilla
// de materiales R8UI propia. Sin assets, ata texturas 1x1 válidas en las
// unidades de detalle y el shader cae al render solo-paleta.
class TerrainTextures {
public:
    explicit TerrainTextures(const terrain::TerrainData& data);
    ~TerrainTextures();

    TerrainTextures(const TerrainTextures&) = delete;
    TerrainTextures& operator=(const TerrainTextures&) = delete;

    bool available() const { return arrays_ != nullptr; }

    // Fija los uniforms de detalle del programa (con glProgramUniform, no
    // requiere que esté en uso) y ata las tres unidades de detalle. Deja
    // GL_TEXTURE0 como unidad activa.
    void bind(const Shader& shader, bool enabled, float intensity);

private:
    struct UniformLocations {
        GLuint program = 0;
        GLint enabled = -1;
        GLint intensity = -1;
        GLint terrain_half = -1;
        GLint layer_size = -1;
        GLint layer_mean = -1;
    };

    std::shared_ptr<const DetailTextureArrays> arrays_;
    GLuint material_grid_ = 0;
    GLuint normal_map_ = 0;
    GLuint fallback_normal_map_ = 0;
    GLuint fallback_array_ = 0; // 1x1x1, solo sin assets
    float domain_half_ = 1.0f;
    UniformLocations uniforms_;

    void cache_uniforms(const Shader& shader);
    void release();
};

} // namespace render
