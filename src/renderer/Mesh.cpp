#include "Mesh.h"
#include "MeshLod.h"
#include <cstddef>

Mesh::Mesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices, bool lods) {
    upload(verts, indices);
    if (!lods || headless) return;   // (a server draws nothing: no copies needed)
    for (MeshLod::Level& l : MeshLod::build(verts, indices)) {
        m_lods.emplace_back(l.verts, l.idx);
        m_lodPixels.push_back(l.pixels);
    }
}

Mesh::~Mesh() { release(); }

Mesh::Mesh(Mesh&& o) noexcept
    : m_vao(o.m_vao), m_vbo(o.m_vbo), m_ebo(o.m_ebo), m_extra(o.m_extra), m_indexCount(o.m_indexCount),
      m_vboBytes(o.m_vboBytes), m_eboBytes(o.m_eboBytes), m_lods(std::move(o.m_lods)), m_lodPixels(std::move(o.m_lodPixels)) {
    o.m_vao = o.m_vbo = o.m_ebo = o.m_extra = 0;
}

Mesh& Mesh::operator=(Mesh&& o) noexcept {
    if (this != &o) {
        release();
        m_vao = o.m_vao; m_vbo = o.m_vbo; m_ebo = o.m_ebo; m_extra = o.m_extra;
        m_indexCount = o.m_indexCount;
        m_vboBytes = o.m_vboBytes; m_eboBytes = o.m_eboBytes;
        m_lods = std::move(o.m_lods); m_lodPixels = std::move(o.m_lodPixels);
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
    m_vboBytes = m_eboBytes = 0;
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
    m_vboBytes = verts.size() * sizeof(Vertex);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
    m_eboBytes = indices.size() * sizeof(uint32_t);

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
    ++m_version;
    m_lods.clear(); m_lodPixels.clear();   // (the copies were of the old shape)
    if (!m_vao) { upload(verts, indices); return; }   // (headless: upload just counts)
    m_indexCount = (int)indices.size();
    glBindVertexArray(m_vao);
    // The same size as before (a mesh that bends or ripples every frame): refill the
    // buffers in place, so the driver doesn't have to find new room for them each time.
    const size_t vb = verts.size() * sizeof(Vertex), eb = indices.size() * sizeof(uint32_t);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    if (vb == m_vboBytes) glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)vb, verts.data());
    else glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vb, verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    if (eb == m_eboBytes) glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)eb, indices.data());
    else glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)eb, indices.data(), GL_DYNAMIC_DRAW);
    m_vboBytes = vb;
    m_eboBytes = eb;
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
    const Mesh& m = shown();
    trianglesDrawn += m.m_indexCount / 3;
    glBindVertexArray(m.m_vao);
    glDrawElements(GL_TRIANGLES, m.m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::drawInstanced(GLuint perCopy, int count) const {
    const Mesh& m = shown();
    if (count <= 0 || !m.m_vao) return;
    trianglesDrawn += (long)(m.m_indexCount / 3) * count;
    glBindVertexArray(m.m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, perCopy);
    const GLsizei stride = sizeof(InstanceData);
    for (int i = 0; i < 7; ++i) {   // 4 for the matrix's columns, then colour, info and extra
        const size_t at = i < 4 ? offsetof(InstanceData, model) + sizeof(glm::vec4) * i
                        : i == 4 ? offsetof(InstanceData, color) : i == 5 ? offsetof(InstanceData, info) : offsetof(InstanceData, extra);
        glEnableVertexAttribArray(8 + i);
        glVertexAttribPointer(8 + i, 4, GL_FLOAT, GL_FALSE, stride, (void*)at);
        glVertexAttribDivisor(8 + i, 1);
    }
    glDrawElementsInstanced(GL_TRIANGLES, m.m_indexCount, GL_UNSIGNED_INT, nullptr, count);
    for (int i = 0; i < 7; ++i) {   // (so plain draws of this mesh don't read them)
        glVertexAttribDivisor(8 + i, 0);
        glDisableVertexAttribArray(8 + i);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Mesh::drawIndirect(GLuint perCopy, size_t offset) const {
#if defined(GB_IOS) || defined(GB_GLES) || defined(__EMSCRIPTEN__)
    (void)perCopy; (void)offset;   // (phones and the web draw the old way)
#else
    const Mesh& m = shown();
    if (!m.m_vao) return;   // (the triangles are counted by GpuCuller, a frame late)
    glBindVertexArray(m.m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, perCopy);
    const GLsizei stride = sizeof(InstanceData);
    for (int i = 0; i < 7; ++i) {   // 4 for the matrix's columns, then colour, info and extra
        const size_t at = i < 4 ? offsetof(InstanceData, model) + sizeof(glm::vec4) * i
                        : i == 4 ? offsetof(InstanceData, color) : i == 5 ? offsetof(InstanceData, info) : offsetof(InstanceData, extra);
        glEnableVertexAttribArray(8 + i);
        glVertexAttribPointer(8 + i, 4, GL_FLOAT, GL_FALSE, stride, (void*)at);
        glVertexAttribDivisor(8 + i, 1);
    }
    glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)offset);
    for (int i = 0; i < 7; ++i) {   // (so plain draws of this mesh don't read them)
        glVertexAttribDivisor(8 + i, 0);
        glDisableVertexAttribArray(8 + i);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif
}
