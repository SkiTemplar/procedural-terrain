#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>

namespace terrain {

// Perlin 3D clásico (improved) con tabla de permutación derivada de la seed.
// Determinista: misma seed → mismos valores. Se muestrea en 3D aunque la isla
// sea plana para que el mismo código sirva sobre la esfera en la fase 2.
class Noise {
public:
    explicit Noise(std::uint32_t seed);

    // Ruido base, rango ~[-1, 1]. 0 en los puntos enteros de la retícula.
    float perlin(glm::vec3 p) const;

    // Suma fractal de octavas, normalizada a ~[-1, 1].
    float fbm(glm::vec3 p, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;

    // Crestas afiladas para cordilleras, rango [0, 1] (1 = cresta).
    float ridged(glm::vec3 p, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;

    // Distorsiona p con dos fbm antes de evaluar otro campo (costas irregulares).
    glm::vec3 warp(glm::vec3 p, float amplitude, float frequency) const;

private:
    std::array<std::uint8_t, 512> perm_{};

    static float fade(float t);
    static float grad(std::uint8_t hash, float x, float y, float z);
};

} // namespace terrain
