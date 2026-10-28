#include "render/terrain_textures.h"

#include "render/shader.h"
#include "terrain/generator.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace render {
namespace {

std::filesystem::path executable_path() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
    if (size > 0 && size < buffer.size()) {
        buffer.resize(size);
        return std::filesystem::path{buffer};
    }
#endif
    return {};
}

std::optional<std::filesystem::path> locate_textures() {
    if (const auto from_cwd =
            find_terrain_texture_directory(std::filesystem::current_path())) {
        return from_cwd;
    }
    const auto executable = executable_path();
    return executable.empty() ? std::nullopt
                              : find_terrain_texture_directory(executable);
}

void configure_array(GLuint texture) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // GL 4.6 incorpora el filtrado anisotropico; respeta el limite del driver.
    GLfloat maximum = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maximum);
    glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY,
                    (std::min)(8.0f, maximum));
}

glm::vec3 srgb_to_linear(glm::vec3 value) {
    const auto convert = [](float c) {
        return c <= 0.04045f ? c / 12.92f
                             : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    return {convert(value.r), convert(value.g), convert(value.b)};
}

} // namespace

TerrainTextures::TerrainTextures(const terrain::TerrainData& data) {
    const auto directory = locate_textures();
    if (!directory) {
        std::cerr << "AVISO: texturas de detalle desactivadas: no se encuentra "
                     "assets/textures/terrain\n";
        return;
    }
    std::string error;
    const auto set = load_terrain_texture_set(*directory, &error);
    if (!set) {
        std::cerr << "AVISO: texturas de detalle desactivadas: " << error << '\n';
        return;
    }

    int width = 0;
    int height = 0;
    std::vector<std::vector<unsigned char>> albedos;
    std::vector<std::vector<unsigned char>> normals;
    albedos.reserve(terrain::kMaterialCount);
    normals.reserve(terrain::kMaterialCount);
    for (std::size_t i = 0; i < set->layers.size(); ++i) {
        const auto& layer = set->layers[i];
        int aw = 0;
        int ah = 0;
        int channels = 0;
        stbi_uc* albedo = stbi_load(layer.albedo_path.string().c_str(), &aw, &ah,
                                    &channels, STBI_rgb_alpha);
        if (albedo == nullptr || (i != 0 && (aw != width || ah != height))) {
            std::cerr << "AVISO: texturas de detalle desactivadas: no se pudo "
                         "cargar una capa albedo homogenea ("
                      << layer.albedo_path << ")\n";
            stbi_image_free(albedo);
            return;
        }
        if (i == 0) {
            width = aw;
            height = ah;
        }
        const std::size_t bytes = static_cast<std::size_t>(aw) * ah * 4;
        means_[i] = srgb_to_linear(mean_albedo(albedo,
            static_cast<std::size_t>(aw) * ah, 4));
        albedos.emplace_back(albedo, albedo + bytes);
        stbi_image_free(albedo);

        int nw = 0;
        int nh = 0;
        stbi_uc* normal = stbi_load(layer.normal_path.string().c_str(), &nw, &nh,
                                    &channels, STBI_rgb_alpha);
        if (normal == nullptr || nw != width || nh != height) {
            std::cerr << "AVISO: texturas de detalle desactivadas: no se pudo "
                         "cargar una capa normal homogenea ("
                      << layer.normal_path << ")\n";
            stbi_image_free(normal);
            return;
        }
        normals.emplace_back(normal, normal + bytes);
        stbi_image_free(normal);
        sizes_[i] = layer.size_m;
    }

    const int mip_levels = 1 + static_cast<int>(std::floor(
        std::log2(static_cast<float>((std::max)(width, height)))));
    glGenTextures(1, &albedo_);
    configure_array(albedo_);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, mip_levels, GL_SRGB8_ALPHA8, width, height,
                   static_cast<GLsizei>(terrain::kMaterialCount));
    glGenTextures(1, &normal_);
    configure_array(normal_);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, mip_levels, GL_RGBA8, width, height,
                   static_cast<GLsizei>(terrain::kMaterialCount));
    for (std::size_t i = 0; i < terrain::kMaterialCount; ++i) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, albedo_);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, static_cast<GLint>(i),
                        width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        albedos[i].data());
        glBindTexture(GL_TEXTURE_2D_ARRAY, normal_);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, static_cast<GLint>(i),
                        width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        normals[i].data());
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, albedo_);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, normal_);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    std::vector<std::uint8_t> materials(data.material.size());
    for (std::size_t i = 0; i < data.material.size(); ++i) {
        materials[i] = static_cast<std::uint8_t>(data.material[i]);
    }
    const int resolution = data.height.resolution();
    glGenTextures(1, &material_grid_);
    glBindTexture(GL_TEXTURE_2D, material_grid_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, resolution, resolution, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_BYTE, materials.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    domain_half_ = data.config.size_m * 0.5f;
    available_ = true;
    std::cout << "texturas de detalle: 10 capas " << width << 'x' << height
              << " cargadas desde " << directory->string() << '\n';
}

TerrainTextures::~TerrainTextures() { release(); }

void TerrainTextures::release() {
    if (albedo_ != 0) glDeleteTextures(1, &albedo_);
    if (normal_ != 0) glDeleteTextures(1, &normal_);
    if (material_grid_ != 0) glDeleteTextures(1, &material_grid_);
    albedo_ = normal_ = material_grid_ = 0;
}

void TerrainTextures::bind(const Shader& shader, bool enabled,
                           float intensity) const {
    shader.set_int("u_detail_enabled", enabled && available_ ? 1 : 0);
    shader.set_float("u_detail_intensity", std::clamp(intensity, 0.0f, 1.5f));
    shader.set_float("u_terrain_half", domain_half_);
    if (!available_) return;

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, albedo_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, normal_);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, material_grid_);
    shader.set_int("u_detail_albedo", 1);
    shader.set_int("u_detail_normal", 2);
    shader.set_int("u_material_grid", 3);
    for (std::size_t i = 0; i < terrain::kMaterialCount; ++i) {
        const std::string index = "[" + std::to_string(i) + "]";
        shader.set_float(("u_layer_size" + index).c_str(), sizes_[i]);
        shader.set_vec3(("u_layer_mean" + index).c_str(), means_[i]);
    }
}

} // namespace render
