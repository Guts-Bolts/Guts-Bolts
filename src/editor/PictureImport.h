#pragma once
#include <string>

// Pictures dropped or imported into Studio. PNG, JPG, BMP, TGA and GIF go in as
// they are; other kinds (.webp, .svg, .psd, .hdr, .ppm, ...) are turned into a
// PNG in the games folder's "Imported Pictures" folder first, so every Player
// (phones too) can show them.
namespace PictureImport {

// Any picture Studio can take, including the ones it turns into PNGs.
bool isPicture(const std::string& path);
// "*.png *.jpg ..." for the Open window.
const char* patterns();
// The picture to use: the file itself, or a PNG copy of it. Empty (and err set) if it can't be read.
std::string usable(const std::string& path, std::string& err);
// A picture kind Studio can't read (.heic, .avif, .tiff, ...): what to do instead. Empty for anything else.
std::string cantOpen(const std::string& path);

} // namespace PictureImport
