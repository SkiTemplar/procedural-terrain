#include "terrain/lem.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <execution>
#include <numeric>
#include <queue>

namespace terrain {

namespace {

constexpr float kDiffusion = 0.21f;      // difusión de laderas (límite de
                                         // estabilidad 0.25). Con 0.17 las
                                         // laderas de cada valle se peinaban en
                                         // barrancos de primer orden paralelos
                                         // y equiespaciados; más difusión (menor
                                         // Péclet) deja menos y más espaciados.
constexpr float kBaseUplift = 0.02f;     // por debajo: nivel de base (mar)
constexpr float kHeadwaterSlope = 1.05f; // pendiente de equilibrio en laderas (~46°)
// Dureza superlineal: S ∝ u^(1+kHardness) — la roca del núcleo orogénico
// resiste más que las faldas. Con S ∝ u (lineal) todo el flanco escalaba
// igual; así los paredones u≈1 llegan a ~42° mientras las faldas medias se
// quedan como estaban → contraste tipo Matterhorn (pared desnuda vs nieve).
constexpr float kHardness = 0.3f;
// Concavidad θ del perfil (Flint: S ∝ A^-θ). El 0.5 físico de ríos maduros
// hunde las cumbres en un dominio de 10 km — los canales largos pierden toda
// la pendiente y el objetivo de altura se vuelve inalcanzable por diseño.
// 0.22 es exageración vertical controlada: con el área crítica pequeña (los
// canales nacen a ~40 m de la divisoria) la pendiente cae desde mucho antes y
// θ = 0.35 dejaba las cumbres en ~770 m; con 0.22 los valles siguen
// cóncavos y conservan cota hacia el interior (cumbres ~1.1-1.2 km).
constexpr float kConcavity = 0.22f;
// Área crítica de canalización (Flint): por debajo no hay canal — la celda es
// LADERA y su pendiente de equilibrio es kHeadwaterSlope, sea cual sea el
// tamaño de celda. Sin esta referencia FÍSICA la pendiente escalaba con la
// celda del grid (cell/√A): a macro fino el relieve de equilibrio se hundía a
// ~200 m y las montañas desaparecían por diseño.
// Con (150 m)² la ladera sin canal era NEUTRA (S constante, sin dependencia
// de A) a lo largo de ~2 km de flujo paralelo D8: la rugosidad inicial no
// crecía, no había convergencia y salían rampas planas kilométricas con un
// peine de barrancos paralelos. A ~40 m (cabeceras reales: 10³-10⁴ m²) la
// inestabilidad canal/ladera actúa desde la escala de pocas celdas y la red
// sale dendrítica, con divisorias sinuosas y valles ramificados. A (40 m)²
// esa inestabilidad era tan fina que cada ladera de valle salía en espina de
// pez; (63 m)² conserva la red dendrítica con tributarios más espaciados y
// cumbres más altas y distintas (dump seed 20260711: cumbre 1310 m).
constexpr float kChannelAreaM2 = 4000.0f; // (~63 m)² de ladera sin canal
// Litología (feedback 2026-07-10): cuánto separa una formación BLANDA (lith=1)
// de una DURA (lith=0) respecto al centro (lith=0.5 == comportamiento clásico,
// mapeo exponencial centrado en 1 para no tocar la calibración).
constexpr float kErodSpan = 1.7f;  // ratio de erodibilidad K blanda/centro
constexpr float kA0Span = 2.5f;    // ratio del área crítica dura/centro
// Precipitación orográfica (feedback 2026-07-10): advección de humedad en la
// dirección del viento. La lluvia por celda = humedad × (base + ganancia por
// pendiente de BARLOVENTO); la humedad se agota al llover (sombra de lluvia
// emergente) y se recarga sobre el mar. El peso final se normaliza a media 1
// sobre tierra con suelo kRainFloor: no cambia la calibración global, cambia
// DÓNDE trabaja el agua.
constexpr float kRainBase = 0.05f;   // fracción de humedad que llueve en llano
constexpr float kRainLiftGain = 0.6f; // ganancia por pendiente de barlovento
constexpr float kRainFracMax = 0.35f; // tope de descarga por celda
// Suelo de lluvia: 0.30 empinaba los canales secos ×1.52 (S ∝ Q^-θ) y las
// cumbres de sotavento se pasaban del contrato 1.30×objetivo (test a escala
// isla: 1862 vs 1820). Con 0.50 el empinamiento queda en ×1.27 y el contraste
// de disección húmedo/seco sigue siendo ×2.5 — visible sin romper la cota.
constexpr float kRainFloor = 0.50f;  // lluvia mínima relativa (sotavento)
// Coeficiente de deposición G (Yuan et al. 2019, ec. 3): adimensional, ~1 en
// sus experimentos. Con G=0 el modelo es detachment-limited puro (todo lo
// erosionado desaparece). La incisión se escala por (1+G): con uplift
// uniforme el estado estacionario queda IDÉNTICO al de G=0 (la calibración de
// cumbres no se toca) y la deposición solo aflora donde la carga que llega de
// aguas arriba supera la que la celda generaría por sí misma: frentes de
// montaña y tierras bajas.
constexpr float kDeposition = 2.0f;
// G depende de la roca de ORIGEN del sedimento (Yuan: G ∝ velocidad de
// sedimentación): la roca dura (lith 0) da clastos gruesos que se depositan
// pronto (G×kGSpan), la blanda (lith 1) da finos que viajan lejos (G/kGSpan).
// El flujo transporta la G media ponderada por volumen de sus fuentes.
constexpr float kGSpan = 2.0f;
// Barridos de Gauss-Seidel del término de sedimento por paso (Yuan: 2-4): el
// flujo depende de las alturas nuevas aguas arriba, que el stack directo
// resuelve DESPUÉS de la celda.
constexpr int kDepositionSweeps = 10;

// Rugosidad inicial (m): perturbación determinista de la condición inicial,
// como en los LEM de referencia (FastScape parte de ruido blanco). Sin ella
// los flancos de uplift suave son planos perfectos y el D8 los drena en
// canales rectos y paralelos alineados con la rejilla (el "peine").
constexpr float kInitialRoughnessM = 4.0f;
// Relieve inicial correlacionado (m, a ~1 km y ~350 m de longitud de onda):
// ondulaciones suaves de la superficie que emerge, como las de una plataforma
// recién levantada. Solo con rugosidad blanca, en los flancos de uplift
// uniforme el primer drenaje bajaba en líneas paralelas por la máxima
// pendiente y el estado estacionario conservaba ese peine de barrancos
// rectos y equiespaciados (la topología de la red apenas se reorganiza
// después). Con ondulaciones el drenaje inicial ya converge en cuencas de
// tamaño desigual y la red sale ramificada. El ruido solo modula la condición
// inicial: las formas finales las talla el stream power.
constexpr float kInitialReliefM = 24.0f;

// Hash entero → [0, 1): determinista por celda, sin estado ni RNG de la STL.
float cell_hash01(std::uint32_t i) {
    std::uint32_t h = i * 0x9E3779B1u;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

// Value noise con el hash de celda (el LEM no depende del mesher ni del
// render): retícula de paso 1 en las unidades de entrada.
float lattice_noise(float x, float z) {
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const auto ix = static_cast<std::uint32_t>(static_cast<std::int32_t>(fx) + (1 << 20));
    const auto iz = static_cast<std::uint32_t>(static_cast<std::int32_t>(fz) + (1 << 20));
    const auto at = [](std::uint32_t a, std::uint32_t b) {
        return cell_hash01(a * 0x8DA6B343u ^ b * 0xD8163841u);
    };
    float tx = x - fx;
    float tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const float top = std::lerp(at(ix, iz), at(ix + 1u, iz), tx);
    const float bot = std::lerp(at(ix, iz + 1u), at(ix + 1u, iz + 1u), tx);
    return std::lerp(top, bot, tz);
}

// Ondulación inicial en [-kInitialReliefM/2, kInitialReliefM/2] en la celda i
// (dos octavas en metros de mundo: no depende de la resolución del grid).
float initial_relief(size_t i, int res, float cell_size_m) {
    const float x = static_cast<float>(i % static_cast<size_t>(res)) * cell_size_m;
    const float z = static_cast<float>(i / static_cast<size_t>(res)) * cell_size_m;
    const float broad = lattice_noise(x * 0.0010f + 17.3f, z * 0.0010f - 41.9f);
    const float fine = lattice_noise(x * 0.0029f - 5.1f, z * 0.0029f + 63.7f);
    return kInitialReliefM * (0.65f * broad + 0.35f * fine - 0.5f);
}

// Diaclasado fino (iter. 9): la fracturación de la litología (~0.7 km) es
// más ancha que el espaciado de los barrancos de primer orden (40-150 m), así
// que a lo largo de un flanco uniforme la erodibilidad no variaba a esa
// escala y el stream power los tallaba en peine: barrancos paralelos,
// equiespaciados y alineados con la máxima pendiente (la arista de cumbre era
// una sierra de dientes iguales con canales de nieve paralelos). Una red de
// diaclasas de ~60 m modula la K por celda: los cauces que caen en roca
// diaclasada ganan y el espaciado sale irregular. Contraste suave y una sola
// escala, por debajo de la de los valles: con ×1.6 y una octava de ~190 m los
// valles mayores seguían al ruido en gusanos y la cumbre bajaba un 20 %. La
// K calibrada es la de la roca MÁS diaclasada (×kJointSpan^0.5); la masiva
// resiste más (hasta ×kJointSpan^-1.5): como los cauces buscan la roca
// blanda, con el rango centrado en 1 la cumbre bajaba un 13 %.
// Coordenadas giradas: el value noise de retícula cuadrada no debe alinear
// los cauces con los ejes.
constexpr float kJointSpan = 1.3f;
constexpr float kJointWavelengthM = 60.0f;

float joint_factor(size_t i, int res, float cell_size_m) {
    const float x = static_cast<float>(i % static_cast<size_t>(res)) * cell_size_m;
    const float z = static_cast<float>(i / static_cast<size_t>(res)) * cell_size_m;
    const float n = lattice_noise((0.8253f * x - 0.5646f * z) / kJointWavelengthM + 29.7f,
                                  (0.5646f * x + 0.8253f * z) / kJointWavelengthM - 29.7f);
    const float t = std::clamp((n - 0.25f) / 0.50f, 0.0f, 1.0f);
    const float j = t * t * (3.0f - 2.0f * t);
    return std::pow(kJointSpan, 2.0f * j - 1.5f);
}

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

std::vector<float> run_lem(const std::vector<float>& uplift,
                           const std::vector<float>& lithology, int macro_res,
                           float cell_size_m, float uplift_total_m,
                           int iterations, glm::vec2 wind,
                           const std::vector<float>& seabed, LemStats* stats) {
    const int res = macro_res;
    const size_t n = static_cast<size_t>(res) * static_cast<size_t>(res);
    const float uplift_per_iter = uplift_total_m / static_cast<float>(iterations);
    // Modo batimetría (N2): mar = h vivo bajo cero, no una máscara de uplift.
    const bool has_seabed = !seabed.empty();

    // Erodibilidad y área crítica por celda desde la litología (invariantes
    // por iteración: precomputadas — el solver del paso 4 es serial y no puede
    // pagar dos pow más por celda×iteración). lith 0.5 → k=1, a0=kChannelAreaM2
    // exactos: la calibración clásica no se toca.
    std::vector<float> k_lith(n, 1.0f);
    std::vector<float> a0(n, kChannelAreaM2);
    if (!lithology.empty()) {
        for (size_t i = 0; i < n; ++i) {
            const float lith = lithology[i];
            k_lith[i] = std::pow(kErodSpan, 2.0f * lith - 1.0f);
            a0[i] = kChannelAreaM2 * std::pow(kA0Span, 1.0f - 2.0f * lith);
        }
    }
    for (size_t i = 0; i < n; ++i) {
        k_lith[i] *= joint_factor(i, res, cell_size_m);
    }

    // Calibración física e independiente del grid: la pendiente de equilibrio
    // es kHeadwaterSlope en el dominio de ladera (A ≤ kChannelAreaM2) y decae
    // como (A0/A)^θ en los canales (perfil cóncavo de Flint). La pendiente
    // máxima del macro queda acotada por diseño. OJO: en estado estacionario
    // S = u·kHeadwaterSlope·(A0/A)^θ — el uplift total se CANCELA (k_dt
    // escala con él); la cota de cumbre la fijan estas constantes y el campo
    // de uplift, no el budget.
    const float k_dt = uplift_per_iter / kHeadwaterSlope;

    // Semilla: gradiente inicial proporcional al uplift, para que el primer
    // routing ya tenga pendientes hacia el mar. Con batimetría, la semilla
    // del mar ES su prior de fondo (la tierra parte de ~0 y se levanta).
    std::vector<float> h(n);
    for (size_t i = 0; i < n; ++i) {
        h[i] = (has_seabed ? seabed[i] : 0.0f) + uplift[i] * 2.0f
             + kInitialRoughnessM * cell_hash01(static_cast<std::uint32_t>(i))
             + initial_relief(i, res, cell_size_m) * std::min(uplift[i] * 4.0f, 1.0f);
    }
    // Distancia efectiva de las diagonales por celda (ρ8, Fairfield y
    // Leymarie 1991): √2 se sustituye por 2 − r con r ∈ [0, 1) fijo por
    // celda. En media equivale al D8 (E[1/(2−r)] = ln 2 ≈ 1/√2), pero rompe
    // el sesgo a 8 direcciones: los canales dejan de ir en rectas de rejilla
    // y la red sale dendrítica.
    std::vector<float> diag_dist(n);
    for (size_t i = 0; i < n; ++i) {
        diag_dist[i] = cell_size_m
                     * (2.0f - cell_hash01(static_cast<std::uint32_t>(i) ^ 0x5bd1e995u));
    }
    fill_pits(h, res);

    // Precipitación orográfica: preparación del pase de humedad. El viento se
    // ajusta a la dirección de grid más cercana (8-dir) para que la celda de
    // barlovento sea un vecino fijo; el recorrido en orden de proyección del
    // viento garantiza que el barlovento ya está resuelto al llegar a cada
    // celda. rain_w == 1 uniforme si no hay viento (comportamiento clásico).
    std::vector<float> rain_w(n, 1.0f);
    const bool has_wind = glm::length(wind) > 0.5f;
    int sdx = 0;
    int sdz = 0;
    std::vector<std::uint32_t> wind_order;
    std::vector<float> moisture;
    if (has_wind) {
        const float a_snap = std::round(std::atan2(wind.y, wind.x) / 0.7853982f)
                           * 0.7853982f;
        sdx = static_cast<int>(std::lround(std::cos(a_snap)));
        sdz = static_cast<int>(std::lround(std::sin(a_snap)));
        wind_order.resize(n);
        std::iota(wind_order.begin(), wind_order.end(), 0u);
        std::sort(wind_order.begin(), wind_order.end(),
                  [&](std::uint32_t a, std::uint32_t b) {
                      const int ax = static_cast<int>(a) % res;
                      const int az = static_cast<int>(a) / res;
                      const int bx = static_cast<int>(b) % res;
                      const int bz = static_cast<int>(b) / res;
                      return ax * sdx + az * sdz < bx * sdx + bz * sdz;
                  });
        moisture.assign(n, 1.0f);
    }
    const float wind_dist =
        cell_size_m * ((sdx != 0 && sdz != 0) ? 1.4142f : 1.0f);
    // Mar a efectos de humedad: en modo batimetría es el RELIEVE VIVO bajo
    // cero (la masa de aire se recarga sobre el agua real, islotes incluidos);
    // en clásico, la máscara de uplift de siempre.
    const auto is_sea = [&](std::uint32_t i) {
        return has_seabed ? h[i] <= 0.0f : uplift[i] < kBaseUplift;
    };
    const auto recompute_rain = [&]() {
        float rain_sum = 0.0f;
        size_t land = 0;
        for (const std::uint32_t i : wind_order) {
            const int x = static_cast<int>(i) % res;
            const int z = static_cast<int>(i) / res;
            const int ux = x - sdx;
            const int uz = z - sdz;
            const bool edge = ux < 0 || uz < 0 || ux >= res || uz >= res;
            const float m_up =
                edge ? 1.0f : moisture[static_cast<size_t>(uz) * res + ux];
            if (is_sea(i)) {
                // Mar: la masa de aire se recarga; no cuenta para la media.
                moisture[i] = m_up + (1.0f - m_up) * 0.35f;
                rain_w[i] = 0.0f;
                continue;
            }
            const float h_up =
                edge ? h[i] : h[static_cast<size_t>(uz) * res + ux];
            const float lift = std::max(0.0f, (h[i] - h_up) / wind_dist);
            const float frac =
                std::min(kRainFracMax, kRainBase + kRainLiftGain * lift);
            const float rain = m_up * frac;
            moisture[i] = m_up - rain;
            rain_w[i] = rain;
            rain_sum += rain;
            ++land;
        }
        if (land == 0 || rain_sum <= 0.0f) {
            std::fill(rain_w.begin(), rain_w.end(), 1.0f);
            return;
        }
        // Normalizada a media 1 sobre tierra con suelo kRainFloor: la
        // calibración global (k_dt, umbrales) no se toca — cambia DÓNDE
        // trabaja el agua, no cuánta hay en total.
        const float inv_mean = static_cast<float>(land) / rain_sum;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i) {
            rain_w[i] = is_sea(i)
                            ? 1.0f
                            : kRainFloor
                                  + (1.0f - kRainFloor) * rain_w[i] * inv_mean;
        }
    };
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
    // Sedimentación: alturas al inicio del paso, coeficiente implícito por
    // celda (<0 = sin incisión) y flujo de sedimento acumulado.
    std::vector<float> h_prev(n);
    std::vector<float> fcoef(n);
    std::vector<double> qs(n);
    std::vector<float> area_geom(n);
    std::vector<double> qsg(n);    // Σ volumen·G de las fuentes
    std::vector<float> alluvium(n, 0.0f);
    std::vector<float> g_src(n, kDeposition);
    if (!lithology.empty()) {
        for (size_t i = 0; i < n; ++i) {
            g_src[i] = kDeposition * std::pow(kGSpan, 1.0f - 2.0f * lithology[i]);
        }
    }
    std::vector<int> rows(static_cast<size_t>(res));
    std::iota(rows.begin(), rows.end(), 0);

    const auto idx = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };

