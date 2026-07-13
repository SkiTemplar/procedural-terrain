#include "terrain/riverscape.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace terrain {

namespace {

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

constexpr float kNoWater = std::numeric_limits<float>::quiet_NaN();

// Caudal garantizado del efluente (× umbral de río): visible pero estrecho.
// Q_out = ΣQ_in ya lo da el D8 sobre la superficie rellenada; este floor solo
// cubre lagos cuya cuenca queda por debajo del umbral de render.
constexpr float kEffluentFlow = 1.2f;

float step_dist(std::size_t a, std::size_t b, int res, float cell) {
    const int xa = static_cast<int>(a % static_cast<std::size_t>(res));
    const int za = static_cast<int>(a / static_cast<std::size_t>(res));
    const int xb = static_cast<int>(b % static_cast<std::size_t>(res));
    const int zb = static_cast<int>(b / static_cast<std::size_t>(res));
    return cell * ((xa != xb && za != zb) ? 1.4142f : 1.0f);
}

// Radio de cauce en celdas (misma ley w ∝ Q^0.4 del tallado de canales).
int channel_radius(const Hydrology& out, std::size_t i, float max_flow,
                   int max_radius) {
    return glm::clamp(
        static_cast<int>(std::round(
            static_cast<float>(max_radius)
            * std::pow(glm::clamp(out.flow[i] / max_flow, 0.0f, 1.0f), 0.4f))),
        1, std::max(1, max_radius));
}

// Hash entero → [0,1): determinismo de deltas e islotes sin <random>.
std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float hash01(std::uint32_t& state) {
    state = hash32(state + 0x9e3779b9u);
    return static_cast<float>(state & 0xFFFFFFu) / 16777215.0f;
}

glm::vec2 rotate2(const glm::vec2& v, float rad) {
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

} // namespace

void connect_lake_effluents(Heightfield& hf, Hydrology& out,
                            const std::vector<std::size_t>& downstream,
                            float river_threshold, float sea_level,
                            std::vector<std::uint8_t>& from_lake) {
    const int res = hf.resolution();
    const std::size_t total = out.lake.size();
    std::vector<bool> visited(total, false);
    std::vector<std::size_t> component;
    std::vector<std::size_t> stack;

    for (std::size_t start = 0; start < total; ++start) {
        if (!out.lake[start] || visited[start]) {
            continue;
        }
        // Componente conexa del lago + su cota de lámina.
        component.clear();
        stack.assign(1, start);
        visited[start] = true;
        float lake_level = out.water_level[start];
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            component.push_back(i);
            lake_level = std::max(lake_level, out.water_level[i]);
            const int x = static_cast<int>(i % static_cast<std::size_t>(res));
            const int z = static_cast<int>(i / static_cast<std::size_t>(res));
            for (int n = 0; n < 8; ++n) {
                const int nx = x + kDx[n];
                const int nz = z + kDz[n];
                if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                    continue;
                }
                const std::size_t ni =
                    static_cast<std::size_t>(nz) * static_cast<std::size_t>(res)
                    + static_cast<std::size_t>(nx);
                if (out.lake[ni] && !visited[ni]) {
                    visited[ni] = true;
                    stack.push_back(ni);
                }
            }
        }

        // Salida: la receptora D8 EXTERIOR con más caudal (el saddle que el
        // priority-flood usó para drenar la cubeta).
        std::size_t exit = kNoDownstream;
        float exit_flow = 0.0f;
        for (const std::size_t i : component) {
            const std::size_t d = downstream[i];
            if (d == kNoDownstream || out.lake[d]) {
                continue;
            }
            if (out.flow[d] > exit_flow) {
                exit_flow = out.flow[d];
                exit = d;
            }
        }
        if (exit == kNoDownstream) {
            continue; // lago pegado al borde del dominio: sin efluente
        }

        // Muesca del labio + caudal heredado hasta el mar (o el próximo lago,
        // que encadena con su propio efluente).
        float notch = lake_level - 0.7f;
        std::size_t j = exit;
        std::size_t guard = 0;
        const std::size_t max_steps = total;
        while (j != kNoDownstream && !out.lake[j]
               && hf.data()[j] > sea_level - 1.0f && guard < max_steps) {
            from_lake[j] = 1;
            out.flow[j] = std::max(out.flow[j], kEffluentFlow * river_threshold);
            if (guard < 2) {
                hf.data()[j] = std::min(hf.data()[j], notch);
                notch -= 0.25f;
            }
            j = downstream[j];
            ++guard;
        }
    }
}

