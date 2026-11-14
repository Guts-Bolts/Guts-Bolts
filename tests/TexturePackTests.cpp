// Pictures packed ahead of time (step 7e of the photoreal upgrade, src/core/TexturePack.h):
// each squeezed format keeps the picture close to the original, the .gbtex file holds
// every smaller copy in both formats, and the game picks the right one for each device.
// (The block layouts were also checked against a real graphics driver's own unpacking.)
//
//   TexturePackTests
#include "core/DeviceFeatures.h"
#include "core/TexturePack.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace TexturePack;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

uint32_t s_seed = 99;
int rnd(int n) { s_seed = s_seed * 1664525u + 1013904223u; return (int)((s_seed >> 8) % (uint32_t)n); }

// A made-up picture like a game texture: soft gradients, a few hard edges, some grain.
std::vector<uint8_t> picture(int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &px[((size_t)y * w + x) * 4];
            const bool brick = ((x / 16 + (y / 8) % 2) % 2) == 0 && x % 16 != 0 && y % 8 != 0;
            const int grain = rnd(13) - 6;
            p[0] = (uint8_t)std::clamp((brick ? 170 : 90) + x * 40 / w + grain, 0, 255);
            p[1] = (uint8_t)std::clamp((brick ? 70 : 85) + y * 30 / h + grain, 0, 255);
            p[2] = (uint8_t)std::clamp((brick ? 50 : 80) + grain, 0, 255);
            p[3] = (uint8_t)(x < w / 2 ? 255 : 128 + y * 127 / h);
        }
    return px;
}

// A bumpy normal map: directions one unit long, packed as 0-255.
std::vector<uint8_t> bumps(int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float nx = 0.4f * std::sin(x * 0.3f), ny = 0.4f * std::cos(y * 0.2f);
            const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
            uint8_t* p = &px[((size_t)y * w + x) * 4];
            p[0] = (uint8_t)std::lround((nx * 0.5f + 0.5f) * 255);
            p[1] = (uint8_t)std::lround((ny * 0.5f + 0.5f) * 255);
            p[2] = (uint8_t)std::lround((nz * 0.5f + 0.5f) * 255);
            p[3] = 255;
        }
    return px;
}

// Unpacks one level the way a graphics card would, block by block.
std::vector<uint8_t> unpack(const Level& l, Format f) {
    std::vector<uint8_t> out((size_t)l.width * l.height * 4, 0);
    const int bw = (l.width + 3) / 4, bh = (l.height + 3) / 4;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* b = l.data + ((size_t)by * bw + bx) * 16;
            uint8_t blk[64] = {};
            if (f == BC7) decodeBC7(b, blk);
            else if (f == ETC2) decodeETC2(b, blk);
            else {
                uint8_t r[16], g[16];
                if (f == BC5) { decodeBC4(b, r); decodeBC4(b + 8, g); }
                else { decodeEAC11(b, r); decodeEAC11(b + 8, g); }
                for (int p = 0; p < 16; ++p) { blk[p * 4] = r[p]; blk[p * 4 + 1] = g[p]; blk[p * 4 + 3] = 255; }
            }
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const int px = bx * 4 + x, py = by * 4 + y;
                    if (px < l.width && py < l.height)
                        for (int c = 0; c < 4; ++c) out[((size_t)py * l.width + px) * 4 + c] = blk[(y * 4 + x) * 4 + c];
                }
        }
    return out;
}

// How close two pictures are (PSNR in dB: 30+ is hard to tell apart, 40+ very close).
double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int channels) {
    double se = 0;
    size_t n = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        for (int c = 0; c < channels; ++c) { const double d = (double)a[i + c] - b[i + c]; se += d * d; ++n; }
    if (se == 0) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / (se / n));
}

std::string db(double v) { return std::to_string((int)std::lround(v)) + " dB"; }
} // namespace

