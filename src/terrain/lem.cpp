#include "terrain/lem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <execution>
#include <numeric>
#include <queue>

namespace terrain {

namespace {

constexpr float kDiffusion = 0.17f;      // difusión de laderas: crestas definidas
                                         // pero sin agujas (límite de estabilidad
                                         // 0.25; con 0.20 las aristas quedaban
                                         // demasiado romas para el look alpino)
constexpr float kBaseUplift = 0.02f;     // por debajo: nivel de base (mar)
constexpr float kHeadwaterSlope = 0.9f;  // pendiente de equilibrio en laderas (~42°)
// Dureza superlineal: S ∝ u^(1+kHardness) — la roca del núcleo orogénico
// resiste más que las faldas. Con S ∝ u (lineal) todo el flanco escalaba
// igual; así los paredones u≈1 llegan a ~42° mientras las faldas medias se
// quedan como estaban → contraste tipo Matterhorn (pared desnuda vs nieve).
constexpr float kHardness = 0.3f;
// Concavidad θ del perfil (Flint: S ∝ A^-θ). El 0.5 físico de ríos maduros
// hunde las cumbres en un dominio de 10 km — los canales largos pierden toda
// la pendiente y el objetivo de altura se vuelve inalcanzable por diseño.
// 0.35 es exageración vertical controlada: valles todavía cóncavos, pero que
// conservan cota hacia el interior (cumbres ~1.2-1.4 km con uplift 1400).
constexpr float kConcavity = 0.35f;
// Área crítica de canalización (Flint): por debajo no hay canal — la celda es
// LADERA y su pendiente de equilibrio es kHeadwaterSlope, sea cual sea el
// tamaño de celda. Sin esta referencia FÍSICA la pendiente escalaba con la
// celda del grid (cell/√A): a macro fino el relieve de equilibrio se hundía a
// ~200 m y las montañas desaparecían por diseño.
constexpr float kChannelAreaM2 = 22500.0f; // (~150 m)² de ladera sin canal

constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// Priority-flood + epsilon (Barnes): rellena los pits con pendiente mínima
// hacia el borde. Dentro del bucle LEM es el manejo de lagos: sin él, las
// cuencas endorreicas se inflan en bloque hasta formar mesetas.
void fill_pits(std::vector<float>& h, int res) {
    struct Cell {
        float height;
        int x;
        int z;
    };
    struct Higher {
        bool operator()(const Cell& a, const Cell& b) const {
            return a.height > b.height;
        }
    };
    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };

    std::vector<bool> done(h.size(), false);
    std::priority_queue<Cell, std::vector<Cell>, Higher> open;
    for (int x = 0; x < res; ++x) {
        for (const int z : {0, res - 1}) {
            open.push({h[idx(x, z)], x, z});
            done[idx(x, z)] = true;
        }
    }
    for (int z = 1; z < res - 1; ++z) {
        for (const int x : {0, res - 1}) {
            open.push({h[idx(x, z)], x, z});
            done[idx(x, z)] = true;
        }
    }
    while (!open.empty()) {
        const Cell c = open.top();
        open.pop();
        for (int n = 0; n < 8; ++n) {
            const int nx = c.x + kDx[n];
            const int nz = c.z + kDz[n];
            if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                continue;
            }
            const size_t ni = idx(nx, nz);
            if (done[ni]) {
                continue;
            }
            done[ni] = true;
            // Epsilon mínimo: solo para que el D8 desempate. Con 0.01 m/celda
            // las cuencas grandes acumulaban rampas artificiales de ~1 m que
            // la bicúbica luego amplificaba.
            h[ni] = std::max(h[ni], c.height + 0.0005f);
            open.push({h[ni], nx, nz});
        }
    }
}

} // namespace

