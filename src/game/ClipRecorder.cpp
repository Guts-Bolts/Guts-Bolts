#include "ClipRecorder.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kR = 6, kG = 7, kB = 6;   // palette steps per channel (6 * 7 * 6 = 252 colours)

// 4x4 ordered dithering: a tiny nudge per pixel spot, -0.5 .. 0.5 of a palette step.
constexpr float kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

inline int level(float v, int steps) { return std::clamp((int)std::lround(v * (float)(steps - 1) / 255.0f), 0, steps - 1); }

// --- GIF writing (LZW, the format's own compression) ---
struct BitWriter {
    std::string& out;
    std::string block;           // the current data sub-block (at most 255 bytes)
    uint32_t bits = 0; int count = 0;
    void put(int code, int size) {
        bits |= (uint32_t)code << count;
        count += size;
        while (count >= 8) { byte((uint8_t)(bits & 0xFF)); bits >>= 8; count -= 8; }
    }
    void byte(uint8_t b) {
        block.push_back((char)b);
        if (block.size() == 255) flushBlock();
    }
    void flushBlock() {
        if (block.empty()) return;
        out.push_back((char)block.size());
        out += block;
        block.clear();
    }
    void finish() {
        if (count > 0) byte((uint8_t)(bits & 0xFF));
        bits = 0; count = 0;
        flushBlock();
        out.push_back(0);   // end of the image's data
    }
};

// One frame's palette indexes, LZW-compressed with 8-bit codes.
class Lzw {
public:
    Lzw() : m_code(4096 * 256, 0), m_stamp(4096 * 256, 0) {}
    void encode(const std::vector<uint8_t>& px, std::string& out) {
        constexpr int kMin = 8, kClear = 1 << kMin, kEnd = kClear + 1;
        out.push_back((char)kMin);
        BitWriter w{out};
        int size = kMin + 1, next = kEnd + 1;
        auto reset = [&]() { ++m_gen; size = kMin + 1; next = kEnd + 1; };
        reset();
        w.put(kClear, size);
        if (px.empty()) { w.put(kEnd, size); w.finish(); return; }
        int prefix = px[0];
        for (size_t i = 1; i < px.size(); ++i) {
            const int c = px[i];
            const size_t key = (size_t)prefix * 256 + (size_t)c;
            if (m_stamp[key] == m_gen) { prefix = m_code[key]; continue; }
            w.put(prefix, size);
            m_code[key] = (uint16_t)next;
            m_stamp[key] = m_gen;
            if (next == (1 << size) && size < 12) ++size;   // (the decoder widens as it adds this one)
            if (next == 4095) {   // the table is full: start over
                w.put(kClear, size);
                reset();
            } else {
                ++next;
            }
            prefix = c;
        }
        w.put(prefix, size);
        w.put(kEnd, size);
        w.finish();
    }

private:
    std::vector<uint16_t> m_code;    // (prefix, next byte) -> code ...
    std::vector<uint32_t> m_stamp;   // ...valid only when its stamp is this table's (no clearing 8 MB each time)
    uint32_t m_gen = 0;
};

void put16(std::string& out, int v) { out.push_back((char)(v & 0xFF)); out.push_back((char)((v >> 8) & 0xFF)); }
} // namespace

const std::vector<uint8_t>& ClipRecorder::palette() {
    static const std::vector<uint8_t> pal = [] {
        std::vector<uint8_t> p(256 * 3, 0);
        int i = 0;
        for (int r = 0; r < kR; ++r)
            for (int g = 0; g < kG; ++g)
                for (int b = 0; b < kB; ++b, ++i) {
                    p[(size_t)i * 3 + 0] = (uint8_t)std::lround(r * 255.0 / (kR - 1));
                    p[(size_t)i * 3 + 1] = (uint8_t)std::lround(g * 255.0 / (kG - 1));
                    p[(size_t)i * 3 + 2] = (uint8_t)std::lround(b * 255.0 / (kB - 1));
                }
        return p;
    }();
    return pal;
}

uint8_t ClipRecorder::nearest(int r, int g, int b) {
    return (uint8_t)((level((float)r, kR) * kG + level((float)g, kG)) * kB + level((float)b, kB));
}

void ClipRecorder::frameSize(int w, int h, int& outW, int& outH) {
    if (w <= 0 || h <= 0) { outW = outH = 0; return; }
    outW = std::min(w, kMaxWidth);
    outH = std::max(1, (int)std::lround((double)h * outW / w));
    outW &= ~1; outH &= ~1;   // (even sizes: some players are fussy)
    outW = std::max(outW, 2); outH = std::max(outH, 2);
}

void ClipRecorder::addFrame(const uint8_t* rgba, int w, int h, bool flipY, double now) {
    if (!rgba || w <= 0 || h <= 0) return;
    m_lastAt = now;
    Frame f{w, h, std::vector<uint8_t>((size_t)w * h)};
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = rgba + (size_t)(flipY ? h - 1 - y : y) * w * 4;
        uint8_t* outRow = f.px.data() + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            const float d = (kBayer[y & 3][x & 3] + 0.5f) / 16.0f - 0.5f;   // -0.5 .. 0.5
            const uint8_t* p = row + (size_t)x * 4;
            const int r = level(p[0] + d * 255.0f / (kR - 1), kR), g = level(p[1] + d * 255.0f / (kG - 1), kG),
                      b = level(p[2] + d * 255.0f / (kB - 1), kB);
            outRow[x] = (uint8_t)((r * kG + g) * kB + b);
        }
    }
    // A new size (the window changed): the old frames don't fit a GIF with them, so they go.
    if (!m_frames.empty() && (m_frames.back().w != w || m_frames.back().h != h)) m_frames.clear();
    m_frames.push_back(std::move(f));
    while ((double)m_frames.size() > kSeconds * kFps) m_frames.pop_front();
}

ClipRecorder::Snapshot ClipRecorder::snapshot() const {
    Snapshot s;
    if (m_frames.empty()) return s;
    s.w = m_frames.back().w;
    s.h = m_frames.back().h;
    for (const Frame& f : m_frames) if (f.w == s.w && f.h == s.h) s.frames.push_back(f.px);
    return s;
}

std::string ClipRecorder::toGif(const Snapshot& clip) {
    if (clip.frames.empty() || clip.w <= 0 || clip.h <= 0) return "";
    std::string out = "GIF89a";
    put16(out, clip.w);
    put16(out, clip.h);
    out.push_back((char)0xF7);   // a global colour table of 256 entries
    out.push_back(0);            // background colour
    out.push_back(0);            // (pixel shape)
    const std::vector<uint8_t>& pal = palette();
    out.append((const char*)pal.data(), pal.size());
    // Loop forever (the NETSCAPE2.0 extension).
    out += std::string("\x21\xFF\x0B" "NETSCAPE2.0" "\x03\x01\x00\x00\x00", 19);
    Lzw lzw;
    const int delay = 100 / kFps;   // hundredths of a second per frame
    for (const auto& px : clip.frames) {
        out += std::string("\x21\xF9\x04\x04", 4);   // graphic control: keep the frame (disposal 1)
        put16(out, delay);
        out.push_back(0); out.push_back(0);
        out.push_back(0x2C);                          // image descriptor: the whole picture
        put16(out, 0); put16(out, 0); put16(out, clip.w); put16(out, clip.h);
        out.push_back(0);
        lzw.encode(px, out);
    }
    out.push_back(0x3B);
    return out;
}
