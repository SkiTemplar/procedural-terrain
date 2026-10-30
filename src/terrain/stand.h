#pragma once

// Mosaico de rodales del bosque, compartido por la colocación de árboles
// (veg::VegRenderer) y el color del suelo (Palette::ground): un claro sin
// árboles tiene que leerse también como pradera en el suelo.
//
// forest_density (ecología física) da la capacidad del sitio; un bosque real
// no la llena de forma uniforme: el régimen de perturbaciones (derribos de
// vendaval, deslizamientos, edad de cada rodal) deja claros, rodales densos y
// manchas de composición distinta. El ruido solo modula ese régimen; la
// susceptibilidad sube donde el sitio ya es marginal (densidad potencial baja),
// así que los claros se concentran en bordes y zonas de estrés y casi no
// aparecen en el bosque cerrado de fondo de valle.

#include "terrain/mesher.h"

#include <glm/common.hpp>

namespace terrain {

// Mosaico de dos escalas (~170 m y ~55 m) en [0, 1], media 0.5.
inline float stand_mosaic(float wx, float wz) noexcept {
    return 0.65f * value_noise(wx * 0.006f + 41.0f, wz * 0.006f - 13.0f)
         + 0.35f * value_noise(wx * 0.018f - 7.0f, wz * 0.018f + 29.0f);
}

// Intensidad de claro [0, 1]: 1 = claro abierto (pradera), 0 = rodal intacto.
inline float stand_gap(float wx, float wz, float forest_density) noexcept {
    const float threshold = 0.58f + 0.10f * glm::clamp(forest_density, 0.0f, 1.0f);
    return glm::smoothstep(threshold, threshold + 0.07f, stand_mosaic(wx, wz));
}

// Factor de espesura del rodal (0.6 a 1.4): rodales maduros densos frente a
// regenerados abiertos, a escala de ~40 m.
inline float stand_thickness(float wx, float wz) noexcept {
    return 0.6f + 0.8f * value_noise(wx * 0.025f + 3.0f, wz * 0.025f + 61.0f);
}

// Desplazamiento de composición [-0.5, 0.5] a escala de ~250 m: rodales de
// coníferas frente a rodales de frondosas a la misma cota.
inline float stand_composition(float wx, float wz) noexcept {
    return value_noise(wx * 0.004f - 19.0f, wz * 0.004f + 5.0f) - 0.5f;
}

// Vigor del rodal [-0.5, 0.5] a escala de ~110 m: edad y fenología comunes a
// todos los árboles de una mancha (el tinte de copa cambia por rodales, no
// árbol a árbol).
inline float stand_vigor(float wx, float wz) noexcept {
    return value_noise(wx * 0.009f + 71.0f, wz * 0.009f - 37.0f) - 0.5f;
}

} // namespace terrain
