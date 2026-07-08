#include "terrain/hydrology.h"

#include "terrain/riverscape.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <vector>

namespace terrain {

namespace {

constexpr float kNoWater = std::numeric_limits<float>::quiet_NaN();
constexpr float kLakeEpsilon = 0.15f; // m de relleno para considerar lago
// Pendiente mínima del relleno (priority-flood+epsilon, Barnes 2014): sin
// ella las zonas rellenadas quedan planas, el D8 no encuentra vecino más
// bajo y la acumulación de flujo se corta — los ríos morían a trozos.
constexpr float kFillEpsilon = 0.0002f;

struct Cell {
    float height;
    int x;
    int z;
};

struct HigherThan {
    bool operator()(const Cell& a, const Cell& b) const { return a.height > b.height; }
};

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// Priority-flood (Barnes 2014, versión simple): partiendo del borde, propaga
// hacia dentro elevando cada celda al menos al nivel de su vecino más bajo ya
// procesado. Devuelve la superficie "rellenada".
std::vector<float> fill_depressions(const Heightfield& hf) {
    const int res = hf.resolution();
    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };

    std::vector<float> filled(hf.data());
    std::vector<bool> processed(filled.size(), false);
    std::priority_queue<Cell, std::vector<Cell>, HigherThan> open;

    for (int x = 0; x < res; ++x) {
        for (const int z : {0, res - 1}) {
            open.push({filled[idx(x, z)], x, z});
            processed[idx(x, z)] = true;
        }
    }
    for (int z = 1; z < res - 1; ++z) {
        for (const int x : {0, res - 1}) {
            open.push({filled[idx(x, z)], x, z});
            processed[idx(x, z)] = true;
        }
    }

    while (!open.empty()) {
        const Cell cell = open.top();
        open.pop();

        for (int n = 0; n < 8; ++n) {
            const int nx = cell.x + kDx[n];
            const int nz = cell.z + kDz[n];
            if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                continue;
            }
            const size_t ni = idx(nx, nz);
            if (processed[ni]) {
                continue;
            }
            processed[ni] = true;
            filled[ni] = std::max(filled[ni], cell.height + kFillEpsilon);
            open.push({filled[ni], nx, nz});
        }
    }
    return filled;
}

} // namespace

