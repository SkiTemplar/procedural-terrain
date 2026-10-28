#pragma once

#include "terrain/generator.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

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

constexpr std::size_t material_layer(terrain::Material material) {
    return static_cast<std::size_t>(material);
}

glm::vec3 mean_albedo(const std::uint8_t* pixels, std::size_t pixel_count,
                      int channels);

std::optional<TerrainTextureSet> load_terrain_texture_set(
    const std::filesystem::path& directory, std::string* error = nullptr);

std::optional<std::filesystem::path> find_terrain_texture_directory(
    std::filesystem::path start);

} // namespace render
