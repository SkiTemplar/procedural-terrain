// windows.h antes que glad (vía terrain_textures.h): si va después,
// redefine APIENTRY (C4005).
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "render/terrain_textures.h"

#include "render/shader.h"
#include "render/terrain_normal_map.h"
#include "render/terrain_shader_constants.h"
#include "terrain/generator.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

struct Image {
    stbi_uc* pixels = nullptr;
    int width = 0;
    int height = 0;

    Image() = default;
    explicit Image(const std::filesystem::path& path) {
        int channels = 0;
        pixels = stbi_load(path.string().c_str(), &width, &height, &channels,
                           STBI_rgb_alpha);
    }
    ~Image() { stbi_image_free(pixels); }
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
};

GLuint create_layer_array(GLenum internal_format, int width, int height) {
    const int mip_levels = 1 + static_cast<int>(std::floor(
        std::log2(static_cast<float>((std::max)(width, height)))));
    GLuint texture = 0;
    glGenTextures(1, &texture);
    configure_array(texture);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, mip_levels, internal_format, width,
                   height, static_cast<GLsizei>(terrain::kMaterialCount));
    return texture;
}

void upload_layer(GLuint texture, std::size_t layer, const Image& image) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, static_cast<GLint>(layer),
                    image.width, image.height, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                    image.pixels);
}

// Decodifica y sube las 10 capas, capa a capa (sin acumular 80 MB de JPG
// decodificados). nullptr si alguna falta o no tiene el tamaño de la primera.
std::shared_ptr<DetailTextureArrays> upload_texture_set(
    const TerrainTextureSet& set, TextureSetStamp stamp) {
    auto arrays = std::make_shared<DetailTextureArrays>();
    arrays->stamp = std::move(stamp);
    int width = 0;
    int height = 0;
    for (std::size_t i = 0; i < set.layers.size(); ++i) {
        const auto& layer = set.layers[i];
        const Image albedo{layer.albedo_path};
        const Image normal{layer.normal_path};
        const bool first = i == 0;
        if (first && albedo.pixels != nullptr) {
            width = albedo.width;
            height = albedo.height;
            arrays->albedo = create_layer_array(GL_SRGB8_ALPHA8, width, height);
            arrays->normal = create_layer_array(GL_RGBA8, width, height);
        }
        const auto homogeneous = [&](const Image& image) {
            return image.pixels != nullptr && image.width == width
                && image.height == height;
        };
        if (!homogeneous(albedo) || !homogeneous(normal)) {
            std::cerr << "AVISO: texturas de detalle desactivadas: capa '"
                      << layer.name << "' ilegible o de tamano distinto ("
                      << layer.albedo_path << ", " << layer.normal_path
                      << ")\n";
            return nullptr; // el destructor libera lo ya creado
        }
        arrays->means[i] = mean_albedo(
            albedo.pixels, static_cast<std::size_t>(width) * height, 4);
        arrays->sizes[i] = layer.size_m;
        upload_layer(arrays->albedo, i, albedo);
        upload_layer(arrays->normal, i, normal);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, arrays->albedo);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, arrays->normal);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    std::cout << "texturas de detalle: " << set.layers.size() << " capas "
              << width << 'x' << height << " cargadas desde "
              << arrays->stamp.directory.string() << '\n';
    return arrays;
}

// Caché de proceso de los arrays. weak_ptr: el mundo nuevo se construye antes
// de destruir el anterior (main.cpp), así que al regenerar los arrays siguen
// vivos y se reutilizan si la huella de los ficheros no ha cambiado; cuando
// ya no hay mundos, se liberan con el contexto GL todavía activo.
std::shared_ptr<const DetailTextureArrays> acquire_detail_arrays() {
    static std::weak_ptr<const DetailTextureArrays> cache;

    const auto directory = locate_textures();
    if (!directory) {
        std::cerr << "AVISO: texturas de detalle desactivadas: no se encuentra "
                     "assets/textures/terrain\n";
        return nullptr;
    }
    std::string error;
    const auto set = load_terrain_texture_set(*directory, &error);
    if (!set) {
        std::cerr << "AVISO: texturas de detalle desactivadas: " << error << '\n';
        return nullptr;
    }
    auto stamp = stamp_texture_set(*directory, *set);
    if (!stamp) {
        std::cerr << "AVISO: texturas de detalle desactivadas: no se pueden "
                     "leer los ficheros de "
                  << directory->string() << '\n';
        return nullptr;
    }
    if (auto cached = cache.lock(); cached && cached->stamp == *stamp) {
        return cached;
    }
    std::shared_ptr<const DetailTextureArrays> fresh =
        upload_texture_set(*set, std::move(*stamp));
    cache = fresh;
    return fresh;
}

// R8UI con el material de cada nodo de la rejilla. Alineación de 1 byte: la
// resolución no tiene por qué ser múltiplo de 4.
GLuint upload_material_grid(const std::uint8_t* materials, int resolution) {
    GLint previous_alignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, resolution, resolution, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_BYTE, materials);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previous_alignment);
    return texture;
}

