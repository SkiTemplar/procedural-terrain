#pragma once

// Utilidades de rejilla y estadística de la costa, y métricas de costa rocosa
// para el dump de verificación (--dump-costa) y los tests. Solo cabecera: el
// target terrain_gen tiene la lista de fuentes cerrada.

#include "terrain/heightfield.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace terrain {

namespace grid {

struct Neighbor {
    int dx;
    int dz;
    float len; // en celdas
    bool axial;
};

inline constexpr float kSqrt2 = 1.41421356f;
// Los 4 primeros son los axiales (frente de erosión 4-conexo).
inline constexpr Neighbor kNeighbors8[8] = {
    {-1, 0, 1.0f, true},    {1, 0, 1.0f, true},      {0, -1, 1.0f, true},
    {0, 1, 1.0f, true},     {-1, -1, kSqrt2, false}, {1, -1, kSqrt2, false},
    {-1, 1, kSqrt2, false}, {1, 1, kSqrt2, false},
};

// Índice de la vecina de la celda i, o -1 fuera del dominio.
inline long long neighbor_index(int res, std::size_t i, const Neighbor& nb) {
    const int x = static_cast<int>(i % static_cast<std::size_t>(res)) + nb.dx;
    const int z = static_cast<int>(i / static_cast<std::size_t>(res)) + nb.dz;
    if (x < 0 || z < 0 || x >= res || z >= res) {
        return -1;
    }
    return static_cast<long long>(z) * res + x;
}

inline float pearson(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = a.size();
    if (n < 2 || b.size() != n) {
        return 0.0f;
    }
    double ma = 0.0;
    double mb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);
    double sab = 0.0;
    double saa = 0.0;
    double sbb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = a[i] - ma;
        const double db = b[i] - mb;
        sab += da * db;
        saa += da * da;
        sbb += db * db;
    }
    if (saa <= 0.0 || sbb <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>(sab / std::sqrt(saa * sbb));
}

// Rangos con empates promediados (para Spearman).
inline std::vector<float> ranks(const std::vector<float>& v) {
    std::vector<std::size_t> order(v.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&v](std::size_t a, std::size_t b) { return v[a] < v[b]; });
    std::vector<float> r(v.size());
    for (std::size_t i = 0; i < order.size();) {
        std::size_t j = i;
        while (j + 1 < order.size() && v[order[j + 1]] == v[order[i]]) {
            ++j;
        }
        const float avg = 0.5f * static_cast<float>(i + j);
        for (std::size_t k = i; k <= j; ++k) {
            r[order[k]] = avg;
        }
        i = j + 1;
    }
    return r;
}

inline float spearman(const std::vector<float>& a, const std::vector<float>& b) {
    return pearson(ranks(a), ranks(b));
}

} // namespace grid

// Métricas de costa rocosa.
struct RockyCoastMetrics {
    int coast_cells = 0;            // toda la tierra con mar en el vecindario 8
    float coast_energy_p50 = 0.0f;
    float coast_energy_p90 = 0.0f;
    int cliff_toe_cells = 0;        // tierra junto al mar con ≥2.5 m de pared
    int platform_cells = 0;         // −2 < h ≤ mar, pendiente < 0.05, unida a un pie
    float platform_area_m2 = 0.0f;
    float mean_platform_width_m = 0.0f; // media sobre pies con plataforma
    // Anchura (pie → borde mar adentro de la plataforma) frente a E: solo en
    // pies de acantilado, y en toda la costa (anchura 0 sin plataforma).
    float width_energy_pearson = 0.0f;
    float width_energy_spearman = 0.0f;
    float coast_width_energy_spearman = 0.0f;
    float toe_energy_p50 = 0.0f;
    float toe_energy_p90 = 0.0f;
    float toe_face_deg_p50 = 0.0f;  // inclinación del frente pie → agua
    int stacks = 0;                // islotes < 0.5 ha a < 300 m de costa rocosa
    int tall_stacks = 0;            // de ellos, con más de 3 m sobre el mar
    float stack_mean_area_m2 = 0.0f;
    float stack_max_area_m2 = 0.0f;
};

