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
    // Normal media del heightfield a la escala `half_step` (diferencias
    // centrales a ±half_step): la orientación de la ladera alrededor de una
    // faceta, sin el relieve propio de la cara.
    glm::vec3 normal_at_scale(float wx, float wz, float half_step) const;
    // Iluminación precomputada (bilineal sobre el grid de luz de resolución
    // propia). 1.0 si aún no se ha calculado. shadow: 1=al sol, 0=en sombra;
    // sky_ao: 1=cielo abierto, 0=ocluido.
    float shadow_at(float wx, float wz) const;
    float sky_ao_at(float wx, float wz) const;

private:
    const TerrainData* data_;
    Noise noise_;

    float light_sample(const std::vector<float>& field, float wx, float wz) const;

    static float material_roughness(Material m);
    // Rugosidad interpolada bilinealmente entre celdas: con nearest, cada
    // celda suelta de roca en la pradera saltaba de 0.4 a 1.5 de golpe y al
    // caminar aparecían montículos con bordes de un vértice.
    float roughness_at(float wx, float wz) const;
};

} // namespace terrain
