#include "terrain/noise.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace terrain {

Noise::Noise(std::uint32_t seed) {
    std::array<std::uint8_t, 256> base{};
    std::iota(base.begin(), base.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(base.begin(), base.end(), rng);
    for (int i = 0; i < 256; ++i) {
        perm_[i] = base[static_cast<size_t>(i)];
        perm_[i + 256] = base[static_cast<size_t>(i)];
    }
}

float Noise::fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Noise::grad(std::uint8_t hash, float x, float y, float z) {
    // 12 direcciones de gradiente (Perlin improved).
    const int h = hash & 15;
    const float u = h < 8 ? x : y;
    const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float Noise::perlin(glm::vec3 p) const {
    const glm::vec3 floor_p = glm::floor(p);
    const int xi = static_cast<int>(floor_p.x) & 255;
    const int yi = static_cast<int>(floor_p.y) & 255;
    const int zi = static_cast<int>(floor_p.z) & 255;

    const float x = p.x - floor_p.x;
    const float y = p.y - floor_p.y;
    const float z = p.z - floor_p.z;

    const float u = fade(x);
    const float v = fade(y);
    const float w = fade(z);

    const auto& pm = perm_;
    const int a = pm[xi] + yi;
    const int aa = pm[a] + zi;
    const int ab = pm[a + 1] + zi;
    const int b = pm[xi + 1] + yi;
    const int ba = pm[b] + zi;
    const int bb = pm[b + 1] + zi;

    const float res = std::lerp(
        std::lerp(std::lerp(grad(pm[aa], x, y, z), grad(pm[ba], x - 1, y, z), u),
                  std::lerp(grad(pm[ab], x, y - 1, z), grad(pm[bb], x - 1, y - 1, z), u), v),
        std::lerp(std::lerp(grad(pm[aa + 1], x, y, z - 1), grad(pm[ba + 1], x - 1, y, z - 1), u),
                  std::lerp(grad(pm[ab + 1], x, y - 1, z - 1), grad(pm[bb + 1], x - 1, y - 1, z - 1), u), v),
        w);
    return res;
}

float Noise::fbm(glm::vec3 p, int octaves, float lacunarity, float gain) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total_amplitude = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * perlin(p);
        total_amplitude += amplitude;
        amplitude *= gain;
        p *= lacunarity;
    }
    return total_amplitude > 0.0f ? sum / total_amplitude : 0.0f;
}

float Noise::ridged(glm::vec3 p, int octaves, float lacunarity, float gain) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total_amplitude = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        const float n = 1.0f - std::abs(perlin(p)); // cresta en los ceros del ruido
        sum += amplitude * n * n;
        total_amplitude += amplitude;
        amplitude *= gain;
        p *= lacunarity;
    }
    return total_amplitude > 0.0f ? sum / total_amplitude : 0.0f;
}

glm::vec3 Noise::warp(glm::vec3 p, float amplitude, float frequency) const {
    // Offsets grandes y distintos por eje para descorrelacionar los campos.
    const float dx = fbm(p * frequency + glm::vec3{31.416f, 0.0f, 47.123f}, 4);
    const float dz = fbm(p * frequency + glm::vec3{-17.271f, 0.0f, 89.732f}, 4);
    return p + glm::vec3{dx, 0.0f, dz} * amplitude;
}

} // namespace terrain
