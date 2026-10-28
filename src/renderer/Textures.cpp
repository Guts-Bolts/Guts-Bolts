#include "Textures.h"
#include "GL.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "ShirtTemplate.h"   // generated: assets/templates/shirt_template.png
#include "PantsTemplate.h"   // generated: assets/templates/pants_template.png

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>   // (Studio saves game pictures as PNG with it)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <cstring>
#include <vector>

namespace Textures {

namespace {

struct Entry {
    unsigned tex = 0;
    int      w = 0, h = 0;
    bool     broken = false;      // tried and it isn't a picture
    double   retryAt = 0.0;       // not downloaded yet: look again later
};

std::map<std::string, Entry>& cache() {
    static std::map<std::string, Entry> c;
    return c;
}

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::filesystem::path locate(const std::string& id) {
    if (id.rfind("gb:", 0) == 0) return Paths::downloaded(id.substr(3));
    std::error_code ec;
    std::filesystem::path p = Paths::gamesFolder() / id;
    if (std::filesystem::exists(p, ec)) return p;
    p = id;
    if (std::filesystem::exists(p, ec)) return p;
    return {};
}

// The classic smiley, as a flat picture (a 2D drawing, made here): two oval eyes
// and a U-shaped smile, see-through around them. Laid out like every face picture:
// the whole front of the head, bottom row first. Same shapes and places as the old
// 3D face (head-local units: x, y from -0.5 to 0.5).
void makeClassicFace(Entry& e) {
    const int n = 512;
    std::vector<unsigned char> px((size_t)n * n * 4, 0);
    auto smileY = [](float x) { float t = std::abs(x) / 0.2f; return -0.27f + 0.22f * std::pow(t, 1.7f); };
    const float aa = 1.2f / n;   // smooth edges (about a pixel)
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            const float x = (c + 0.5f) / n - 0.5f, y = (r + 0.5f) / n - 0.5f;
            float ink = 0.0f;
            // Eyes: ovals 0.065 wide and 0.13 tall, at (+-0.1, 0.16).
            for (float ex : {-0.1f, 0.1f}) {
                const float dx = (x - ex) / 0.0325f, dy = (y - 0.16f) / 0.065f;
                const float d = (std::sqrt(dx * dx + dy * dy) - 1.0f) * 0.0325f;   // ~distance to the edge
                ink = std::max(ink, std::clamp(0.5f - d / aa, 0.0f, 1.0f));
            }
            // Smile: a 0.055-thick line along the curve, with round ends.
            float best = 1e9f;
            for (int k = 0; k <= 80; ++k) {
                const float sx = -0.2f + 0.4f * k / 80.0f;
                best = std::min(best, std::hypot(x - sx, y - smileY(sx)));
            }
            ink = std::max(ink, std::clamp(0.5f - (best - 0.0275f) / aa, 0.0f, 1.0f));
            unsigned char* o = &px[((size_t)r * n + c) * 4];
            o[0] = o[1] = o[2] = 0;
            o[3] = (unsigned char)std::lround(ink * 255.0f);
        }
    glGenTextures(1, &e.tex);
    glBindTexture(GL_TEXTURE_2D, e.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    e.w = e.h = n;
}

void load(const std::string& id, Entry& e) {
    if (id == kClassicFace) { makeClassicFace(e); return; }
    stbi_set_flip_vertically_on_load(1);                   // GL wants the bottom row first
    int w = 0, h = 0, n = 0;
    unsigned char* px = nullptr;
    if (id == kShirtTemplatePicture || id == kPantsTemplatePicture) {   // baked into the program
        const bool shirt = id == kShirtTemplatePicture;
        px = stbi_load_from_memory(shirt ? kShirtTemplate : kPantsTemplate,
                                   (int)(shirt ? kShirtTemplateSize : kPantsTemplateSize), &w, &h, &n, 4);
    } else {
        std::filesystem::path p = locate(id);
        if (p.empty()) { e.retryAt = now() + 1.0; return; }   // maybe it's still downloading
        px = stbi_load(p.string().c_str(), &w, &h, &n, 4);
    }
    if (!px) {
        e.broken = true;
        Log::warn("\"" + id + "\" isn't a picture Guts and Bolts can read (use PNG or JPG).");
        return;
    }
    glGenTextures(1, &e.tex);
    glBindTexture(GL_TEXTURE_2D, e.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(px);
    e.w = w;
    e.h = h;
}

} // namespace

unsigned get(const std::string& id) {
    if (id.empty()) return 0;
    Entry& e = cache()[id];
    if (!e.tex && !e.broken && now() >= e.retryAt) load(id, e);
    return e.tex;
}

bool size(const std::string& id, int& w, int& h) {
    if (!get(id)) return false;
    const Entry& e = cache()[id];
    w = e.w;
    h = e.h;
    return true;
}

void forget(const std::string& id) {
    auto it = cache().find(id);
    if (it == cache().end()) return;
    if (it->second.tex) glDeleteTextures(1, &it->second.tex);
    cache().erase(it);
}

unsigned put(const std::string& id, const unsigned char* rgba, int w, int h) {
    Entry& e = cache()[id];
    if (!e.tex) glGenTextures(1, &e.tex);
    // GL wants the bottom row first.
    std::vector<unsigned char> flipped((size_t)w * h * 4);
    for (int r = 0; r < h; ++r)
        std::memcpy(&flipped[(size_t)r * w * 4], rgba + (size_t)(h - 1 - r) * w * 4, (size_t)w * 4);
    glBindTexture(GL_TEXTURE_2D, e.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    e.w = w;
    e.h = h;
    e.broken = false;
    return e.tex;
}

void clear() {
    for (auto& [id, e] : cache())
        if (e.tex) glDeleteTextures(1, &e.tex);
    cache().clear();
}

} // namespace Textures
