#pragma once
#include <string>

// Pictures on the graphics card (decals, and previews on the site / in Studio).
//
// An image id is "gb:<asset id>" (a decal uploaded to a Guts&Bolts server,
// once downloaded), a file in the games folder ("images/door.png"), or a full
// path. PNG, JPG, BMP, TGA and GIF (first frame) work.
namespace Textures {

// The GL texture for an image, loading it the first time (0 = not there yet,
// or not a picture). Cheap to call every frame.
unsigned get(const std::string& id);
// The built-in classic smiley face picture (see Player::addFace).
inline constexpr const char* kClassicFace = "builtin:face";
// The blank shirt / pants clothing templates (assets/templates), to try a body on with.
inline constexpr const char* kShirtTemplatePicture = "builtin:shirt-template";
inline constexpr const char* kPantsTemplatePicture = "builtin:pants-template";
bool     size(const std::string& id, int& width, int& height);
// Forget one image (e.g. a file that was just replaced) or all of them.
void     forget(const std::string& id);
// A picture made in the program (rows top first, RGBA): `id` shows it from now on (made
// again over the old one each call). For previews, like the Material Graph's.
unsigned put(const std::string& id, const unsigned char* rgba, int width, int height);
void     clear();   // before the GL context goes away

} // namespace Textures
