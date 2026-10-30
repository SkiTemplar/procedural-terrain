#include "veg/models.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace veg {

namespace {

const glm::vec3 kTrunkColor{0.42f, 0.30f, 0.19f};
const glm::vec3 kPineColor{0.19f, 0.38f, 0.20f}; // algo más clara: la umbría de la copa no cae a negro
const glm::vec3 kLeafColor{0.30f, 0.50f, 0.22f};
const glm::vec3 kRockColor{0.52f, 0.50f, 0.47f};

// Hash determinista para deformaciones (sin RNG: modelos reproducibles).
float hash01(std::uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n = n ^ (n >> 4);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15);
    return static_cast<float>(n & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

// Tronco: prisma de `sides` lados entre y0 e y1.
void add_trunk(ModelData& out, float radius, float y0, float y1, int sides) {
    const auto base = static_cast<std::uint32_t>(out.vertices.size());
    for (int i = 0; i < sides; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / sides;
        const float x = radius * std::cos(a);
        const float z = radius * std::sin(a);
        out.vertices.push_back({{x, y0, z}, kTrunkColor});
        out.vertices.push_back({{x * 0.8f, y1, z * 0.8f}, kTrunkColor});
    }
    for (int i = 0; i < sides; ++i) {
        const auto a0 = base + static_cast<std::uint32_t>(2 * i);
        const auto a1 = a0 + 1;
        const auto b0 = base + static_cast<std::uint32_t>(2 * ((i + 1) % sides));
        const auto b1 = b0 + 1;
        out.indices.insert(out.indices.end(), {a0, b0, a1, a1, b0, b1});
    }
}

// Cono de `sides` lados: base en y0 con `radius`, punta en y1.
void add_cone(ModelData& out, float radius, float y0, float y1, int sides,
              const glm::vec3& color, std::uint32_t seed) {
    const auto apex = static_cast<std::uint32_t>(out.vertices.size());
    out.vertices.push_back({{0.0f, y1, 0.0f}, color});
    const auto base = static_cast<std::uint32_t>(out.vertices.size());
    for (int i = 0; i < sides; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / sides;
        const float wobble = 0.92f + 0.16f * hash01(seed * 31u + static_cast<std::uint32_t>(i));
        out.vertices.push_back(
            {{radius * wobble * std::cos(a), y0, radius * wobble * std::sin(a)}, color});
    }
    // Sin culling de caras y con la normal derivada por dFdx, las traseras se
    // iluminan solas: no hace falta cara inferior.
    for (int i = 0; i < sides; ++i) {
        const auto b0 = base + static_cast<std::uint32_t>(i);
        const auto b1 = base + static_cast<std::uint32_t>((i + 1) % sides);
        out.indices.insert(out.indices.end(), {apex, b1, b0});
    }
}

// Icosaedro con una subdivisión, deformado radialmente por hash.
void add_deformed_icosphere(ModelData& out, float radius, float y_center,
                            float y_squash, float deform, const glm::vec3& color,
                            std::uint32_t seed) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<glm::vec3> pos = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
        {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
        {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1},
    };
    std::vector<std::uint32_t> idx = {
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
        1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
        3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
        4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1,
    };

    // Una subdivisión (cada triángulo → 4).
    std::vector<std::uint32_t> subdivided;
    for (size_t f = 0; f < idx.size(); f += 3) {
        const glm::vec3 a = pos[idx[f]];
        const glm::vec3 b = pos[idx[f + 1]];
        const glm::vec3 c = pos[idx[f + 2]];
        const auto add = [&pos](const glm::vec3& p) {
            pos.push_back(p);
            return static_cast<std::uint32_t>(pos.size() - 1);
        };
        const auto ab = add((a + b) * 0.5f);
        const auto bc = add((b + c) * 0.5f);
        const auto ca = add((c + a) * 0.5f);
        subdivided.insert(subdivided.end(),
                          {idx[f], ab, ca, ab, idx[f + 1], bc, ca, bc, idx[f + 2],
                           ab, bc, ca});
    }

    const auto base = static_cast<std::uint32_t>(out.vertices.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        glm::vec3 p = glm::normalize(pos[i]);
        const float bump =
            1.0f + deform * (hash01(seed * 17u + static_cast<std::uint32_t>(i)) - 0.5f);
        p *= radius * bump;
        p.y = p.y * y_squash + y_center;
        out.vertices.push_back({p, color});
    }
    for (const std::uint32_t i : subdivided) {
        out.indices.push_back(base + i);
    }
}

} // namespace

ModelData make_pine() {
    ModelData out;
    add_trunk(out, 0.28f, 0.0f, 2.2f, 6);
    add_cone(out, 2.4f, 1.8f, 4.6f, 8, kPineColor, 11u);
    add_cone(out, 1.8f, 3.8f, 6.6f, 8, kPineColor * 1.06f, 23u);
    add_cone(out, 1.15f, 5.8f, 9.0f, 8, kPineColor * 1.12f, 37u);
    return out;
}

ModelData make_broadleaf() {
    ModelData out;
    add_trunk(out, 0.32f, 0.0f, 2.6f, 6);
    add_deformed_icosphere(out, 2.6f, 4.6f, 0.82f, 0.45f, kLeafColor, 51u);
    return out;
}

