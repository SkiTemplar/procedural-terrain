#include "terrain/mesher.h"

#include "terrain/water_flow.h"
#include "terrain/water_surface.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace terrain {

glm::vec3 material_color(Material m) {
    switch (m) {
        case Material::Seabed: return {0.55f, 0.53f, 0.38f};
        case Material::Sand: return {0.86f, 0.79f, 0.58f};
        case Material::Grass: return {0.42f, 0.62f, 0.28f};
        case Material::Forest: return {0.24f, 0.45f, 0.20f};
        case Material::Rock: return {0.49f, 0.46f, 0.43f};
        case Material::Snow: return {0.93f, 0.94f, 0.96f};
        case Material::RiverBed: return {0.56f, 0.53f, 0.43f};
        case Material::LakeBed: return {0.42f, 0.42f, 0.32f};
        case Material::Gravel: return {0.58f, 0.55f, 0.50f};
        case Material::Marsh: return {0.45f, 0.49f, 0.30f};
    }
    return {1.0f, 0.0f, 1.0f};
}

float hash01(int x, int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                    + static_cast<std::uint32_t>(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

float value_noise(float x, float z) {
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const int xi = static_cast<int>(fx);
    const int zi = static_cast<int>(fz);
    float tx = x - fx;
    float tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const float a = hash01(xi, zi);
    const float b = hash01(xi + 1, zi);
    const float c = hash01(xi, zi + 1);
    const float d = hash01(xi + 1, zi + 1);
    return glm::mix(glm::mix(a, b, tx), glm::mix(c, d, tx), tz);
}

namespace {

// Pendiente máxima de la normal de lámina: por encima solo hay cascadas, que
// ya pinta la espuma; acota el vuelco de las celdas de orilla hundidas.
constexpr float kMaxSurfaceSlope = 0.35f;

// Altura de lámina de la muestra (x, z) o la de (cx, cz) si está fuera del
// dominio o la malla no la usa (NaN): diferencia lateral en la orilla.
float surface_y_or(const WaterSurface& s, int x, int z, int cx, int cz) {
    if (x < 0 || z < 0 || x >= s.res || z >= s.res) {
        return s.vertex_y[static_cast<size_t>(cz) * s.res + cx];
    }
    const float y = s.vertex_y[static_cast<size_t>(z) * s.res + x];
    return std::isnan(y) ? s.vertex_y[static_cast<size_t>(cz) * s.res + cx] : y;
}

// Normal SUAVE de la lámina por diferencias centrales de su altura (la de la
// malla). Interpolada por vértice sustituye a la normal de cara dFdx/dFdy,
// cuyo salto entre triángulos vecinos dibujaba un damero en el reflejo.
glm::vec3 surface_normal(const WaterSurface& s, int x, int z, float cell) {
    const float dx = surface_y_or(s, x + 1, z, x, z) - surface_y_or(s, x - 1, z, x, z);
    const float dz = surface_y_or(s, x, z + 1, x, z) - surface_y_or(s, x, z - 1, x, z);
    glm::vec2 slope{dx / (2.0f * cell), dz / (2.0f * cell)};
    const float len = glm::length(slope);
    if (len > kMaxSurfaceSlope) {
        slope *= kMaxSurfaceSlope / len;
    }
    return glm::normalize(glm::vec3{-slope.x, 1.0f, -slope.y});
}

} // namespace

std::optional<render::Mesh> build_water_mesh(const TerrainData& data) {
    const auto& hf = data.height;
    const int res = hf.resolution();
    const auto sample_index = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };

    // Alturas y cobertura de la lámina (terrain/water_surface): la misma
    // geometría que recorta el plano del mar, así que no quedan huecos.
    const WaterSurface surface = compute_water_surface(data);

    // C4-W3 (auditoría 2026-10 #1): CORRIENTE por vértice en m/s — dirección
    // del eje del cauce suavizada a lo largo del río y módulo de Manning
    // (terrain/water_flow). Los lagos y las orillas sin lámina llevan v = 0.
    // El shader advecta el rizado con este vector (flow map de dos fases),
    // sin depender de la posición absoluta.
    const RiverFlowField flow = compute_river_flow(data);

    // Malla indexada con vértices compartidos entre celdas vecinas (~4x menos
    // memoria y ancho de banda que un quad suelto por celda).
    constexpr std::uint32_t kNoVertex = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> vertex_of(static_cast<size_t>(res) * res, kNoVertex);
    std::vector<render::Vertex> vertices;
    std::vector<std::uint32_t> indices;

    const auto get_or_create = [&](int x, int z) {
        const size_t i = sample_index(x, z);
        if (vertex_of[i] != kNoVertex) {
            return vertex_of[i];
        }
        const float y = surface.vertex_y[i];
        vertex_of[i] = static_cast<std::uint32_t>(vertices.size());
        // C4-W1: PROFUNDIDAD REAL por vértice (lámina − lecho) en normal.y —
        // el canal normal está libre en el agua. El shader la usa para el
        // color Beer-Lambert: orillas someras claras, pozas y ejes oscuros.
        // Sin ruido: es el calado físico del cauce tallado.
        const float depth = std::max(0.0f, y - hf.at(x, z));
        const glm::vec2 vel = flow.at(i);
        // light.x lleva la marca de whitewater de la hidrología (0 = agua
        // tranquila, 0.5 = rápidos, 1 = cascada) para la espuma del shader;
        // light.y = 1 en lagos (oleaje de viento por vértice, sin corriente).
        const float ww =
            static_cast<float>(data.hydrology.whitewater[i]) * 0.5f;
        const float is_lake = data.hydrology.lake[i] ? 1.0f : 0.0f;
        // color = normal suave de la lámina (el agua no lleva tinte: su
        // color sale de la óptica del shader).
        vertices.push_back({{hf.world_x(x), y, hf.world_z(z)},
                            surface_normal(surface, x, z, hf.cell_size()),
                            {vel.x, depth, vel.y},
                            {ww, is_lake}});
        return vertex_of[i];
    };

    for (int z = 0; z < res - 1; ++z) {
        for (int x = 0; x < res - 1; ++x) {
            if (!surface.quad_covered[surface.quad_index(x, z)]) {
                continue;
            }
            const std::uint32_t v00 = get_or_create(x, z);
            const std::uint32_t v10 = get_or_create(x + 1, z);
            const std::uint32_t v01 = get_or_create(x, z + 1);
            const std::uint32_t v11 = get_or_create(x + 1, z + 1);
            indices.insert(indices.end(), {v00, v01, v10, v10, v01, v11});
        }
    }

    if (indices.empty()) {
        return std::nullopt;
    }
    return render::Mesh{vertices, indices};
}

namespace {

// Contorno de la rejilla del mar (índices de vértice) en sentido único,
// empezando en la esquina (0, 0): 4·kRes vértices sin repetir.
std::vector<std::uint32_t> sea_grid_border(int res) {
    std::vector<std::uint32_t> border;
    border.reserve(static_cast<size_t>(4 * res));
    const auto at = [res](int x, int z) {
        return static_cast<std::uint32_t>(z * (res + 1) + x);
    };
    for (int x = 0; x < res; ++x) border.push_back(at(x, 0));
    for (int z = 0; z < res; ++z) border.push_back(at(res, z));
    for (int x = res; x > 0; --x) border.push_back(at(x, res));
    for (int z = res; z > 0; --z) border.push_back(at(0, z));
    return border;
}

// FALDÓN HASTA EL HORIZONTE: un anillo de quads une el contorno de la
// rejilla con un círculo de kSeaHorizonRadiusM. La Tierra plana del mundo no
// tiene horizonte geométrico; a esa distancia la niebla con altura ya ha
// saturado al color del cielo desde cualquier altitud de vuelo, así que el
// mar se funde en la bruma sin borde recto. El render lo dibuja con depth
// clamp (el anillo queda más allá del plano lejano de la cámara).
void append_sea_skirt(std::vector<render::Vertex>& vertices,
                      std::vector<std::uint32_t>& indices, int res, float sea_level) {
    const std::vector<std::uint32_t> border = sea_grid_border(res);
    const auto first_outer = static_cast<std::uint32_t>(vertices.size());
    for (const std::uint32_t b : border) {
        const glm::vec3 p = vertices[b].position;
        const glm::vec2 dir = glm::normalize(glm::vec2{p.x, p.z});
        vertices.push_back({{dir.x * kSeaHorizonRadiusM, sea_level,
                             dir.y * kSeaHorizonRadiusM},
                            {0.0f, 1.0f, 0.0f}});
    }
    const auto n = static_cast<std::uint32_t>(border.size());
    for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint32_t k1 = (k + 1) % n;
        const std::uint32_t a = border[k], b = border[k1];
        const std::uint32_t c = first_outer + k1, d = first_outer + k;
        // Mismo sentido de giro que la rejilla (normal geométrica +y).
        const glm::vec3 e1 = vertices[b].position - vertices[a].position;
        const glm::vec3 e2 = vertices[d].position - vertices[a].position;
        if (glm::cross(e1, e2).y < 0.0f) {
            indices.insert(indices.end(), {a, d, b, b, d, c});
        } else {
            indices.insert(indices.end(), {a, b, d, b, c, d});
        }
    }
}

} // namespace