void enforce_monotone_beds(Heightfield& hf, const Hydrology& out,
                           const std::vector<std::size_t>& order,
                           const std::vector<std::size_t>& downstream,
                           float river_threshold, float sea_level) {
    // De cumbre a mar: cada celda de río empuja a su receptora como máximo a
    // su propia cota − ε. El perfil cóncavo real ya lo pone la incisión SPL;
    // esto solo elimina contrapendientes.
    constexpr float kMinDrop = 0.02f;
    for (const std::size_t i : order) {
        if (out.flow[i] < river_threshold || out.lake[i]) {
            continue;
        }
        if (hf.data()[i] <= sea_level) {
            continue;
        }
        const std::size_t d = downstream[i];
        if (d == kNoDownstream || out.lake[d] || out.flow[d] < river_threshold) {
            continue;
        }
        const float max_bed = hf.data()[i] - kMinDrop;
        if (hf.data()[d] > max_bed && max_bed > sea_level - 2.0f) {
            hf.data()[d] = max_bed;
        }
    }
}

namespace {

// Colapsa una knickzone (celdas del eje trace[a..b+1]) en pisos SOLO
// EXCAVANDO: cada piso se rebana hasta el nivel de su base, la pared vertical
// queda en el ARRANQUE del piso (el lecho intacto de aguas arriba hace de
// rellano superior) y al pie de cada pared se sobreexcava la plunge pool.
// Nunca se eleva el lecho: elevarlo creaba bancos flotantes por encima de la
// garganta (agua sin orilla) y murallas artificiales en las laderas.
void sculpt_fall(Heightfield& hf, Hydrology& out,
                 const std::vector<std::size_t>& trace,
                 const std::vector<float>& old_bed, int a, int b,
                 float max_flow, int max_radius) {
    auto& bed = hf.data();
    const int res = hf.resolution();
    const int last = std::min(b + 1, static_cast<int>(trace.size()) - 1);
    const float z_top = old_bed[static_cast<std::size_t>(a)];
    const float z_bot = old_bed[static_cast<std::size_t>(last)];
    const float drop = z_top - z_bot;
    const int zone_cells = last - a + 1;
    if (zone_cells < 2) {
        return;
    }

    // Saltos > ~14 m se parten en pisos (doc: 12-15 m por salto); como mucho
    // una pared cada 2 celdas.
    const int steps =
        glm::clamp(static_cast<int>(std::ceil(drop / 14.0f)), 1,
                   std::max(1, zone_cells / 2));
    const float step_drop = drop / static_cast<float>(steps);
    if (step_drop > 22.0f) {
        // Pared inverosímil (río saltando un acantilado costero de decenas de
        // metros): se conserva el chute empinado del SPL y solo se marca la
        // espuma para el render.
        for (int k = 0; k < zone_cells; ++k) {
            const std::size_t i = trace[static_cast<std::size_t>(a + k)];
            out.whitewater[i] = std::max<std::uint8_t>(out.whitewater[i], 1);
        }
        return;
    }

    // Piso s ocupa [s·N/steps, (s+1)·N/steps); su banco se rebana al nivel de
    // BASE del piso — la caída completa queda concentrada en la primera celda
    // (la pared). La última celda conecta exacta con el lecho aguas abajo.
    const auto segment_of = [&](int k) {
        return std::min(k * steps / zone_cells, steps - 1);
    };
    const auto target_of = [&](int k) {
        if (k == zone_cells - 1) {
            return z_bot;
        }
        return z_top
             - static_cast<float>(segment_of(k) + 1) * step_drop;
    };

    // Radio de canal por celda (misma ley w ∝ Q^0.4 del tallado).
    const auto radius_of = [&](std::size_t i) {
        return glm::clamp(
            static_cast<int>(std::round(
                static_cast<float>(max_radius)
                * std::pow(glm::clamp(out.flow[i] / max_flow, 0.0f, 1.0f),
                           0.4f))),
            1, 4);
    };

    // Pasada 1: bancos. Cada celda del piso se rebana (min) al nivel de base;
    // la lámina se reasienta conservando la profundidad local previa. La
    // celda de arranque del piso es la pared: su labio de aguas arriba se
    // marca whitewater = 2 (caída vertical exenta de la rampa de tensión).
    for (int k = 0; k < zone_cells; ++k) {
        const std::size_t i = trace[static_cast<std::size_t>(a + k)];
        const int x = static_cast<int>(i % static_cast<std::size_t>(res));
        const int z = static_cast<int>(i / static_cast<std::size_t>(res));
        const float t = target_of(k);
        const float axis_old = old_bed[static_cast<std::size_t>(a + k)];
        const float wl_old = out.water_level[i];
        const float depth_w = std::isnan(wl_old)
            ? 0.4f
            : glm::clamp(wl_old - axis_old, 0.25f, 1.2f);
        const int radius = radius_of(i);
        const bool is_wall = (k == 0) || segment_of(k) != segment_of(k - 1)
                          || k == zone_cells - 1;
        if (is_wall && a + k > 0) {
            const std::size_t lip = trace[static_cast<std::size_t>(a + k - 1)];
            out.whitewater[lip] = 2;
        }
        const std::uint8_t flag = is_wall ? 2 : 1;

        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int nx = x + dx;
                const int nz = z + dz;
                if (nx < 1 || nz < 1 || nx >= res - 1 || nz >= res - 1
                    || dx * dx + dz * dz > radius * radius) {
                    continue;
                }
                const std::size_t ni =
                    static_cast<std::size_t>(nz) * static_cast<std::size_t>(res)
                    + static_cast<std::size_t>(nx);
                // Solo el canal PROPIO: celdas a cota de este cauce. La puerta
                // por cota evita segar laderas o tocar otro río cercano.
                if (std::abs(bed[ni] - axis_old) > 4.0f) {
                    continue;
                }
                if (bed[ni] > t) {
                    bed[ni] = t;
                }
                // Regla de MÍNIMO (la misma de la incisión, hydrology.cpp):
                // en la frontera entre dos pisos la celda recibía la lámina
                // del piso ALTO sobre el lecho ya rebanado del piso BAJO y
                // el agua quedaba volando la altura del salto (torres de
                // 20-40 m del feedback 2026-07-08). El agua se asienta en el
                // nivel más bajo que la reclame.
                const float sheet = t + depth_w;
                out.water_level[ni] = std::isnan(out.water_level[ni])
                    ? sheet
                    : std::min(out.water_level[ni], sheet);
                out.whitewater[ni] = std::max(out.whitewater[ni], flag);
            }
        }
    }

    // Pasada 2: plunge pool al pie de cada pared (solo excava). La poza vive
    // en la celda de arranque del piso, justo bajo la caída.
    for (int k = 0; k < zone_cells; ++k) {
        const bool is_wall = (k == 0) || segment_of(k) != segment_of(k - 1);
        if (!is_wall) {
            continue;
        }
        const std::size_t pi = trace[static_cast<std::size_t>(a + k)];
        const int px = static_cast<int>(pi % static_cast<std::size_t>(res));
        const int pz = static_cast<int>(pi / static_cast<std::size_t>(res));
        const float pool_bed = target_of(k);
        const float pool_depth = glm::clamp(0.8f + 0.12f * step_drop, 1.0f, 3.0f);
        const int pr = radius_of(pi) + 1;
        for (int dz = -pr; dz <= pr; ++dz) {
            for (int dx = -pr; dx <= pr; ++dx) {
                const int nx = px + dx;
                const int nz = pz + dz;
                const float r2 = static_cast<float>(dx * dx + dz * dz)
                               / static_cast<float>(pr * pr);
                if (nx < 1 || nz < 1 || nx >= res - 1 || nz >= res - 1
                    || r2 > 1.0f) {
                    continue;
                }
                const std::size_t ni =
                    static_cast<std::size_t>(nz) * static_cast<std::size_t>(res)
                    + static_cast<std::size_t>(nx);
                if (std::abs(bed[ni] - pool_bed) > 4.0f) {
                    continue; // misma puerta de cota que los bancos
                }
                const float dig = pool_bed - pool_depth * (1.0f - r2);
                if (bed[ni] > dig) {
                    bed[ni] = dig;
                }
                out.water_level[ni] = std::isnan(out.water_level[ni])
                    ? pool_bed + 0.3f
                    : std::max(out.water_level[ni], pool_bed + 0.3f);
                out.whitewater[ni] = 2;
            }
        }
    }
}

} // namespace

