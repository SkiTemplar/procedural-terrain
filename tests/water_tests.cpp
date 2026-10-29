// Tests sintéticos del agua en la costa: lámina del render frente al mar
// recortado. Mundos
// pequeños construidos a mano: cada test aísla un mecanismo.
#include "terrain/generator.h"
#include "terrain/water_surface.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kCell = 2.5f;

struct Scene {
    terrain::Heightfield hf;
    terrain::Hydrology hydro;
    int res;

    Scene(int resolution, float ground)
        : hf(resolution, kCell * static_cast<float>(resolution - 1)), res(resolution) {
        const auto n = static_cast<std::size_t>(res) * static_cast<std::size_t>(res);
        hydro.water_level.assign(n, kNaN);
        hydro.flow.assign(n, 1.0f);
        hydro.lake.assign(n, 0);
        hydro.whitewater.assign(n, 0);
        for (int z = 0; z < res; ++z) {
            for (int x = 0; x < res; ++x) {
                hf.at(x, z) = ground;
            }
        }
    }
    [[nodiscard]] std::size_t at(int x, int z) const {
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(res)
             + static_cast<std::size_t>(x);
    }
    void wet(int x, int z, float bed, float level) {
        hf.at(x, z) = bed;
        hydro.water_level[at(x, z)] = level;
    }
};

} // namespace

TEST(AguaCosta, LaLaminaNoBajaAlFondoDondeElMarSeRecorta) {
    // Auditoría 2026-10 (bloqueantes #1/#4/#6): el mar se descarta en los
    // quads que cubre la lámina interior; un vértice de esa lámina sin agua
    // propia y con el lecho sumergido bajaba a h − 0.27 y dejaba el fondo al
    // aire. Debe quedar a cota de mar.
    terrain::TerrainData data;
    data.config.sea_level = 0.0f;
    Scene s(6, 3.0f);
    s.wet(2, 2, -1.0f, 0.0f);     // remanso del estuario
    s.hf.at(3, 2) = -2.2f;        // eje sumergido sin lámina
    data.height = s.hf;
    data.hydrology = s.hydro;

    const terrain::WaterSurface surface = terrain::compute_water_surface(data);
    ASSERT_EQ(surface.quad_covered.size(), 25u);
    EXPECT_EQ(surface.quad_covered[surface.quad_index(2, 2)], 1);
    EXPECT_EQ(surface.quad_covered[surface.quad_index(0, 0)], 0);
    const float y_dry = surface.vertex_y[s.at(3, 2)];
    EXPECT_FLOAT_EQ(y_dry, 0.0f) << "vértice sumergido sin lámina a " << y_dry << " m";
    EXPECT_FLOAT_EQ(surface.vertex_y[s.at(2, 2)], 0.0f) << "el remanso no se hunde";
    // Orilla seca sobre el mar: sigue enterrada bajo el talud.
    EXPECT_LT(surface.vertex_y[s.at(1, 1)], 3.0f);
    EXPECT_GE(surface.vertex_y[s.at(1, 1)], 0.0f);
}
