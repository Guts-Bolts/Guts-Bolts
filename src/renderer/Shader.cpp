#include "Shader.h"
#include "Shaders.h"
#include <stdexcept>
#include <string>
#include <glm/gtc/type_ptr.hpp>

Shader::Shader(const char* vertSrc, const char* fragSrc) {
    GLuint vert = compile(GL_VERTEX_SHADER,   vertSrc);
    GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc);

    m_id = glCreateProgram();
    glAttachShader(m_id, vert);
    glAttachShader(m_id, frag);
    glLinkProgram(m_id);

    GLint ok;
    glGetProgramiv(m_id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m_id, 1024, nullptr, log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        throw std::runtime_error(std::string("Shader link error: ") + log);
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
}

Shader::~Shader() { if (m_id) glDeleteProgram(m_id); }

Shader::Shader(Shader&& o) noexcept : m_id(o.m_id), m_locs(std::move(o.m_locs)) { o.m_id = 0; }
Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) { if (m_id) glDeleteProgram(m_id); m_id = o.m_id; m_locs = std::move(o.m_locs); o.m_id = 0; }
    return *this;
}

GLint Shader::loc(const char* name) const {
    auto it = m_locs.find(std::string_view(name));
    if (it != m_locs.end()) return it->second;
    GLint l = glGetUniformLocation(m_id, name);
    m_locs.emplace(name, l);
    return l;
}

void Shader::setVec4Array(const char* n, const glm::vec4* v, int count) const {
    if (count > 0) glUniform4fv(loc(n), count, glm::value_ptr(v[0]));
}

void Shader::setVec3Array(const char* n, const glm::vec3* v, int count) const {
    if (count > 0) glUniform3fv(loc(n), count, glm::value_ptr(v[0]));
}

void Shader::setFloatArray(const char* n, const float* v, int count) const {
    if (count > 0) glUniform1fv(loc(n), count, v);
}

void Shader::bind()   const { glUseProgram(m_id); }
void Shader::unbind() const { glUseProgram(0); }

void Shader::setMat4 (const char* n, const glm::mat4& v) const { glUniformMatrix4fv(loc(n),1,GL_FALSE,glm::value_ptr(v)); }
void Shader::setMat3 (const char* n, const glm::mat3& v) const { glUniformMatrix3fv(loc(n),1,GL_FALSE,glm::value_ptr(v)); }
void Shader::setVec2 (const char* n, const glm::vec2& v) const { glUniform2fv(loc(n),1,glm::value_ptr(v)); }
void Shader::setVec3 (const char* n, const glm::vec3& v) const { glUniform3fv(loc(n),1,glm::value_ptr(v)); }
void Shader::setVec4 (const char* n, const glm::vec4& v) const { glUniform4fv(loc(n),1,glm::value_ptr(v)); }
void Shader::setFloat(const char* n, float v)             const { glUniform1f (loc(n),v); }
void Shader::setInt  (const char* n, int v)               const { glUniform1i (loc(n),v); }
void Shader::setBool (const char* n, bool v)              const { glUniform1i (loc(n),(int)v); }

GLuint Shader::compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    std::string text = src;
    const std::string marker = "#pragma gb_common";
    for (size_t at; (at = text.find(marker)) != std::string::npos;)
        text.replace(at, marker.size(), Shaders::common);   // shared helper functions
    const std::string rayMarker = "#pragma gb_fluidray";
    for (size_t at; (at = text.find(rayMarker)) != std::string::npos;)
        text.replace(at, rayMarker.size(), Shaders::fluidRay);   // hitting a liquid drop (Shaders.h)
#ifdef GB_GLES
    // Phones: same shaders, OpenGL ES header instead of desktop GLSL 4.1.
    const std::string desktop = "#version 410 core";
    size_t at = text.find(desktop);
    if (at != std::string::npos)
        text.replace(at, desktop.size(),
                     "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;");
#endif
    const char* finalSrc = text.c_str();
    glShaderSource(s, 1, &finalSrc, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("Shader compile error: ") + log);
    }
    return s;
}