void carve_cascades(Heightfield& hf, Hydrology& out,
                    const std::vector<std::size_t>& order,
                    const std::vector<std::size_t>& downstream,
                    float river_threshold, float max_flow, int max_radius,
                    float sea_level) {
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    auto& bed = hf.data();

    // Umbrales de tipología (Rosgen por pendiente, Peytavie 2019): cascada
    // por encima del 16%, rápido entre 2.5% y 10%.
    constexpr float kFallSlope = 0.16f;
    constexpr float kRapidLo = 0.025f;
    constexpr float kRapidHi = 0.10f;
    constexpr float kMinFallDrop = 2.5f;
    constexpr float kRapidStep = 0.6f; // micro-escalones de los rápidos

    const auto is_axis = [&](std::size_t i) {
        return out.flow[i] >= river_threshold && !out.lake[i]
            && bed[i] > sea_level + 0.5f;
    };

    // Cabeceras: celdas del eje sin aportador de eje (nacimientos y salidas
    // de lago). Cada traza sigue el D8 hasta mar/lago/celda ya visitada.
    std::vector<std::uint8_t> has_parent(bed.size(), 0);
    for (const std::size_t i : order) {
        if (!is_axis(i)) {
            continue;
        }
        const std::size_t d = downstream[i];
        if (d != kNoDownstream && is_axis(d)) {
            has_parent[d] = 1;
        }
    }

    std::vector<std::uint8_t> visited(bed.size(), 0);
    std::vector<std::size_t> trace;
    std::vector<float> prof;
    std::vector<float> arc;
    std::vector<float> sm;
    std::vector<float> slope;

    for (const std::size_t head : order) {
        if (!is_axis(head) || has_parent[head] || visited[head]) {
            continue;
        }
        trace.clear();
        for (std::size_t j = head;
             j != kNoDownstream && is_axis(j) && !visited[j]; j = downstream[j]) {
            visited[j] = 1;
            trace.push_back(j);
        }
        const int n = static_cast<int>(trace.size());
        if (n < 6) {
            continue;
        }

        // Perfil z(s) y suavizado (~45 m): el knickpoint se detecta sobre la
        // señal limpia, no sobre el ruido de celda.
        prof.assign(static_cast<std::size_t>(n), 0.0f);
        arc.assign(static_cast<std::size_t>(n), 0.0f);
        for (int k = 0; k < n; ++k) {
            prof[static_cast<std::size_t>(k)] = bed[trace[static_cast<std::size_t>(k)]];
            if (k > 0) {
                arc[static_cast<std::size_t>(k)] =
                    arc[static_cast<std::size_t>(k - 1)]
                    + step_dist(trace[static_cast<std::size_t>(k - 1)],
                                trace[static_cast<std::size_t>(k)], res, cell);
            }
        }
        const int w = std::max(2, static_cast<int>(std::round(22.0f / cell)));
        sm.assign(static_cast<std::size_t>(n), 0.0f);
        for (int k = 0; k < n; ++k) {
            const int lo = std::max(0, k - w);
            const int hi = std::min(n - 1, k + w);
            float acc = 0.0f;
            for (int q = lo; q <= hi; ++q) {
                acc += prof[static_cast<std::size_t>(q)];
            }
            sm[static_cast<std::size_t>(k)] = acc / static_cast<float>(hi - lo + 1);
        }
        slope.assign(static_cast<std::size_t>(n - 1), 0.0f);
        for (int k = 0; k < n - 1; ++k) {
            const float ds = arc[static_cast<std::size_t>(k + 1)]
                           - arc[static_cast<std::size_t>(k)];
            slope[static_cast<std::size_t>(k)] = std::max(
                0.0f, (sm[static_cast<std::size_t>(k)]
                       - sm[static_cast<std::size_t>(k + 1)]) / ds);
        }

        int k = 0;
        while (k < n - 1) {
            if (slope[static_cast<std::size_t>(k)] > kFallSlope) {
                // Knickzone: extender mientras dure (huecos ≤ 2 celdas).
                int end = k;
                int gap = 0;
                for (int q = k + 1; q < n - 1; ++q) {
                    if (slope[static_cast<std::size_t>(q)] > kFallSlope) {
                        end = q;
                        gap = 0;
                    } else if (++gap > 2) {
                        break;
                    }
                }
                const int last = std::min(end + 1, n - 1);
                const float drop = prof[static_cast<std::size_t>(k)]
                                 - prof[static_cast<std::size_t>(last)];
                if (drop >= kMinFallDrop) {
                    sculpt_fall(hf, out, trace, prof, k, end, max_flow,
                                max_radius);
                } else {
                    for (int q = k; q <= last; ++q) {
                        const std::size_t i = trace[static_cast<std::size_t>(q)];
                        out.whitewater[i] = std::max<std::uint8_t>(
                            out.whitewater[i], 1);
                    }
                }
                k = end + 1;
            } else if (slope[static_cast<std::size_t>(k)] > kRapidLo
                       && slope[static_cast<std::size_t>(k)] <= kRapidHi) {
                // Rápido: flag whitewater + micro-escalones (solo talla).
                int end = k;
                while (end < n - 1
                       && slope[static_cast<std::size_t>(end)] > kRapidLo
                       && slope[static_cast<std::size_t>(end)] <= kRapidHi) {
                    ++end;
                }
                if (arc[static_cast<std::size_t>(end)]
                        - arc[static_cast<std::size_t>(k)] > 3.0f * cell) {
                    const float z_start = prof[static_cast<std::size_t>(k)];
                    for (int q = k; q <= end; ++q) {
                        const std::size_t i = trace[static_cast<std::size_t>(q)];
                        out.whitewater[i] = std::max<std::uint8_t>(
                            out.whitewater[i], 1);
                        const float d0 = z_start - bed[i];
                        if (d0 > 0.0f) {
                            bed[i] = z_start
                                   - std::ceil(d0 / kRapidStep) * kRapidStep;
                        }
                    }
                }
                k = end;
            } else {
                ++k;
            }
        }
    }
}

