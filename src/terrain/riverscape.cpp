#include "terrain/riverscape.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

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
                out.water_level[ni] = t + depth_w;
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

} // namespace terrain
