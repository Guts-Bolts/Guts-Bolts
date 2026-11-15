#pragma once
// Pictures packed ahead of time (step 7e of the photoreal upgrade).
//
// A PNG or JPG has to be unpacked by the processor every time a game loads it, then
// sent to the graphics card at full size (4 bytes a pixel), and then the graphics card
// makes its smaller copies (mipmaps). A packed picture (a ".gbtex" file next to the
// picture) is already squeezed into a format the graphics card reads as it is, with
// every smaller copy made ahead of time, so loading is just "copy it over". It also
// takes 4 to 8 times less graphics memory.
//
// Each .gbtex holds the picture in two squeezed formats, one for computers and one for
// phones, and the game picks the one its graphics chip can read:
//   * colour pictures: BC7 (computers) and ETC2 (phones, some browsers);
//   * normal maps (bumps): BC5 (computers) and EAC RG11 (phones). Only the bump's
//     sideways directions are kept; the shader works out the third (Shaders.h).
// A device that can read neither (Macs, Safe mode) loads the PNG as before.
//
// Made by `gbpack` (tools/gbpack) for a games folder, and by the game itself for
// pictures it downloads (the first load packs it in the background; later loads are
// fast). The squeezing is all written here, with no other library.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace TexturePack {

enum Format : uint32_t { None = 0, BC7 = 1, BC5 = 2, ETC2 = 3, EACRG11 = 4 };

// Can a picture this size be packed? (Sides a multiple of 4, from 4 to 8192.)
bool packable(int width, int height);
// Does the file name say it's a normal map ("brick_normal.png", "rock_n.png")?
bool looksLikeNormalMap(const std::string& path);

// The whole .gbtex for a picture (RGBA, rows in the order given). Empty if it can't
// be packed. `normalMap`: keep only the bump's directions (BC5 + EAC RG11).
std::vector<uint8_t> pack(const uint8_t* rgba, int width, int height, bool normalMap);

// One mipmap level of one format, inside a .gbtex.
struct Level {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    int            width = 0, height = 0;
};
struct View {
    int    width = 0, height = 0;
    bool   normalMap = false;
    Format format = None;
    std::vector<Level> levels;   // full size first, down to 1 x 1
};
// Finds `want` in a .gbtex (`file` must outlive the view). False: not a .gbtex, broken,
// or it doesn't have that format.
bool read(const std::vector<uint8_t>& file, Format want, View& out);
// Which formats a .gbtex has (0 = not a .gbtex).
bool has(const std::vector<uint8_t>& file, Format f);

// Which format this graphics chip should use for a picture, given what it can read.
Format pick(bool normalMap, bool bc7, bool bc5, bool etc2);
// OpenGL's number for a format (for glCompressedTexImage2D).
unsigned glFormat(Format f);
// Bytes for one level of a format at that size.
size_t levelSize(Format f, int width, int height);

// The smaller copies: each one half the size (rounded down, at least 1), every pixel
// the average of the 2 x 2 (or fewer, at an odd edge) above it. Full size first.
void buildMips(const uint8_t* rgba, int width, int height, std::vector<std::vector<uint8_t>>& out,
               std::vector<int>& widths, std::vector<int>& heights);

// --- One 4 x 4 block (16 pixels, RGBA, rows top first) at a time (for the tests) ---
void encodeBC7(const uint8_t px[64], uint8_t out[16]);
void decodeBC7(const uint8_t in[16], uint8_t px[64]);            // (mode 6, what encodeBC7 makes)
void encodeBC4(const uint8_t values[16], uint8_t out[8]);        // one channel; BC5 = two of these
void decodeBC4(const uint8_t in[8], uint8_t values[16]);
void encodeETC2(const uint8_t px[64], uint8_t out[16]);          // ETC2 RGBA8 (EAC alpha, then colour)
void decodeETC2(const uint8_t in[16], uint8_t px[64]);           // (the modes encodeETC2 uses)
void encodeEAC11(const uint8_t values[16], uint8_t out[8]);      // one channel; RG11 = two of these
void decodeEAC11(const uint8_t in[8], uint8_t values[16]);       // (rounded back to 0-255)

} // namespace TexturePack
