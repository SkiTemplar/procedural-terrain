#include "terrain/generator.h"

#include "terrain/erosion.h"
#include "terrain/lem.h"
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

struct CoastSample {
    float continent;
    float climate;
};

// Continentalidad + clima en un punto (compartido entre el uplift macro y la
// amplificación): bahías/penínsulas por warp compuesto, sectores de clima
// nítidos, islotes satélite concentrados en el sector cálido.
CoastSample coast_at(const Noise& noise, const TerrainConfig& cfg,
                     const glm::vec3& p_km) {
    const float half_km = cfg.size_m * 0.5f / 1000.0f;
    const glm::vec3 pw = noise.warp(
        noise.warp(p_km, 1.8f, 0.12f), cfg.coast_warp_m / 1000.0f, kWarpFreq);

    const float d = glm::length(glm::vec2{pw.x, pw.z}) / half_km;
    float continent = 1.0f - smoothstep(cfg.island_radius, 1.0f, d);

    const float climate_raw =
        noise.fbm(pw * 0.14f + glm::vec3{-64.2f, 0.0f, 38.5f}, 2) * 0.5f + 0.5f;
    const float climate = smoothstep(0.42f, 0.58f, climate_raw);

    const float satellite =
        smoothstep(0.60f, 0.72f,
                   noise.fbm(pw * 0.9f + glm::vec3{97.1f, 0.0f, -44.3f}, 3)
                       * 0.5f + 0.5f)
        * smoothstep(0.78f, 0.95f, d) * smoothstep(1.35f, 1.0f, d)
        * (0.5f + climate);
    continent = std::max(continent, satellite * 0.5f);

    return {continent, climate};
}

// Pipeline INVERTIDO (Cordonnier 2016 / FastScape): la forma macro sale de un
// modelo de evolución del paisaje (uplift + stream power a estado
// estacionario → red dendrítica de valles y divisorias reales), y el ruido
// queda como mero detalle de laderas amortiguado por pendiente (Quilez,
// morenoise). El ruido ya no ES la montaña.
void build_base_relief(TerrainData& data, const Noise& noise, const TerrainConfig& cfg) {
    Heightfield& hf = data.height;
    const int res = hf.resolution();
    data.climate.assign(hf.data().size(), 0.5f);

    // ---- Fase A: uplift en grid macro + LEM ----
    const int macro_res = std::clamp(cfg.resolution / 4, 64, 1024);
    const float macro_cell = cfg.size_m / static_cast<float>(macro_res - 1);
    std::vector<float> uplift(static_cast<size_t>(macro_res) * macro_res);
    {
        // Orientación tectónica fija por seed: el uplift es ANISÓTROPO
        // (crestas ridged alargadas ~3:1 a lo largo del eje) — cordilleras
        // lineales con espolones, como las reales, en vez de macizos blob.
        const float theta =
            noise.fbm(glm::vec3{7.7f, 0.0f, 13.1f}, 1) * 3.14159f;
        const glm::vec2 axis{std::cos(theta), std::sin(theta)};

        const auto mrows = row_indices(macro_res);
        std::for_each(std::execution::par, mrows.begin(), mrows.end(), [&](int z) {
            for (int x = 0; x < macro_res; ++x) {
                const float wx = -cfg.size_m * 0.5f + static_cast<float>(x) * macro_cell;
                const float wz = -cfg.size_m * 0.5f + static_cast<float>(z) * macro_cell;
                const glm::vec3 p_km{wx / 1000.0f, 0.0f, wz / 1000.0f};
                const CoastSample cs = coast_at(noise, cfg, p_km);

                // Espinazo: ridged muestreado con el dominio comprimido a
                // través del eje (λ ~12 km a lo largo, ~4 km a través).
                const float u = p_km.x * axis.x + p_km.z * axis.y;
                const float v = -p_km.x * axis.y + p_km.z * axis.x;
                const float spine = smoothstep(
                    0.30f, 0.72f,
                    noise.ridged(glm::vec3{u * 0.08f, 0.0f, v * 0.26f}
                                     + glm::vec3{19.3f, 0.0f, -7.4f}, 3));
                // Máscara regional lenta: dónde vive la cordillera (evita un
                // espinazo uniforme que cruce toda la isla).
                const float mass = smoothstep(
                    0.32f, 0.68f,
                    noise.fbm(p_km * 0.10f + glm::vec3{53.7f, 0.0f, -11.9f}, 2)
                        * 0.5f + 0.5f);
                // El LEM solo levanta lo que el uplift marca (S ∝ u): con
                // crestas u=1 de una celda las cumbres se quedaban al ~58%
                // del objetivo. Ganancia del espinazo alta para que el
                // NÚCLEO SATURE ancho (macizos enteros a u≈1, no filos);
                // la masa regional sigue poniendo altiplanos y decidiendo
                // dónde culmina la cordillera.
                uplift[static_cast<size_t>(z) * macro_res + x] =
                    std::pow(cs.continent, 1.5f)
                    * (0.15f + 0.85f * glm::clamp(spine * (0.75f + 0.45f * mass)
                                                      + 0.35f * mass,
                                                  0.0f, 1.0f));
            }
        });
    }
    // El uplift acumulado dobla la cota objetivo: la incisión del propio LEM
    // rebaja hasta dejar cumbres del orden de mountain_height.
    const std::vector<float> macro_h =
        run_lem(uplift, macro_res, macro_cell, 2.0f * cfg.mountain_height_m);

    // ---- Fase B: amplificación bicúbica + colinillas ----
    const float to_macro = static_cast<float>(macro_res - 1) / static_cast<float>(res - 1);
    const auto rows = row_indices(res);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        for (int x = 0; x < res; ++x) {
            const glm::vec3 p_km{hf.world_x(x) / 1000.0f, 0.0f, hf.world_z(z) / 1000.0f};
            const CoastSample cs = coast_at(noise, cfg, p_km);
            data.climate[static_cast<size_t>(z) * res + static_cast<size_t>(x)] =
                cs.climate;

            // El macro ya viene en metros con pendientes físicas.
            const float macro = sample_bicubic(
                macro_h, macro_res, static_cast<float>(x) * to_macro,
                static_cast<float>(z) * to_macro);

            float h = cfg.sea_level + macro;
            h -= (1.0f - cs.continent) * cfg.sea_depth;
            // Colinillas suaves de pradera (el LEM deja las zonas de poco
            // uplift casi planas; esto les da su ondulación).
            h += cs.continent
               * noise.fbm(p_km * 0.35f + glm::vec3{11.3f, 0.0f, -6.1f}, 3)
               * cfg.base_height_m;
            hf.at(x, z) = h;
        }
    });

    // ---- Detalle de laderas amortiguado por pendiente ----
    // Máximo en pendiente media (laderas), casi nulo en fondos de valle
    // (deposición) y en paredes (roca lisa): el detalle no delata el ruido.
    const std::vector<float> base(hf.data());
    const float inv_cell = 1.0f / hf.cell_size();
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        if (z < 1 || z >= res - 1) {
            return;
        }
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = static_cast<size_t>(z) * res + static_cast<size_t>(x);
            const float slope = std::max(
                std::abs(base[i + 1] - base[i - 1]),
                std::abs(base[i + static_cast<size_t>(res)]
                         - base[i - static_cast<size_t>(res)]))
                * 0.5f * inv_cell;
            const float amp = 32.0f * glm::clamp(slope / 0.12f, 0.0f, 1.0f)
                            / (1.0f + 10.0f * slope * slope);
            if (amp < 0.5f) {
                continue;
            }
            // Pocas octavas y longitud de onda larga: la amplitud amortigua,
            // pero es el GRADIENTE de las octavas finas lo que crea paredes.
            const glm::vec3 p_km{hf.world_x(x) / 1000.0f, 0.0f, hf.world_z(z) / 1000.0f};
            hf.data()[i] +=
                amp * noise.fbm(p_km * 0.8f + glm::vec3{-71.6f, 0.0f, 33.9f}, 3);
        }
    });
}

