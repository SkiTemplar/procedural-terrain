#pragma once

#include "terrain/generator.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace render {

struct TerrainTextureLayer {
    terrain::Material material{};
    std::string name;
    float size_m = 0.0f;
    std::filesystem::path albedo_path;
    std::filesystem::path normal_path;
};

struct TerrainTextureSet {
    std::array<TerrainTextureLayer, terrain::kMaterialCount> layers;
};

// Huella de los ficheros de un juego de texturas (manifest + 20 mapas): si no
// cambia, los arrays GL ya subidos se reutilizan sin volver a decodificar.
struct TextureSetStamp {
    std::filesystem::path directory;
    std::vector<std::filesystem::file_time_type> write_times;
    std::vector<std::uintmax_t> sizes;

    bool operator==(const TextureSetStamp&) const = default;
};

constexpr std::size_t material_layer(terrain::Material material) {
    return static_cast<std::size_t>(material);
}

// Conversión sRGB -> lineal de un canal en [0, 1] (IEC 61966-2-1).
float srgb_to_linear(float encoded);

// Media LINEAL de una imagen codificada en sRGB: cada píxel se linealiza antes
// de promediar (promediar en sRGB y linealizar al final da una media menor por
// la convexidad de la curva, y el ratio albedo/media aclararía el terreno).
// El resultado es comparable con lo que devuelve un sampler GL_SRGB8_ALPHA8.
glm::vec3 mean_albedo(const std::uint8_t* pixels, std::size_t pixel_count,
                      int channels);

std::optional<TerrainTextureSet> load_terrain_texture_set(
    const std::filesystem::path& directory, std::string* error = nullptr);

// Huella del juego (fechas y tamaños). nullopt si algún fichero no es legible.
std::optional<TextureSetStamp> stamp_texture_set(
    const std::filesystem::path& directory, const TerrainTextureSet& set);

std::optional<std::filesystem::path> find_terrain_texture_directory(
    std::filesystem::path start);

} // namespace render
