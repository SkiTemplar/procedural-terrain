#pragma once

#include "terrain/heightfield.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace render {

// Calcula la normal de la muestra (x, z) del heightfield usando el gradiente
// 3x3 de Scharr. Función pura sin dependencias de OpenGL, testeable en
// aislamiento.
// Para un plano inclinado conocido devuelve la normal exacta analítica.
// Para un heightfield constante devuelve vec3(0, 1, 0).
glm::vec3 compute_scharr_normal(const terrain::Heightfield& hf, int x, int z);

// Decodifica una normal a partir de dos componentes SNORM16 (x, z).
// Reconstruye y = sqrt(max(0, 1 - x^2 - z^2)) y normaliza.
glm::vec3 decode_snorm16_normal(std::int16_t sx, std::int16_t sz);

// Calcula el normal map del heightfield completo en CPU en paralelo con
// std::execution::par. Devuelve un buffer con pares de enteros con signo de
// 16 bits (x, z) en formato SNORM16 (2 * res * res elementos).
std::vector<std::int16_t> compute_scharr_normal_map(const terrain::Heightfield& hf);

} // namespace render
