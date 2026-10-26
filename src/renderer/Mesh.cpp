#include "Mesh.h"

Mesh::Mesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices) {
    upload(verts, indices);
}

Mesh::~Mesh() { release(); }

Mesh::Mesh(Mesh&& o) noexcept
    : m_vao(o.m_vao), m_vbo(o.m_vbo), m_ebo(o.m_ebo), m_extra(o.m_extra), m_indexCount(o.m_indexCount) {
    o.m_vao = o.m_vbo = o.m_ebo = o.m_extra = 0;
}

Mesh& Mesh::operator=(Mesh&& o) noexcept {
    if (this != &o) {
        release();
        m_vao = o.m_vao; m_vbo = o.m_vbo; m_ebo = o.m_ebo; m_extra = o.m_extra;
        m_indexCount = o.m_indexCount;
        o.m_vao = o.m_vbo = o.m_ebo = o.m_extra = 0;
    }
    return *this;
}

void Mesh::release() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_extra) glDeleteBuffers(1, &m_extra);
    m_vao = m_vbo = m_ebo = m_extra = 0;
}

void Mesh::upload(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices) {
    m_indexCount = (int)indices.size();
    if (headless) return;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)), verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));

    glBindVertexArray(0);
}

void Mesh::update(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices) {
    if (!m_vao) { upload(verts, indices); return; }   // (headless: upload just counts)
    m_indexCount = (int)indices.size();
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)), verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(uint32_t)), indices.data(), GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
}

void Mesh::setExtra(const std::vector<glm::vec4>& extra) {
    if (!m_vao) return;
    glBindVertexArray(m_vao);
    if (!m_extra) glGenBuffers(1, &m_extra);
    glBindBuffer(GL_ARRAY_BUFFER, m_extra);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(extra.size() * sizeof(glm::vec4)), extra.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), nullptr);
    glBindVertexArray(0);
}

void Mesh::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
