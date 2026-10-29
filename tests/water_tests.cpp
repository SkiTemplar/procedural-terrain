// Tests sintéticos del agua en la costa: lámina del render frente al mar
// recortado, knickzone de desembocadura y bolsas del estuario. Mundos
// pequeños construidos a mano: cada test aísla un mecanismo.
#include "terrain/generator.h"
#include "terrain/river_mouth.h"
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

// Canal de una celda de ancho hacia -x que llega a la costa colgado `hang`
// metros sobre el mar (x = 0..1 es mar, la lámina sube 1 % aguas arriba).
Scene hanging_mouth(float hang) {
    Scene s(48, 6.0f);
    for (int z = 0; z < s.res; ++z) {
        s.hf.at(0, z) = -1.0f;
        s.hf.at(1, z) = -1.0f;
    }
    for (int x = 2; x < s.res - 1; ++x) {
        const float level = hang + 0.01f * kCell * static_cast<float>(x - 2);
        s.wet(x, 24, level - 0.4f, level);
    }
    return s;
}

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

TEST(AguaCosta, UnaBocaColgadaSeRebajaEnUnTramoYNoEnUnaCelda) {
    // Bloqueante #2: el pin de una celda a cota de mar subía el escalón una
    // celda aguas arriba. La caída se reparte en una knickzone.
    Scene s = hanging_mouth(1.4f);
    terrain::grade_coastal_knickzones(s.hf, s.hydro, 0.0f);

    EXPECT_FLOAT_EQ(s.hydro.water_level[s.at(2, 24)], 0.0f) << "la boca enrasa con el mar";
    float max_drop = 0.0f;
    for (int x = 3; x < s.res - 1; ++x) {
        const float up = s.hydro.water_level[s.at(x, 24)];
        const float down = s.hydro.water_level[s.at(x - 1, 24)];
        EXPECT_GE(up, down) << "la lámina sube aguas abajo en x = " << x;
        max_drop = std::max(max_drop, up - down);
        EXPECT_LE(s.hf.at(x, 24), up - 0.12f) << "lámina sin cauce debajo en x = " << x;
    }
    EXPECT_LT(max_drop, 0.25f) << "escalón de " << max_drop << " m en el tramo final";
    // Lejos de la boca la lámina no cambia.
    EXPECT_FLOAT_EQ(s.hydro.water_level[s.at(s.res - 2, 24)],
                    1.4f + 0.01f * kCell * static_cast<float>(s.res - 4));
}

TEST(AguaCosta, LaCascadaDeAcantiladoSeRespeta) {
    // Un valle colgado sobre un acantilado (caída > 2.5 m) no es una boca
    // colgada: no se talla una garganta hasta el mar.
    Scene s = hanging_mouth(4.0f);
    const float before = s.hydro.water_level[s.at(2, 24)];
    EXPECT_EQ(terrain::grade_coastal_knickzones(s.hf, s.hydro, 0.0f), 0);
    EXPECT_FLOAT_EQ(s.hydro.water_level[s.at(2, 24)], before);
}

TEST(AguaCosta, LaBolsaDelEstuarioSeLlenaYElMarAbiertoNo) {
    // Bloqueantes #4/#5: celdas bajo el mar sin lámina, encerradas entre la
    // lámina del remanso, reciben la cota de mar; el mar abierto (conexo con
    // el borde) sigue siendo mar.
    Scene s(12, 4.0f);
    for (int z = 0; z < s.res; ++z) {
        s.hf.at(0, z) = -3.0f; // mar abierto en el borde
    }
    for (int x = 1; x < 9; ++x) {
        s.wet(x, 5, -1.0f, 0.0f); // estuario en remanso
        s.wet(x, 7, -1.0f, 0.0f);
    }
    s.wet(1, 6, -1.0f, 0.0f);
    s.wet(8, 6, -1.0f, 0.0f);
    for (int x = 2; x < 8; ++x) {
        s.hf.at(x, 6) = -2.2f; // eje sin lámina, encerrado
    }
    s.hf.at(10, 10) = -1.0f; // hoyo aislado sin agua al lado: no se toca

    const int flooded = terrain::flood_estuary_pockets(s.hf, s.hydro, 0.0f);
    EXPECT_EQ(flooded, 6);
    for (int x = 2; x < 8; ++x) {
        EXPECT_FLOAT_EQ(s.hydro.water_level[s.at(x, 6)], 0.0f) << "x = " << x;
    }
    EXPECT_TRUE(std::isnan(s.hydro.water_level[s.at(0, 6)])) << "el mar abierto no es río";
    EXPECT_TRUE(std::isnan(s.hydro.water_level[s.at(10, 10)]));
}
