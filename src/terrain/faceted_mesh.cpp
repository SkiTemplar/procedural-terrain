#include "terrain/faceted_mesh.h"

#include "render/terrain_shader_constants.h"
#include "terrain/mesher.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <execution>
#include <numeric>
#include <vector>

namespace terrain {

namespace {

constexpr float kSkirtDepth = 5.0f; // m hacia abajo para tapar grietas de LOD
// Desplazamiento XZ máximo de un nodo, en fracción del paso de rejilla: rompe
// la retícula sin crear triángulos degenerados (dos nodos vecinos nunca se
// acercan a menos de la mitad del paso).
constexpr float kNodeJitter = 0.25f;

// Domain warp del muestreo de material (feedback 2026-07-08 "colores en
// cuadrado"): desplaza la posición donde se lee el material unos metros según
// un ruido suave → los bordes hierba↔roca↔arena SERPENTEAN en vez de seguir la
// rejilla de celdas. Solo afecta al COLOR; la altura usa la posición real.
// Las constantes viven en render/terrain_shader_constants.h: el fragment del
// terreno replica este warp (mismo ruido) para la textura de detalle.
glm::vec2 material_warp(float wx, float wz) {
    const float k = PT_MATERIAL_WARP_AMP;
    const float f = PT_MATERIAL_WARP_FREQ;
    const float nx = value_noise(wx * f + PT_MATERIAL_WARP_X_OFFSET_X,
                                 wz * f + PT_MATERIAL_WARP_X_OFFSET_Z) - 0.5f;
    const float nz = value_noise(wx * f + PT_MATERIAL_WARP_Z_OFFSET_X,
                                 wz * f + PT_MATERIAL_WARP_Z_OFFSET_Z) - 0.5f;
    return {nx * 2.0f * k, nz * 2.0f * k};
}

// Prefiltrado de la altura en los niveles bastos (paso >= kFilterStride
// celdas): una cresta más estrecha que la faceta (cordón de duna, arista de
// cumbre) muestreada en puntos sueltos se dibujaba en dientes de sierra, con
// nodos alternos dentro y fuera de ella, y un rizado de periodo parecido al
// paso batía en una fila de "tiendas". Binomial 5×5 (1 4 6 4 1) con
// muestras a ±r/2 y ±r, r = un paso: el mip de la superficie a la escala de la
// faceta. La tienda 3×3 de medio paso anulaba el periodo = paso pero dejaba
// pasar ~25-50 % de los periodos entre 1 y 2 pasos, y los barrancos paralelos
// del LEM batían con la rejilla en franjas claras y oscuras alineadas con sus
// ejes; la binomial los deja por debajo del 25 % (0 en periodo 1-1.3 pasos).
// Iter. 9: en las facetas de 39-78 m (vistas de 2-7 km) los barrancos de
// 120-200 m caen en 1.5-2.5 pasos y aun así pasaban un 25-40 %: la ladera sur
// se dibujaba como un código de barras de columnas alternas claras y oscuras
// alineadas con la rejilla, que la exageración de relieve del fragment
// triplicaba. Con r = 1.5 pasos la respuesta en 2-2.5 pasos baja al 2-10 %
// y un relieve de 4+ pasos conserva la mitad o más.
constexpr int kFilterStride = 2;
constexpr float kFilterRadius = 1.0f;
constexpr int kWideFilterStride = 16;
constexpr float kWideFilterRadius = 1.5f;

// Radio del prefiltro para una faceta de `cells` celdas (0 = sin filtro).
float filter_radius(float cells, float cell_m) {
    if (cells < static_cast<float>(kFilterStride)) {
        return 0.0f;
    }
    const float r = cells >= static_cast<float>(kWideFilterStride) ? kWideFilterRadius
                                                                   : kFilterRadius;
    return r * cells * cell_m;
}

float node_height(const DetailSampler& sampler, float wx, float wz, float radius) {
    if (radius <= 0.0f) {
        return sampler.height(wx, wz);
    }
    constexpr float kBinomial[5] = {1.0f, 4.0f, 6.0f, 4.0f, 1.0f};
    const float half = 0.5f * radius;
    float acc = 0.0f;
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            const float w = kBinomial[dx + 2] * kBinomial[dz + 2];
            acc += w * sampler.height(wx + half * static_cast<float>(dx),
                                      wz + half * static_cast<float>(dz));
        }
    }
    return acc / 256.0f;
}

