#pragma once
#include <string>
#include <vector>

// Languages for the Player and Studio. The words come from website/lang/<code>.json
// (the same files the website uses), baked into the program.
//
// You don't have to change UI code to translate it: ImGui is built with a small
// hook (cmake/PatchImGui.cmake) that swaps any text that exactly matches an
// English line in the file for its translation, just before it's measured and
// drawn. So ImGui::Button("Play") shows "Jugar" in Spanish, and IDs stay English.
// Text with no translation just stays English.
namespace I18n {

struct Language {
    std::string code;   // "es"
    std::string name;   // its own name: "Español"
};

// Turns the hook on with the saved language (Settings: language, "" = the computer's).
void init();
// Every language there is, English first.
const std::vector<Language>& languages();
// Pick one ("" = the same as the computer). Saves it too.
void setLanguage(const std::string& code);
// The language that's showing now ("en", "es"...).
const std::string& current();
// The computer's language, as a short code like "en" or "ru" (English if it's
// one we don't have).
std::string systemLanguage();

// The translation of an English line (itself if there isn't one). Only needed
// for text drawn without ImGui's widgets, or to translate on purpose.
const char* tr(const char* english);

// Keep text as it is while this is alive: things people made or typed (game
// GUIs, object names, code) shouldn't change even if they match a UI word.
struct Pause {
    Pause();
    ~Pause();
    Pause(const Pause&) = delete;
    Pause& operator=(const Pause&) = delete;
};

} // namespace I18n
