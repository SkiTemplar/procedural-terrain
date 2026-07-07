#include "terrain/generator.h"

#include "terrain/coast.h"
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
    float cliff; // 0-1: tramo de costa acantilada (sesgo al sector frío)
};

// Continentalidad + clima en un punto (compartido entre el uplift macro y la
// amplificación): bahías/penínsulas por warp compuesto, sectores de clima
// nítidos, islotes satélite concentrados en el sector cálido.
CoastSample coast_at(const Noise& noise, const TerrainConfig& cfg,
                     const glm::vec3& p_km) {
    const float half_km = cfg.size_m * 0.5f / 1000.0f;
    const glm::vec3 pw = noise.warp(
        noise.warp(p_km, 1.8f, 0.12f), cfg.coast_warp_m / 1000.0f, kWarpFreq);

    // Lóbulos macro (λ ~11 km): penínsulas y bahías de escala kilométrica.
    // El warp fino ya dibuja calas; sin esto la isla era un disco con flecos.
    const float lobe =
        noise.fbm(pw * 0.09f + glm::vec3{5.9f, 0.0f, -31.7f}, 2);
    const float d = glm::length(glm::vec2{pw.x, pw.z}) / half_km
                  + 0.22f * lobe;
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

    // El mar SIEMPRE rodea el mapa: los lóbulos no pueden empujar la tierra
    // hasta el borde del dominio.
    const float d_raw = glm::length(glm::vec2{p_km.x, p_km.z}) / half_km;
    continent *= 1.0f - smoothstep(0.88f, 1.0f, d_raw);

    // Tramos de costa acantilada: ruido lento sobre el dominio warpeado (el
    // sector sigue la forma real de la costa), sesgado al clima frío. Donde
    // la máscara es cero la costa conserva su perfil suave de siempre.
    const float cliff_raw =
        noise.fbm(pw * 0.22f + glm::vec3{23.7f, 0.0f, 71.2f}, 2) * 0.5f + 0.5f;
    // EXPOSICIÓN: los cabos y penínsulas (lóbulo negativo) reciben el oleaje
    // de frente y se acantilan aunque el sector de ruido no lo marque — así
    // la mainland siempre tiene tramos acantilados y las bahías quedan
    // resguardadas para playas.
    const float headland = smoothstep(0.0f, -0.35f, lobe);
    const float cliff = glm::clamp(smoothstep(0.48f, 0.64f, cliff_raw)
                                       + 0.55f * headland,
                                   0.0f, 1.0f)
                      * (1.0f - 0.55f * climate);

    return {continent, climate, cliff};
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
                // Tierras bajas: sectores enteros casi sin uplift (λ ~11 km)
                // → el LEM deja llanuras y colinas de pradera en vez de
                // flanco de montaña por toda la isla. El espinazo se respeta:
                // la cordillera no pierde su núcleo, pierde sus faldas.
                const float lowland = smoothstep(
                    0.52f, 0.70f,
                    noise.fbm(p_km * 0.09f + glm::vec3{-88.4f, 0.0f, 27.6f}, 2)
                            * 0.5f + 0.5f)
                    * (1.0f - 0.70f * spine);
                // En sectores de costa expuesta el uplift NO se hunde hacia
                // el mar (exponente 1.5 → 0.4): la montaña llega a la costa
                // por sí misma y el acantilado es su cara contra el agua —
                // nada de mesetas artificiales pegadas después.
                uplift[static_cast<size_t>(z) * macro_res + x] =
                    std::pow(cs.continent, 1.5f - 1.1f * cs.cliff)
                    * (0.15f + 0.85f * glm::clamp(spine * (0.75f + 0.45f * mass)
                                                      + 0.35f * mass,
                                                  0.0f, 1.0f))
                    * (1.0f - 0.92f * lowland);
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
            // Caída al fondo marino. En costa suave es el degradado de
            // siempre; en tramo acantilado TODA la caída se concentra en una
            // franja estrecha de continentalidad: la meseta conserva su cota
            // hasta la misma línea de costa y cae a pico al mar (tipo
            // Escocia). Los ríos que alcanzan el borde saltan en cascada.
            const float smooth_drop = 1.0f - cs.continent;
            // Frente DENTADO: el acantilado no sigue la costa suave — un
            // jitter de continentalidad (λ ~600 m) abre calas y adelanta
            // espolones; sin él la muralla era una regla recta y uniforme.
            const float c_face = cs.continent
                + 0.06f * cs.cliff
                      * noise.fbm(p_km * 1.7f + glm::vec3{88.8f, 0.0f, -3.4f},
                                  2);
            const float cliff_drop = 1.0f - smoothstep(0.04f, 0.09f, c_face);
            // PUERTA DE RELIEVE: el perfil abrupto solo se aplica donde el
            // LEM ya trae altura a la costa (el uplift de sector expuesto la
            // trae). Si la tierra baja toca el mar, la costa queda suave —
            // sin acantilados fake junto a praderas con lagos.
            const float cliff_w = cs.cliff * smoothstep(12.0f, 40.0f, macro);
            h -= glm::mix(smooth_drop, cliff_drop, cliff_w) * cfg.sea_depth;
            // Colinillas suaves de pradera (el LEM deja las zonas de poco
            // uplift casi planas; esto les da su ondulación). En los sectores
            // de tierras bajas (misma máscara que el uplift de Fase A) se
            // amortiguan: a plena amplitud cerraban cubetas de 5-15 m y la
            // llanura se llenaba de lagos-charco con forma de burbuja.
            const float lowland = smoothstep(
                0.52f, 0.70f,
                noise.fbm(p_km * 0.09f + glm::vec3{-88.4f, 0.0f, 27.6f}, 2)
                        * 0.5f + 0.5f);
            h += cs.continent * (1.0f - 0.65f * lowland)
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

// Paredones estratificados: en laderas ya muy empinadas la altura se remapea
// hacia bancos casi horizontales separados por saltos verticales (estratos).
// Crea paredes rocosas con cornisas donde el LEM ya puso pendiente — cumbres,
// gargantas y caras de acantilado — sin tocar valles ni praderas. Se aplica
// DESPUÉS de la erosión térmica para que el talus no desmoche los saltos.
void sharpen_crags(Heightfield& hf, const Noise& noise) {
    const int res = hf.resolution();
    const float inv_cell = 1.0f / hf.cell_size();
    const std::vector<float> base(hf.data());
    const auto rows = row_indices(res);
    // Los estratos NO pintan la alta montaña: allí la nieve remarca cada
    // banco (saltos oscuros, bancos blancos) y el patrón periódico domina
    // toda la cumbre. Se desvanecen hacia arriba y quedan para los riscos
    // de media ladera y los acantilados costeros.
    const float max_h = *std::max_element(base.begin(), base.end());
    const float fade_lo = 0.55f * max_h;
    const float fade_hi = 0.75f * max_h;

    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
        if (z < 1 || z >= res - 1) {
            return;
        }
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(res)
                           + static_cast<size_t>(x);
            const float slope = std::max(
                std::abs(base[i + 1] - base[i - 1]),
                std::abs(base[i + static_cast<size_t>(res)]
                         - base[i - static_cast<size_t>(res)]))
                * 0.5f * inv_cell;
            // Solo pendiente de roca: arranca a ~35° y es plena a ~48°. El
            // peso suave evita costuras con el terreno que queda intacto.
            const float sw = smoothstep(0.70f, 1.10f, slope)
                           * (1.0f - smoothstep(fade_lo, fade_hi, base[i]));
            if (sw < 0.02f) {
                continue;
            }
            const glm::vec3 p_km{hf.world_x(x) / 1000.0f, 0.0f,
                                 hf.world_z(z) / 1000.0f};
            // Máscara de afloramiento: los estratos salen en RODALES (crestas
            // y frentes concretos), no en manta sobre toda la montaña — en
            // manta la vista aérea era un moiré de bancales artificial.
            const float outcrop = smoothstep(
                0.58f, 0.76f,
                noise.fbm(p_km * 0.9f + glm::vec3{27.9f, 0.0f, -52.1f}, 2)
                        * 0.5f + 0.5f);
            const float w = sw * outcrop;
            if (w < 0.02f) {
                continue;
            }
            // Espesor de estrato variable y buzamiento REAL: un desnivel
            // lento de ±45 m inclina y desplaza las capas entre espolones —
            // sin él los bancos eran curvas de nivel globales y toda la
            // montaña se leía como una tarta de pisos.
            const float spacing = 55.0f
                * (1.0f + 0.35f * noise.perlin(p_km * 0.4f
                                               + glm::vec3{61.7f, 0.0f, -24.3f}));
            const float dip = 30.0f
                * noise.perlin(p_km * 0.25f + glm::vec3{33.1f, 0.0f, -78.2f})
                + 15.0f
                * noise.perlin(p_km * 0.9f + glm::vec3{-9.4f, 0.0f, 47.6f})
                // Componente media: rompe los anillos concéntricos que las
                // capas dibujaban alrededor de las cumbres cónicas.
                + 9.0f
                * noise.perlin(p_km * 2.4f + glm::vec3{72.3f, 0.0f, 14.8f});
            const float hh = base[i] + dip;
            const float band = std::floor(hh / spacing);
            const float t = hh / spacing - band;
            // Gain: aplasta el banco y concentra el salto en el centro del
            // estrato. Desplazamiento acotado a ~spacing/2 — no crea picos.
            const float shelf = smoothstep(0.38f, 0.62f, t);
            const float target = (band + shelf) * spacing - dip;
            hf.data()[i] = glm::mix(base[i], target, 0.38f * w);
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
void classify_materials(TerrainData& data, const Noise& noise,
                        const CoastExposure& coast) {
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

    // Línea de nieve ADAPTATIVA: cada seed culmina a una cota distinta; si la
    // cordillera no llega a la línea nominal, la nieve baja para coronar
    // igualmente. 0.60: las paredes Matterhorn (muy empinadas) restan mucha
    // superficie donde la nieve agarra — con 0.68 quedaba solo un 1% del
    // mapa nevado. El slider queda como tope.
    const float max_h =
        *std::max_element(hf.data().begin(), hf.data().end());
    const float snow_line = std::min(cfg.snow_line_m, 0.60f * max_h);
    const float treeline_base = snow_line - 240.0f;

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

            // Tipo de orilla por energía de oleaje (Agua v2 paso 8). La banda
            // de arena uniforme se sustituye por 4 regímenes físicos:
            //   E alta + pendiente → grava/cantos (costa expuesta batida)
            //   E alta + llano     → playa disipativa (ancha con aporte)
            //   E baja  + aporte   → marisma (sedimento fino sin resaca)
            //   E baja  sin aporte → roca lamida (nada deposita arena)
            Material shore_m = Material::Grass;
            bool is_shore = false;
            if (coast.shore_band[i] && h > cfg.sea_level) {
                const float e = coast.wave_energy[i];
                const float supply = coast.river_supply[i];
                const float rel = h - cfg.sea_level;
                // Anchura f(E, aporte, clima): NUNCA por debajo de la banda
                // original (factor >= 1) — el primer intento (0.45+...)
                // encogía las playas abrigadas a la mitad y rompía el look.
                const float width_m =
                    (4.0f + (cfg.beach_band_m - 2.0f) * data.climate[i])
                    * cfg.shore_width_scale
                    * (1.0f + 0.90f * e + 0.50f * supply);
                if (rel < width_m && slope < 0.35f) {
                    is_shore = true;
                    if (e > 0.45f) {
                        shore_m = slope > 0.18f ? Material::Gravel
                                                : Material::Sand;
                    } else if (supply > 0.30f && slope < 0.12f && rel < 2.5f) {
                        shore_m = Material::Marsh;
                    } else if (supply > 0.30f) {
                        shore_m = Material::Sand;
                    } else if (rel < 3.5f) {
                        shore_m = Material::Rock;
                    } else {
                        // Trasplaya abrigada: arena continua. Dejarla caer al
                        // clasificador general metía HIERBA en mitad de la
                        // playa (regresión visual del primer intento).
                        shore_m = Material::Sand;
                    }
                    // Ribera de río o lago dentro de la banda litoral: nunca
                    // grava/roca — el cauce conserva su orilla de arena (la
                    // marisma de desembocadura sí es deseada). Sin esto los
                    // últimos metros de los ríos quedaban flanqueados de
                    // bloques grises.
                    if (riparian[i] && (shore_m == Material::Gravel
                                        || shore_m == Material::Rock)) {
                        shore_m = Material::Sand;
                    }
                }
            }

            Material m = Material::Grass;
            if (h <= cfg.sea_level) {
                m = Material::Seabed;
            } else if (!std::isnan(wl) && h < wl - 0.05f) {
                // Sumergido bajo lago o río: lecho, nunca hierba/bosque.
                m = data.hydrology.flow[i] >= threshold_flow ? Material::RiverBed
                                                             : Material::LakeBed;
            } else if (is_shore) {
                m = shore_m;
            } else if (riparian[i] && h < 60.0f && slope < 0.35f) {
                // Orilla de lago/río de tierras bajas: franja de arena/limo.
                // Sin ella el agua cortaba la hierba a ras y los lagos de
                // pradera parecían pegados encima del prado.
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
                    (h - (snow_line + snow_edge_jitter)) / 120.0f;
                snow_score += 0.30f * std::max(n.z, 0.0f); // cara norte
                // La nieve NO se acumula en paredes: penalización fuerte por
                // pendiente — los acantilados quedan de roca viva.
                snow_score -= 0.80f * slope;
                snow_score += 0.25f * patch;               // parches irregulares
                snow_score += 0.18f * speckle;             // moteado del borde

                // Treeline por debajo de la nieve; con la nieve baja, el
                // bosque conserva la mayor parte de la isla.
                const float treeline = treeline_base
                    + noise.fbm(p_km * 2.3f + glm::vec3{41.0f, 0.0f, -8.5f}, 2) * 80.0f;

                // Corte físico duro: por encima de ~49° la nieve no puede
                // agarrar, da igual la altitud.
                if (snow_score > 0.0f && slope < 1.15f) {
                    m = Material::Snow;
                } else if (slope > cfg.rock_slope) {
                    m = Material::Rock;
                } else if (h > treeline) {
                    // Piso alpino: cada vez más roca al acercarse a la nieve.
                    const float alpine_t = smoothstep(treeline, snow_line, h);
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

TerrainData generate(const TerrainConfig& cfg,
                     const std::function<void(const char*)>& progress) {
    TerrainData data;
    data.config = cfg;
    data.height = Heightfield(cfg.resolution, cfg.size_m);

    const Noise noise(cfg.seed);
    const auto report = [&progress](const char* phase) {
        if (progress) {
            progress(phase);
        }
    };

    report("relieve (LEM)");
    build_base_relief(data, noise, cfg);
    report("erosion hidraulica");
    erode(data.height, cfg);
    report("erosion termica");
    thermal_erode(data.height, cfg.thermal_iterations, cfg.talus_slope);
    smooth_peaks(data.height, 2, 1.2f);
    sharpen_crags(data.height, noise);
    report("costa");
    shape_coast(data, cfg);
    report("hidrologia");
    data.hydrology = compute_hydrology(data.height, cfg, noise);
    report("oleaje");
    CoastExposure coast = compute_coast_exposure(data.height, data.hydrology, cfg);
    report("materiales");
    classify_materials(data, noise, coast);
    data.wave_energy = std::move(coast.wave_energy);

    return data;
}

} // namespace terrain
