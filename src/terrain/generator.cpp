#include "terrain/generator.h"

#include "terrain/erosion.h"
#include "terrain/noise.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <execution>
#include <numeric>
#include <vector>

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

std::vector<int> row_indices(int res) {
    std::vector<int> rows(static_cast<size_t>(res));
    std::iota(rows.begin(), rows.end(), 0);
    return rows;
}

void build_base_relief(Heightfield& hf, const Noise& noise, const TerrainConfig& cfg) {
    const float half_km = cfg.size_m * 0.5f / 1000.0f;
    const auto rows = row_indices(hf.resolution());

    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
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
    });
}

// Erosión costera simplificada: la franja baja junto al mar se comprime hacia
// el nivel del mar → playas anchas y llanas. Solo donde la costa ya es suave:
// las costas abruptas se dejan intactas y quedan como acantilados de roca.
void flatten_coast(Heightfield& hf, const TerrainConfig& cfg) {
    const float band = cfg.coast_flatten_band_m;
    const int res = hf.resolution();
    const std::vector<float> original(hf.data());
    const float inv_cell = 1.0f / hf.cell_size();

    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(res)
                           + static_cast<size_t>(x);
            const float rel = original[i] - cfg.sea_level;
            if (rel <= 0.0f || rel >= band) {
                continue;
            }
            // Pendiente local sobre el terreno original.
            const float slope = std::max(
                std::abs(original[i + 1] - original[i - 1]),
                std::abs(original[i + static_cast<size_t>(res)]
                         - original[i - static_cast<size_t>(res)]))
                * 0.5f * inv_cell;
            // Costa suave → playa; costa abrupta → acantilado sin tocar.
            const float cliff_keep = smoothstep(0.4f, 0.8f, slope);
            if (cliff_keep >= 1.0f) {
                continue;
            }
            const float t = rel / band;
            const float ease = t * t * (3.0f - 2.0f * t);
            const float compress =
                1.0f - cfg.coast_flatten_strength * (1.0f - ease) * (1.0f - cliff_keep);
            hf.data()[i] = cfg.sea_level + rel * compress;
        }
    }
}

// Nieve y pisos de vegetación realistas. La nieve no es una cota: depende de
// la altitud (borde irregular), la pendiente (las paredes no acumulan), la
// orientación (las umbrías conservan nieve más abajo) y parches de ruido.
// La vegetación pierde altura por pisos: bosque → pradera alpina → roca.
void classify_materials(TerrainData& data, const Noise& noise) {
    const auto& cfg = data.config;
    const auto& hf = data.height;
    const int res = hf.resolution();
    data.material.assign(static_cast<size_t>(res) * static_cast<size_t>(res),
                         Material::Grass);

    const float threshold_flow = cfg.river_flow_threshold
                               * (static_cast<float>(res) * static_cast<float>(res))
                               / (1024.0f * 1024.0f);
    const auto rows = row_indices(res);

    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(res)
                           + static_cast<size_t>(x);
            const float h = hf.at(x, z);
            const glm::vec3 n = hf.normal_at(x, z);
            const float slope =
                std::sqrt(std::max(0.0f, 1.0f - n.y * n.y)) / std::max(n.y, 1e-3f);

            const glm::vec3 p_km{hf.world_x(x) / 1000.0f, 0.0f, hf.world_z(z) / 1000.0f};

            const float wl = data.hydrology.water_level[i];

            Material m = Material::Grass;
            if (h <= cfg.sea_level) {
                m = Material::Seabed;
            } else if (!std::isnan(wl) && h < wl - 0.05f) {
                // Sumergido bajo lago o río: lecho, nunca hierba/bosque.
                m = data.hydrology.flow[i] >= threshold_flow ? Material::RiverBed
                                                             : Material::LakeBed;
            } else if (h < cfg.sea_level + cfg.beach_band_m && slope < 0.35f) {
                m = Material::Sand;
            } else {
                const float snow_edge_jitter =
                    noise.fbm(p_km * 1.7f + glm::vec3{7.3f, 0.0f, 91.4f}, 3) * 130.0f;
                const float patch =
                    noise.fbm(p_km * 11.0f + glm::vec3{-3.1f, 0.0f, 17.9f}, 3);

                // Score de nieve: altitud relativa + umbría − pendiente + parche.
                float snow_score =
                    (h - (cfg.snow_line_m + snow_edge_jitter)) / 140.0f;
                snow_score += 0.30f * std::max(n.z, 0.0f); // cara norte
                snow_score -= 0.85f * slope;               // paredes desnudas
                snow_score += 0.25f * patch;               // parches irregulares

                const float treeline = cfg.snow_line_m - 260.0f
                    + noise.fbm(p_km * 2.3f + glm::vec3{41.0f, 0.0f, -8.5f}, 2) * 80.0f;

                if (snow_score > 0.0f) {
                    m = Material::Snow;
                } else if (slope > cfg.rock_slope) {
                    m = Material::Rock;
                } else if (h > treeline) {
                    // Piso alpino: pradera rala con roquedo a parches, más
                    // roca cuanto más cerca de la nieve.
                    const float alpine_t = smoothstep(treeline, cfg.snow_line_m, h);
                    m = (patch > 0.35f - 0.45f * alpine_t) ? Material::Rock
                                                           : Material::Grass;
                } else {
                    // Bosque: denso abajo, se aclara al acercarse a la treeline.
                    const float forest =
                        noise.fbm(p_km * kForestFreq + glm::vec3{-29.1f, 0.0f, 63.8f}, 4);
                    const float forest_threshold =
                        0.06f + 0.55f * smoothstep(treeline - 250.0f, treeline, h);
                    if (forest > forest_threshold) {
                        m = Material::Forest;
                    }
                }
            }
            data.material[i] = m;
        }
    });
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
