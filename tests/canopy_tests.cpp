#include "veg/canopy_layer.h"

#include <gtest/gtest.h>

// Dominio de 100 m en 10 texeles: texel de 10 m (100 m²).

TEST(Dosel, ColorMedioPonderadoPorAreaDeCopa) {
    veg::CanopyAccumulator acc(100.0f, 10);
    // Dos copas en el mismo texel: la grande pesa cuatro veces más.
    acc.add(-45.0f, -45.0f, 2.0f, glm::vec3{1.0f, 0.0f, 0.0f});
    acc.add(-44.0f, -46.0f, 1.0f, glm::vec3{0.0f, 1.0f, 0.0f});
    const auto texels = acc.resolve();
    const glm::vec4 t = texels[0];
    EXPECT_NEAR(t.r, 0.8f, 1e-5f);
    EXPECT_NEAR(t.g, 0.2f, 1e-5f);
    EXPECT_NEAR(t.a, 3.14159265f * 5.0f / 100.0f, 1e-5f);
}

TEST(Dosel, CoberturaSaturaEnUnoYTexelVacioEsCero) {
    veg::CanopyAccumulator acc(100.0f, 10);
    for (int k = 0; k < 20; ++k) {
        acc.add(5.0f, 5.0f, 3.0f, glm::vec3{0.2f, 0.4f, 0.2f});
    }
    acc.add(500.0f, 0.0f, 3.0f, glm::vec3{1.0f}); // fuera del dominio: se ignora
    const auto texels = acc.resolve();
    EXPECT_FLOAT_EQ(texels[5 * 10 + 5].a, 1.0f);
    EXPECT_NEAR(texels[5 * 10 + 5].g, 0.4f, 1e-5f);
    EXPECT_FLOAT_EQ(texels[0].a, 0.0f);
    EXPECT_FLOAT_EQ(texels[0].r, 0.0f);
}
