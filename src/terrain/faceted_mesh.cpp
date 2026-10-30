#include "terrain/faceted_mesh.h"

#include "render/terrain_shader_constants.h"
#include "terrain/mesher.h"

#include <glm/geometric.hpp>

#include <algorithm>
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
    const int last = hf.resolution() - 1;
    NodeGrid g;
    g.w = (r.cells_x + r.stride - 1) / r.stride;
    g.h = (r.cells_z + r.stride - 1) / r.stride;
    g.pos.resize(static_cast<size_t>(g.w + 1) * static_cast<size_t>(g.h + 1));
    const float amp = kNodeJitter * hf.cell_size() * static_cast<float>(r.stride);
    std::vector<int> rows(static_cast<size_t>(g.h + 1));
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int j) {
        const int gz = r.z0 + std::min(j * r.stride, r.cells_z);
        for (int i = 0; i <= g.w; ++i) {
            const int gx = r.x0 + std::min(i * r.stride, r.cells_x);
            float wx = hf.world_x(gx);
            float wz = hf.world_z(gz);
            if (gx > 0 && gx < last && gz > 0 && gz < last) {
                wx += amp * (2.0f * hash01(gx * 3 + 1, gz * 5 + 7) - 1.0f);
                wz += amp * (2.0f * hash01(gx * 7 + 11, gz * 3 + 2) - 1.0f);
            }
            g.pos[static_cast<size_t>(j) * static_cast<size_t>(g.w + 1) + static_cast<size_t>(i)] =
                {wx, sampler.height(wx, wz), wz};
        }
    });
    return g;
}

// Color y luz de una faceta: campos muestreados en su centroide.
struct FaceShade {
    glm::vec3 color;
    glm::vec2 light;
};

FaceShade shade_face(const DetailSampler& sampler, const Palette& palette,
                     const glm::vec3& c, const glm::vec3& n, std::uint32_t id) {
    const glm::vec2 mw = material_warp(c.x, c.z);
    const float cx = c.x + mw.x;
    const float cz = c.z + mw.y;
    const Material m = sampler.material_near(cx, cz);
    const glm::vec3 base = palette.ground(m, sampler.forest_density_near(cx, cz),
                                          sampler.climate_near(cx, cz), c.y, c.x, c.z,
                                          cx, cz);
    return {palette.face(base, m, n, c.y, cx, cz, id),
            {sampler.shadow_at(c.x, c.z), sampler.sky_ao_at(c.x, c.z)}};
}

// Desde este paso de rejilla la faceta cubre decenas de metros: su color es
// la media de cuatro muestras (centroide y puntos medios hacia cada vértice)
// en vez de la del centroide, que en lenguas finas lejanas (flechas, puntas)
// alternaba arena/hierba/roca cara a cara en un perfil de sierra.
constexpr int kAverageColourStride = 4;

FaceShade shade_face_area(const DetailSampler& sampler, const Palette& palette,
                          const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                          const glm::vec3& n, std::uint32_t id) {
    const glm::vec3 g = (a + b + c) / 3.0f;
    FaceShade acc = shade_face(sampler, palette, g, n, id);
    for (const glm::vec3* v : {&a, &b, &c}) {
        const FaceShade s = shade_face(sampler, palette, (g + *v) * 0.5f, n, id);
        acc.color += s.color;
        acc.light += s.light;
    }
    return {acc.color * 0.25f, acc.light * 0.25f};
}

void emit_triangle(const DetailSampler& sampler, const Palette& palette,
                   const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                   std::uint32_t id, int stride, render::Vertex* out) {
    const glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    const FaceShade s = stride >= kAverageColourStride
        ? shade_face_area(sampler, palette, a, b, c, n, id)
        : shade_face(sampler, palette, (a + b + c) / 3.0f, n, id);
    out[0] = render::Vertex{a, s.color, n, s.light};
    out[1] = render::Vertex{b, s.color, n, s.light};
    out[2] = render::Vertex{c, s.color, n, s.light};
}

// Faldón: quad vertical bajo la arista a→b, con el color de su punto medio.
void emit_skirt(const DetailSampler& sampler, const Palette& palette,
                const glm::vec3& a, const glm::vec3& b, std::vector<render::Vertex>& out) {
    const glm::vec3 da{a.x, a.y - kSkirtDepth, a.z};
    const glm::vec3 db{b.x, b.y - kSkirtDepth, b.z};
    const glm::vec3 mid = (a + b) * 0.5f;
    const glm::vec3 n = glm::normalize(glm::cross(b - a, glm::vec3{0.0f, -1.0f, 0.0f}));
    const FaceShade s = shade_face(sampler, palette, mid, glm::vec3{0.0f, 1.0f, 0.0f}, 0u);
    for (const glm::vec3& p : {a, da, b, b, da, db}) {
        out.push_back(render::Vertex{p, s.color, n, s.light});
    }
}

} // namespace

render::Mesh build_faceted_mesh(const TerrainData& data, const DetailSampler& sampler,
                                const Palette& palette, const FacetRegion& region) {
    const NodeGrid g = build_nodes(data, sampler, region);
    std::vector<render::Vertex> vertices(static_cast<size_t>(g.w) * g.h * 6);
    std::vector<int> rows(static_cast<size_t>(g.h));
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int j) {
        const int gz = region.z0 + j * region.stride;
        for (int i = 0; i < g.w; ++i) {
            const int gx = region.x0 + i * region.stride;
            const glm::vec3& tl = g.at(i, j);
            const glm::vec3& tr = g.at(i + 1, j);
            const glm::vec3& bl = g.at(i, j + 1);
            const glm::vec3& br = g.at(i + 1, j + 1);
            render::Vertex* out = &vertices[(static_cast<size_t>(j) * g.w + i) * 6];
            const auto id = static_cast<std::uint32_t>(gz) * 65536u + static_cast<std::uint32_t>(gx);
            // Diagonal por hash del quad global: la triangulación no dibuja
            // rayas paralelas a la rejilla.
            if (hash01(gx * 13 + 5, gz * 17 + 3) < 0.5f) {
                emit_triangle(sampler, palette, tl, bl, tr, id * 2u, region.stride, out);
                emit_triangle(sampler, palette, tr, bl, br, id * 2u + 1u, region.stride, out + 3);
            } else {
                emit_triangle(sampler, palette, tl, bl, br, id * 2u, region.stride, out);
                emit_triangle(sampler, palette, tl, br, tr, id * 2u + 1u, region.stride, out + 3);
            }
        }
    });
    vertices.reserve(vertices.size() + 12 * static_cast<size_t>(g.w + g.h));
    for (int i = 0; i < g.w; ++i) {
        emit_skirt(sampler, palette, g.at(i, 0), g.at(i + 1, 0), vertices);
        emit_skirt(sampler, palette, g.at(i + 1, g.h), g.at(i, g.h), vertices);
    }
    for (int j = 0; j < g.h; ++j) {
        emit_skirt(sampler, palette, g.at(0, j + 1), g.at(0, j), vertices);
        emit_skirt(sampler, palette, g.at(g.w, j), g.at(g.w, j + 1), vertices);
    }
    std::vector<std::uint32_t> indices(vertices.size());
    std::iota(indices.begin(), indices.end(), 0u);
    return render::Mesh{vertices, indices};
}

} // namespace terrain
