#include "ui/field_maps.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ui {

namespace {

constexpr int kMapRes = 512; // 4096² → 512²: sobra para un minimapa

struct Rgb {
    std::uint8_t r, g, b;
};

Rgb to_rgb(const glm::vec3& c) {
    const glm::vec3 s = glm::clamp(c, 0.0f, 1.0f) * 255.0f;
    return {static_cast<std::uint8_t>(s.r), static_cast<std::uint8_t>(s.g),
            static_cast<std::uint8_t>(s.b)};
}

glm::vec3 lerp(const glm::vec3& a, const glm::vec3& b, float t) {
    return a + (b - a) * glm::clamp(t, 0.0f, 1.0f);
}

// Rampa por tramos: puntos (t, color) ordenados.
glm::vec3 ramp(std::initializer_list<std::pair<float, glm::vec3>> stops,
               float t) {
    const auto* prev = stops.begin();
    for (const auto& s : stops) {
        if (t <= s.first) {
            const float span = s.first - prev->first;
            const float u = span > 0.0f ? (t - prev->first) / span : 0.0f;
            return lerp(prev->second, s.second, u);
        }
        prev = &s;
    }
    return prev->second;
}

unsigned int upload(const std::vector<Rgb>& pixels) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, kMapRes, kMapRes, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Colores de referencia por material (aproximación plana de la paleta del
// render — esto es un mapa de diagnóstico, no arte).
glm::vec3 material_color(terrain::Material m) {
    using terrain::Material;
    switch (m) {
        case Material::Seabed:   return {0.16f, 0.25f, 0.44f};
        case Material::Sand:     return {0.86f, 0.79f, 0.58f};
        case Material::Grass:    return {0.33f, 0.55f, 0.27f};
        case Material::Forest:   return {0.16f, 0.36f, 0.18f};
        case Material::Rock:     return {0.48f, 0.44f, 0.42f};
        case Material::Snow:     return {0.93f, 0.94f, 0.96f};
        case Material::RiverBed: return {0.35f, 0.47f, 0.66f};
        case Material::LakeBed:  return {0.25f, 0.40f, 0.60f};
        case Material::Gravel:   return {0.60f, 0.57f, 0.50f};
        case Material::Marsh:    return {0.42f, 0.52f, 0.36f};
    }
    return {1.0f, 0.0f, 1.0f}; // magenta: valor fuera del enum
}

} // namespace

FieldMaps::~FieldMaps() {
    clear();
}

void FieldMaps::clear() {
    for (const Map& m : maps_) {
        GLuint tex = m.texture;
        if (tex != 0) {
            glDeleteTextures(1, &tex);
        }
    }
    maps_.clear();
}

void FieldMaps::rebuild(const terrain::TerrainData& data) {
    clear();

    const auto& hf = data.height;
    const int res = hf.resolution();
    const float sea = data.config.sea_level;

    // Índice fuente por píxel del minimapa (nearest).
    const auto src = [&](int px, int pz) {
        const int x = px * (res - 1) / (kMapRes - 1);
        const int z = pz * (res - 1) / (kMapRes - 1);
        return static_cast<size_t>(z) * static_cast<size_t>(res)
             + static_cast<size_t>(x);
    };

    float min_h = 1e9f;
    float max_h = -1e9f;
    for (const float h : hf.data()) {
        min_h = std::min(min_h, h);
        max_h = std::max(max_h, h);
    }
    float max_flow = 0.0f;
    for (const float f : data.hydrology.flow) {
        max_flow = std::max(max_flow, f);
    }
    const float inv_log_flow = 1.0f / std::log(1.0f + std::max(max_flow, 1.0f));

    std::vector<Rgb> px(static_cast<size_t>(kMapRes) * kMapRes);
    const auto fill = [&](const char* name, auto&& color_of) {
        for (int z = 0; z < kMapRes; ++z) {
            for (int x = 0; x < kMapRes; ++x) {
                px[static_cast<size_t>(z) * kMapRes + x] =
                    to_rgb(color_of(src(x, z)));
            }
        }
        maps_.push_back({name, upload(px)});
    };

    // Altura: batimetría azul bajo el mar, hipsométrica clásica encima.
    fill("altura", [&](size_t i) {
        const float h = hf.data()[i];
        if (h <= sea) {
            const float d = min_h < sea ? (sea - h) / (sea - min_h) : 0.0f;
            return lerp({0.55f, 0.70f, 0.85f}, {0.08f, 0.15f, 0.35f}, d);
        }
        const float t = max_h > sea ? (h - sea) / (max_h - sea) : 0.0f;
        return ramp({{0.0f, {0.25f, 0.50f, 0.25f}},
                     {0.35f, {0.55f, 0.60f, 0.30f}},
                     {0.65f, {0.55f, 0.42f, 0.30f}},
                     {0.85f, {0.60f, 0.58f, 0.56f}},
                     {1.0f, {0.98f, 0.98f, 0.98f}}},
                    t);
    });

    // Caudal (flow accumulation, escala log): la red de drenaje entera.
    fill("caudal", [&](size_t i) {
        const float t =
            std::log(1.0f + data.hydrology.flow[i]) * inv_log_flow;
        return ramp({{0.0f, {0.97f, 0.97f, 0.95f}},
                     {0.5f, {0.55f, 0.70f, 0.90f}},
                     {1.0f, {0.05f, 0.15f, 0.55f}}},
                    t);
    });

    // Clima (0 = frío, 1 = cálido).
    fill("clima", [&](size_t i) {
        return ramp({{0.0f, {0.35f, 0.50f, 0.75f}},
                     {0.5f, {0.75f, 0.75f, 0.65f}},
                     {1.0f, {0.90f, 0.65f, 0.25f}}},
                    data.climate[i]);
    });

    // Biomas: color plano por material.
    fill("biomas", [&](size_t i) { return material_color(data.material[i]); });

    // Energía de oleaje (REI/fetch en la banda litoral).
    fill("oleaje", [&](size_t i) {
        const float e = data.wave_energy.empty() ? 0.0f : data.wave_energy[i];
        return ramp({{0.0f, {0.12f, 0.16f, 0.28f}},
                     {0.5f, {0.30f, 0.55f, 0.60f}},
                     {1.0f, {0.98f, 0.85f, 0.30f}}},
                    e);
    });
}

} // namespace ui
