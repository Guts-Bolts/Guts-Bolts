#include "EditableImages.h"
#include "Mesh.h"
#include "Textures.h"

#include <stb_image_write.h>   // (its code is in Textures.cpp)
#include <algorithm>
#include <cmath>
#include <map>

namespace EditableImages {
namespace {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool dirty = true;
    unsigned tex = 0;
};
std::map<int, Image>& images() {
    static std::map<int, Image> m;
    return m;
}
int& nextId() {
    static int n = 1;
    return n;
}
long long usedPixels() {
    long long n = 0;
    for (const auto& [id, im] : images()) n += (long long)im.w * im.h;
    return n;
}
Image* find(int id) {
    auto it = images().find(id);
    return it == images().end() ? nullptr : &it->second;
}
uint8_t byte(float v) { return (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); }

void plot(Image& im, int x, int y, const Colour& c, bool blend, float cover = 1.0f) {
    if (x < 0 || y < 0 || x >= im.w || y >= im.h) return;
    uint8_t* p = &im.px[((size_t)y * im.w + x) * 4];
    const float a = std::clamp(c.a, 0.0f, 1.0f) * std::clamp(cover, 0.0f, 1.0f);
    if (!blend) {
        p[0] = byte(c.r); p[1] = byte(c.g); p[2] = byte(c.b); p[3] = byte(a);
        return;
    }
    // "Source over": the new colour on top of what's there.
    const float da = p[3] / 255.0f, oa = a + da * (1.0f - a);
    if (oa <= 0.0f) { p[0] = p[1] = p[2] = p[3] = 0; return; }
    const float src[3] = {c.r, c.g, c.b};
    for (int i = 0; i < 3; ++i) p[i] = byte((std::clamp(src[i], 0.0f, 1.0f) * a + (p[i] / 255.0f) * da * (1.0f - a)) / oa);
    p[3] = byte(oa);
}

} // namespace

int create(int width, int height) {
    if (width < 1 || height < 1 || width > kMostSide || height > kMostSide) return 0;
    if (usedPixels() + (long long)width * height > kMostPixels) return 0;
    const int id = nextId()++;
    Image& im = images()[id];
    im.w = width;
    im.h = height;
    im.px.assign((size_t)width * height * 4, 0);
    return id;
}

bool exists(int id) { return find(id) != nullptr; }

void destroy(int id) {
    if (!find(id)) return;
    if (!Mesh::headless) Textures::forget(contentId(id));
    images().erase(id);
}

std::vector<uint8_t>* pixels(int id, int* width, int* height) {
    Image* im = find(id);
    if (!im) return nullptr;
    if (width) *width = im->w;
    if (height) *height = im->h;
    return &im->px;
}

void touch(int id) {
    if (Image* im = find(id)) im->dirty = true;
}

std::string contentId(int id) { return "editable://" + std::to_string(id); }

int idOf(const std::string& c) {
    if (c.rfind("editable://", 0) != 0) return 0;
    const int id = std::atoi(c.c_str() + 11);
    return find(id) ? id : 0;
}

unsigned texture(const std::string& c) {
    Image* im = find(idOf(c));
    if (!im || Mesh::headless) return 0;
    if (im->dirty || !im->tex) {
        im->tex = Textures::put(c, im->px.data(), im->w, im->h);
        im->dirty = false;
    }
    return im->tex;
}

void clearAll() {
    if (!Mesh::headless)
        for (const auto& [id, im] : images()) Textures::forget(contentId(id));
    images().clear();
}

void fillRect(int id, int x, int y, int w, int h, Colour c, bool blend) {
    Image* im = find(id);
    if (!im) return;
    const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(im->w, x + w), y1 = std::min(im->h, y + h);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) plot(*im, xx, yy, c, blend);
    im->dirty = true;
}

void fillCircle(int id, float cx, float cy, float radius, Colour c, bool blend) {
    Image* im = find(id);
    if (!im || radius <= 0.0f) return;
    const int x0 = std::max(0, (int)std::floor(cx - radius - 1)), x1 = std::min(im->w - 1, (int)std::ceil(cx + radius + 1));
    const int y0 = std::max(0, (int)std::floor(cy - radius - 1)), y1 = std::min(im->h - 1, (int)std::ceil(cy + radius + 1));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            // pixel centres; a soft edge one pixel wide
            const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            const float cover = std::clamp(radius - d + 0.5f, 0.0f, 1.0f);
            if (cover > 0.0f) plot(*im, x, y, c, blend || cover < 1.0f, cover);
        }
    im->dirty = true;
}

void line(int id, float x0, float y0, float x1, float y1, float thickness, Colour c, bool blend) {
    Image* im = find(id);
    if (!im) return;
    const float r = std::max(0.5f, thickness * 0.5f);
    const float len = std::hypot(x1 - x0, y1 - y0);
    const int steps = std::max(1, (int)std::ceil(len / std::max(0.5f, r * 0.5f)));
    if (r <= 0.5f) {   // one pixel wide: straight pixels, no soft edges
        for (int i = 0; i <= steps; ++i) {
            const float t = (float)i / steps;
            plot(*im, (int)std::floor(x0 + (x1 - x0) * t), (int)std::floor(y0 + (y1 - y0) * t), c, blend);
        }
    } else {
        for (int i = 0; i <= steps; ++i) {
            const float t = (float)i / steps;
            fillCircle(id, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, c, blend);
        }
    }
    im->dirty = true;
}

void setPixel(int id, int x, int y, Colour c, bool blend) {
    Image* im = find(id);
    if (!im) return;
    plot(*im, x, y, c, blend);
    im->dirty = true;
}

bool getPixel(int id, int x, int y, Colour& out) {
    Image* im = find(id);
    if (!im || x < 0 || y < 0 || x >= im->w || y >= im->h) return false;
    const uint8_t* p = &im->px[((size_t)y * im->w + x) * 4];
    out = {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f};
    return true;
}

std::string png(int id) {
    Image* im = find(id);
    if (!im) return "";
    std::string out;
    stbi_write_png_to_func([](void* ctx, void* data, int size) { static_cast<std::string*>(ctx)->append((const char*)data, (size_t)size); },
                           &out, im->w, im->h, 4, im->px.data(), im->w * 4);
    return out;
}

} // namespace EditableImages
