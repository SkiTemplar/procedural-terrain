#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace render {

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal{0.0f, 0.0f, 0.0f}; // normal suave (para el blend lejano)
    // Iluminación precomputada del terreno (sol fijo): x = sombra arrojada
    // (1=al sol, 0=en sombra de montaña), y = AO de cielo (1=abierto, 0=ocluido).
    // Por defecto {1,1} = sin ocluir (agua, mar y modelos de veg no la usan).
    glm::vec2 light{1.0f, 1.0f};
};

// VAO+VBO+EBO indexado. RAII, move-only.
class Mesh {
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void draw() const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei index_count_ = 0;

    void release();
};

} // namespace render
