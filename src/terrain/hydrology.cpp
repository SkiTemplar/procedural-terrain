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

Hydrology compute_hydrology(Heightfield& hf, const TerrainConfig& cfg) {
    const int res = hf.resolution();
    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };
    constexpr size_t kNone = std::numeric_limits<size_t>::max();

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

    // Flujo D8: cada celda vierte a su vecino más bajo. Se guarda el índice
    // aguas abajo para poder recorrer los cauces después.
    std::vector<size_t> downstream(filled.size(), kNone);
    for (const size_t i : order) {
        const int x = static_cast<int>(i % static_cast<size_t>(res));
        const int z = static_cast<int>(i / static_cast<size_t>(res));

        float best_drop = 0.0f;
        size_t best = kNone;
        for (int n = 0; n < 8; ++n) {
            const int nx = x + kDx[n];
            const int nz = z + kDz[n];
            if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                continue;
            }
            const float drop = filled[i] - filled[idx(nx, nz)];
            if (drop > best_drop) {
                best_drop = drop;
                best = idx(nx, nz);
            }
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
            const float f = cfg.fluvial_strength
                          * std::sqrt(out.flow[i] / threshold);
            h = (h + f * h_down) / (1.0f + f);
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
    const auto intensity_of = [&](size_t i) {
        return std::min(std::log(out.flow[i] / threshold + 1.0f) / 3.0f, 1.0f);
    };

    // Los ríos nacen gradualmente: la lámina crece de 0 durante el primer
    // tramo tras superar el umbral (nada de arroyos que aparecen de la nada).
    const auto water_depth_of = [&](size_t i) {
        const float birth = glm::clamp(
            (out.flow[i] / threshold - 1.0f) / 1.5f, 0.0f, 1.0f);
        return (0.35f + 1.1f * intensity_of(i)) * birth;
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
            const float depth = cfg.river_max_depth_m * (0.35f + 0.65f * intensity);
            const int radius = std::max(
                1, static_cast<int>(std::round(max_radius * intensity)));
            const float surface = level[i];
            // El cauce siempre queda bajo la lámina: donde el lecho subía,
            // el río corta su salida (canal continuo, sin tramos secos).
            const float axis_bed =
                std::min(bed0[i] - depth * 0.4f, surface - 0.5f);

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
                    if (r <= 1.15f && !out.lake[ni]) {
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

    return out;
}

} // namespace terrain