int main() {
    // --- Which pictures can be packed ---
    check(packable(256, 128) && packable(4, 4) && packable(1020, 516), "sides that are a multiple of 4 can be packed");
    check(!packable(255, 256) && !packable(2, 2) && !packable(16384, 16), "odd sizes, tiny and huge pictures are left as they are");
    check(looksLikeNormalMap("textures/Brick_Normal.png") && looksLikeNormalMap("rock_n.jpg") && looksLikeNormalMap("C:\\a\\wall_nrm.png"),
          "normal maps are spotted by their names");
    check(!looksLikeNormalMap("textures/brick.png") && !looksLikeNormalMap("normal-maps/sign.png"),
          "plain pictures aren't (only the file's own name counts)");
    check(!looksLikeNormalMap("signs/no_entry.png") && !looksLikeNormalMap("lantern.png"), "names that merely contain an n aren't either");

    // --- Each format keeps the picture close ---
    const int W = 128, H = 64;
    const std::vector<uint8_t> img = picture(W, H);
    const std::vector<uint8_t> file = pack(img.data(), W, H, false);
    View bc7, etc2;
    check(read(file, BC7, bc7) && read(file, ETC2, etc2), "a colour picture is packed for computers (BC7) and phones (ETC2)");
    check(!has(file, BC5) && !has(file, EACRG11), "and not as a normal map");
    check(bc7.width == W && bc7.height == H && !bc7.normalMap, "it remembers its size");
    check(bc7.levels.size() == 8 && bc7.levels.back().width == 1 && bc7.levels.back().height == 1,
          "every smaller copy is in it, down to 1 x 1");
    check(bc7.levels[0].size == (size_t)W * H, "a squeezed level is 1 byte a pixel (4 times smaller than unpacked)");
    const double qBc7 = psnr(img, unpack(bc7.levels[0], BC7), 4), qEtc = psnr(img, unpack(etc2.levels[0], ETC2), 4);
    check(qBc7 > 38.0, "BC7 keeps it very close (" + db(qBc7) + ")");
    check(qEtc > 35.0, "ETC2 keeps it close (" + db(qEtc) + ")");
    {
        // The smaller copies are averages of the bigger ones.
        std::vector<std::vector<uint8_t>> mips;
        std::vector<int> ws, hs;
        buildMips(img.data(), W, H, mips, ws, hs);
        const int a = img[0] + img[4] + img[W * 4] + img[W * 4 + 4];
        check(mips.size() == 8 && ws[1] == 64 && hs[1] == 32 && mips[1][0] == (a + 2) / 4, "each smaller copy averages 2 x 2 pixels");
        const double qMip = psnr(mips[2], unpack(bc7.levels[2], BC7), 4);
        check(qMip > 34.0, "and is packed just as well (" + db(qMip) + ")");
    }
    {
        // Flat colour and see-through edges come out exact (or nearly).
        uint8_t flat[64], enc[16], dec[64];
        for (int p = 0; p < 16; ++p) { flat[p * 4] = 200; flat[p * 4 + 1] = 30; flat[p * 4 + 2] = 90; flat[p * 4 + 3] = 255; }
        encodeBC7(flat, enc);
        decodeBC7(enc, dec);
        bool close = true;
        for (int i = 0; i < 64; ++i) close = close && std::abs(dec[i] - flat[i]) <= 1;
        check(close, "BC7: a flat colour stays the same colour");
        encodeETC2(flat, enc);
        decodeETC2(enc, dec);
        bool opaque = true;
        for (int p = 0; p < 16; ++p) opaque = opaque && dec[p * 4 + 3] == 255;
        check(opaque, "ETC2: solid stays fully solid");
        uint8_t clear[64];
        for (int p = 0; p < 16; ++p) { clear[p * 4] = clear[p * 4 + 1] = clear[p * 4 + 2] = 255; clear[p * 4 + 3] = (p % 4 < 2) ? 0 : 255; }
        encodeETC2(clear, enc);
        decodeETC2(enc, dec);
        bool edge = true;
        for (int p = 0; p < 16; ++p) edge = edge && std::abs(dec[p * 4 + 3] - clear[p * 4 + 3]) <= 3;
        encodeBC7(clear, enc);
        decodeBC7(enc, dec);
        for (int p = 0; p < 16; ++p) edge = edge && std::abs(dec[p * 4 + 3] - clear[p * 4 + 3]) <= 3;
        check(edge, "a see-through edge stays sharp in both");
    }

    // --- Normal maps ---
    {
        const std::vector<uint8_t> n = bumps(64, 64);
        const std::vector<uint8_t> nf = pack(n.data(), 64, 64, true);
        View bc5, rg11;
        check(read(nf, BC5, bc5) && read(nf, EACRG11, rg11) && bc5.normalMap, "a normal map is packed as BC5 and EAC RG11");
        const double q5 = psnr(n, unpack(bc5.levels[0], BC5), 2), q11 = psnr(n, unpack(rg11.levels[0], EACRG11), 2);
        check(q5 > 40.0 && q11 > 40.0, "both keep the bumps' directions very close (" + db(q5) + ", " + db(q11) + ")");
        // The shader works the third direction out again (Shaders.h): it comes back right.
        const std::vector<uint8_t> back = unpack(bc5.levels[0], BC5);
        double worst = 0;
        for (size_t i = 0; i < n.size(); i += 4) {
            const float x = back[i] / 255.0f * 2 - 1, y = back[i + 1] / 255.0f * 2 - 1;
            const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
            worst = std::max(worst, (double)std::fabs(z - (n[i + 2] / 255.0f * 2 - 1)));
        }
        check(worst < 0.05, "the third direction, worked out again, is right (off by " + std::to_string(worst) + " at most)");
    }

    // --- The file ---
    {
        check(pack(img.data(), 30, 30, false).empty() && pack(nullptr, 64, 64, false).empty(), "a picture that can't be packed makes no file");
        std::vector<uint8_t> broken = file;
        broken[0] = 'X';
        View v;
        check(!read(broken, BC7, v), "a file that isn't a .gbtex is turned down");
        std::vector<uint8_t> cut(file.begin(), file.begin() + (long)file.size() / 2);
        check(!read(cut, ETC2, v), "so is a cut-off one (the game loads the PNG instead)");
        std::vector<uint8_t> empty;
        check(!read(empty, BC7, v), "and an empty one");
        check(glFormat(BC7) == 0x8E8C && glFormat(BC5) == 0x8DBD && glFormat(ETC2) == 0x9278 && glFormat(EACRG11) == 0x9272,
              "each format has OpenGL's number for it");
    }

    // --- Which device gets which ---
    {
        const GpuCaps pc = GpuCaps::from(46, false, "", 16);
        const GpuCaps mac = GpuCaps::from(41, false, "", 0);
        const GpuCaps phone = GpuCaps::from(32, true, "", 8);
        check(pick(false, pc.bc7, pc.bc5, pc.etc2) == BC7 && pick(true, pc.bc7, pc.bc5, pc.etc2) == BC5, "computers: BC7 and BC5");
        check(pick(false, phone.bc7, phone.bc5, phone.etc2) == ETC2 && pick(true, phone.bc7, phone.bc5, phone.etc2) == EACRG11,
              "phones: ETC2 and EAC RG11");
        check(pick(false, mac.bc7, mac.bc5, mac.etc2) == None, "a Mac (OpenGL 4.1) loads the PNG as before");
    }

    // --- Speed ---
    {
        const std::vector<uint8_t> big = picture(512, 512);
        const auto t0 = std::chrono::steady_clock::now();
        const std::vector<uint8_t> f = pack(big.data(), 512, 512, false);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(!f.empty() && secs < 20.0, "a 512 x 512 picture packs in " + std::to_string(secs).substr(0, 4) + "s (done once, ahead of time)");
    }

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
