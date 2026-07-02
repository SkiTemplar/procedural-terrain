#pragma once

#include "render/mesh.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace render {

struct Instance {
    glm::vec4 pos_scale; // xyz posición, w escala uniforme
    glm::vec4 tint_yaw;  // rgb multiplicador de color, w giro en radianes
};

// Malla + buffer de instancias (posición/escala/tinte/giro por instancia).
// RAII, move-only. Un draw call por lote.
class InstancedMesh {
public:
    InstancedMesh(const std::vector<Vertex>& vertices,
                  const std::vector<std::uint32_t>& indices,
                  const std::vector<Instance>& instances);
    ~InstancedMesh();

    InstancedMesh(const InstancedMesh&) = delete;
    InstancedMesh& operator=(const InstancedMesh&) = delete;
    InstancedMesh(InstancedMesh&& other) noexcept;
    InstancedMesh& operator=(InstancedMesh&& other) noexcept;

    void draw() const;
    GLsizei instance_count() const { return instance_count_; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLuint instance_vbo_ = 0;
    GLsizei index_count_ = 0;
    GLsizei instance_count_ = 0;

    void release();
};

} // namespace render
