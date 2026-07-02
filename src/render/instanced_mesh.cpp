#include "render/instanced_mesh.h"

#include <utility>

namespace render {

InstancedMesh::InstancedMesh(const std::vector<Vertex>& vertices,
                             const std::vector<std::uint32_t>& indices,
                             const std::vector<Instance>& instances)
    : index_count_(static_cast<GLsizei>(indices.size())),
      instance_count_(static_cast<GLsizei>(instances.size())) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glGenBuffers(1, &instance_vbo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, color)));

    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(instances.size() * sizeof(Instance)),
                 instances.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, pos_scale)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, tint_yaw)));
    glVertexAttribDivisor(3, 1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

void InstancedMesh::release() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        glDeleteBuffers(1, &vbo_);
        glDeleteBuffers(1, &ebo_);
        glDeleteBuffers(1, &instance_vbo_);
        vao_ = vbo_ = ebo_ = instance_vbo_ = 0;
    }
}

InstancedMesh::~InstancedMesh() {
    release();
}

InstancedMesh::InstancedMesh(InstancedMesh&& other) noexcept
    : vao_(std::exchange(other.vao_, 0)),
      vbo_(std::exchange(other.vbo_, 0)),
      ebo_(std::exchange(other.ebo_, 0)),
      instance_vbo_(std::exchange(other.instance_vbo_, 0)),
      index_count_(std::exchange(other.index_count_, 0)),
      instance_count_(std::exchange(other.instance_count_, 0)) {}

InstancedMesh& InstancedMesh::operator=(InstancedMesh&& other) noexcept {
    if (this != &other) {
        release();
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        ebo_ = std::exchange(other.ebo_, 0);
        instance_vbo_ = std::exchange(other.instance_vbo_, 0);
        index_count_ = std::exchange(other.index_count_, 0);
        instance_count_ = std::exchange(other.instance_count_, 0);
    }
    return *this;
}

void InstancedMesh::draw() const {
    if (instance_count_ == 0) {
        return;
    }
    glBindVertexArray(vao_);
    glDrawElementsInstanced(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr,
                            instance_count_);
    glBindVertexArray(0);
}

} // namespace render