render::Mesh build_sea_plane(float island_size_m, float sea_level) {
    const float half = sea_plane_half(island_size_m);

    // Subdividido para que las olas de vértice del shader se noten. El color
    // del vértice es la normal de reposo de la lámina (+y), como en los ríos.
    constexpr int kRes = kSeaPlaneRes;
    std::vector<render::Vertex> vertices;
    vertices.reserve((kRes + 1) * (kRes + 1) + 4 * kRes);
    for (int z = 0; z <= kRes; ++z) {
        for (int x = 0; x <= kRes; ++x) {
            const float wx = -half + 2.0f * half * static_cast<float>(x) / kRes;
            const float wz = -half + 2.0f * half * static_cast<float>(z) / kRes;
            vertices.push_back({{wx, sea_level, wz}, {0.0f, 1.0f, 0.0f}});
        }
    }
    std::vector<std::uint32_t> indices;
    indices.reserve(kRes * kRes * 6 + 4 * kRes * 6);
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            const auto tl = static_cast<std::uint32_t>(z * (kRes + 1) + x);
            const auto tr = tl + 1;
            const auto bl = tl + kRes + 1;
            const auto br = bl + 1;
            indices.insert(indices.end(), {tl, bl, tr, tr, bl, br});
        }
    }
    append_sea_skirt(vertices, indices, kRes, sea_level);
    return render::Mesh{vertices, indices};
}

} // namespace terrain