namespace rocky_detail {

inline constexpr float kCliffReliefM = 2.5f; // pared mínima sobre el mar
inline constexpr int kCliffRadius = 2;       // ventana de la pared (celdas)
inline constexpr float kPlatformMaxDepthM = 2.0f;
inline constexpr float kPlatformMaxSlope = 0.05f;
inline constexpr float kMaxPlatformM = 400.0f; // tope de búsqueda de plataforma
inline constexpr float kStackReachM = 300.0f;
inline constexpr float kStackMaxM2 = 5000.0f;
inline constexpr float kTallStackM = 3.0f;
inline constexpr std::uint16_t kUnreached = 0xFFFF;

struct Coastline {
    std::vector<std::uint32_t> toes;  // pies de acantilado
    std::vector<std::uint32_t> cells; // toda la costa
};

// Pies de acantilado: tierra con mar al lado y pared de >= 2.5 m a menos de
// dos celdas (pendiente media del frente > ~0.5: acantilado bajo).
inline Coastline find_coastline(const std::vector<float>& h, int res, float sea) {
    Coastline c;
    const auto at = [res](int x, int z) {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
             + static_cast<std::size_t>(x);
    };
    for (int z = kCliffRadius; z < res - kCliffRadius; ++z) {
        for (int x = kCliffRadius; x < res - kCliffRadius; ++x) {
            const std::size_t i = at(x, z);
            if (h[i] <= sea) {
                continue;
            }
            bool sea_next = false;
            float top = h[i];
            for (int dz = -kCliffRadius; dz <= kCliffRadius; ++dz) {
                for (int dx = -kCliffRadius; dx <= kCliffRadius; ++dx) {
                    const float hn = h[at(x + dx, z + dz)];
                    top = std::max(top, hn);
                    sea_next = sea_next
                            || (std::abs(dx) <= 1 && std::abs(dz) <= 1 && hn <= sea);
                }
            }
            if (sea_next) {
                c.cells.push_back(static_cast<std::uint32_t>(i));
            }
            if (sea_next && top >= sea + kCliffReliefM) {
                c.toes.push_back(static_cast<std::uint32_t>(i));
            }
        }
    }
    return c;
}

// Mar somero (−2 < h ≤ mar) y casi llano. La pendiente se mide solo entre
// celdas sumergidas: la pared contigua no cuenta como pendiente del fondo.
inline bool is_platform_cell(const std::vector<float>& h, int res, float cell,
                             float sea, std::size_t i) {
    if (h[i] > sea || h[i] <= sea - kPlatformMaxDepthM) {
        return false;
    }
    const int x = static_cast<int>(i % static_cast<std::size_t>(res));
    const int z = static_cast<int>(i / static_cast<std::size_t>(res));
    if (x < 1 || z < 1 || x >= res - 1 || z >= res - 1) {
        return false;
    }
    const auto axis_slope = [&](std::size_t lo, std::size_t hi) {
        const bool a = h[lo] <= sea;
        const bool b = h[hi] <= sea;
        if (a && b) {
            return (h[hi] - h[lo]) * 0.5f / cell;
        }
        if (a) {
            return (h[i] - h[lo]) / cell;
        }
        return b ? (h[hi] - h[i]) / cell : 0.0f;
    };
    const std::size_t row = static_cast<std::size_t>(res);
    const float gx = axis_slope(i - 1, i + 1);
    const float gz = axis_slope(i - row, i + row);
    return std::sqrt(gx * gx + gz * gz) < kPlatformMaxSlope;
}

// Plataforma unida a los pies (relleno desde ellos, tope kMaxPlatformM).
inline std::vector<std::uint8_t> flood_platform(const std::vector<float>& h,
                                                int res, float cell, float sea,
                                                const std::vector<std::uint32_t>& toes,
                                                int& count) {
    std::vector<std::uint8_t> on(h.size(), 0);
    std::vector<std::uint32_t> cur;
    std::vector<std::uint32_t> nxt;
    const auto visit = [&](std::uint32_t from, std::vector<std::uint32_t>& out) {
        for (const auto& nb : grid::kNeighbors8) {
            const long long sni = grid::neighbor_index(res, from, nb);
            if (sni >= 0 && !on[static_cast<std::size_t>(sni)]
                && is_platform_cell(h, res, cell, sea, static_cast<std::size_t>(sni))) {
                on[static_cast<std::size_t>(sni)] = 1;
                out.push_back(static_cast<std::uint32_t>(sni));
            }
        }
    };
    for (const std::uint32_t toe : toes) {
        visit(toe, cur);
    }
    count = 0;
    const int max_levels = static_cast<int>(kMaxPlatformM / cell);
    for (int level = 1; level <= max_levels && !cur.empty(); ++level) {
        nxt.clear();
        for (const std::uint32_t i : cur) {
            ++count;
            visit(i, nxt);
        }
        std::swap(cur, nxt);
    }
    return on;
}

// Saltos hasta el borde mar adentro de la plataforma (BFS desde el agua que
// no es plataforma). El marco del dominio no es borde de plataforma.
inline std::vector<std::uint16_t> distance_to_edge(const std::vector<float>& h,
                                                   int res, float sea,
                                                   const std::vector<std::uint8_t>& on) {
    std::vector<std::uint16_t> d(h.size(), kUnreached);
    const auto is_edge_water = [&](long long sni) {
        if (sni < 0) {
            return false;
        }
        const std::size_t ni = static_cast<std::size_t>(sni);
        const int x = static_cast<int>(ni % static_cast<std::size_t>(res));
        const int z = static_cast<int>(ni / static_cast<std::size_t>(res));
        return x > 0 && z > 0 && x < res - 1 && z < res - 1 && h[ni] <= sea
            && !on[ni];
    };
    std::vector<std::uint32_t> cur;
    std::vector<std::uint32_t> nxt;
    for (std::size_t i = 0; i < h.size(); ++i) {
        if (!on[i]) {
            continue;
        }
        for (const auto& nb : grid::kNeighbors8) {
            if (is_edge_water(grid::neighbor_index(res, i, nb))) {
                d[i] = 1;
                cur.push_back(static_cast<std::uint32_t>(i));
                break;
            }
        }
    }
    for (std::uint16_t level = 2; !cur.empty(); ++level) {
        nxt.clear();
        for (const std::uint32_t i : cur) {
            for (const auto& nb : grid::kNeighbors8) {
                const long long sni = grid::neighbor_index(res, i, nb);
                if (sni >= 0 && on[static_cast<std::size_t>(sni)]
                    && d[static_cast<std::size_t>(sni)] == kUnreached) {
                    d[static_cast<std::size_t>(sni)] = level;
                    nxt.push_back(static_cast<std::uint32_t>(sni));
                }
            }
        }
        std::swap(cur, nxt);
    }
    return d;
}

// Componentes 8-conexas de tierra: etiqueta por celda (-1 = mar) y área.
inline std::vector<int> label_land(const std::vector<float>& h, int res, float sea,
                                   std::vector<int>& area) {
    std::vector<int> label(h.size(), -1);
    std::vector<std::uint32_t> stack;
    for (std::size_t s = 0; s < h.size(); ++s) {
        if (h[s] <= sea || label[s] >= 0) {
            continue;
        }
        const int id = static_cast<int>(area.size());
        area.push_back(0);
        label[s] = id;
        stack.push_back(static_cast<std::uint32_t>(s));
        while (!stack.empty()) {
            const std::uint32_t i = stack.back();
            stack.pop_back();
            ++area[static_cast<std::size_t>(id)];
            for (const auto& nb : grid::kNeighbors8) {
                const long long sni = grid::neighbor_index(res, i, nb);
                if (sni >= 0 && h[static_cast<std::size_t>(sni)] > sea
                    && label[static_cast<std::size_t>(sni)] < 0) {
                    label[static_cast<std::size_t>(sni)] = id;
                    stack.push_back(static_cast<std::uint32_t>(sni));
                }
            }
        }
    }
    return label;
}

// Farallones: islotes pequeños a menos de 300 m de un pie de acantilado de
// una masa de tierra mayor (BFS por saltos desde esos pies).
inline void count_stacks(const std::vector<float>& h, int res, float cell,
                         float sea, const std::vector<std::uint32_t>& toes,
                         RockyCoastMetrics& m) {
    const float cell_m2 = cell * cell;
    std::vector<int> area;
    const std::vector<int> label = label_land(h, res, sea, area);
    const auto small = [&](int id) {
        return static_cast<float>(area[static_cast<std::size_t>(id)]) * cell_m2
            <= kStackMaxM2;
    };
    std::vector<std::uint8_t> near(h.size(), 0);
    std::vector<std::uint32_t> cur;
    std::vector<std::uint32_t> nxt;
    for (const std::uint32_t i : toes) {
        if (!small(label[i])) {
            near[i] = 1;
            cur.push_back(i);
        }
    }
    const int reach = static_cast<int>(kStackReachM / cell);
    for (int level = 1; level <= reach && !cur.empty(); ++level) {
        nxt.clear();
        for (const std::uint32_t i : cur) {
            for (const auto& nb : grid::kNeighbors8) {
                const long long sni = grid::neighbor_index(res, i, nb);
                if (sni >= 0 && !near[static_cast<std::size_t>(sni)]) {
                    near[static_cast<std::size_t>(sni)] = 1;
                    nxt.push_back(static_cast<std::uint32_t>(sni));
                }
            }
        }
        std::swap(cur, nxt);
    }
    std::vector<std::uint8_t> is_stack(area.size(), 0);
    std::vector<float> top(area.size(), sea);
    for (std::size_t i = 0; i < h.size(); ++i) {
        const int id = label[i];
        if (id < 0 || !small(id)) {
            continue;
        }
        top[static_cast<std::size_t>(id)] = std::max(top[static_cast<std::size_t>(id)], h[i]);
        is_stack[static_cast<std::size_t>(id)] |= near[i];
    }
    double total = 0.0;
    for (std::size_t id = 0; id < area.size(); ++id) {
        if (!is_stack[id]) {
            continue;
        }
        const float a = static_cast<float>(area[id]) * cell_m2;
        ++m.stacks;
        m.tall_stacks += top[id] - sea > kTallStackM ? 1 : 0;
        total += a;
        m.stack_max_area_m2 = std::max(m.stack_max_area_m2, a);
    }
    if (m.stacks > 0) {
        m.stack_mean_area_m2 = static_cast<float>(total / m.stacks);
    }
}

inline float percentile(std::vector<float> v, float q) {
    if (v.empty()) {
        return 0.0f;
    }
    std::sort(v.begin(), v.end());
    return v[static_cast<std::size_t>(q * static_cast<float>(v.size() - 1))];
}

} // namespace rocky_detail