    for (int it = 0; it < iterations; ++it) {
        // Lluvia orográfica sobre el relieve VIVO (misma cadencia que el
        // manejo de lagos): la sombra de lluvia se muda con las montañas.
        if (has_wind && it % 10 == 0) {
            recompute_rain();
        }
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
                    const bool diagonal = kDx[nb] != 0 && kDz[nb] != 0;
                    const float d = diagonal ? cell_size_m * 1.4142f : cell_size_m;
                    const float slope =
                        (h[i] - h[idx(nx, nz)]) / (diagonal ? diag_dist[i] : d);
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
        //    antes que receptores). Con viento es CAUDAL efectivo: cada celda
        //    aporta su área × su lluvia — los valles de sotavento llevan menos
        //    agua y tallan menos, los de barlovento más.
        const float cell_area = cell_size_m * cell_size_m;
        for (size_t i = 0; i < n; ++i) {
            area[i] = cell_area * rain_w[i];
        }
        std::fill(area_geom.begin(), area_geom.end(), cell_area);
        for (size_t s = n; s-- > 0;) {
            const std::uint32_t i = stack[s];
            if (receiver[i] != i) {
                area[receiver[i]] += area[i];
                area_geom[receiver[i]] += area_geom[i];
            }
        }

        // 4) Solver implícito con sedimentación (Yuan et al. 2019):
        //      h' = (h + U + F·h_r' + G·Qs/A) / (1 + F·(1+G))
        //    Qs = volumen neto erosionado aguas arriba en este paso (celda
        //    incluida); A = área drenada GEOMÉTRICA, no el caudal: con el
        //    caudal ponderado por lluvia, en sotavento (lluvia 0.5) la
        //    deposición doblaba a la compensación (1+G) y todo agradaba
        //    (cumbres ×2). La lluvia ya actúa en la incisión. Qs depende de las h' de aguas
        //    arriba, que el stack directo resuelve después: Gauss-Seidel con
        //    el Qs del barrido anterior. El primero parte de h' = h (erosión
        //    = uplift, la hipótesis de equilibrio): arrancar con Qs = 0 y la
        //    incisión ya escalada por (1+G) sobreerosionaba y el barrido
        //    oscilaba (cumbres a la mitad).
        h_prev = h;
        for (size_t i = 0; i < n; ++i) {
            fcoef[i] = -1.0f;
            if (receiver[i] == i) {
                continue; // pit: se rellena y drenará
            }
            if (has_seabed ? h_prev[i] <= 0.0f : uplift[i] < kBaseUplift) {
                continue; // mar: sin incisión fluvial
            }
            // El divisor de dureza hace S ∝ u^(1+kHardness): erosiona más
            // deprisa la roca blanda de poco uplift, menos el núcleo.
            const float hardness =
                std::pow(std::max(uplift[i], 0.05f), kHardness);
            fcoef[i] = (1.0f + g_src[i]) * k_dt * k_lith[i]
                     * std::pow(std::max(area[i], a0[i]) / a0[i], kConcavity)
                     / (rcv_dist[i] * hardness);
        }
        for (int sweep = 0; sweep < kDepositionSweeps; ++sweep) {
            for (size_t i = 0; i < n; ++i) {
                qs[i] = fcoef[i] < 0.0f
                            ? 0.0
                            : static_cast<double>(h_prev[i]
                                                  + uplift_per_iter * uplift[i]
                                                  - h[i])
                                  * cell_area;
                qsg[i] = qs[i] * g_src[i];
            }
            for (size_t s = n; s-- > 0;) {
                const std::uint32_t i = stack[s];
                const std::uint32_t r = receiver[i];
                // El sedimento que llega al mar o a un pit se queda allí:
                // solo viaja entre celdas con incisión.
                if (r != i && fcoef[r] >= 0.0f) {
                    qs[r] += qs[i];
                    qsg[r] += qsg[i];
                }
            }
            for (size_t s = 0; s < n; ++s) {
                const std::uint32_t i = stack[s];
                const float up = uplift_per_iter * uplift[i];
                if (fcoef[i] < 0.0f) {
                    // Nivel de base clásico clavado a 0; fondo marino (solo
                    // tectónica + difusión) o pit: suben con el uplift.
                    h[i] = (!has_seabed && uplift[i] < kBaseUplift)
                               ? 0.0f
                               : h_prev[i] + up;
                    continue;
                }
                // Con batimetría, el nivel de base fluvial es el MAR (h=0): un
                // río que desemboca no talla su lecho por debajo del receptor
                // sumergido — grada a la superficie del agua.
                const std::uint32_t r = receiver[i];
                const float hr = has_seabed ? std::max(h[r], 0.0f) : h[r];
                // G·Qs/A con G la media de las fuentes: Σ(e·G)/A.
                const float dep =
                    static_cast<float>(std::max(qsg[i], 0.0) / area_geom[i]);
                const float target =
                    (h_prev[i] + up + fcoef[i] * hr + dep) / (1.0f + fcoef[i]);
                if (sweep == 0) {
                    h[i] = target;
                } else {
                    // Relajación 1/(1+G): sin ella el barrido NO converge con
                    // G > 1 — una perturbación de toda una cuenca vuelve
                    // multiplicada por ≈ −G y el resultado salta entre dos
                    // estados (cumbre 1090 m con 3 barridos, 342 m con 10).
                    // Las mesetas, paredes y lagos de b583495 eran eso.
                    const double gq = qs[i] > 0.0 ? qsg[i] / qs[i] : g_src[i];
                    const float g_mix = static_cast<float>(std::clamp(gq, 0.0, 16.0));
                    h[i] += (target - h[i]) / (1.0f + g_mix);
                }
            }
        }
        // Aluvión: la agradación neta lo engrosa; la erosión lo consume
        // antes de morder la roca.
        for (size_t i = 0; i < n; ++i) {
            if (fcoef[i] < 0.0f) {
                continue;
            }
            const float d = h[i] - (h_prev[i] + uplift_per_iter * uplift[i]);
            alluvium[i] = std::max(0.0f, alluvium[i] + d);
        }
        if (stats != nullptr && it == iterations - 1) {
            *stats = LemStats{};
            for (size_t i = 0; i < n; ++i) {
                if (fcoef[i] < 0.0f) {
                    continue;
                }
                const double e =
                    static_cast<double>(h_prev[i] + uplift_per_iter * uplift[i] - h[i])
                    * cell_area;
                if (e >= 0.0) {
                    stats->eroded_m3 += e;
                } else {
                    stats->deposited_m3 -= e;
                    ++stats->aggrading_cells;
                }
            }
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
        // Exportación (solo lectura): denudación total del último paso, antes
        // del relleno de pits, que no es erosión sino manejo de lagos.
        if (stats != nullptr && it == iterations - 1 && uplift_per_iter > 0.0f) {
            stats->denudation.resize(n);
            for (size_t i = 0; i < n; ++i) {
                stats->denudation[i] =
                    (h_prev[i] + uplift_per_iter * uplift[i] - h[i])
                    / uplift_per_iter;
            }
        }

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
    if (stats != nullptr) {
        stats->alluvium_m = std::move(alluvium);
        if (has_wind) {
            stats->rain = rain_w;
        }
    }
    // El clamp a ≥0 solo tiene sentido sin batimetría: con fondo marino real
    // el relieve devuelto ES con signo (la costa se corta después en h=0).
    if (!has_seabed) {
        for (float& v : h) {
            v = std::max(v, 0.0f);
        }
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