// Rejilla de nodos (posiciones de mundo) de una región, con jitter XZ por
// nodo global. Los nodos del borde del MAPA no se mueven (el mapa acaba recto).
struct NodeGrid {
    int w = 0; // quads por lado
    int h = 0;
    std::vector<glm::vec3> pos;
    const glm::vec3& at(int i, int j) const {
        return pos[static_cast<size_t>(j) * static_cast<size_t>(w + 1) + static_cast<size_t>(i)];
    }
};

NodeGrid build_nodes(const TerrainData& data, const DetailSampler& sampler,
                     const FacetRegion& r) {
    const auto& hf = data.height;
    // Coordenadas en subceldas: con sub = 1 coinciden con las celdas y el
    // hash del jitter es el de siempre (los niveles lejanos no cambian).
    const int last = (hf.resolution() - 1) * r.sub;
    const int span_x = r.cells_x * r.sub;
    const int span_z = r.cells_z * r.sub;
    const float step = hf.cell_size() / static_cast<float>(r.sub);
    NodeGrid g;
    g.w = (span_x + r.stride - 1) / r.stride;
    g.h = (span_z + r.stride - 1) / r.stride;
    g.pos.resize(static_cast<size_t>(g.w + 1) * static_cast<size_t>(g.h + 1));
    const float amp = kNodeJitter * step * static_cast<float>(r.stride);
    const float origin = hf.world_x(0);
    const float own_cells = static_cast<float>(r.stride) / static_cast<float>(r.sub);
    const float own_filter = filter_radius(own_cells, hf.cell_size());
    // Filtro de los nodos de cada borde: el del lado más basto, para que el
    // vecino y este chunk muestreen igual los nodos que comparten.
    std::array<float, 4> edge_filter{};
    for (size_t e = 0; e < 4; ++e) {
        edge_filter[e] = std::max(own_filter, filter_radius(r.edge_cells[e], hf.cell_size()));
    }
    std::vector<int> rows(static_cast<size_t>(g.h + 1));
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int j) {
        const int gz = r.z0 * r.sub + std::min(j * r.stride, span_z);
        for (int i = 0; i <= g.w; ++i) {
            const int gx = r.x0 * r.sub + std::min(i * r.stride, span_x);
            float wx = origin + static_cast<float>(gx) * step;
            float wz = origin + static_cast<float>(gz) * step;
            // El borde del chunk no se desplaza: dos niveles de LOD vecinos
            // muestrean la arista en los mismos puntos (los del basto son un
            // subconjunto de los del fino) y el escalón que tapa el faldón
            // queda en centímetros en vez de medio metro en ladera.
            const bool edge = i == 0 || i == g.w || j == 0 || j == g.h;
            if (!edge && gx > 0 && gx < last && gz > 0 && gz < last) {
                wx += amp * (2.0f * hash01(gx * 3 + 1, gz * 5 + 7) - 1.0f);
                wz += amp * (2.0f * hash01(gx * 7 + 11, gz * 3 + 2) - 1.0f);
            }
            float filter_m = own_filter;
            filter_m = std::max(filter_m, j == 0 ? edge_filter[0] : 0.0f);
            filter_m = std::max(filter_m, j == g.h ? edge_filter[1] : 0.0f);
            filter_m = std::max(filter_m, i == 0 ? edge_filter[2] : 0.0f);
            filter_m = std::max(filter_m, i == g.w ? edge_filter[3] : 0.0f);
            g.pos[static_cast<size_t>(j) * static_cast<size_t>(g.w + 1) + static_cast<size_t>(i)] =
                {wx, node_height(sampler, wx, wz, filter_m), wz};
        }
    });
    return g;
}

// Costura sin escalón con un vecino más basto: en ese borde los nodos que el
// vecino no tiene toman la interpolación lineal de los que sí comparten
// (cada `ratio` nodos), así que la arista es la misma recta que dibuja él.
// El faldón queda para los instantes en que un vecino aún no se ha rehecho.
void stitch_line(NodeGrid& g, int ratio, bool row, int fixed) {
    const int n = row ? g.w : g.h;
    const auto at = [&](int k) -> glm::vec3& {
        const int i = row ? k : fixed;
        const int j = row ? fixed : k;
        return g.pos[static_cast<size_t>(j) * static_cast<size_t>(g.w + 1) + static_cast<size_t>(i)];
    };
    for (int k = 0; k < n; k += ratio) {
        const int k1 = std::min(k + ratio, n);
        const float y0 = at(k).y;
        const float y1 = at(k1).y;
        for (int m = k + 1; m < k1; ++m) {
            const float t = static_cast<float>(m - k) / static_cast<float>(k1 - k);
            at(m).y = y0 + (y1 - y0) * t;
        }
    }
}

