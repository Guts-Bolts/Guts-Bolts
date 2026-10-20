#include "PictureImport.h"
#include "../core/Paths.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <vector>

// A private copy of stb_image with every format on (the engine's own copy
// leaves out PSD, HDR, PIC and PNM to stay small).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#include <webp/decode.h>
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

namespace fs = std::filesystem;

namespace PictureImport {

namespace {

std::string extOf(const std::string& path) {
    std::string e = fs::path(path).extension().string();
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

bool isPlain(const std::string& e) {
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga" || e == ".gif";
}

bool needsPng(const std::string& e) {
    return e == ".webp" || e == ".svg" || e == ".psd" || e == ".hdr" || e == ".pic" || e == ".pnm" ||
           e == ".ppm" || e == ".pgm" || e == ".jfif" || e == ".jpe";
}

constexpr int kSvgSize = 1024;   // the longest side a drawing is painted at

// The pixels of the picture, 4 bytes (RGBA) each, top row first.
bool decode(const std::string& path, const std::string& e, std::vector<unsigned char>& px, int& w, int& h) {
    if (e == ".svg") {
        NSVGimage* img = nsvgParseFromFile(path.c_str(), "px", 96.0f);
        if (!img) return false;
        if (img->width <= 0 || img->height <= 0) { nsvgDelete(img); return false; }
        const float scale = kSvgSize / std::max(img->width, img->height);
        w = std::max(1, (int)(img->width * scale));
        h = std::max(1, (int)(img->height * scale));
        px.assign((size_t)w * h * 4, 0);
        NSVGrasterizer* r = nsvgCreateRasterizer();
        nsvgRasterize(r, img, 0, 0, scale, px.data(), w, h, w * 4);
        nsvgDeleteRasterizer(r);
        nsvgDelete(img);
        return true;
    }
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> bytes;
    unsigned char chunk[65536];
    for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) bytes.insert(bytes.end(), chunk, chunk + n);
    std::fclose(f);
    if (bytes.empty()) return false;
    if (unsigned char* p = WebPDecodeRGBA(bytes.data(), bytes.size(), &w, &h)) {
        px.assign(p, p + (size_t)w * h * 4);
        WebPFree(p);
        return true;
    }
    int n = 0;
    if (unsigned char* p = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4)) {
        px.assign(p, p + (size_t)w * h * 4);
        stbi_image_free(p);
        return true;
    }
    return false;
}

} // namespace

bool isPicture(const std::string& path) {
    const std::string e = extOf(path);
    return isPlain(e) || needsPng(e);
}

const char* patterns() {
    return "*.png *.jpg *.jpeg *.bmp *.tga *.gif *.webp *.svg *.psd *.hdr *.ppm *.pgm *.pnm *.pic *.jfif";
}

std::string usable(const std::string& path, std::string& err) {
    const std::string e = extOf(path);
    if (isPlain(e)) return path;
    std::vector<unsigned char> px;
    int w = 0, h = 0;
    if (!decode(path, e, px, w, h)) { err = "it isn't a picture Studio can read (it may be damaged)"; return {}; }
    std::error_code ec;
    const fs::path folder = Paths::gamesFolder() / "Imported Pictures";
    fs::create_directories(folder, ec);
    std::string stem = fs::path(path).stem().string();
    if (stem.empty()) stem = "Picture";
    fs::path out = folder / (stem + ".png");
    for (int i = 2; fs::exists(out, ec); ++i) out = folder / (stem + " " + std::to_string(i) + ".png");
    if (!stbi_write_png(out.string().c_str(), w, h, 4, px.data(), w * 4)) {
        err = "couldn't save the PNG copy in " + folder.string();
        return {};
    }
    return out.string();
}

std::string cantOpen(const std::string& path) {
    const std::string e = extOf(path);
    if (e == ".heic" || e == ".heif" || e == ".avif" || e == ".jxl" || e == ".tif" || e == ".tiff" || e == ".exr" ||
        e == ".dds" || e == ".ktx" || e == ".ktx2" || e == ".ico" || e == ".cr2" || e == ".nef")
        return "Studio can't read " + e + " pictures. Open it in any photo app (Paint, Preview, Photos, GIMP) and save it "
               "as a PNG or JPG, then import that.";
    return {};
}

} // namespace PictureImport