void smooth_confluences(Heightfield& hf, Hydrology& out,
                        const std::vector<std::size_t>& order,
                        const std::vector<std::size_t>& downstream,
                        const RiverscapeParams& p) {
    const int res = hf.resolution();
    auto& bed = hf.data();

    const auto is_axis = [&](std::size_t i) {
        return out.flow[i] >= p.threshold && !out.lake[i]
            && bed[i] > p.sea_level + 0.2f;
    };
    // Solo uniones de brazos MADUROS (≥2× umbral): las de arroyos recién
    // nacidos no tienen cauce visible que limpiar.
    const auto mature = [&](std::size_t i) {
        return out.flow[i] >= 2.0f * p.threshold;
    };

    std::vector<std::uint8_t> parents(bed.size(), 0);
    for (const std::size_t i : order) {
        if (!is_axis(i) || !mature(i)) {
            continue;
        }
        const std::size_t d = downstream[i];
        if (d != kNoDownstream && is_axis(d)
            && parents[d] < std::numeric_limits<std::uint8_t>::max()) {
            ++parents[d];
        }
    }

    for (std::size_t j = 0; j < bed.size(); ++j) {
        if (parents[j] < 2 || !is_axis(j) || out.whitewater[j] == 2) {
            continue;
        }
        const float wl = out.water_level[j];
        if (std::isnan(wl)) {
            continue;
        }
        const int x = static_cast<int>(j % static_cast<std::size_t>(res));
        const int z = static_cast<int>(j / static_cast<std::size_t>(res));
        const int r = channel_radius(out, j, p.max_flow, p.max_radius) + 1;

        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                const int nx = x + dx;
                const int nz = z + dz;
                const float r2 = static_cast<float>(dx * dx + dz * dz)
                               / static_cast<float>(r * r);
                if (nx < 1 || nz < 1 || nx >= res - 1 || nz >= res - 1
                    || r2 > 1.0f) {
                    continue;
                }
                const std::size_t ni =
                    static_cast<std::size_t>(nz) * static_cast<std::size_t>(res)
                    + static_cast<std::size_t>(nx);
                if (std::abs(bed[ni] - bed[j]) > 5.0f) {
                    continue; // puerta de cota: no tocar laderas
                }
                if (!std::isnan(out.water_level[ni])
                    || bed[ni] < wl - 0.05f) {
                    // Cama de la unión: socavación suave al mínimo de ambos
                    // cauces (los ríos excavan una poza en las confluencias).
                    const float dig = bed[j] - 0.5f * (1.0f - r2);
                    if (bed[ni] > dig) {
                        bed[ni] = dig;
                    }
                } else if (bed[ni] > wl + 0.05f && bed[ni] < wl + 2.5f) {
                    // Fillet del esquinazo interior: la cuña de hierba entre
                    // los dos brazos se rebaja a espigón de grava redondeado.
                    const float spit = wl + 0.25f + 0.35f * r2;
                    if (bed[ni] > spit) {
                        bed[ni] = spit;
                    }
                }
            }
        }
    }
}