void stitch_edges(NodeGrid& g, const FacetRegion& r) {
    const float own_cells = static_cast<float>(r.stride) / static_cast<float>(r.sub);
    for (int e = 0; e < 4; ++e) {
        const int ratio = static_cast<int>(
            std::lround(r.edge_cells[static_cast<size_t>(e)] / own_cells));
        if (ratio < 2) {
            continue;
        }
        const bool row = e < 2; // z0 / z1 son filas de nodos
        const int fixed = (e == 0 || e == 2) ? 0 : (row ? g.h : g.w);
        stitch_line(g, ratio, row, fixed);
    }
}

// Color y luz de un nodo: campos muestreados en su posición.
struct FaceShade {
    glm::vec3 color;
    glm::vec2 light;
};

FaceShade shade_face(const DetailSampler& sampler, const Palette& palette,
                     const glm::vec3& c, const glm::vec3& n, std::uint32_t id,
                     float facet_m = 0.0f) {
    const glm::vec2 mw = material_warp(c.x, c.z);
    const float cx = c.x + mw.x;
    const float cz = c.z + mw.y;
    const Material m = sampler.material_near(cx, cz);
    const glm::vec3 base = palette.ground(m, sampler.forest_density_near(cx, cz),
                                          sampler.climate_near(cx, cz), c.y, c.x, c.z,
                                          cx, cz);
    return {palette.face(base, m, n, c.y, cx, cz, id, facet_m),
            {sampler.shadow_at(c.x, c.z), sampler.sky_ao_at(c.x, c.z)}};
}

// Desde este paso de rejilla el nodo representa decenas de metros: su color
// es la media de cinco muestras (el nodo y cuatro a un cuarto de paso) en
// vez de la del punto, que en lenguas finas lejanas (flechas, puntas)
// alternaba arena/hierba/roca nodo a nodo en un perfil de sierra.
constexpr int kAverageColourStride = 4;

// Estilo cartoon (feedback 2026-10-30, "menos poligonal"): el color y la luz
// se calculan por NODO y se interpolan entre los vértices, en lugar de un
// color plano por triángulo. Los nodos se comparten entre caras vecinas, así
// que la ladera es un degradado continuo y no un mosaico de triángulos. El id
// sale de la posición del nodo (medio metro): dos chunks que comparten un
// nodo le dan el mismo color, y los umbrales de pick() siguen repartidos
// alrededor de 0.5 (con un id constante todos se desplazaban a la vez y la
// nieve de toda la isla menguaba).
std::uint32_t node_id(const glm::vec3& p) {
    const auto qx = static_cast<std::uint32_t>(std::lround(p.x * 2.0f) + (1 << 20));
    const auto qz = static_cast<std::uint32_t>(std::lround(p.z * 2.0f) + (1 << 20));
    return qx * 73856093u ^ qz * 19349663u;
}

FaceShade shade_node(const DetailSampler& sampler, const Palette& palette,
                     const glm::vec3& p, float facet_m, int cells) {
    const glm::vec3 n = sampler.normal_at_scale(p.x, p.z, facet_m);
    const std::uint32_t id = node_id(p);
    FaceShade acc = shade_face(sampler, palette, p, n, id, facet_m);
    if (cells < kAverageColourStride) {
        return acc;
    }
    const float q = 0.25f * facet_m;
    for (const glm::vec2 o : {glm::vec2{q, 0.0f}, glm::vec2{-q, 0.0f},
                              glm::vec2{0.0f, q}, glm::vec2{0.0f, -q}}) {
        const glm::vec3 s_p{p.x + o.x, p.y, p.z + o.y};
        const FaceShade s = shade_face(sampler, palette, s_p, n, id, facet_m);
        acc.color += s.color;
        acc.light += s.light;
    }
    return {acc.color * 0.2f, acc.light * 0.2f};
}

std::vector<FaceShade> shade_nodes(const DetailSampler& sampler, const Palette& palette,
                                   const NodeGrid& g, float facet_m, int cells) {
    std::vector<FaceShade> shades(g.pos.size());
    std::vector<int> rows(static_cast<size_t>(g.h + 1));
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int j) {
        for (int i = 0; i <= g.w; ++i) {
            const size_t k = static_cast<size_t>(j) * static_cast<size_t>(g.w + 1)
                           + static_cast<size_t>(i);
            shades[k] = shade_node(sampler, palette, g.pos[k], facet_m, cells);
        }
    });
    return shades;
}

