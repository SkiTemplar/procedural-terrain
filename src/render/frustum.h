#pragma once

#include <glm/glm.hpp>

#include <array>

namespace render {

// Frustum de una matriz view_proj (Gribb-Hartmann): los 6 planos salen de
// sumar/restar filas. Sirve igual para la perspectiva de la cámara que para
// la ortográfica del sol (cascadas de sombra). Sin dependencias de OpenGL.
class Frustum {
public:
    explicit Frustum(const glm::mat4& view_proj) {
        const glm::mat4 m = glm::transpose(view_proj); // filas de view_proj
        planes_ = {m[3] + m[0],  // izquierda
                   m[3] - m[0],  // derecha
                   m[3] + m[1],  // abajo
                   m[3] - m[1],  // arriba
                   m[3] + m[2],  // cerca
                   m[3] - m[2]}; // lejos
    }

    // Test del vértice positivo: el AABB queda fuera si su esquina más
    // adentrada respecto a algún plano sigue en el semiespacio exterior.
    // Conservador (puede aceptar cajas fuera junto a una arista), nunca
    // descarta una caja que corta el volumen.
    bool intersects(const glm::vec3& aabb_min, const glm::vec3& aabb_max) const {
        for (const glm::vec4& p : planes_) {
            const glm::vec3 positive{p.x >= 0.0f ? aabb_max.x : aabb_min.x,
                                     p.y >= 0.0f ? aabb_max.y : aabb_min.y,
                                     p.z >= 0.0f ? aabb_max.z : aabb_min.z};
            if (glm::dot(glm::vec3{p}, positive) + p.w < 0.0f) {
                return false;
            }
        }
        return true;
    }

private:
    std::array<glm::vec4, 6> planes_{};
};

} // namespace render
