#pragma once

#include "terrain/generator.h"
#include "terrain/noise.h"

namespace terrain {

// Superficie continua final: heightfield bilineal + ruido fino modulado por
// material. La usan el mesher (chunks cercanos subdivididos) y el controller
// (el suelo que pisas coincide exactamente con el que ves).
class DetailSampler {
public:
    explicit DetailSampler(const TerrainData& data);

    float height(float wx, float wz) const;
    Material material_near(float wx, float wz) const;
    float forest_density_near(float wx, float wz) const;
    float climate_near(float wx, float wz) const;
    glm::vec3 normal_near(float wx, float wz) const;

private:
    const TerrainData* data_;
    Noise noise_;

    static float material_roughness(Material m);
    // Rugosidad interpolada bilinealmente entre celdas: con nearest, cada
    // celda suelta de roca en la pradera saltaba de 0.4 a 1.5 de golpe y al
    // caminar aparecían montículos con bordes de un vértice.
    float roughness_at(float wx, float wz) const;
};

} // namespace terrain