// Normal de referencia nula: el fragment no exagera la cara (sombreado suave).
render::Vertex node_vertex(const glm::vec3& p, const FaceShade& s) {
    return render::Vertex{p, s.color, glm::vec3{0.0f}, s.light};
}

// Faldón: quad vertical bajo la arista a→b, con el color de sus dos nodos
// (se funde con la cara de al lado en vez de dibujar una franja vertical).
void emit_skirt(const glm::vec3& a, const glm::vec3& b, const FaceShade& sa,
                const FaceShade& sb, std::vector<render::Vertex>& out) {
    const glm::vec3 da{a.x, a.y - kSkirtDepth, a.z};
    const glm::vec3 db{b.x, b.y - kSkirtDepth, b.z};
    out.push_back(node_vertex(a, sa));
    out.push_back(node_vertex(da, sa));
    out.push_back(node_vertex(b, sb));
    out.push_back(node_vertex(b, sb));
    out.push_back(node_vertex(da, sa));
    out.push_back(node_vertex(db, sb));
}

} // namespace

render::Mesh build_faceted_mesh(const TerrainData& data, const DetailSampler& sampler,
                                const Palette& palette, const FacetRegion& region) {
    NodeGrid g = build_nodes(data, sampler, region);
    stitch_edges(g, region);
    const int cells = region.stride / region.sub; // celdas por faceta (0 = subcelda)
    // Lado de la faceta en metros: escala a la que se mide el relieve que
    // decide el color (nieve, afloramiento, canchal).
    const float facet_m = data.height.cell_size() * static_cast<float>(region.stride)
                        / static_cast<float>(region.sub);
    const std::vector<FaceShade> shades = shade_nodes(sampler, palette, g, facet_m, cells);
    const auto shade_at = [&](int i, int j) -> const FaceShade& {
        return shades[static_cast<size_t>(j) * static_cast<size_t>(g.w + 1) + static_cast<size_t>(i)];
    };
    std::vector<render::Vertex> vertices(static_cast<size_t>(g.w) * g.h * 6);
    std::vector<int> rows(static_cast<size_t>(g.h));
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int j) {
        const int gz = region.z0 * region.sub + j * region.stride;
        for (int i = 0; i < g.w; ++i) {
            const int gx = region.x0 * region.sub + i * region.stride;
            const render::Vertex tl = node_vertex(g.at(i, j), shade_at(i, j));
            const render::Vertex tr = node_vertex(g.at(i + 1, j), shade_at(i + 1, j));
            const render::Vertex bl = node_vertex(g.at(i, j + 1), shade_at(i, j + 1));
            const render::Vertex br = node_vertex(g.at(i + 1, j + 1), shade_at(i + 1, j + 1));
            render::Vertex* out = &vertices[(static_cast<size_t>(j) * g.w + i) * 6];
            // Diagonal por hash del quad global: la triangulación no dibuja
            // rayas paralelas a la rejilla.
            if (hash01(gx * 13 + 5, gz * 17 + 3) < 0.5f) {
                out[0] = tl; out[1] = bl; out[2] = tr;
                out[3] = tr; out[4] = bl; out[5] = br;
            } else {
                out[0] = tl; out[1] = bl; out[2] = br;
                out[3] = tl; out[4] = br; out[5] = tr;
            }
        }
    });
    vertices.reserve(vertices.size() + 12 * static_cast<size_t>(g.w + g.h));
    for (int i = 0; i < g.w; ++i) {
        emit_skirt(g.at(i, 0), g.at(i + 1, 0), shade_at(i, 0), shade_at(i + 1, 0), vertices);
        emit_skirt(g.at(i + 1, g.h), g.at(i, g.h), shade_at(i + 1, g.h), shade_at(i, g.h),
                   vertices);
    }
    for (int j = 0; j < g.h; ++j) {
        emit_skirt(g.at(0, j + 1), g.at(0, j), shade_at(0, j + 1), shade_at(0, j), vertices);
        emit_skirt(g.at(g.w, j), g.at(g.w, j + 1), shade_at(g.w, j), shade_at(g.w, j + 1),
                   vertices);
    }
    std::vector<std::uint32_t> indices(vertices.size());
    std::iota(indices.begin(), indices.end(), 0u);
    return render::Mesh{vertices, indices};
}

} // namespace terrain