// Playas: la franja baja junto al mar se comprime SOLO donde la costa ya es
// suave Y el clima acompaña — en el sector frío casi no se aplana (costa
// abrupta tipo Escocia), en el cálido las playas son anchas y llanas.
void shape_coast(TerrainData& data, const TerrainConfig& cfg) {
    Heightfield& hf = data.height;
    const float band = cfg.coast_flatten_band_m;
    const int res = hf.resolution();
    const std::vector<float> original(hf.data());
    const float inv_cell = 1.0f / hf.cell_size();
    const auto rows = row_indices(res);

    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        if (z < 1 || z >= res - 1) {
            return;
        }
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(res)
                           + static_cast<size_t>(x);
            const float rel = original[i] - cfg.sea_level;
            if (rel <= 0.0f || rel >= band) {
                continue;
            }
            const float slope = std::max(
                std::abs(original[i + 1] - original[i - 1]),
                std::abs(original[i + static_cast<size_t>(res)]
                         - original[i - static_cast<size_t>(res)]))
                * 0.5f * inv_cell;
            const float keep = smoothstep(0.35f, 0.7f, slope);
            if (keep >= 1.0f) {
                continue;
            }
            const float t = rel / band;
            // Quintic de Perlin: C2-continua en ambos extremos — sin cambio
            // de rasante donde acaba la playa.
            const float ease = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
            const float strength =
                cfg.coast_flatten_strength * (0.25f + 0.9f * data.climate[i]);
            const float compress =
                1.0f - glm::min(strength, 1.0f) * (1.0f - ease) * (1.0f - keep);
            hf.data()[i] = cfg.sea_level + rel * compress;
        }
    });
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
    data.forest_density.assign(data.material.size(), 0.0f);

    const float threshold_flow = cfg.river_flow_threshold
                               * (static_cast<float>(res) * static_cast<float>(res))
                               / (1024.0f * 1024.0f);
    const auto rows = row_indices(res);

    // Máscara de ribera: agua interior dilatada ~4 celdas. Los ríos y lagos
    // concentran humedad → bosque de galería a su alrededor.
    std::vector<std::uint8_t> riparian(data.hydrology.water_level.size(), 0);
    for (size_t i = 0; i < riparian.size(); ++i) {
        riparian[i] = std::isnan(data.hydrology.water_level[i]) ? 0 : 1;
    }
    std::vector<std::uint8_t> dilated(riparian.size());
    for (int pass = 0; pass < 2; ++pass) {
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            for (int x = 0; x < res; ++x) {
                const size_t i = static_cast<size_t>(z) * static_cast<size_t>(res)
                               + static_cast<size_t>(x);
                std::uint8_t v = riparian[i];
                for (int dz = -1; dz <= 1 && !v; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        const int nz = z + dz;
                        if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                            continue;
                        }
                        if (riparian[static_cast<size_t>(nz) * res + nx]) {
                            v = 1;
                            break;
                        }
                    }
                }
                dilated[i] = v;
            }
        });
        std::swap(riparian, dilated);
    }

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
            } else if (h < cfg.sea_level + 4.0f
                           + (cfg.beach_band_m - 2.0f) * data.climate[i]
                       && slope < 0.35f) {
                // La banda de arena crece con el clima: playones caribeños
                // en el sector cálido, orillas mínimas en el frío.
                m = Material::Sand;
            } else {
                const float snow_edge_jitter =
                    noise.fbm(p_km * 1.7f + glm::vec3{7.3f, 0.0f, 91.4f}, 3) * 60.0f;
                const float patch =
                    noise.fbm(p_km * 11.0f + glm::vec3{-3.1f, 0.0f, 17.9f}, 3);
                // Moteado fino (~30 m): rompe el borde nieve/roca en manchas.
                const float speckle =
                    noise.perlin(p_km * 33.0f + glm::vec3{58.2f, 0.0f, -4.4f});

                // Score de nieve: altitud relativa + umbría − pendiente + parches.
                float snow_score =
                    (h - (cfg.snow_line_m + snow_edge_jitter)) / 120.0f;
                snow_score += 0.30f * std::max(n.z, 0.0f); // cara norte
                // La nieve NO se acumula en paredes: penalización fuerte por
                // pendiente — los acantilados quedan de roca viva.
                snow_score -= 0.80f * slope;
                snow_score += 0.25f * patch;               // parches irregulares
                snow_score += 0.18f * speckle;             // moteado del borde

                // Treeline por debajo de la nieve; con la nieve baja, el
                // bosque conserva la mayor parte de la isla.
                const float treeline = cfg.snow_line_m - 240.0f
                    + noise.fbm(p_km * 2.3f + glm::vec3{41.0f, 0.0f, -8.5f}, 2) * 80.0f;

                // Corte físico duro: por encima de ~49° la nieve no puede
                // agarrar, da igual la altitud.
                if (snow_score > 0.0f && slope < 1.15f) {
                    m = Material::Snow;
                } else if (slope > cfg.rock_slope) {
                    m = Material::Rock;
                } else if (h > treeline) {
                    // Piso alpino: cada vez más roca al acercarse a la nieve.
                    const float alpine_t = smoothstep(treeline, cfg.snow_line_m, h);
                    m = (patch > 0.25f - 0.55f * alpine_t) ? Material::Rock
                                                           : Material::Grass;
                } else {
                    // Bosque en MASAS: capa lenta (3-6 km) + borde irregular.
                    // Los claros emergen solos donde la señal cae del umbral;
                    // la ribera de ríos y lagos siempre lleva bosque de galería.
                    const float f_bulk = noise.fbm(
                        p_km * 0.15f + glm::vec3{-29.1f, 0.0f, 63.8f}, 2, 2.5f, 0.50f);
                    const float f_edge = noise.fbm(
                        p_km * 0.45f + glm::vec3{14.3f, 0.0f, -7.2f}, 3, 2.0f, 0.48f);
                    const float forest_n =
                        (0.65f * f_bulk + 0.35f * f_edge) * 0.5f + 0.5f;
                    float forest_threshold =
                        0.44f + 0.48f * smoothstep(treeline - 350.0f, treeline, h);
                    if (riparian[i]) {
                        forest_threshold -= 0.20f;
                    }
                    // GRADIENTE, no binario: la densidad crece suave a través
                    // del umbral; el material solo etiqueta el núcleo denso.
                    const float density = glm::clamp(
                        (forest_n - forest_threshold + 0.18f) / 0.36f, 0.0f, 1.0f);
                    data.forest_density[i] = density;
                    if (density > 0.5f) {
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

    build_base_relief(data, noise, cfg);
    erode(data.height, cfg);
    thermal_erode(data.height, cfg.thermal_iterations, cfg.talus_slope);
    smooth_peaks(data.height, 2, 1.2f);
    shape_coast(data, cfg);
    data.hydrology = compute_hydrology(data.height, cfg, noise);
    classify_materials(data, noise);

    return data;
}

} // namespace terrain
