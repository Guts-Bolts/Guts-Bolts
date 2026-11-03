#include "Framebuffer.h"
#include <stb_image_write.h>   // (its code is in Textures.cpp)
#include <algorithm>
#include <vector>

void Framebuffer::resize(int w, int h) {
    if (w == m_w && h == m_h) return;
    destroy();
    if (w > 0 && h > 0) create(w, h);
}

void Framebuffer::create(int w, int h) {
    m_w = w; m_h = h;

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_color);
    glBindTexture(GL_TEXTURE_2D, m_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);

    glGenRenderbuffers(1, &m_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depth);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::destroy() {
    if (m_fbo)   { glDeleteFramebuffers(1,    &m_fbo);   m_fbo   = 0; }
    if (m_color) { glDeleteTextures(1,         &m_color); m_color = 0; }
    if (m_depth) { glDeleteRenderbuffers(1,    &m_depth); m_depth = 0; }
    m_w = m_h = 0;
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_w, m_h);
}

void Framebuffer::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Framebuffer::readShrunk(Framebuffer& small, int w, int h, std::vector<unsigned char>& rgba) const {
    if (m_w <= 0 || m_h <= 0 || w <= 0 || h <= 0) return false;
    small.resize(w, h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, small.m_fbo);
    glBlitFramebuffer(0, 0, m_w, m_h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, small.m_fbo);
    rgba.resize((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

std::string Framebuffer::toPng() const {
    const int w = m_w, h = m_h;
    if (w <= 0 || h <= 0) return {};
    std::vector<unsigned char> px((size_t)w * h * 4);
    bind();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    unbind();
    // OpenGL's rows go bottom-up; pictures go top-down. And no see-through pixels.
    std::vector<unsigned char> img(px.size());
    const size_t row = (size_t)w * 4;
    for (int y = 0; y < h; ++y)
        std::copy(px.begin() + (size_t)(h - 1 - y) * row, px.begin() + (size_t)(h - y) * row, img.begin() + (size_t)y * row);
    for (size_t i = 3; i < img.size(); i += 4) img[i] = 255;
    std::string out;
    stbi_write_png_to_func([](void* ctx, void* data, int size) {
        static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), (size_t)size);
    }, &out, w, h, 4, img.data(), (int)row);
    return out;
}
