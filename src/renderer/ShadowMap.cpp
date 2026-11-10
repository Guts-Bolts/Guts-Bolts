#include "ShadowMap.h"

ShadowMap::~ShadowMap() {
    if (m_depth) glDeleteTextures(1, &m_depth);
    if (m_fbo)   glDeleteFramebuffers(1, &m_fbo);
}

void ShadowMap::init(int size) { init(size, size); }

void ShadowMap::init(int size, int height) {
    // Safe to call again (e.g. when the shadow quality setting changes).
    if (m_depth) { glDeleteTextures(1, &m_depth); m_depth = 0; }
    if (m_fbo)   { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
    m_size = size;
    m_height = height;

    glGenTextures(1, &m_depth);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, height, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Clamp to a white border so anything outside the light frustum is lit.
#ifdef GB_GLES
    // OpenGL ES 3.0 has no border colour; the shader treats outside as lit anyway.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
#endif

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth, 0);
    GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMap::bindForWrite() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_size, m_height);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void ShadowMap::bindForRead(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glActiveTexture(GL_TEXTURE0);
}
