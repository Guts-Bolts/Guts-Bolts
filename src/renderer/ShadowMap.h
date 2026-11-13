#pragma once
#include "GL.h"

// A depth-only framebuffer used as a shadow map. Render the scene into it from
// the light's point of view, then sample it in the lit shader.
class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();

    ShadowMap(const ShadowMap&)            = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    void init(int size);
    void init(int width, int height);   // (not square: the lamp-shadow sheet)
    void bindForWrite() const;       // bind FBO + viewport, clears depth
    void bindForRead(int unit) const;// bind the depth texture to a texture unit
    // Draw into one piece of it only (a tile of the shadow atlas): that piece is cleared,
    // the rest keeps what it had.
    void bindTileForWrite(int x, int y, int width, int height) const;

    GLuint depthTexture() const { return m_depth; }
    int    size()         const { return m_size; }
    int    height()       const { return m_height; }

private:
    GLuint m_fbo   = 0;
    GLuint m_depth = 0;
    int    m_size  = 0;   // width
    int    m_height = 0;
};
