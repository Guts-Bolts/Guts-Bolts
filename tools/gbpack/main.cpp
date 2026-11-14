// gbpack: packs a game's pictures ahead of time (TexturePack.h), so they load fast.
//
//   gbpack <picture or folder> [more...]   packs every PNG / JPG / TGA / BMP it finds
//   gbpack --normal bumps.png               pack as a normal map (bumps), whatever its name
//   gbpack --force <folder>                 pack again even if the packed copy is up to date
//
// Each "door.png" gets a "door.png.gbtex" next to it. The game loads that instead when
// the graphics chip can read it, and the PNG when it can't (or when the PNG is newer).
// Pictures whose sides aren't a multiple of 4 are left as they are.
#include "core/TexturePack.h"

#include <stb_image.h>   // (the reader itself is built into the engine: Textures.cpp)

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool isPicture(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
}

struct Counts { int packed = 0, upToDate = 0, skipped = 0, failed = 0; size_t before = 0, after = 0; };

void packOne(const fs::path& pic, bool forceNormal, bool force, Counts& n) {
    fs::path out = pic;
    out += ".gbtex";
    std::error_code ec;
    if (!force && fs::exists(out, ec) && fs::last_write_time(out, ec) >= fs::last_write_time(pic, ec)) { ++n.upToDate; return; }
    stbi_set_flip_vertically_on_load(1);   // (the same way up as the game loads it)
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(pic.string().c_str(), &w, &h, &ch, 4);
    if (!px) { std::printf("  can't read  %s\n", pic.string().c_str()); ++n.failed; return; }
    if (!TexturePack::packable(w, h)) {
        std::printf("  left as is  %s (%d x %d: sides must be a multiple of 4)\n", pic.string().c_str(), w, h);
        stbi_image_free(px);
        ++n.skipped;
        return;
    }
    const bool normal = forceNormal || TexturePack::looksLikeNormalMap(pic.string());
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<uint8_t> bytes = TexturePack::pack(px, w, h, normal);
    stbi_image_free(px);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::ofstream f(out, std::ios::binary);
    f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    if (!f) { std::printf("  can't write %s\n", out.string().c_str()); ++n.failed; return; }
    std::printf("  packed      %s (%d x %d%s, %.1fs)\n", pic.string().c_str(), w, h, normal ? ", normal map" : "", secs);
    ++n.packed;
    n.before += (size_t)w * h * 4 * 4 / 3;   // (what it takes on the graphics card unpacked, with its smaller copies)
    n.after += bytes.size() / 2;             // (one of the two formats)
}

} // namespace

int main(int argc, char** argv) {
    bool normal = false, force = false;
    std::vector<fs::path> targets;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--normal") normal = true;
        else if (a == "--force") force = true;
        else if (a == "-h" || a == "--help") targets.clear(), argc = 0;
        else targets.push_back(a);
    }
    if (targets.empty()) {
        std::printf("gbpack: packs pictures ahead of time so Guts&Bolts games load them fast.\n"
                    "  gbpack <picture or folder> [more...]\n"
                    "  --normal  pack as normal maps (bumps)\n"
                    "  --force   pack again even if up to date\n");
        return 1;
    }
    Counts n;
    for (const fs::path& t : targets) {
        std::error_code ec;
        if (fs::is_directory(t, ec)) {
            for (const auto& e : fs::recursive_directory_iterator(t, ec))
                if (e.is_regular_file() && isPicture(e.path())) packOne(e.path(), normal, force, n);
        } else if (fs::exists(t, ec)) {
            packOne(t, normal, force, n);
        } else {
            std::printf("  not found   %s\n", t.string().c_str());
            ++n.failed;
        }
    }
    std::printf("%d packed, %d already up to date, %d left as is, %d failed.", n.packed, n.upToDate, n.skipped, n.failed);
    if (n.packed && n.after) std::printf(" Graphics memory: %.1f MB -> %.1f MB.", n.before / 1048576.0, n.after / 1048576.0);
    std::printf("\n");
    return n.failed ? 1 : 0;
}
