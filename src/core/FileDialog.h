#pragma once
#include <string>

// The computer's own "Open file" window (the Browse... buttons). Phones don't
// have one, so there you type a path instead: check available() first.
namespace FileDialog {

bool available();
// The picked file's full path, or "" if cancelled.
std::string openImage(const std::string& title);
std::string openAudio(const std::string& title);
std::string openAny(const std::string& title, const std::string& what, const std::string& patterns);
// The computer's own "Save as" window, starting at `suggested` (a full path). The
// path to write, or "" if cancelled.
std::string saveAny(const std::string& title, const std::string& suggested, const std::string& what, const std::string& patterns);

} // namespace FileDialog
