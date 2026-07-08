#include "veg/grass_field.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace veg {

namespace {

constexpr float kRadius = 95.0f;       // m: alcance del césped alrededor del jugador
constexpr float kFadeStart = 0.72f;    // fracción del radio donde empieza a encogerse
constexpr float kRebuildMove = 16.0f;  // m: reconstruir al desplazarse esto
constexpr int kPerCell = 15;           // matas por celda de pradera (~2.4 m): tupido

float hash01(std::uint32_t x, std::uint32_t z, std::uint32_t salt) {
    std::uint32_t h = x * 374761393u + z * 668265263u + salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

} // namespace

GrassField::GrassField(const terrain::TerrainData& data,
                       const terrain::DetailSampler& sampler)
    : data_(&data), sampler_(&sampler), model_(make_grass()) {}

void GrassField::update(const glm::vec3& camera_pos) {
    const glm::vec2 c{camera_pos.x, camera_pos.z};
    if (!mesh_ || glm::length(c - last_center_) > kRebuildMove) {
        rebuild(camera_pos);
        last_center_ = c;
    }
}

void GrassField::rebuild(const glm::vec3& camera_pos) {
    const auto& hf = data_->height;
    const int res = hf.resolution();
    const float cell = hf.cell_size();
    const float half = data_->config.size_m * 0.5f;
    const float r2 = kRadius * kRadius;
    const float fade_r = kRadius * kFadeStart;

    const auto clampi = [res](int v) { return std::clamp(v, 0, res - 1); };
    const int cx0 = clampi(static_cast<int>((camera_pos.x - kRadius + half) / cell));
    const int cx1 = clampi(static_cast<int>((camera_pos.x + kRadius + half) / cell));
    const int cz0 = clampi(static_cast<int>((camera_pos.z - kRadius + half) / cell));
    const int cz1 = clampi(static_cast<int>((camera_pos.z + kRadius + half) / cell));

    std::vector<render::Instance> inst;
    inst.reserve(4096);
    const std::uint32_t salt = data_->config.seed ^ 0x6A55u;

    for (int z = cz0; z <= cz1; ++z) {
        for (int x = cx0; x <= cx1; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(res)
                           + static_cast<size_t>(x);
            const auto m = data_->material[i];
            if (m != terrain::Material::Grass && m != terrain::Material::Forest) {
                continue;
            }
            if (!std::isnan(data_->hydrology.water_level[i])) {
                continue; // lámina de agua: sin césped
            }
            // Bajo bosque denso hay menos hierba (sombra); la pradera va llena.
            const float density = (m == terrain::Material::Forest) ? 0.4f : 1.0f;
            const int n = std::max(1, static_cast<int>(kPerCell * density));
            for (int k = 0; k < n; ++k) {
                const auto ku = static_cast<std::uint32_t>(k);
                const float jx = hash01(static_cast<std::uint32_t>(x),
                                        static_cast<std::uint32_t>(z), salt + ku * 2u)
                               - 0.5f;
                const float jz = hash01(static_cast<std::uint32_t>(x),
                                        static_cast<std::uint32_t>(z), salt + ku * 2u + 1u)
                               - 0.5f;
                const float wx = hf.world_x(x) + jx * cell;
                const float wz = hf.world_z(z) + jz * cell;
                const float dx = wx - camera_pos.x;
                const float dz = wz - camera_pos.z;
                const float d2 = dx * dx + dz * dz;
                if (d2 > r2) {
                    continue;
                }
                const float wy = sampler_->height(wx, wz);
                if (wy < data_->config.sea_level + 0.3f) {
                    continue;
                }
                // Encoge hacia el borde del disco para que no haya anillo neto.
                const float dist = std::sqrt(d2);
                const float fade = dist <= fade_r
                    ? 1.0f
                    : std::max(0.0f, 1.0f - (dist - fade_r) / (kRadius - fade_r));
                const float scale = (0.65f + 0.7f * hash01(
                    static_cast<std::uint32_t>(x) * 7u + ku,
                    static_cast<std::uint32_t>(z) * 3u, salt + 9u)) * fade;
                if (scale < 0.05f) {
                    continue;
                }
                const float yaw = hash01(static_cast<std::uint32_t>(x) + ku,
                                         static_cast<std::uint32_t>(z), salt + 13u)
                                * 6.2831853f;
                const float tint = 0.78f + 0.4f * hash01(
                    static_cast<std::uint32_t>(x),
                    static_cast<std::uint32_t>(z) + ku, salt + 21u);
                inst.push_back(render::Instance{{wx, wy - 0.03f, wz, scale},
                                                {tint, tint, tint, yaw}});
            }
        }
    }

    mesh_.emplace(model_.vertices, model_.indices, inst);
}

void GrassField::draw() const {
    if (mesh_) {
        mesh_->draw();
    }
}

} // namespace veg