inline RockyCoastMetrics measure_rocky_coast(const Heightfield& hf,
                                             const std::vector<float>& wave_energy,
                                             float sea_level) {
    using namespace rocky_detail;
    RockyCoastMetrics m;
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    const auto& h = hf.data();

    const Coastline coast = find_coastline(h, res, sea_level);
    m.cliff_toe_cells = static_cast<int>(coast.toes.size());
    m.coast_cells = static_cast<int>(coast.cells.size());

    const std::vector<std::uint8_t> on =
        flood_platform(h, res, cell, sea_level, coast.toes, m.platform_cells);
    m.platform_area_m2 = static_cast<float>(m.platform_cells) * cell * cell;
    const std::vector<std::uint16_t> to_edge = distance_to_edge(h, res, sea_level, on);
    const float max_hops = kMaxPlatformM / cell;
    // Anchura: del pie al borde mar adentro de la plataforma (robusta en
    // costas festoneadas, donde un transecto recto choca con el espolón).
    const auto width_at = [&](std::size_t i) {
        float w = 0.0f;
        for (const auto& nb : grid::kNeighbors8) {
            const long long sni = grid::neighbor_index(res, i, nb);
            if (sni >= 0 && on[static_cast<std::size_t>(sni)]) {
                const std::uint16_t d = to_edge[static_cast<std::size_t>(sni)];
                w = std::max(w, (d == kUnreached ? max_hops : static_cast<float>(d)) * cell);
            }
        }
        return w;
    };
    const auto sample = [&](const std::vector<std::uint32_t>& cells,
                            std::vector<float>& w, std::vector<float>& e) {
        for (const std::uint32_t i : cells) {
            w.push_back(width_at(i));
            e.push_back(wave_energy[i]);
        }
    };

    std::vector<float> toe_w;
    std::vector<float> toe_e;
    sample(coast.toes, toe_w, toe_e);
    double sum = 0.0;
    int with_platform = 0;
    for (const float w : toe_w) {
        sum += w;
        with_platform += w > 0.0f ? 1 : 0;
    }
    m.mean_platform_width_m =
        with_platform > 0 ? static_cast<float>(sum / with_platform) : 0.0f;
    m.width_energy_pearson = grid::pearson(toe_w, toe_e);
    m.width_energy_spearman = grid::spearman(toe_w, toe_e);
    m.toe_energy_p50 = percentile(toe_e, 0.5f);
    m.toe_energy_p90 = percentile(toe_e, 0.9f);
    // Frente: la caída más fuerte del pie a una vecina sumergida.
    std::vector<float> face_deg;
    for (const std::uint32_t i : coast.toes) {
        float steepest = 0.0f;
        for (const auto& nb : grid::kNeighbors8) {
            const long long sni = grid::neighbor_index(res, i, nb);
            if (sni >= 0 && h[static_cast<std::size_t>(sni)] <= sea_level) {
                steepest = std::max(steepest, (h[i] - h[static_cast<std::size_t>(sni)])
                                                  / (nb.len * cell));
            }
        }
        face_deg.push_back(std::atan(steepest) * 57.2957795f);
    }
    m.toe_face_deg_p50 = percentile(face_deg, 0.5f);

    // La misma relación sobre TODA la costa (anchura 0 sin plataforma): sin
    // el sesgo de selección de mirar solo pies, que solo existen donde el
    // oleaje ya superó a la roca.
    std::vector<float> coast_w;
    std::vector<float> coast_e;
    sample(coast.cells, coast_w, coast_e);
    m.coast_width_energy_spearman = grid::spearman(coast_w, coast_e);
    m.coast_energy_p50 = percentile(coast_e, 0.5f);
    m.coast_energy_p90 = percentile(coast_e, 0.9f);

    count_stacks(h, res, cell, sea_level, coast.toes, m);
    return m;
}

} // namespace terrain