void build_deltas(Heightfield& hf, Hydrology& out,
                  const std::vector<std::size_t>& order,
                  const std::vector<std::size_t>& downstream,
                  const RiverscapeParams& p) {
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    auto& bed = hf.data();

    // 1) Bocas: última celda de río antes del mar, con su dirección de salida.
    struct Mouth {
        std::size_t i;
        float flow;
        glm::vec2 dir;
    };
    std::vector<Mouth> mouths;
    for (const std::size_t i : order) {
        if (out.flow[i] < p.threshold || out.lake[i]
            || bed[i] <= p.sea_level) {
            continue;
        }
        const std::size_t d = downstream[i];
        if (d == kNoDownstream || bed[d] > p.sea_level) {
            continue;
        }
        const glm::vec2 pi{static_cast<float>(i % static_cast<std::size_t>(res)),
                           static_cast<float>(i / static_cast<std::size_t>(res))};
        const glm::vec2 pd{static_cast<float>(d % static_cast<std::size_t>(res)),
                           static_cast<float>(d / static_cast<std::size_t>(res))};
        mouths.push_back({i, out.flow[i], glm::normalize(pd - pi)});
    }
    if (mouths.empty()) {
        return;
    }

    // 2) Umbral deltaico: p85 del caudal de las bocas (doc). Solo los ríos
    //    grandes acumulan sedimento suficiente para progradar un delta.
    std::vector<float> flows;
    flows.reserve(mouths.size());
    for (const Mouth& m : mouths) {
        flows.push_back(m.flow);
    }
    std::sort(flows.begin(), flows.end());
    const float p85 =
        flows[static_cast<std::size_t>(static_cast<float>(flows.size() - 1) * 0.85f)];

    int built = 0;
    for (const Mouth& m : mouths) {
        if (m.flow < p85 || built >= 4) {
            continue; // como mucho unos pocos deltas por isla
        }
        const int mx = static_cast<int>(m.i % static_cast<std::size_t>(res));
        const int mz = static_cast<int>(m.i / static_cast<std::size_t>(res));
        // Boca baja (llanura deltaica) y costa TENDIDA: si a ~150 m mar
        // adentro el fondo ya es profundo, es costa abrupta → estuario, sin
        // lóbulo.
        if (bed[m.i] > p.sea_level + 2.0f) {
            continue;
        }
        const int probe_x = glm::clamp(
            mx + static_cast<int>(m.dir.x * 150.0f / cell), 1, res - 2);
        const int probe_z = glm::clamp(
            mz + static_cast<int>(m.dir.y * 150.0f / cell), 1, res - 2);
        if (bed[static_cast<std::size_t>(probe_z) * static_cast<std::size_t>(res)
                + static_cast<std::size_t>(probe_x)] < p.sea_level - 7.0f) {
            continue;
        }
        ++built;
        // Verificable sin volar a ciegas (mismo criterio que los lagos).
        std::cout << "  delta @ (" << static_cast<int>(hf.world_x(mx)) << ", "
                  << static_cast<int>(hf.world_z(mz)) << ")\n";

        std::uint32_t rng = p.seed ^ hash32(static_cast<std::uint32_t>(m.i));
        const glm::vec2 m0{static_cast<float>(mx), static_cast<float>(mz)};
        const float w0_m = glm::clamp(
            static_cast<float>(2 * channel_radius(out, m.i, p.max_flow,
                                                  p.max_radius) + 1) * cell,
            12.0f, 45.0f);
        const float radius_m = glm::clamp(8.0f * w0_m, 180.0f, 520.0f);
        const float radius_c = radius_m / cell;

        // 3) LÓBULO de deposición: plataforma a ras de mar que cae al fondo
        //    en el borde, solo a favor de la dirección de salida. Las islas
        //    interdistributarias emergen al surcarla con los brazos.
        const int box = static_cast<int>(radius_c * 1.15f) + 1;
        for (int dz = -box; dz <= box; ++dz) {
            for (int dx = -box; dx <= box; ++dx) {
                const int nx = mx + dx;
                const int nz = mz + dz;
                if (nx < 1 || nz < 1 || nx >= res - 1 || nz >= res - 1) {
                    continue;
                }
                const glm::vec2 rel{static_cast<float>(dx),
                                    static_cast<float>(dz)};
                const float dist = glm::length(rel);
                if (dist > radius_c * 1.1f
                    || glm::dot(rel, m.dir) < -0.15f * radius_c) {
                    continue;
                }
                const float t = dist / radius_c;
                const float fade =
                    glm::smoothstep(0.60f, 1.05f, t);
                const float target = p.sea_level + 0.35f - 3.5f * fade;
                const std::size_t ni =
                    static_cast<std::size_t>(nz) * static_cast<std::size_t>(res)
                    + static_cast<std::size_t>(nx);
                // Solo DEPOSITA sobre celdas SUMERGIDAS y someras: nunca
                // rellena mar profundo ni pisa la playa existente (pisar
                // tierra dejaba un banco con escalón contra la costa).
                // Y NUNCA sobre el canal ACTIVO (feedback 2026-07-13 "el
                // delta bloquea el río"): el caudal mantiene su surco abierto
                // a través del lóbulo — antes el depósito enterraba el tramo
                // final y el río moría en seco contra su propio delta.
                if (bed[ni] <= p.sea_level && bed[ni] > p.sea_level - 7.0f
                    && bed[ni] < target && out.flow[ni] < p.threshold) {
                    bed[ni] = target;
                }
            }
        }

        // 4) DISTRIBUTARIOS: bifurcación recursiva mar adentro. w_hijo =
        //    w_padre/√2, 60-75° entre hermanos, brazos de 100-220 m.
        // Snapshot ANTES de cavar: la muerte por tocar tierra debe leer el
        // terreno original — el disco del propio brazo (radio 4-9 celdas) ya
        // ha excavado el centro cuando la sonda llega, y con el lecho vivo
        // la comprobación nunca saltaba (zanjas rectas a través de la playa).
        const std::vector<float> pre_arms(bed);
        struct Node {
            glm::vec2 pos;
            glm::vec2 dir;
            float w_m;
            int depth;
        };
        std::vector<Node> stack{{m0, m.dir, w0_m, 0}};
        int arms = 0;
        while (!stack.empty() && arms < 48) {
            const Node n = stack.back();
            stack.pop_back();
            if (n.w_m < 5.0f || n.depth > 5
                || glm::length(n.pos - m0) > radius_c) {
                continue;
            }
            ++arms;
            const float len_c = (100.0f + 120.0f * hash01(rng)) / cell;
            const glm::vec2 dir =
                rotate2(n.dir, (hash01(rng) - 0.5f) * 0.30f);
            glm::vec2 pos = n.pos;
            const int r_ch = std::max(
                1, static_cast<int>(std::round(0.5f * n.w_m / cell)));
            const float depth_ch = 0.7f + 0.6f * (n.w_m / w0_m);
            bool alive = true;
            for (int s = 0; s < static_cast<int>(len_c) && alive; ++s) {
                pos += dir;
                const int cx = static_cast<int>(std::round(pos.x));
                const int cz = static_cast<int>(std::round(pos.y));
                if (cx < 2 || cz < 2 || cx >= res - 2 || cz >= res - 2
                    || glm::length(pos - m0) > radius_c) {
                    alive = false;
                    break;
                }
                // El brazo muere al tocar TIERRA real (por encima del lóbulo).
                const std::size_t look =
                    static_cast<std::size_t>(cz) * static_cast<std::size_t>(res)
                    + static_cast<std::size_t>(cx);
                if (pre_arms[look] > p.sea_level + 1.2f) {
                    alive = false;
                    break;
                }
                for (int dz = -r_ch; dz <= r_ch; ++dz) {
                    for (int dx = -r_ch; dx <= r_ch; ++dx) {
                        const float r2 = static_cast<float>(dx * dx + dz * dz)
                                       / static_cast<float>(r_ch * r_ch);
                        if (r2 > 1.0f) {
                            continue;
                        }
                        const std::size_t ni =
                            static_cast<std::size_t>(cz + dz)
                                * static_cast<std::size_t>(res)
                            + static_cast<std::size_t>(cx + dx);
                        const float dig =
                            p.sea_level - depth_ch * (1.0f - 0.6f * r2);
                        if (bed[ni] > dig) {
                            bed[ni] = dig;
                        }
                    }
                }
                // En agua honda el brazo muere (el lóbulo acabó).
                const std::size_t ci =
                    static_cast<std::size_t>(cz) * static_cast<std::size_t>(res)
                    + static_cast<std::size_t>(cx);
                if (bed[ci] < p.sea_level - 4.0f) {
                    alive = false;
                }
            }
            if (!alive) {
                continue;
            }
            // Los distributarios siempre progradan MAR ADENTRO: un hijo que
            // apunte de vuelta a tierra no se genera.
            const auto push_seaward = [&](const glm::vec2& d2, float w,
                                          int depth) {
                if (glm::dot(d2, m.dir) > 0.10f) {
                    stack.push_back({pos, d2, w, depth});
                }
            };
            if (hash01(rng) < 0.62f && n.depth < 4) {
                const float half_angle = 0.52f + 0.13f * hash01(rng); // 60-75°
                push_seaward(rotate2(dir, half_angle), n.w_m / 1.4142f,
                             n.depth + 1);
                push_seaward(rotate2(dir, -half_angle), n.w_m / 1.4142f,
                             n.depth + 1);
            } else {
                push_seaward(dir, n.w_m * 0.9f, n.depth + 1);
            }
        }
    }
}

