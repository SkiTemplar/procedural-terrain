#include "veg/grass_field.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace veg {

namespace {

// El césped se genera hasta kRadius; el DESVANECIDO visual lo hace el shader
// de hierba (encoge la altura entre ~70 y ~104 m). Por eso el radio de
// generación va justo por fuera del fin del fade: las briznas del borde ya se
// dibujan a altura ~0 y reconstruir el campo al moverse no produce "pop".
constexpr float kRadius = 108.0f;      // m: alcance de generación
constexpr float kRebuildMove = 24.0f;  // m: reconstruir al desplazarse esto
constexpr int kPerCell = 12;           // matas por celda de pradera (~2.4 m)

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

    const auto clampi = [res](int v) { return std::clamp(v, 0, res - 1); };
    const int cx0 = clampi(static_cast<int>((camera_pos.x - kRadius + half) / cell));
    const int cx1 = clampi(static_cast<int>((camera_pos.x + kRadius + half) / cell));
    const int cz0 = clampi(static_cast<int>((camera_pos.z - kRadius + half) / cell));
    const int cz1 = clampi(static_cast<int>((camera_pos.z + kRadius + half) / cell));

    std::vector<render::Instance> inst;
    inst.reserve(8192);
    const std::uint32_t salt = data_->config.seed ^ 0x6A55u;

    // ¿Hay agua cerca? Anillo barato de muestras (~4 y ~8 celdas): la ribera
    // húmeda lleva césped más alto y frondoso (decisión "mezcla por zona").
    const auto near_water = [&](int x, int z) -> bool {
        static const int off[8][2] = {{4, 0}, {-4, 0}, {0, 4}, {0, -4},
                                      {6, 6}, {-6, 6}, {6, -6}, {-6, -6}};
        for (const auto& o : off) {
            const int nx = std::clamp(x + o[0], 0, res - 1);
            const int nz = std::clamp(z + o[1], 0, res - 1);
            if (!std::isnan(data_->hydrology.water_level[
                    static_cast<size_t>(nz) * static_cast<size_t>(res)
                    + static_cast<size_t>(nx)])) {
                return true;
            }
        }
        return false;
    };

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
            // Altura por ZONA (feedback 2026-07-08): corto en pradera llana,
            // alto en ladera y ribera. Pendiente desde la normal del terreno.
            const glm::vec3 nrm = hf.normal_at(x, z);
            const float slope = std::sqrt(std::max(0.0f, 1.0f - nrm.y * nrm.y))
                              / std::max(nrm.y, 1e-3f);
            float base_h = 0.12f + 0.50f * std::min(slope / 0.55f, 1.0f);
            const bool riparian = near_water(x, z);
            if (riparian) {
                base_h += 0.22f; // pasto húmedo de ribera, más alto
            }
            // Bajo bosque denso hay menos hierba y más corta (sombra).
            const float density = (m == terrain::Material::Forest) ? 0.45f : 1.0f;
            if (m == terrain::Material::Forest) {
                base_h *= 0.7f;
            }
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
                if (dx * dx + dz * dz > r2) {
                    continue;
                }
                const float wy = sampler_->height(wx, wz);
                if (wy < data_->config.sea_level + 0.3f) {
                    continue;
                }
                // Altura final en METROS (la instancia escala el modelo ~1.0).
                const float height = base_h * (0.75f + 0.5f * hash01(
                    static_cast<std::uint32_t>(x) * 7u + ku,
                    static_cast<std::uint32_t>(z) * 3u, salt + 9u));
                const float yaw = hash01(static_cast<std::uint32_t>(x) + ku,
                                         static_cast<std::uint32_t>(z), salt + 13u)
                                * 6.2831853f;
                const float green = riparian ? 1.05f : 1.0f;
                const float tint = (0.8f + 0.35f * hash01(
                    static_cast<std::uint32_t>(x),
                    static_cast<std::uint32_t>(z) + ku, salt + 21u)) * green;
                inst.push_back(render::Instance{{wx, wy - 0.02f, wz, height},
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
