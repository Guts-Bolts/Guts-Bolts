#pragma once
// EditableImages: pictures a game draws on while it runs (AssetService:CreateEditableImage).
// Each has pixels on this computer (RGBA, rows top first) and a content id, "editable://<n>",
// that ImageLabels, ImageButtons and Decals show like any picture (Textures::get asks here).
// The picture on the graphics card is made again only after something was drawn.
// Like Roblox's, they stay on the computer that made them (they're not sent to other players).
#include <cstdint>
#include <string>
#include <vector>

namespace EditableImages {

constexpr int kMostSide = 1024;                     // biggest width or height
constexpr long long kMostPixels = 4096LL * 4096LL;  // all of one game's images together

// A new see-through picture. 0 if the size is wrong or the game has too many pixels already.
int  create(int width, int height);
bool exists(int id);
void destroy(int id);
// The pixels (RGBA, rows top first), or nullptr. Call touch() after changing them.
std::vector<uint8_t>* pixels(int id, int* width = nullptr, int* height = nullptr);
void touch(int id);
std::string contentId(int id);                       // "editable://<n>"
int  idOf(const std::string& contentId);             // 0 if it isn't one of ours
// The GL texture for "editable://<n>" (0 when there's no such picture, or no graphics card).
unsigned texture(const std::string& contentId);
// Every picture goes (a game run ends, or a new one starts).
void clearAll();

// Drawing, in pixels (0,0 = top left). colour 0..1, alpha 0..1 (1 = solid).
// blend: true mixes over what's there (BlendSourceOver), false replaces it (Overwrite).
struct Colour { float r = 0, g = 0, b = 0, a = 1; };
void fillRect(int id, int x, int y, int w, int h, Colour c, bool blend);
void fillCircle(int id, float cx, float cy, float radius, Colour c, bool blend);
void line(int id, float x0, float y0, float x1, float y1, float thickness, Colour c, bool blend);
void setPixel(int id, int x, int y, Colour c, bool blend);
bool getPixel(int id, int x, int y, Colour& out);

// The picture as a .png file (for publishing). Empty if there's no such picture.
std::string png(int id);

} // namespace EditableImages
