#include "app/camera_path.h"

#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>

namespace {

std::vector<app::PathKey> three_keys() {
    return {
        {0.0f, 0.0f, 100.0f, 0.0f, 170.0f, -10.0f},
        {2.0f, 50.0f, 120.0f, 10.0f, -170.0f, -5.0f},
        {5.0f, 90.0f, 110.0f, 40.0f, -150.0f, 0.0f},
    };
}

} // namespace

TEST(CameraPath, PasaPorLasClaves) {
    const auto keys = three_keys();
    const app::PathKey mid = app::sample_path(keys, 2.0f);
    EXPECT_NEAR(mid.x, 50.0f, 1e-4f);
    EXPECT_NEAR(mid.y, 120.0f, 1e-4f);
    EXPECT_NEAR(mid.z, 10.0f, 1e-4f);
    EXPECT_NEAR(mid.pitch, -5.0f, 1e-4f);
}

TEST(CameraPath, SaturaFueraDeRango) {
    const auto keys = three_keys();
    EXPECT_NEAR(app::sample_path(keys, -3.0f).x, 0.0f, 1e-6f);
    EXPECT_NEAR(app::sample_path(keys, 99.0f).x, 90.0f, 1e-6f);
}

// 170° → -170° es un giro de 20°, no de 340°: el yaw intermedio no puede
// pasar por 0.
TEST(CameraPath, ElYawNoDaLaVueltaAlCruzar180) {
    const auto keys = three_keys();
    const app::PathKey half = app::sample_path(keys, 1.0f);
    EXPECT_GT(half.yaw, 170.0f);
    EXPECT_LT(half.yaw, 190.0f);
}

TEST(CameraPath, ParseaTomasConComentariosYTiempoOpcional) {
    std::istringstream in("# cabecera\n\nnorte 0 -4200 90 -20 1300\nboca -3950 -1542 -40 -25 84 1.5\n");
    const auto shots = app::parse_shots(in);
    ASSERT_EQ(shots.size(), 2u);
    EXPECT_EQ(shots[0].name, "norte");
    EXPECT_FLOAT_EQ(shots[0].z, -4200.0f);
    EXPECT_FLOAT_EQ(shots[0].time_s, 0.0f);
    EXPECT_FLOAT_EQ(shots[1].time_s, 1.5f);
}

TEST(CameraPath, RechazaTomaIncompleta) {
    std::istringstream in("norte 0 -4200 90\n");
    EXPECT_THROW(app::parse_shots(in), std::invalid_argument);
}

TEST(CameraPath, RechazaVueloConTiempoNoCreciente) {
    std::istringstream in("0 0 0 100 0 0\n0 10 0 100 0 0\n");
    EXPECT_THROW(app::parse_flight(in), std::invalid_argument);
}
