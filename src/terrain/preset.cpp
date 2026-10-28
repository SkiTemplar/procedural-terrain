#include "terrain/preset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace terrain {

namespace {

using nlohmann::json;

// Lector con default: los presets viejos siguen cargando cuando el config
// gana campos nuevos (el default del struct manda).
template <typename T>
void read_or_keep(const json& j, const char* key, T& out) {
    if (const auto it = j.find(key); it != j.end()) {
        out = it->get<T>();
    }
}

} // namespace

std::uint64_t heightfield_hash(const Heightfield& hf) {
    std::uint64_t h = 1469598103934665603ull; // offset basis FNV-1a
    for (const float v : hf.data()) {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        for (int b = 0; b < 4; ++b) {
            h ^= (bits >> (8 * b)) & 0xFFu;
            h *= 1099511628211ull;
        }
    }
    return h;
}

json preset_to_json(const TerrainConfig& cfg) {
    json j;
    j["seed"] = cfg.seed;
    j["size_m"] = cfg.size_m;
    j["resolution"] = cfg.resolution;
    j["sea_level"] = cfg.sea_level;
    j["sea_depth"] = cfg.sea_depth;
    j["island_radius"] = cfg.island_radius;
    j["coast_warp_m"] = cfg.coast_warp_m;
    j["coast_flood_m"] = cfg.coast_flood_m;
    j["base_height_m"] = cfg.base_height_m;
    j["mountain_height_m"] = cfg.mountain_height_m;
    j["erosion_droplets"] = cfg.erosion_droplets;
    j["erosion_strength"] = cfg.erosion_strength;
    j["erosion_deposit"] = cfg.erosion_deposit;
    j["erosion_inertia"] = cfg.erosion_inertia;
    j["thermal_iterations"] = cfg.thermal_iterations;
    j["talus_slope"] = cfg.talus_slope;
    j["coast_flatten_band_m"] = cfg.coast_flatten_band_m;
    j["coast_flatten_strength"] = cfg.coast_flatten_strength;
    j["river_flow_threshold"] = cfg.river_flow_threshold;
    j["river_max_depth_m"] = cfg.river_max_depth_m;
    j["river_max_width_m"] = cfg.river_max_width_m;
    j["fluvial_strength"] = cfg.fluvial_strength;
    j["fluvial_iterations"] = cfg.fluvial_iterations;
    j["detail_amplitude_m"] = cfg.detail_amplitude_m;
    j["snow_line_m"] = cfg.snow_line_m;
    j["treeline_m"] = cfg.treeline_m;
    j["rock_slope"] = cfg.rock_slope;
    j["beach_band_m"] = cfg.beach_band_m;
    j["wind_dir_deg"] = cfg.wind_dir_deg;
    j["wind_focus"] = cfg.wind_focus;
    j["shore_width_scale"] = cfg.shore_width_scale;
    return j;
}

bool save_preset(const std::string& path, const TerrainConfig& cfg,
                 std::uint64_t height_hash) {
    try {
        json j = preset_to_json(cfg);
        if (height_hash != 0) {
            j["height_hash"] = height_hash;
        }
        std::ofstream out(path);
        if (!out) {
            return false;
        }
        out << j.dump(2) << '\n';
        return out.good();
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<Preset> load_preset(const std::string& path) {
    try {
        std::ifstream in(path);
        if (!in) {
            return std::nullopt;
        }
        const json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object()) {
            return std::nullopt;
        }
        Preset p;
        read_or_keep(j, "seed", p.config.seed);
        read_or_keep(j, "size_m", p.config.size_m);
        read_or_keep(j, "resolution", p.config.resolution);
        read_or_keep(j, "sea_level", p.config.sea_level);
        read_or_keep(j, "sea_depth", p.config.sea_depth);
        read_or_keep(j, "island_radius", p.config.island_radius);
        read_or_keep(j, "coast_warp_m", p.config.coast_warp_m);
        read_or_keep(j, "coast_flood_m", p.config.coast_flood_m);
        read_or_keep(j, "base_height_m", p.config.base_height_m);
        read_or_keep(j, "mountain_height_m", p.config.mountain_height_m);
        read_or_keep(j, "erosion_droplets", p.config.erosion_droplets);
        read_or_keep(j, "erosion_strength", p.config.erosion_strength);
        read_or_keep(j, "erosion_deposit", p.config.erosion_deposit);
        read_or_keep(j, "erosion_inertia", p.config.erosion_inertia);
        read_or_keep(j, "thermal_iterations", p.config.thermal_iterations);
        read_or_keep(j, "talus_slope", p.config.talus_slope);
        read_or_keep(j, "coast_flatten_band_m", p.config.coast_flatten_band_m);
        read_or_keep(j, "coast_flatten_strength",
                     p.config.coast_flatten_strength);
        read_or_keep(j, "river_flow_threshold", p.config.river_flow_threshold);
        read_or_keep(j, "river_max_depth_m", p.config.river_max_depth_m);
        read_or_keep(j, "river_max_width_m", p.config.river_max_width_m);
        read_or_keep(j, "fluvial_strength", p.config.fluvial_strength);
        read_or_keep(j, "fluvial_iterations", p.config.fluvial_iterations);
        read_or_keep(j, "detail_amplitude_m", p.config.detail_amplitude_m);
        read_or_keep(j, "snow_line_m", p.config.snow_line_m);
        read_or_keep(j, "treeline_m", p.config.treeline_m);
        read_or_keep(j, "rock_slope", p.config.rock_slope);
        read_or_keep(j, "beach_band_m", p.config.beach_band_m);
        read_or_keep(j, "wind_dir_deg", p.config.wind_dir_deg);
        read_or_keep(j, "wind_focus", p.config.wind_focus);
        read_or_keep(j, "shore_width_scale", p.config.shore_width_scale);
        read_or_keep(j, "height_hash", p.height_hash);
        return p;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<std::string> list_presets(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace terrain
