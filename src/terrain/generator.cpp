#include "terrain/generator.h"

#include "terrain/erosion.h"
#include "terrain/noise.h"

#include <glm/glm.hpp>

#include <cmath>

namespace terrain {

namespace {

// Frecuencias en 1/km: el ruido se muestrea con la posición en kilómetros
// para que los parámetros sean independientes del tamaño del mapa.
constexpr float kBaseFreq = 0.45f;     // colinas
constexpr float kMountainFreq = 0.28f; // cordilleras
constexpr float kRangeMaskFreq = 0.13f; // dónde hay cordillera
constexpr float kForestFreq = 0.9f;
constexpr float kWarpFreq = 0.35f;

float smoothstep(float edge0, float edge1, float x) {
    const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void build_base_relief(Heightfield& hf, const Noise& noise, const TerrainConfig& cfg) {
    const float half_km = cfg.size_m * 0.5f / 1000.0f;

    for (int z = 0; z < hf.resolution(); ++z) {
        for (int x = 0; x < hf.resolution(); ++x) {
            const glm::vec3 p_km{hf.world_x(x) / 1000.0f, 0.0f, hf.world_z(z) / 1000.0f};
            const glm::vec3 pw = noise.warp(p_km, cfg.coast_warp_m / 1000.0f, kWarpFreq);

            // Continentalidad: 1 en el centro, 0 mar adentro; la costa cae
            // entre island_radius y 1.0 del semiancho (sobre la pos. distorsionada).
            const float d = glm::length(glm::vec2{pw.x, pw.z}) / half_km;
            const float continent = 1.0f - smoothstep(cfg.island_radius, 1.0f, d);

            const float base01 = noise.fbm(pw * kBaseFreq, 5) * 0.5f + 0.5f;

            const float ridge = noise.ridged(pw * kMountainFreq, 5);
            const float range_mask = smoothstep(
                0.45f, 0.75f,
                noise.fbm(pw * kRangeMaskFreq + glm::vec3{53.7f, 0.0f, -11.9f}, 3) * 0.5f + 0.5f);

            float land = continent * (cfg.base_height_m * base01)
                       + std::pow(continent, 1.5f) * cfg.mountain_height_m
                             * ridge * ridge * range_mask;

            // Curva de valles: comprime las alturas bajas para que los fondos
            // de valle sean anchos y planos y el relieve se concentre arriba.
            // Se normaliza por el techo local (colinas + cordillera efectiva),
            // no por el máximo global, para no aplastar las zonas sin montaña.
            if (land > 0.0f) {
                const float local_ceiling =
                    cfg.base_height_m + cfg.mountain_height_m * range_mask;
                const float norm = std::min(land / local_ceiling, 1.0f);
                land = local_ceiling * std::pow(norm, 1.35f);
            }

            const float seabed = (1.0f - continent) * cfg.sea_depth;
            hf.at(x, z) = cfg.sea_level + land - seabed;
        }
    }
}

// Erosión costera simplificada: la franja baja junto al mar se comprime hacia
// el nivel del mar → playas anchas y llanas donde el terreno ya era suave.
void flatten_coast(Heightfield& hf, const TerrainConfig& cfg) {
    const float band = cfg.coast_flatten_band_m;
    for (float& h : hf.data()) {
        const float rel = h - cfg.sea_level;
        if (rel > 0.0f && rel < band) {
            const float t = rel / band; // 0 en la orilla, 1 al final de la banda
            const float ease = t * t * (3.0f - 2.0f * t);
            const float compress = 1.0f - cfg.coast_flatten_strength * (1.0f - ease);
            h = cfg.sea_level + rel * compress;
        }
    }
}

void classify_materials(TerrainData& data, const Noise& noise) {
    const auto& cfg = data.config;
    const auto& hf = data.height;
    const int res = hf.resolution();
    data.material.assign(static_cast<size_t>(res) * static_cast<size_t>(res),
                         Material::Grass);

    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(res)
                           + static_cast<size_t>(x);
            const float h = hf.at(x, z);
            const glm::vec3 n = hf.normal_at(x, z);
            const float slope = std::sqrt(std::max(0.0f, 1.0f - n.y * n.y)) / std::max(n.y, 1e-3f);

            Material m = Material::Grass;
            if (h <= cfg.sea_level) {
                m = Material::Seabed;
            } else if (!std::isnan(data.hydrology.water_level[i])
                       && data.hydrology.flow[i] >= cfg.river_flow_threshold) {
                m = Material::RiverBed;
            } else if (h < cfg.sea_level + cfg.beach_band_m && slope < 0.35f) {
                m = Material::Sand;
            } else if (slope > cfg.rock_slope) {
                m = Material::Rock;
            } else {
                // Línea de nieve con borde irregular.
                const glm::vec3 p_km{hf.world_x(x) / 1000.0f, 0.0f, hf.world_z(z) / 1000.0f};
                const float snow_jitter =
                    noise.fbm(p_km * 1.7f + glm::vec3{7.3f, 0.0f, 91.4f}, 3) * 80.0f;
                if (h > cfg.snow_line_m + snow_jitter) {
                    m = Material::Snow;
                } else {
                    const float forest =
                        noise.fbm(p_km * kForestFreq + glm::vec3{-29.1f, 0.0f, 63.8f}, 4);
                    if (forest > 0.08f && h < cfg.snow_line_m - 150.0f) {
                        m = Material::Forest;
                    }
                }
            }
            data.material[i] = m;
        }
    }
}

} // namespace

TerrainData generate(const TerrainConfig& cfg) {
    TerrainData data;
    data.config = cfg;
    data.height = Heightfield(cfg.resolution, cfg.size_m);

    const Noise noise(cfg.seed);

    build_base_relief(data.height, noise, cfg);
    erode(data.height, cfg);
    thermal_erode(data.height, cfg.thermal_iterations, cfg.talus_slope);
    flatten_coast(data.height, cfg);
    data.hydrology = compute_hydrology(data.height, cfg);
    classify_materials(data, noise);

    return data;
}

} // namespace terrain