std::vector<float> run_lem(const std::vector<float>& uplift, int macro_res,
                           float cell_size_m, float uplift_total_m,
                           int iterations) {
    const int res = macro_res;
    const size_t n = static_cast<size_t>(res) * static_cast<size_t>(res);
    const float uplift_per_iter = uplift_total_m / static_cast<float>(iterations);
    // Calibración física e independiente del grid: la pendiente de equilibrio
    // es kHeadwaterSlope en el dominio de ladera (A ≤ kChannelAreaM2) y decae
    // como (A0/A)^θ en los canales (perfil cóncavo de Flint). La pendiente
    // máxima del macro queda acotada por diseño. OJO: en estado estacionario
    // S = u·kHeadwaterSlope·(A0/A)^θ — el uplift total se CANCELA (k_dt
    // escala con él); la cota de cumbre la fijan estas constantes y el campo
    // de uplift, no el budget.
    const float k_dt = uplift_per_iter / kHeadwaterSlope;

    // Semilla: gradiente inicial proporcional al uplift, para que el primer
    // routing ya tenga pendientes hacia el mar.
    std::vector<float> h(n);
    for (size_t i = 0; i < n; ++i) {
        h[i] = uplift[i] * 2.0f;
    }
    fill_pits(h, res);
    std::vector<std::uint32_t> receiver(n);
    std::vector<float> rcv_dist(n);
    std::vector<float> area(n);
    // Stack topológico de Braun & Willett: donantes en CRS (ndon/delta/donors)
    // y recorrido BFS desde los nodos base. Sustituye al sort por altura:
    // O(n) exacto por iteración y el receptor SIEMPRE se resuelve antes que
    // sus donantes (el sort solo lo aproximaba).
    std::vector<std::uint32_t> ndon(n);
    std::vector<std::uint32_t> delta(n + 1);
    std::vector<std::uint32_t> donors(n);
    std::vector<std::uint32_t> stack(n);
    std::vector<float> lap(n);
    std::vector<int> rows(static_cast<size_t>(res));
    std::iota(rows.begin(), rows.end(), 0);

    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };

    for (int it = 0; it < iterations; ++it) {
        // 1) Receptor de cada celda: vecino con máxima pendiente de bajada.
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            for (int x = 0; x < res; ++x) {
                const size_t i = idx(x, z);
                float best_slope = 0.0f;
                std::uint32_t best = static_cast<std::uint32_t>(i);
                float best_d = cell_size_m;
                for (int nb = 0; nb < 8; ++nb) {
                    const int nx = x + kDx[nb];
                    const int nz = z + kDz[nb];
                    if (nx < 0 || nz < 0 || nx >= res || nz >= res) {
                        continue;
                    }
                    const float d = (kDx[nb] != 0 && kDz[nb] != 0)
                                  ? cell_size_m * 1.4142f
                                  : cell_size_m;
                    const float slope = (h[i] - h[idx(nx, nz)]) / d;
                    if (slope > best_slope) {
                        best_slope = slope;
                        best = static_cast<std::uint32_t>(idx(nx, nz));
                        best_d = d;
                    }
                }
                receiver[i] = best;
                rcv_dist[i] = best_d;
            }
        });

        // 2) Stack topológico: lista de donantes (CRS) + BFS desde los base
        //    nodes (receiver[i] == i). Cada nodo aparece DESPUÉS de su receptor.
        std::fill(ndon.begin(), ndon.end(), 0u);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i) {
            if (receiver[i] != i) {
                ++ndon[receiver[i]];
            }
        }
        delta[0] = 0;
        for (size_t i = 0; i < n; ++i) {
            delta[i + 1] = delta[i] + ndon[i];
        }
        std::fill(ndon.begin(), ndon.end(), 0u); // se reusa como cursor
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i) {
            const std::uint32_t r = receiver[i];
            if (r != i) {
                donors[delta[r] + ndon[r]++] = i;
            }
        }
        size_t top = 0;
        for (std::uint32_t b = 0; b < static_cast<std::uint32_t>(n); ++b) {
            if (receiver[b] == b) {
                stack[top++] = b;
            }
        }
        for (size_t s = 0; s < top; ++s) { // top crece durante el recorrido
            const std::uint32_t c = stack[s];
            for (std::uint32_t k = delta[c]; k < delta[c] + ndon[c]; ++k) {
                stack[top++] = donors[k];
            }
        }

        // 3) Área de drenaje acumulada aguas abajo (stack inverso: donantes
        //    antes que receptores).
        const float cell_area = cell_size_m * cell_size_m;
        std::fill(area.begin(), area.end(), cell_area);
        for (size_t s = n; s-- > 0;) {
            const std::uint32_t i = stack[s];
            if (receiver[i] != i) {
                area[receiver[i]] += area[i];
            }
        }

        // 4) Solver implícito (stack directo: el receptor ya está resuelto).
        for (size_t s = 0; s < n; ++s) {
            const std::uint32_t i = stack[s];
            if (uplift[i] < kBaseUplift) {
                h[i] = 0.0f; // nivel de base: mar
                continue;
            }
            const std::uint32_t r = receiver[i];
            if (r == i) {
                h[i] += uplift_per_iter * uplift[i]; // pit: se rellena y drenará
                continue;
            }
            // El divisor de dureza hace S ∝ u^(1+kHardness): erosiona más
            // deprisa la roca blanda de poco uplift, menos el núcleo.
            const float hardness =
                std::pow(std::max(uplift[i], 0.05f), kHardness);
            const float f = k_dt
                          * std::pow(std::max(area[i], kChannelAreaM2)
                                         / kChannelAreaM2,
                                     kConcavity)
                          / (rcv_dist[i] * hardness);
            h[i] = (h[i] + uplift_per_iter * uplift[i] + f * h[r]) / (1.0f + f);
        }

        // 5) Difusión de laderas: crestas redondeadas, sin agujas.
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            if (z < 1 || z >= res - 1) {
                return;
            }
            for (int x = 1; x < res - 1; ++x) {
                const size_t i = idx(x, z);
                lap[i] = h[idx(x - 1, z)] + h[idx(x + 1, z)] + h[idx(x, z - 1)]
                       + h[idx(x, z + 1)] - 4.0f * h[i];
            }
        });
        std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int z) {
            if (z < 1 || z >= res - 1) {
                return;
            }
            for (int x = 1; x < res - 1; ++x) {
                const size_t i = idx(x, z);
                h[i] += kDiffusion * lap[i];
            }
        });

        // 6) Manejo de lagos: relleno periódico para que las cuencas
        // endorreicas drenen en vez de inflarse como mesetas.
        if (it % 10 == 9) {
            fill_pits(h, res);
        }
    }

    // Dos pasadas de suavizado 3×3: elimina las transiciones valle-cresta de
    // una sola celda macro antes de la amplificación bicúbica. Sin
    // normalización: el LEM devuelve metros con pendientes físicas.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> smoothed(h);
        for (int z = 1; z < res - 1; ++z) {
            for (int x = 1; x < res - 1; ++x) {
                const size_t i = idx(x, z);
                smoothed[i] = 0.4f * h[i]
                            + 0.15f * (h[idx(x - 1, z)] + h[idx(x + 1, z)]
                                       + h[idx(x, z - 1)] + h[idx(x, z + 1)]);
            }
        }
        h.swap(smoothed);
    }
    for (float& v : h) {
        v = std::max(v, 0.0f);
    }
    return h;
}

namespace {

float catmull_rom(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t
                   + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                   + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

} // namespace

float sample_bicubic(const std::vector<float>& grid, int res, float fx, float fz) {
    const auto at = [&](int x, int z) {
        x = std::clamp(x, 0, res - 1);
        z = std::clamp(z, 0, res - 1);
        return grid[static_cast<size_t>(z) * static_cast<size_t>(res)
                    + static_cast<size_t>(x)];
    };
    const int x1 = static_cast<int>(std::floor(fx));
    const int z1 = static_cast<int>(std::floor(fz));
    const float tx = fx - static_cast<float>(x1);
    const float tz = fz - static_cast<float>(z1);

    float rows[4];
    for (int r = 0; r < 4; ++r) {
        const int z = z1 - 1 + r;
        rows[r] = catmull_rom(at(x1 - 1, z), at(x1, z), at(x1 + 1, z),
                              at(x1 + 2, z), tx);
    }
    return catmull_rom(rows[0], rows[1], rows[2], rows[3], tz);
}

} // namespace terrain