// Array 1x1x1 neutro (albedo gris, normal hacia +z tangente) para que las
// unidades de detalle tengan siempre una textura completa del tipo correcto.
GLuint create_fallback_array() {
    const std::uint8_t texel[4] = {128, 128, 255, 255};
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, 1, 1, 1);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 1, 1, 1, GL_RGBA,
                    GL_UNSIGNED_BYTE, texel);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return texture;
}

// Textura 2D RG16_SNORM con el normal map de Scharr 3x3 del heightfield completo,
// con mipmaps y filtrado lineal.
GLuint upload_terrain_normal_map(const std::int16_t* data, int resolution) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16_SNORM, resolution, resolution, 0,
                 GL_RG, GL_SHORT, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLfloat maximum = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maximum);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY,
                    std::min(maximum, 8.0f));
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

// Fallback 1x1 con normal cenital (0, 1, 0)
GLuint create_fallback_normal_map() {
    const std::int16_t texel[2] = {0, 0};
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16_SNORM, 1, 1, 0,
                 GL_RG, GL_SHORT, texel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

} // namespace

DetailTextureArrays::~DetailTextureArrays() {
    if (albedo != 0) glDeleteTextures(1, &albedo);
    if (normal != 0) glDeleteTextures(1, &normal);
}

TerrainTextures::TerrainTextures(const terrain::TerrainData& data)
    : arrays_(acquire_detail_arrays()),
      domain_half_(data.config.size_m * 0.5f) {
    if (data.height.resolution() >= 2) {
        const auto normal_data = compute_scharr_normal_map(data.height);
        normal_map_ = upload_terrain_normal_map(normal_data.data(),
                                                data.height.resolution());
    }
    if (!arrays_) {
        // Render solo-paleta: rejilla 1x1 y array neutro, nunca unidades vacías.
        const std::uint8_t material = 0;
        material_grid_ = upload_material_grid(&material, 1);
        fallback_array_ = create_fallback_array();
        return;
    }
    std::vector<std::uint8_t> materials(data.material.size());
    std::transform(data.material.begin(), data.material.end(), materials.begin(),
                   [](terrain::Material m) {
                       return static_cast<std::uint8_t>(m);
                   });
    material_grid_ = upload_material_grid(materials.data(),
                                          data.height.resolution());
}

TerrainTextures::~TerrainTextures() { release(); }

void TerrainTextures::release() {
    if (material_grid_ != 0) glDeleteTextures(1, &material_grid_);
    if (fallback_array_ != 0) glDeleteTextures(1, &fallback_array_);
    if (normal_map_ != 0) glDeleteTextures(1, &normal_map_);
    if (fallback_normal_map_ != 0) glDeleteTextures(1, &fallback_normal_map_);
    material_grid_ = fallback_array_ = normal_map_ = fallback_normal_map_ = 0;
}

void TerrainTextures::cache_uniforms(const Shader& shader) {
    uniforms_.program = shader.id();
    uniforms_.enabled = shader.uniform_location("u_detail_enabled");
    uniforms_.intensity = shader.uniform_location("u_detail_intensity");
    uniforms_.terrain_half = shader.uniform_location("u_terrain_half");
    uniforms_.layer_size = shader.uniform_location("u_layer_size");
    uniforms_.layer_mean = shader.uniform_location("u_layer_mean");

    // Los valores por capa y el dominio no cambian en la vida de este mundo:
    // se suben una vez por programa.
    const GLuint program = uniforms_.program;
    glProgramUniform1f(program, uniforms_.terrain_half, domain_half_);
    if (arrays_) {
        const auto count = static_cast<GLsizei>(terrain::kMaterialCount);
        glProgramUniform1fv(program, uniforms_.layer_size, count,
                            arrays_->sizes.data());
        glProgramUniform3fv(program, uniforms_.layer_mean, count,
                            &arrays_->means[0].x);
    }
}

void TerrainTextures::bind(const Shader& shader, bool enabled, float intensity) {
    if (uniforms_.program != shader.id()) {
        cache_uniforms(shader);
    }
    const GLuint program = uniforms_.program;
    glProgramUniform1i(program, uniforms_.enabled,
                       enabled && available() ? 1 : 0);
    glProgramUniform1f(program, uniforms_.intensity,
                       std::clamp(intensity, 0.0f, 1.5f));

    const GLuint albedo = arrays_ ? arrays_->albedo : fallback_array_;
    const GLuint normal = arrays_ ? arrays_->normal : fallback_array_;
    const GLuint norm_map = normal_map_ != 0 ? normal_map_
                          : (fallback_normal_map_ != 0 ? fallback_normal_map_
                             : (fallback_normal_map_ = create_fallback_normal_map()));

    glActiveTexture(GL_TEXTURE0 + PT_DETAIL_ALBEDO_UNIT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, albedo);
    glActiveTexture(GL_TEXTURE0 + PT_DETAIL_NORMAL_UNIT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, normal);
    glActiveTexture(GL_TEXTURE0 + PT_MATERIAL_GRID_UNIT);
    glBindTexture(GL_TEXTURE_2D, material_grid_);
    glActiveTexture(GL_TEXTURE0 + PT_TERRAIN_NORMAL_MAP_UNIT);
    glBindTexture(GL_TEXTURE_2D, norm_map);
    glActiveTexture(GL_TEXTURE0);
}

} // namespace render
