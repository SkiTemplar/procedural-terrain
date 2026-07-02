#include "terrain/hydrology.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>
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

    const std::vector<float> filled = fill_depressions(hf);

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

    // Lagos: donde el relleno superó el suelo original.
    for (size_t i = 0; i < filled.size(); ++i) {
        if (filled[i] > hf.data()[i] + kLakeEpsilon && filled[i] > cfg.sea_level) {
            out.water_level[i] = filled[i];
        }
    }

    // Purga de microlagos: la erosión deja miles de charcos de 1-3 celdas que
    // ensucian el mapa. Se eliminan las componentes conexas pequeñas o someras.
    {
        constexpr int kMinLakeCells = 24;
        constexpr float kMinLakeDepth = 0.6f;

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
            }
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

    const std::vector<float> bed0(hf.data()); // lecho tras la incisión

    const auto is_river = [&](size_t i) {
        return out.flow[i] >= threshold && bed0[i] > cfg.sea_level;
    };

    // 2) Nivel de agua sobre el LECHO final (no sobre el terreno sin tallar:
    //    eso dejaba el agua flotando). Profundidad según caudal, y monótono
    //    decreciente aguas abajo.
    std::vector<float> level(filled.size(), std::numeric_limits<float>::infinity());
    // Contraste de caudal marcado: el río principal es claramente más ancho
    // y hondo que sus tributarios (jerarquía visible).
    const auto intensity_of = [&](size_t i) {
        return std::min(std::log(out.flow[i] / threshold + 1.0f) / 2.2f, 1.0f);
    };

    // Los ríos nacen gradualmente: cauce y lámina crecen desde cero durante
    // el primer tramo (rampa hasta 3× el umbral — física: duplicar el área
    // de drenaje mínima).
    const auto birth_of = [&](size_t i) {
        return glm::clamp((out.flow[i] / threshold - 1.0f) / 2.0f, 0.0f, 1.0f);
    };
    const auto water_depth_of = [&](size_t i) {
        return (0.35f + 1.1f * intensity_of(i)) * birth_of(i);
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
            const float intensity = intensity_of(i);
            // El nacimiento escala TODO el cauce (profundidad y anchura), no
            // solo la lámina: sin esto el canal tallado aparecía de golpe.
            const float birth = birth_of(i);
            if (birth < 0.05f) {
                continue; // aún no hay cauce visible
            }
            const float depth =
                cfg.river_max_depth_m * (0.15f + 0.85f * intensity) * birth;
            const int radius = std::max(
                1, static_cast<int>(std::round(max_radius * intensity * birth)));
            const float surface = level[i];
            // El cauce siempre queda bajo la lámina: donde el lecho subía,
            // el río corta su salida (canal continuo, sin tramos secos).
            const float axis_bed =
                std::min(bed0[i] - depth * 0.4f, surface - 0.5f * birth);

            // Llanura aluvial: los ríos grandes abren un valle ancho y somero.
            if (intensity > 0.35f) {
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