ModelData make_rock() {
    ModelData out;
    // Deformación moderada: pedrusco irregular pero sin picos rotos.
    add_deformed_icosphere(out, 1.0f, 0.12f, 0.62f, 0.35f, kRockColor, 77u);
    return out;
}

ModelData make_tuft() {
    ModelData out;
    const glm::vec3 blade{0.36f, 0.56f, 0.24f};
    // Abanico de 5 hojas: conos finos e inclinados alrededor del centro.
    for (int i = 0; i < 5; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / 5.0f;
        const float lean = 0.16f + 0.1f * hash01(91u + static_cast<std::uint32_t>(i));
        const float height = 0.38f + 0.25f * hash01(113u + static_cast<std::uint32_t>(i));

        const auto apex = static_cast<std::uint32_t>(out.vertices.size());
        out.vertices.push_back(
            {{lean * std::cos(a), height, lean * std::sin(a)},
             blade * (0.9f + 0.2f * hash01(131u + static_cast<std::uint32_t>(i)))});
        const auto base = static_cast<std::uint32_t>(out.vertices.size());
        for (int k = 0; k < 3; ++k) {
            const float b = glm::two_pi<float>() * static_cast<float>(k) / 3.0f;
            out.vertices.push_back(
                {{0.05f * std::cos(b) + 0.04f * std::cos(a),
                  0.0f,
                  0.05f * std::sin(b) + 0.04f * std::sin(a)},
                 blade * 0.8f});
        }
        for (int k = 0; k < 3; ++k) {
            out.indices.insert(out.indices.end(),
                               {apex, base + static_cast<std::uint32_t>((k + 1) % 3),
                                base + static_cast<std::uint32_t>(k)});
        }
    }
    return out;
}

ModelData make_grass() {
    // Grass CARD para el campo cercano (veg/grass_field): 3 quads verticales
    // cruzados (0/60/120°). El shader kGrassFrag dibuja MUCHAS briznas dentro
    // de cada quad de forma procedural y descarta el hueco (alpha) — densidad
    // de césped real con ~6 triángulos por mata (≈10× menos que las briznas
    // 3D). El color del vértice porta la UV (u,v) que el fragment usa para las
    // briznas; la altura de modelo es 1.0 (la instancia escala a metros por
    // zona) y el ancho fijo ~0.5 m.
    ModelData out;
    constexpr int kQuads = 3;
    constexpr float kHalfWidth = 0.25f;
    for (int q = 0; q < kQuads; ++q) {
        const float a = glm::pi<float>() * static_cast<float>(q)
                      / static_cast<float>(kQuads);
        const glm::vec3 right{std::cos(a), 0.0f, std::sin(a)};
        const auto b = static_cast<std::uint32_t>(out.vertices.size());
        const auto vert = [&](float u, float v) {
            const glm::vec3 p = right * ((u - 0.5f) * 2.0f * kHalfWidth)
                              + glm::vec3{0.0f, v, 0.0f};
            out.vertices.push_back({p, {u, v, 0.0f}}); // color = UV del card
        };
        vert(0.0f, 0.0f);
        vert(1.0f, 0.0f);
        vert(1.0f, 1.0f);
        vert(0.0f, 1.0f);
        out.indices.insert(out.indices.end(),
                           {b, b + 1, b + 2, b, b + 2, b + 3});
    }
    return out;
}

ModelData make_bush() {
    ModelData out;
    add_deformed_icosphere(out, 0.7f, 0.45f, 0.75f, 0.5f, kLeafColor * 0.9f, 143u);
    return out;
}

ModelData make_log() {
    ModelData out;
    const glm::vec3 bark = kTrunkColor * 0.85f;
    constexpr int kSides = 6;
    constexpr float kLength = 4.2f;
    constexpr float kR0 = 0.38f; // base
    constexpr float kR1 = 0.26f; // punta

    // Prisma tumbado a lo largo de +X, apoyado en el suelo (y ≈ radio).
    const auto base = static_cast<std::uint32_t>(out.vertices.size());
    for (int i = 0; i < kSides; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / kSides;
        const float cy = std::cos(a);
        const float cz = std::sin(a);
        out.vertices.push_back({{0.0f, kR0 * (cy + 0.85f), kR0 * cz}, bark});
        out.vertices.push_back(
            {{kLength, kR1 * (cy + 0.85f), kR1 * cz}, bark * 1.12f});
    }
    for (int i = 0; i < kSides; ++i) {
        const auto a0 = base + static_cast<std::uint32_t>(2 * i);
        const auto a1 = a0 + 1;
        const auto b0 = base + static_cast<std::uint32_t>(2 * ((i + 1) % kSides));
        const auto b1 = b0 + 1;
        out.indices.insert(out.indices.end(), {a0, b0, a1, a1, b0, b1});
    }
    // Tapa del extremo grueso (abanico).
    const auto center = static_cast<std::uint32_t>(out.vertices.size());
    out.vertices.push_back({{0.0f, kR0 * 0.85f, 0.0f}, bark * 0.9f});
    for (int i = 0; i < kSides; ++i) {
        const auto a0 = base + static_cast<std::uint32_t>(2 * i);
        const auto b0 = base + static_cast<std::uint32_t>(2 * ((i + 1) % kSides));
        out.indices.insert(out.indices.end(), {center, a0, b0});
    }
    return out;
}

} // namespace veg
