#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>

namespace render {

// Programa GL compilado desde fuentes VS/FS. RAII, move-only.
class Shader {
public:
    Shader(const char* vertex_src, const char* fragment_src);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void use() const;
    GLuint id() const { return program_; }
    GLint uniform_location(const char* name) const;

    void set_mat4(const char* name, const glm::mat4& value) const;
    void set_vec3(const char* name, const glm::vec3& value) const;
    void set_float(const char* name, float value) const;
    void set_int(const char* name, int value) const;

private:
    GLuint program_ = 0;

    static GLuint compile(GLenum type, const char* src);
};

} // namespace render