void stamp_channel_bars(Heightfield& hf, Hydrology& out,
                        const std::vector<std::size_t>& order,
                        const std::vector<std::size_t>& downstream,
                        const RiverscapeParams& p) {
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    auto& bed = hf.data();

    const auto is_axis = [&](std::size_t i) {
        return out.flow[i] >= p.threshold && !out.lake[i]
            && bed[i] > p.sea_level + 0.5f;
    };
    const auto posf = [&](std::size_t i) {
        return glm::vec2{static_cast<float>(i % static_cast<std::size_t>(res)),
                         static_cast<float>(i / static_cast<std::size_t>(res))};
    };

    std::vector<std::uint8_t> has_parent(bed.size(), 0);
    for (const std::size_t i : order) {
        if (!is_axis(i)) {
            continue;
        }
        const std::size_t d = downstream[i];
        if (d != kNoDownstream && is_axis(d)) {
            has_parent[d] = 1;
        }
    }

    std::vector<std::uint8_t> visited(bed.size(), 0);
    std::vector<std::size_t> trace;
    std::vector<float> arc;
    int bars_logged = 0;

    for (const std::size_t head : order) {
        if (!is_axis(head) || has_parent[head] || visited[head]) {
            continue;
        }
        trace.clear();
        for (std::size_t j = head;
             j != kNoDownstream && is_axis(j) && !visited[j];
             j = downstream[j]) {
            visited[j] = 1;
            trace.push_back(j);
        }
        const int n = static_cast<int>(trace.size());
        if (n < 12) {
            continue;
        }
        arc.assign(static_cast<std::size_t>(n), 0.0f);
        for (int k = 1; k < n; ++k) {
            arc[static_cast<std::size_t>(k)] =
                arc[static_cast<std::size_t>(k - 1)]
                + step_dist(trace[static_cast<std::size_t>(k - 1)],
                            trace[static_cast<std::size_t>(k)], res, cell);
        }

        std::uint32_t rng = p.seed ^ hash32(static_cast<std::uint32_t>(head));
        float last_bar_arc = -1e9f;
        for (int k = 3; k < n - 3; ++k) {
            const std::size_t i = trace[static_cast<std::size_t>(k)];
            if (out.whitewater[i]) {
                continue;
            }
            const float wl = out.water_level[i];
            if (std::isnan(wl)) {
                continue;
            }
            const float w_m = static_cast<float>(
                2 * channel_radius(out, i, p.max_flow, p.max_radius) + 1) * cell;
            if (w_m < 24.0f) {
                continue; // solo tramos anchos (doc: w > 25-30 m)
            }
            const float ds = arc[static_cast<std::size_t>(k + 2)]
                           - arc[static_cast<std::size_t>(k - 2)];
            const float s = (bed[trace[static_cast<std::size_t>(k - 2)]]
                             - bed[trace[static_cast<std::size_t>(k + 2)]]) / ds;
            if (s < 0.0015f || s > 0.02f) {
                continue; // barras solo en régimen tendido (0.2-2%)
            }
            if (arc[static_cast<std::size_t>(k)] - last_bar_arc
                < (6.0f + 4.0f * hash01(rng)) * w_m) {
                continue; // espaciado λ = 6-10 anchuras
            }
            last_bar_arc = arc[static_cast<std::size_t>(k)];
            if (bars_logged < 6) {
                const int lx = static_cast<int>(i % static_cast<std::size_t>(res));
                const int lz = static_cast<int>(i / static_cast<std::size_t>(res));
                std::cout << "  barra fluvial @ ("
                          << static_cast<int>(hf.world_x(lx)) << ", "
                          << static_cast<int>(hf.world_z(lz)) << ")\n";
                ++bars_logged;
            }

            // Elipse gaussiana alineada al flujo, pico sobre la lámina.
            const glm::vec2 dir = glm::normalize(
                posf(trace[static_cast<std::size_t>(k + 2)])
                - posf(trace[static_cast<std::size_t>(k - 2)]));
            const glm::vec2 perp{-dir.y, dir.x};
            const glm::vec2 c = posf(i);
            const float half_len = (0.9f + 0.5f * hash01(rng)) * w_m / cell;
            const float half_wid =
                std::max(1.2f, 0.20f * w_m / cell);
            const float peak = 0.35f + 0.55f * hash01(rng);
            const int box = static_cast<int>(half_len * 2.4f) + 1;
            const int cx = static_cast<int>(c.x);
            const int cz = static_cast<int>(c.y);
            for (int dz = -box; dz <= box; ++dz) {
                for (int dx = -box; dx <= box; ++dx) {
                    const int nx = cx + dx;
                    const int nz = cz + dz;
                    if (nx < 1 || nz < 1 || nx >= res - 1 || nz >= res - 1) {
                        continue;
                    }
                    const glm::vec2 rel{static_cast<float>(dx),
                                        static_cast<float>(dz)};
                    const float u = glm::dot(rel, dir) / half_len;
                    const float v = glm::dot(rel, perp) / half_wid;
                    const float e = u * u + v * v;
                    const std::size_t ni =
                        static_cast<std::size_t>(nz)
                            * static_cast<std::size_t>(res)
                        + static_cast<std::size_t>(nx);
                    if (e <= 1.0f) {
                        // Núcleo de la barra: emerge sobre la lámina — pero
                        // NUNCA sobre el thalweg (feedback 2026-07-13 "los
                        // islotes bloquean el río"): la elipse recta cruzaba
                        // la orilla en las curvas y sellaba el paso. Las
                        // barras reales son laterales al canal activo; el eje
                        // D8 con caudal queda siempre abierto.
                        if (out.flow[ni] >= p.threshold) {
                            continue;
                        }
                        const float target =
                            wl + peak * std::pow(1.0f - e, 1.5f);
                        if (bed[ni] < target && bed[ni] > wl - 3.5f) {
                            bed[ni] = target;
                        }
                        if (bed[ni] >= wl - 0.02f) {
                            out.water_level[ni] = kNoWater;
                        }
                    } else if (e <= 2.4f) {
                        // Anillo: el cauce se ensancha un 20-40% alrededor
                        // para que el agua siga pasando por ambos lados.
                        if (!std::isnan(out.water_level[ni])) {
                            continue; // ya es agua
                        }
                        if (bed[ni] > wl - 0.05f && bed[ni] < wl + 2.0f) {
                            bed[ni] = wl - 0.45f;
                            out.water_level[ni] = wl;
                        }
                    }
                }
            }
        }
    }
}

} // namespace terrain