Hydrology compute_hydrology(Heightfield& hf, const TerrainConfig& cfg,
                            const Noise& noise) {
    const int res = hf.resolution();
    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };
    constexpr size_t kNone = std::numeric_limits<size_t>::max();

    // Sinuosidad: ondulación suave (±1.2 m, λ≈430 m) SOLO en llanuras, antes
    // del relleno — el D8 sobre la superficie ondulada serpentea de forma
    // natural en vez de trazar cauces rectos.
    {
        std::vector<float> flatness(hf.data().size(), 0.0f);
        const float inv_cell = 1.0f / hf.cell_size();
        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                const float sx = std::abs(hf.at(x + 1, z) - hf.at(x - 1, z));
                const float sz = std::abs(hf.at(x, z + 1) - hf.at(x, z - 1));
                const float slope = std::max(sx, sz) * 0.5f * inv_cell;
                flatness[idx(x, z)] =
                    1.0f - glm::clamp(slope / 0.04f, 0.0f, 1.0f);
            }
        }
        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                const size_t i = idx(x, z);
                if (flatness[i] <= 0.0f) {
                    continue;
                }
                const glm::vec3 p_km{hf.world_x(x) / 1000.0f, 0.0f,
                                     hf.world_z(z) / 1000.0f};
                hf.data()[i] += noise.fbm(p_km * 4.5f + glm::vec3{5.9f, 0.0f, -71.2f}, 2)
                              * 0.8f * flatness[i];
            }
        }
    }

    std::vector<float> filled = fill_depressions(hf);

    // Orden de proceso: de mayor a menor altura rellenada (el flujo baja).
    std::vector<size_t> order(filled.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&filled](size_t a, size_t b) { return filled[a] > filled[b]; });

    Hydrology out;
    out.flow.assign(filled.size(), 1.0f);
    out.water_level.assign(filled.size(), kNoWater);
    out.lake.assign(filled.size(), 0);

    // Flujo D8 con desempate estocástico: cuando dos vecinos bajan casi lo
    // mismo, un hash por celda decide — los cauces serpentean localmente en
    // vez de trazar rectas paralelas siguiendo el gradiente dominante.
    const auto cell_hash = [](int x, int z) {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                        + static_cast<std::uint32_t>(z) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<float>(h & 0xFFFFu) / 65535.0f;
    };

    std::vector<size_t> downstream(filled.size(), kNone);
    for (const size_t i : order) {
        const int x = static_cast<int>(i % static_cast<size_t>(res));
        const int z = static_cast<int>(i / static_cast<size_t>(res));

        float best_drop = 0.0f;
        float second_drop = 0.0f;
        size_t best = kNone;
        size_t second = kNone;
        for (int n = 0; n < 8; ++n) {
            const int nx = x + kDx[n];
            const int nz = z + kDz[n];
            if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                continue;
            }
            const float drop = filled[i] - filled[idx(nx, nz)];
            if (drop > best_drop) {
                second_drop = best_drop;
                second = best;
                best_drop = drop;
                best = idx(nx, nz);
            } else if (drop > second_drop) {
                second_drop = drop;
                second = idx(nx, nz);
            }
        }
        if (second != kNone && second_drop > 0.55f * best_drop
            && cell_hash(x, z) < 0.5f) {
            best = second;
        }
        downstream[i] = best;
        if (best != kNone) {
            out.flow[best] += out.flow[i];
        }
    }


    // ---- Ríos ----
    // El umbral se normaliza: expresado en celdas de un mapa 1024, escala con
    // el área real para que el slider signifique lo mismo a cualquier resolución.
    const float threshold = cfg.river_flow_threshold
                          * (static_cast<float>(res) * static_cast<float>(res))
                          / (1024.0f * 1024.0f);

    // 1) Incisión fluvial (stream power law, esquema implícito de Braun &
    //    Willett 2013): cada celda se erosiona hacia su receptora con fuerza
    //    F ∝ √(caudal). Procesando de mar a cumbre (orden ascendente) el
    //    esquema es O(n) e incondicionalmente estable. Resultado: perfiles
    //    longitudinales cóncavos (sin caídas verticales), valles en V y
    //    cañones donde coinciden caudal y pendiente.
    for (int it = 0; it < cfg.fluvial_iterations; ++it) {
        for (auto rit = order.rbegin(); rit != order.rend(); ++rit) {
            const size_t i = *rit;
            const size_t down = downstream[i];
            if (down == kNone) {
                continue;
            }
            float& h = hf.data()[i];
            const float h_down = hf.data()[down];
            if (h <= h_down || h <= cfg.sea_level - 2.0f) {
                continue; // solo talla, nunca eleva
            }
            // Suelo relativo: el lecho queda siempre por encima del lecho
            // aguas abajo — sin tajos excavados hasta la cota del mar.
            const float min_bed = cfg.sea_level + 1.5f
                                + (h_down - cfg.sea_level) * 0.08f;
            if (h <= min_bed) {
                continue;
            }
            // Stream power completo K·A^m·S^n (m=0.5, n=0.7, Whipple &
            // Tucker): la pendiente manda — gargantas y cañones en montaña,
            // incisión casi nula en llanura.
            const int xi = static_cast<int>(i % static_cast<size_t>(res));
            const int zi = static_cast<int>(i / static_cast<size_t>(res));
            const int xd = static_cast<int>(down % static_cast<size_t>(res));
            const int zd = static_cast<int>(down / static_cast<size_t>(res));
            const float dist = hf.cell_size()
                             * ((xi != xd && zi != zd) ? 1.4142f : 1.0f);
            const float slope = (h - h_down) / dist;
            const float f = std::min(
                cfg.fluvial_strength * std::sqrt(out.flow[i] / threshold)
                    * std::pow(slope + 1e-4f, 0.7f),
                1.6f);
            h = std::max((h + f * h_down) / (1.0f + f), min_bed);
        }
    }

    // ---- Sobreexcavación glaciar ----
    // Lagos de montaña EMERGENTES: en los tramos altos y TENDIDOS de los
    // valles con caudal, el hielo sobreexcavó pozas a lo largo del cauce
    // alternadas con umbrales (paternoster). Se talla el terreno real y el
    // priority-flood posterior decide dónde queda agua y hasta qué cota — la
    // orilla sigue las curvas del valle. Nada de cubetas estampadas: las
    // versiones anteriores colocaban circos por ruido y salían lagos
    // colgados en las crestas.
    const float terrain_max =
        *std::max_element(hf.data().begin(), hf.data().end());
    {
        // Tras la incisión fluvial a propósito: es la que crea los fondos de
        // valle tendidos donde el hielo pudo embalsar.
        const float glacial_flow = 0.20f * threshold;
        const float inv_cell = 1.0f / hf.cell_size();
        // Brocha de ~16 m: en mapas gruesos (tests a 128²) es sub-celda y
        // solo haría agujeros desproporcionados — el pase se apaga.
        const int rc = static_cast<int>(std::round(16.0f * inv_cell));
        if (terrain_max > 400.0f && rc >= 3) {
            const float lo = 0.30f * terrain_max;
            const float hi = 0.80f * terrain_max;
            const std::vector<float> snapshot(hf.data());
            for (int z = 2; z < res - 2; ++z) {
                for (int x = 2; x < res - 2; ++x) {
                    const size_t i = idx(x, z);
                    if (out.flow[i] < glacial_flow) {
                        continue; // solo donde el valle lleva caudal
                    }
                    const float h0 = snapshot[i];
                    if (h0 < lo || h0 > hi) {
                        continue; // banda glaciar
                    }
                    // Pendiente A LO LARGO del cauce (hacia la receptora):
                    // la transversal incluye las paredes del valle en V y
                    // vetaba todos los candidatos.
                    const size_t down = downstream[i];
                    if (down == kNone) {
                        continue;
                    }
                    const int xd = static_cast<int>(down % static_cast<size_t>(res));
                    const int zd = static_cast<int>(down / static_cast<size_t>(res));
                    const float dist = hf.cell_size()
                        * ((x != xd && z != zd) ? 1.4142f : 1.0f);
                    const float s = (snapshot[i] - snapshot[down]) / dist;
                    if (s > 0.12f) {
                        continue; // en cascada el agua no se embalsa
                    }
                    // Pulso CORTO a lo largo del valle (λ ~400 m): poza o
                    // umbral. Con λ larga el gradiente de la excavación no
                    // superaba la pendiente del valle y la poza no cerraba
                    // cubeta — profundizaba el cauce sin embalsar nada.
                    const glm::vec3 p_km{hf.world_x(x) / 1000.0f, 0.0f,
                                         hf.world_z(z) / 1000.0f};
                    const float pulse = noise.fbm(
                        p_km * 2.5f + glm::vec3{-14.7f, 0.0f, 83.2f}, 2);
                    if (pulse <= 0.20f) {
                        continue;
                    }
                    // Cuanto más llano el tramo, más honda la poza respecto a
                    // su pendiente — es lo que permite cerrar la cubeta.
                    const float depth =
                        (2.5f + 5.5f * glm::clamp((pulse - 0.20f) / 0.35f,
                                                  0.0f, 1.0f))
                        * (1.0f - s / 0.12f);
                    if (depth < 1.5f) {
                        continue;
                    }
                    for (int dz = -rc; dz <= rc; ++dz) {
                        for (int dx = -rc; dx <= rc; ++dx) {
                            const int nx = x + dx;
                            const int nz = z + dz;
                            if (nx < 1 || nz < 1 || nx >= res - 1
                                || nz >= res - 1) {
                                continue;
                            }
                            const float r2 =
                                static_cast<float>(dx * dx + dz * dz)
                                / static_cast<float>(rc * rc);
                            if (r2 > 1.0f) {
                                continue;
                            }
                            const size_t ni = idx(nx, nz);
                            const float target =
                                snapshot[ni] - depth * (1.0f - r2);
                            if (hf.data()[ni] > target) {
                                hf.data()[ni] = target;
                            }
                        }
                    }
                }
            }
        }
    }
    // La incisión (y la sobreexcavación) han cambiado el terreno desde el
    // primer relleno: recalcular SIEMPRE antes de detectar lagos. Con el
    // relleno viejo, cada cauce tallado quedaba por debajo de él y salía una
    // tira de "lago" fantasma a lo largo de todos los ríos.
    filled = fill_depressions(hf);

    // Lagos: donde el relleno superó el suelo original.
    for (size_t i = 0; i < filled.size(); ++i) {
        if (filled[i] > hf.data()[i] + kLakeEpsilon && filled[i] > cfg.sea_level) {
            out.water_level[i] = filled[i];
        }
    }

    // Purga de microlagos: la erosión deja miles de charcos de 1-3 celdas que
    // ensucian el mapa. Se eliminan las componentes conexas pequeñas o someras.
    {
        constexpr int kMinLakeCells = 60;
        constexpr float kMinLakeDepth = 1.2f;
        int mountain_lakes_logged = 0;

        std::vector<bool> visited(filled.size(), false);
        std::vector<size_t> component;
        std::vector<size_t> stack;

        for (size_t start = 0; start < filled.size(); ++start) {
            if (visited[start] || std::isnan(out.water_level[start])) {
                continue;
            }
            component.clear();
            stack.assign(1, start);
            visited[start] = true;
            float max_depth = 0.0f;

            while (!stack.empty()) {
                const size_t i = stack.back();
                stack.pop_back();
                component.push_back(i);
                max_depth = std::max(max_depth, out.water_level[i] - hf.data()[i]);

                const int x = static_cast<int>(i % static_cast<size_t>(res));
                const int z = static_cast<int>(i / static_cast<size_t>(res));
                for (int n = 0; n < 8; ++n) {
                    const int nx = x + kDx[n];
                    const int nz = z + kDz[n];
                    if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                        continue;
                    }
                    const size_t ni = idx(nx, nz);
                    if (!visited[ni] && !std::isnan(out.water_level[ni])) {
                        visited[ni] = true;
                        stack.push_back(ni);
                    }
                }
            }

            if (static_cast<int>(component.size()) < kMinLakeCells
                || max_depth < kMinLakeDepth) {
                for (const size_t i : component) {
                    out.water_level[i] = kNoWater;
                }
            } else {
                for (const size_t i : component) {
                    out.lake[i] = 1;
                }
                // Verificable sin volar a ciegas: los lagos de montaña son
                // emergentes y encontrarlos a mano cuesta más que generarlos.
                if (out.water_level[start] > 0.28f * terrain_max
                    && mountain_lakes_logged < 8) {
                    const int lx = static_cast<int>(start % static_cast<size_t>(res));
                    const int lz = static_cast<int>(start / static_cast<size_t>(res));
                    std::cout << "  lago de montana @ ("
                              << static_cast<int>(hf.world_x(lx)) << ", "
                              << static_cast<int>(hf.world_z(lz)) << ") cota "
                              << static_cast<int>(out.water_level[start])
                              << " m (" << component.size() << " celdas)\n";
                    ++mountain_lakes_logged;
                }
            }
        }
    }

    // ---- Agua v2: efluentes de lagos + perfil monótono ----
    // Cada lago desagua por su saddle con caudal heredado (el efluente nace
    // río hecho, no arroyo); después se eliminan las contrapendientes del
    // lecho para que ningún cauce quede embalsado por bultos residuales.
    out.whitewater.assign(filled.size(), 0);
    std::vector<std::uint8_t> from_lake(filled.size(), 0);
    connect_lake_effluents(hf, out, downstream, threshold, cfg.sea_level,
                           from_lake);
    enforce_monotone_beds(hf, out, order, downstream, threshold, cfg.sea_level);

    const std::vector<float> bed0(hf.data()); // lecho tras la incisión

    const auto is_river = [&](size_t i) {
        return out.flow[i] >= threshold && bed0[i] > cfg.sea_level;
    };

    // 2) Nivel de agua sobre el LECHO final (no sobre el terreno sin tallar:
    //    eso dejaba el agua flotando). Profundidad según caudal, y monótono
    //    decreciente aguas abajo.
    std::vector<float> level(filled.size(), std::numeric_limits<float>::infinity());
    // Geometría hidráulica (Leopold & Maddock PP-252): anchura y profundidad
    // crecen como potencias DISTINTAS del caudal — w ∝ Q^0.4, d ∝ Q^0.3 —
    // normalizadas al caudal MÁXIMO del mapa. Solo el río principal cerca de
    // la desembocadura abre el cauce pleno; los tributarios escalan solos.
    float max_flow = threshold;
    for (size_t i = 0; i < out.flow.size(); ++i) {
        if (is_river(i)) {
            max_flow = std::max(max_flow, out.flow[i]);
        }
    }
    const auto width_of = [&](size_t i) {
        return std::pow(glm::clamp(out.flow[i] / max_flow, 0.0f, 1.0f), 0.4f);
    };
    const auto depth_of = [&](size_t i) {
        return std::pow(glm::clamp(out.flow[i] / max_flow, 0.0f, 1.0f), 0.3f);
    };

    // Los ríos nacen gradualmente: cauce y lámina crecen desde cero durante
    // el primer tramo (rampa hasta 3× el umbral — física: duplicar el área
    // de drenaje mínima). EXCEPCIÓN: el efluente de un lago nace río hecho —
    // hereda el caudal de toda la cuenca embalsada.
    const auto birth_of = [&](size_t i) {
        if (from_lake[i]) {
            return 1.0f;
        }
        return glm::clamp((out.flow[i] / threshold - 1.0f) / 2.0f, 0.0f, 1.0f);
    };
    const auto water_depth_of = [&](size_t i) {
        return (0.35f + 1.1f * depth_of(i)) * birth_of(i);
    };

    for (const size_t i : order) { // de arriba a abajo
        if (!is_river(i)) {
            continue;
        }
        if (level[i] == std::numeric_limits<float>::infinity()) {
            level[i] = bed0[i] + water_depth_of(i);
        }
        // Los lagos imponen su lámina.
        if (!std::isnan(out.water_level[i])) {
            level[i] = out.water_level[i];
        }
        const size_t down = downstream[i];
        if (down != kNone && is_river(down)) {
            const float own_down = bed0[down] + water_depth_of(down);
            const float candidate = std::min(level[i], own_down);
            if (level[down] == std::numeric_limits<float>::infinity()
                || candidate < level[down]) {
                level[down] = candidate;
            }
        }
    }

    // 3) Anchura del cauce y llanura aluvial respecto al lecho de la incisión.
    const int max_radius = std::max(
        1, static_cast<int>(std::round(cfg.river_max_width_m / hf.cell_size())));

    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = idx(x, z);
            if (!is_river(i) || level[i] == std::numeric_limits<float>::infinity()) {
                continue;
            }
            // Dentro de un lago no se talla cauce ni se pinta lámina de río:
            // el lago ya tiene su superficie de equilibrio (vertedero). Sin
            // esto, el cauce interior pintaba la superficie del lago en un
            // radio que desbordaba la orilla → agua flotando en la ladera
            // de salida. El desagüe arranca desde el lecho de la presa con
            // el grosor normal del río.
            if (out.lake[i]) {
                continue;
            }
            const float width_f = width_of(i);
            const float depth_f = depth_of(i);
            // El nacimiento escala TODO el cauce (profundidad y anchura), no
            // solo la lámina: sin esto el canal tallado aparecía de golpe.
            const float birth = birth_of(i);
            if (birth < 0.05f) {
                continue; // aún no hay cauce visible
            }
            // Régimen hidráulico: en pendiente el canal es ESTRECHO y
            // encajonado (garganta/rápidos — p.ej. el desagüe de un lago de
            // montaña); solo en llanura el río se abre a su anchura plena.
            const float local_slope = std::max(
                std::abs(bed0[i + 1] - bed0[i - 1]),
                std::abs(bed0[i + static_cast<size_t>(res)]
                         - bed0[i - static_cast<size_t>(res)]))
                * 0.5f / hf.cell_size();
            const float narrow =
                glm::clamp(1.0f / (1.0f + 6.0f * local_slope), 0.2f, 1.0f);
            const float depth =
                cfg.river_max_depth_m * (0.15f + 0.85f * depth_f) * birth;
            const int radius = std::max(
                1, static_cast<int>(
                       std::round(max_radius * width_f * birth * narrow)));
            const float surface = level[i];
            // El cauce corta su salida en los vertederos, pero SOLO hasta
            // ~6 m de garganta: sin límite, excavaba zanjas de decenas de
            // metros de paredes verticales atravesando laderas. Si el
            // vertedero es más alto, lo que hay ahí es un lago, no un tajo.
            const float bed_target = bed0[i] - depth * 0.4f;
            const float axis_bed =
                std::max(std::min(bed_target, surface - 0.5f * birth),
                         bed_target - 6.0f);

            // Llanura aluvial: los ríos grandes abren un valle ancho y somero
            // SOLO en terreno llano (los valles gigantes se forman abajo, no
            // en la caída).
            if (width_f > 0.35f && local_slope < 0.05f) {
                const int flood_radius = radius * 4;
                for (int dz = -flood_radius; dz <= flood_radius; ++dz) {
                    for (int dx = -flood_radius; dx <= flood_radius; ++dx) {
                        const int nx = x + dx;
                        const int nz = z + dz;
                        if (nx < 1 || nz < 1 || nx >= res - 1 || nz >= res - 1) {
                            continue;
                        }
                        const float r =
                            std::sqrt(static_cast<float>(dx * dx + dz * dz))
                            / static_cast<float>(flood_radius);
                        if (r > 1.0f) {
                            continue;
                        }
                        const size_t ni = idx(nx, nz);
                        const float target =
                            surface + 0.6f + (hf.data()[ni] - surface) * (r * r);
                        if (hf.data()[ni] > target
                            && hf.data()[ni] < surface + 25.0f) {
                            hf.data()[ni] = target;
                        }
                    }
                }
            }

            for (int dz = -radius - 1; dz <= radius + 1; ++dz) {
                for (int dx = -radius - 1; dx <= radius + 1; ++dx) {
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (nx < 1 || nz < 1 || nx >= res - 1 || nz >= res - 1) {
                        continue;
                    }
                    const float r = std::sqrt(static_cast<float>(dx * dx + dz * dz))
                                  / static_cast<float>(radius);
                    const size_t ni = idx(nx, nz);

                    if (r <= 1.0f) {
                        // Lecho: hondo en el eje, sube hacia la orilla.
                        const float bed = axis_bed + depth * 0.4f * (r * r);
                        if (hf.data()[ni] > bed) {
                            hf.data()[ni] = bed;
                        }
                    }
                    // Lámina de agua justo por encima del lecho local. Regla
                    // de MÍNIMO: el agua se asienta en el nivel más bajo del
                    // canal cercano — con máximo, las celdas aguas arriba
                    // pintaban su nivel alto sobre las de abajo y el agua
                    // quedaba flotando en escalones.
                    if (r <= 1.15f && !out.lake[ni] && birth > 0.1f) {
                        float candidate = surface;
                        if (level[ni] != std::numeric_limits<float>::infinity()) {
                            candidate = std::min(candidate, level[ni]);
                        }
                        if (hf.data()[ni] < candidate - 0.05f) {
                            out.water_level[ni] = std::isnan(out.water_level[ni])
                                ? candidate
                                : std::min(out.water_level[ni], candidate);
                            // Socavón garantizado: toda celda con lámina tiene
                            // cauce debajo — sin agua "pasando por encima".
                            hf.data()[ni] =
                                std::min(hf.data()[ni], candidate - 0.35f);
                        }
                    }
                }
            }
        }
    }

    // ---- Agua v2: cascadas y rápidos ----
    // Sobre el perfil z(s) de cada río: los knickpoints se colapsan en
    // rellano + pared + plunge pool y los tramos de rápido reciben
    // micro-escalones + flag whitewater. Después del tallado: la zona
    // remodelada reasienta su propia lámina.
    carve_cascades(hf, out, order, downstream, threshold, max_flow,
                   max_radius, cfg.sea_level);
    // El tallado y los micro-escalones pueden dejar contrapendientes de
    // decimales en las costuras (frontera de zona de rápidos, confluencias):
    // segunda pasada de monotonía, que solo talla.
    enforce_monotone_beds(hf, out, order, downstream, threshold, cfg.sea_level);

    // ---- Agua v2: confluencias, deltas e islotes ----
    // Poza de socavación + espigón de grava en cada unión madura; lóbulos
    // deltaicos con distributarios en las bocas grandes de costa tendida;
    // barras fluviales emergentes en los tramos anchos y tendidos.
    const RiverscapeParams rsp{threshold, max_flow, max_radius, cfg.sea_level,
                               cfg.seed};
    smooth_confluences(hf, out, order, downstream, rsp);
    build_deltas(hf, out, order, downstream, rsp);
    stamp_channel_bars(hf, out, order, downstream, rsp);

    // 4) Tensión de superficie: la lámina no cae a MUROS verticales. En los
    // vertederos de lago y knickpoints NO declarados el nivel caía varios
    // metros en una celda y el agua se dibujaba como una pared azul cruzando
    // el río. Donde el nivel cae más de ~40° hacia la receptora, se reparte
    // en una rampa de cascada a lo largo del flujo (procesando de cumbre a
    // mar, las rampas largas se encadenan solas). Las CASCADAS declaradas
    // (whitewater == 2) quedan exentas: ahí la lámina cae en vertical con la
    // pared, que para eso tiene poza debajo.
    {
        const float max_ws_slope = 0.85f; // tan de la lámina más empinada
        // Tope físico de la rampa: la lámina nunca se aleja más de esto de su
        // nivel original (≈ profundidad de poza). Sin tope, en tramos
        // empinados largos NO declarados cascada la rampa se encadenaba celda
        // a celda y el agua acababa volando 5-10 m sobre el lecho (las
        // "torres de agua" del feedback 2026-07-08). Donde el tope recorta,
        // el agua baja rota siguiendo el lecho: eso son rápidos (whitewater).
        const float max_ramp_lift = 2.0f;
        for (const size_t i : order) {
            const size_t down = downstream[i];
            if (down == kNone || out.lake[down]) {
                continue; // el lago impone su propia lámina
            }
            if (out.whitewater[i] == 2 || out.whitewater[down] == 2) {
                continue; // cascada declarada: caída vertical legítima
            }
            const float li = out.water_level[i];
            const float ld = out.water_level[down];
            if (std::isnan(li) || std::isnan(ld)) {
                continue;
            }
            const int xi = static_cast<int>(i % static_cast<size_t>(res));
            const int zi = static_cast<int>(i / static_cast<size_t>(res));
            const int xd = static_cast<int>(down % static_cast<size_t>(res));
            const int zd = static_cast<int>(down / static_cast<size_t>(res));
            const float dist = hf.cell_size()
                             * ((xi != xd && zi != zd) ? 1.4142f : 1.0f);
            const float max_drop = max_ws_slope * dist;
            if (li - ld > max_drop) {
                const float ramped = li - max_drop;
                if (ramped - ld > max_ramp_lift) {
                    out.water_level[down] = ld + max_ramp_lift;
                    out.whitewater[down] =
                        std::max(out.whitewater[down], std::uint8_t{1});
                } else {
                    out.water_level[down] = ramped;
                }
            }
        }
    }

    // 4.5) Suavizado de la lámina de río: promedia cada celda de agua NO-lago
    // con sus vecinas de agua (3 pasadas). La superficie de río venía dentada
    // celda a celda (los distintos escritores de riverscape dejaban escalones
    // que se renderizaban como "picos de vértice hacia arriba" — feedback
    // 2026-07-08). Los lagos ya son planos por construcción y se conservan
    // intactos; el promediado de una secuencia monótona (río descendente) la
    // preserva y solo lima los escalones/picos.
    {
        std::vector<float> smooth = out.water_level;
        for (int pass = 0; pass < 3; ++pass) {
            for (int z = 1; z < res - 1; ++z) {
                for (int x = 1; x < res - 1; ++x) {
                    const size_t i = idx(x, z);
                    if (std::isnan(out.water_level[i]) || out.lake[i]) {
                        continue;
                    }
                    float sum = out.water_level[i];
                    int cnt = 1;
                    for (int n = 0; n < 8; ++n) {
                        const size_t ni = idx(x + kDx[n], z + kDz[n]);
                        if (!std::isnan(out.water_level[ni])) {
                            // Los lagos anclan (peso alto): la orilla del río
                            // se funde a la cota del lago sin escalón.
                            const int w = out.lake[ni] ? 3 : 1;
                            sum += static_cast<float>(w) * out.water_level[ni];
                            cnt += w;
                        }
                    }
                    smooth[i] = sum / static_cast<float>(cnt);
                }
            }
            for (size_t i = 0; i < out.water_level.size(); ++i) {
                if (!std::isnan(out.water_level[i]) && !out.lake[i]) {
                    out.water_level[i] = smooth[i];
                }
            }
        }
    }

    // Invariante final de lámina: fuera de lagos, ningún cauce lleva más de
    // max_sheet de agua sobre su propio lecho ni menos de un mínimo (para que
    // no se hunda bajo el terreno tras el suavizado y deje huecos). Cierra la
    // clase entera de "torres de agua" sin perseguir cada costura de pasada.
    {
        const float max_sheet = 3.5f;
        const float min_sheet = 0.12f;
        for (size_t i = 0; i < out.water_level.size(); ++i) {
            if (std::isnan(out.water_level[i]) || out.lake[i]) {
                continue;
            }
            out.water_level[i] = std::clamp(out.water_level[i],
                                            hf.data()[i] + min_sheet,
                                            hf.data()[i] + max_sheet);
        }
    }

    // Limpieza final: sin celdas de agua huérfanas (el nacimiento de un río
    // puede pintar una única celda suelta que se ve como un píxel de agua).
    for (int z = 1; z < res - 1; ++z) {
        for (int x = 1; x < res - 1; ++x) {
            const size_t i = idx(x, z);
            if (std::isnan(out.water_level[i])) {
                continue;
            }
            bool has_neighbor = false;
            for (int n = 0; n < 8 && !has_neighbor; ++n) {
                has_neighbor =
                    !std::isnan(out.water_level[idx(x + kDx[n], z + kDz[n])]);
            }
            if (!has_neighbor) {
                out.water_level[i] = kNoWater;
            }
        }
    }

    return out;
}

} // namespace terrain
