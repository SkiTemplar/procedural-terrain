#include "render/shader.h"

#include <glm/gtc/type_ptr.hpp>

#include <stdexcept>
#include <utility>

namespace render {

GLuint Shader::compile(GLenum type, const char* src) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        const char* stage = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
        throw std::runtime_error(std::string("Shader (") + stage + "): " + log);
    }
    return shader;
}

Shader::Shader(const char* vertex_src, const char* fragment_src) {
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex_src);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_src);

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        glDeleteProgram(program_);
        program_ = 0;
        throw std::runtime_error(std::string("Shader link: ") + log);
    }
}

Shader::~Shader() {
    if (program_) {
        glDeleteProgram(program_);
    }
}

Shader::Shader(Shader&& other) noexcept : program_(std::exchange(other.program_, 0)) {}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (program_) {
            glDeleteProgram(program_);
        }
        program_ = std::exchange(other.program_, 0);
    }
    return *this;
}

void Shader::use() const {
    glUseProgram(program_);
}

GLint Shader::uniform_location(const char* name) const {
    return glGetUniformLocation(program_, name);
}

void Shader::set_mat4(const char* name, const glm::mat4& value) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name), 1, GL_FALSE,
                       glm::value_ptr(value));
}

void Shader::set_vec3(const char* name, const glm::vec3& value) const {
    glUniform3fv(glGetUniformLocation(program_, name), 1, glm::value_ptr(value));
}

void Shader::set_float(const char* name, float value) const {
    glUniform1f(glGetUniformLocation(program_, name), value);
}

void Shader::set_int(const char* name, int value) const {
    glUniform1i(glGetUniformLocation(program_, name), value);
}

} // namespace render
