#include "render/terrain_texture_data.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <system_error>

namespace render {
namespace {

constexpr std::array<const char*, terrain::kMaterialCount> kMaterialNames{
    "seabed", "sand", "grass", "forest", "rock",
    "snow", "riverbed", "lakebed", "gravel", "marsh",
};

void set_error(std::string* out, std::string value) {
    if (out != nullptr) {
        *out = std::move(value);
    }
}

} // namespace

float srgb_to_linear(float encoded) {
    return encoded <= 0.04045f
               ? encoded / 12.92f
               : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

glm::vec3 mean_albedo(const std::uint8_t* pixels, std::size_t pixel_count,
                      int channels) {
    if (pixels == nullptr || pixel_count == 0 || channels < 3) {
        return glm::vec3{0.0f};
    }
    std::array<double, 256> linear{};
    for (std::size_t v = 0; v < linear.size(); ++v) {
        linear[v] = srgb_to_linear(static_cast<float>(v) / 255.0f);
    }
    glm::dvec3 sum{0.0};
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const auto* pixel = pixels + i * static_cast<std::size_t>(channels);
        sum += glm::dvec3{linear[pixel[0]], linear[pixel[1]], linear[pixel[2]]};
    }
    return glm::vec3{sum / static_cast<double>(pixel_count)};
}

std::optional<TerrainTextureSet> load_terrain_texture_set(
    const std::filesystem::path& directory, std::string* error) {
    const auto manifest_path = directory / "manifest.json";
    std::ifstream input{manifest_path};
    if (!input) {
        set_error(error, "no se encuentra " + manifest_path.string());
        return std::nullopt;
    }

    try {
        const auto manifest = nlohmann::json::parse(input);
        const auto& materials = manifest.at("materials");
        TerrainTextureSet result;
        for (std::size_t i = 0; i < kMaterialNames.size(); ++i) {
            const std::string name = kMaterialNames[i];
            const auto& entry = materials.at(name);
            TerrainTextureLayer layer;
            layer.material = static_cast<terrain::Material>(i);
            layer.name = name;
            layer.size_m = entry.at("size_m").get<float>();
            layer.albedo_path = directory / (name + "_albedo.jpg");
            layer.normal_path = directory / (name + "_normal.jpg");
            if (!(layer.size_m > 0.0f)) {
                set_error(error, "size_m invalido para " + name);
                return std::nullopt;
            }
            if (!std::filesystem::is_regular_file(layer.albedo_path)
                || !std::filesystem::is_regular_file(layer.normal_path)) {
                set_error(error, "faltan mapas albedo/normal para " + name);
                return std::nullopt;
            }
            result.layers[i] = std::move(layer);
        }
        return result;
    } catch (const std::exception& ex) {
        set_error(error, "manifest de texturas invalido: " + std::string{ex.what()});
        return std::nullopt;
    }
}

std::optional<TextureSetStamp> stamp_texture_set(
    const std::filesystem::path& directory, const TerrainTextureSet& set) {
    TextureSetStamp stamp;
    stamp.directory = directory;
    std::vector<std::filesystem::path> files{directory / "manifest.json"};
    for (const auto& layer : set.layers) {
        files.push_back(layer.albedo_path);
        files.push_back(layer.normal_path);
    }
    for (const auto& file : files) {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(file, ec);
        if (ec) {
            return std::nullopt;
        }
        const auto size = std::filesystem::file_size(file, ec);
        if (ec) {
            return std::nullopt;
        }
        stamp.write_times.push_back(time);
        stamp.sizes.push_back(size);
    }
    return stamp;
}

std::optional<std::filesystem::path> find_terrain_texture_directory(
    std::filesystem::path start) {
    std::error_code ec;
    start = std::filesystem::absolute(start, ec);
    if (ec) {
        return std::nullopt;
    }
    if (std::filesystem::is_regular_file(start, ec)) {
        start = start.parent_path();
    }
    for (auto current = start; !current.empty();) {
        const auto candidate = current / "assets" / "textures" / "terrain";
        if (std::filesystem::is_directory(candidate, ec)) {
            return candidate;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::nullopt;
}

} // namespace render
