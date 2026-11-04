#pragma once
#include "GL.h"
#include <glm/glm.hpp>
#include <vector>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color{1.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

// One copy's settings when many copies of a mesh are drawn at once (Mesh::drawInstanced).
struct InstanceData {
    glm::mat4 model;   // where it is (shader inputs 8-11)
    glm::vec4 color;   // rgb, alpha (12)
    glm::vec4 info;    // material id, selected (0/1), wet (0..1), unused (13)
};

class Mesh {
public:
    Mesh() = default;
    Mesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices);
    ~Mesh();

    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& o) noexcept;
    Mesh& operator=(Mesh&& o) noexcept;

    void draw() const;
    // Draw `count` copies at once (instancing): `perCopy` is a buffer of InstanceData,
    // one for each copy (shader inputs 8-13). See SceneRenderer's part drawing.
    void drawInstanced(GLuint perCopy, int count) const;
    // Replace the whole mesh (for shapes that change every frame, like waves).
    void update(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices);
    int  indexCount() const { return m_indexCount; }
    // An optional 4th value per corner (shader input 3), after update(). The
    // terrain uses it for how much of each ground is at each corner.
    void setExtra(const std::vector<glm::vec4>& extra);
    // No graphics at all (the game server program): meshes keep their size but
    // nothing goes to a graphics card.
    static inline bool headless = false;

private:
    void upload(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices);
    void release();

    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0, m_extra = 0;
    int    m_indexCount = 0;
};
