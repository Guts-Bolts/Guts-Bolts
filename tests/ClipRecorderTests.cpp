// The clip recorder (src/game/ClipRecorder.h): it keeps the last 30 seconds, and the GIF it
// writes reads back as the same pictures (checked with a small GIF reader right here).
#include "game/ClipRecorder.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Read every frame's palette indexes back out of a GIF (just what ClipRecorder writes).
bool readGif(const std::string& g, int& w, int& h, std::vector<std::vector<uint8_t>>& frames) {
    auto u16 = [&](size_t at) { return (int)(uint8_t)g[at] | ((int)(uint8_t)g[at + 1] << 8); };
    if (g.size() < 13 || g.compare(0, 6, "GIF89a") != 0) return false;
    w = u16(6); h = u16(8);
    size_t at = 13;
    if ((uint8_t)g[10] & 0x80) at += 3u * (1u << (((uint8_t)g[10] & 7) + 1));
    while (at < g.size()) {
        const uint8_t tag = (uint8_t)g[at++];
        if (tag == 0x3B) return true;
        if (tag == 0x21) {   // an extension: skip its blocks
            ++at;
            while (at < g.size() && g[at]) at += (uint8_t)g[at] + 1;
            ++at;
            continue;
        }
        if (tag != 0x2C) return false;
        const int fw = u16(at + 4), fh = u16(at + 6);
        at += 9;
        const int minSize = (uint8_t)g[at++];
        std::string data;
        while (at < g.size() && g[at]) { data.append(g, at + 1, (uint8_t)g[at]); at += (uint8_t)g[at] + 1; }
        ++at;
        // LZW decode.
        const int clear = 1 << minSize, end = clear + 1;
        std::vector<std::vector<uint8_t>> table;
        auto reset = [&]() { table.clear(); for (int i = 0; i < clear + 2; ++i) table.push_back({(uint8_t)i}); };
        reset();
        int size = minSize + 1, prev = -1;
        size_t bit = 0;
        std::vector<uint8_t> out;
        while (bit + (size_t)size <= data.size() * 8) {
            int code = 0;
            for (int i = 0; i < size; ++i, ++bit) code |= (((uint8_t)data[bit / 8] >> (bit % 8)) & 1) << i;
            if (code == clear) { reset(); size = minSize + 1; prev = -1; continue; }
            if (code == end) break;
            std::vector<uint8_t> entry;
            if (code < (int)table.size()) entry = table[(size_t)code];
            else if (prev >= 0 && code == (int)table.size()) { entry = table[(size_t)prev]; entry.push_back(table[(size_t)prev][0]); }
            else return false;
            out.insert(out.end(), entry.begin(), entry.end());
            if (prev >= 0 && table.size() < 4096) {
                std::vector<uint8_t> add = table[(size_t)prev];
                add.push_back(entry[0]);
                table.push_back(add);
            }
            if ((int)table.size() == (1 << size) && size < 12) ++size;
            prev = code;
        }
        if ((int)out.size() != fw * fh) return false;
        frames.push_back(out);
    }
    return false;
}
} // namespace

int main() {
    int w = 0, h = 0;
    ClipRecorder::frameSize(1920, 1080, w, h);
    check(w == 480 && h == 270, "a 1920x1080 view becomes 480x270 frames");
    ClipRecorder::frameSize(300, 200, w, h);
    check(w == 300 && h == 200, "small views keep their size");

    // 40 seconds of a moving gradient at 10 pictures a second (more than it keeps).
    ClipRecorder rec;
    const int fw = 160, fh = 90;
    std::vector<uint8_t> rgba((size_t)fw * fh * 4);
    double t = 0.0;
    int added = 0;
    for (int i = 0; i < 400; ++i, t += 0.1) {
        if (!rec.wantFrame(t)) continue;
        for (int y = 0; y < fh; ++y)
            for (int x = 0; x < fw; ++x) {
                uint8_t* p = &rgba[((size_t)y * fw + x) * 4];
                p[0] = (uint8_t)((x * 255) / fw); p[1] = (uint8_t)((y * 255) / fh); p[2] = (uint8_t)((i * 7) & 255); p[3] = 255;
            }
        rec.addFrame(rgba.data(), fw, fh, true, t);
        ++added;
    }
    check(added == 400, "it takes a picture every tenth of a second");
    check(rec.frameCount() == 300 && rec.seconds() == 30.0, "only the last 30 seconds are kept");
    check(!rec.wantFrame(t - 0.05), "...and waits between pictures");

    const ClipRecorder::Snapshot snap = rec.snapshot();
    const std::string gif = ClipRecorder::toGif(snap);
    check(gif.size() > 1000 && gif.compare(0, 6, "GIF89a") == 0 && gif.back() == 0x3B, "it writes a GIF file");
    std::vector<std::vector<uint8_t>> frames;
    int gw = 0, gh = 0;
    const bool read = readGif(gif, gw, gh, frames);
    check(read && gw == fw && gh == fh && frames.size() == 300, "the GIF reads back: 300 frames of 160x90");
    check(read && frames.size() == snap.frames.size() && frames.front() == snap.frames.front() && frames.back() == snap.frames.back(),
          "...with exactly the same pictures");
    // A colour lands on the palette colour nearest to it.
    const std::vector<uint8_t>& pal = ClipRecorder::palette();
    const uint8_t red = ClipRecorder::nearest(250, 5, 5);
    check(pal[red * 3] == 255 && pal[red * 3 + 1] == 0 && pal[red * 3 + 2] == 0, "red stays red");
    // A different window size starts over (a GIF has one size).
    std::vector<uint8_t> small((size_t)80 * 40 * 4, 128);
    rec.addFrame(small.data(), 80, 40, false, t + 1.0);
    check(rec.frameCount() == 1, "changing the window's size starts a new clip");
    rec.clear();
    check(ClipRecorder::toGif(rec.snapshot()).empty(), "nothing recorded: no file");

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
