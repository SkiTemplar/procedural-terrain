#include "terrain/mesher.h"

#include <glm/glm.hpp>

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
    }
    return {1.0f, 0.0f, 1.0f};
}

float hash01(int x, int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                    + static_cast<std::uint32_t>(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

namespace {

constexpr float kWaterSink = 0.12f; // baja la lámina para no pelear con la orilla
const glm::vec3 kLakeColor{0.16f, 0.36f, 0.52f};

} // namespace

std::optional<render::Mesh> build_water_mesh(const TerrainData& data) {
    const auto& hf = data.height;
    const int res = hf.resolution();
    const auto sample_index = [res](int x, int z) {
        return static_cast<size_t>(z) * static_cast<size_t>(res) + static_cast<size_t>(x);
    };

    std::vector<render::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    const auto& wl = data.hydrology.water_level;

    // Malla indexada con vértices compartidos entre celdas vecinas (~4x menos
    // memoria y ancho de banda que un quad suelto por celda).
    constexpr std::uint32_t kNoVertex = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> vertex_of(static_cast<size_t>(res) * res, kNoVertex);

    const auto get_or_create = [&](int x, int z, float fallback) {
        const size_t i = sample_index(x, z);
        if (vertex_of[i] != kNoVertex) {
            return vertex_of[i];
        }
        const float own = wl[i];
        const float y = (std::isnan(own) ? fallback : own) - kWaterSink;
        const float tint = 0.95f + 0.10f * hash01(x, z);
        vertex_of[i] = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({{hf.world_x(x), y, hf.world_z(z)}, kLakeColor * tint});
        return vertex_of[i];
    };

    for (int z = 0; z < res - 1; ++z) {
        for (int x = 0; x < res - 1; ++x) {
            // Fallback = MÍNIMO de los niveles definidos: con el máximo la
            // lámina se estiraba hacia arriba sobre las orillas.
            float level = 1e9f;
            bool any = false;
            for (const auto [dx, dz] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) {
                const float v = wl[sample_index(x + dx, z + dz)];
                if (!std::isnan(v)) {
                    level = std::min(level, v);
                    any = true;
                }
            }
            if (!any) {
                continue;
            }

            const std::uint32_t v00 = get_or_create(x, z, level);
            const std::uint32_t v10 = get_or_create(x + 1, z, level);
            const std::uint32_t v01 = get_or_create(x, z + 1, level);
            const std::uint32_t v11 = get_or_create(x + 1, z + 1, level);
            indices.insert(indices.end(), {v00, v01, v10, v10, v01, v11});
        }
    }

    if (indices.empty()) {
        return std::nullopt;
    }
    return render::Mesh{vertices, indices};
}

render::Mesh build_sea_plane(float island_size_m, float sea_level) {
    const float half = island_size_m * 2.0f;
    const glm::vec3 sea_color{0.13f, 0.32f, 0.50f};

    // Subdividido para que las olas de vértice del shader se noten.
    constexpr int kRes = 64;
    std::vector<render::Vertex> vertices;
    vertices.reserve((kRes + 1) * (kRes + 1));
    for (int z = 0; z <= kRes; ++z) {
        for (int x = 0; x <= kRes; ++x) {
            const float wx = -half + 2.0f * half * static_cast<float>(x) / kRes;
            const float wz = -half + 2.0f * half * static_cast<float>(z) / kRes;
            const float tint = 0.96f + 0.08f * hash01(x, z);
            vertices.push_back({{wx, sea_level, wz}, sea_color * tint});
        }
    }
    std::vector<std::uint32_t> indices;
    indices.reserve(kRes * kRes * 6);
    for (int z = 0; z < kRes; ++z) {
        for (int x = 0; x < kRes; ++x) {
            const auto tl = static_cast<std::uint32_t>(z * (kRes + 1) + x);
            const auto tr = tl + 1;
            const auto bl = tl + kRes + 1;
            const auto br = bl + 1;
            indices.insert(indices.end(), {tl, bl, tr, tr, bl, br});
        }
    }
    return render::Mesh{vertices, indices};
}

} // namespace terrain
