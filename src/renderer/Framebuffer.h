#pragma once
#include "GL.h"
#include <string>
#include <vector>

class Framebuffer {
public:
    Framebuffer()  = default;
    ~Framebuffer() { destroy(); }

    Framebuffer(const Framebuffer&)            = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    void resize(int w, int h);
    void bind()   const;
    void unbind() const;

    GLuint colorTexture() const { return m_color; }
    int width()  const { return m_w; }
    int height() const { return m_h; }
    // What it shows, as a .png file (no see-through pixels). For thumbnails.
    std::string toPng() const;
    // What it shows, shrunk into `small` (resized to w x h) and read back as RGBA, bottom row
    // first. Quick enough to do several times a second (the clip recorder).
    bool readShrunk(Framebuffer& small, int w, int h, std::vector<unsigned char>& rgba) const;

private:
    void create(int w, int h);
    void destroy();

    GLuint m_fbo   = 0;
    GLuint m_color = 0;
    GLuint m_depth = 0;
    int    m_w = 0, m_h = 0;
};
