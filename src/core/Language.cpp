#include "Language.h"
#include "Settings.h"
#include <cctype>
#include <cstdlib>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif
#ifdef __ANDROID__
#include <SDL.h>
#endif

namespace Lang {
namespace {
// The languages chat can be translated between (TRANSLATE_LANGS in worker/server.js).
constexpr std::pair<const char*, const char*> kNames[] = {
    {"en", "English"},   {"es", "Spanish"},    {"fr", "French"},     {"de", "German"},    {"it", "Italian"},
    {"pt", "Portuguese"}, {"nl", "Dutch"},     {"sv", "Swedish"},    {"da", "Danish"},    {"no", "Norwegian"},
    {"fi", "Finnish"},   {"pl", "Polish"},     {"cs", "Czech"},      {"sk", "Slovak"},    {"hu", "Hungarian"},
    {"ro", "Romanian"},  {"bg", "Bulgarian"},  {"el", "Greek"},      {"ru", "Russian"},   {"uk", "Ukrainian"},
    {"tr", "Turkish"},   {"ar", "Arabic"},     {"he", "Hebrew"},     {"fa", "Persian"},   {"hi", "Hindi"},
    {"bn", "Bengali"},   {"id", "Indonesian"}, {"ms", "Malay"},      {"vi", "Vietnamese"}, {"th", "Thai"},
    {"tl", "Filipino"},  {"zh", "Chinese"},    {"ja", "Japanese"},   {"ko", "Korean"},
};

// "ru_RU.UTF-8", "pt-BR", "zh-Hans-CN" -> "ru", "pt", "zh". Old codes some systems still use
// are swapped for today's ("iw" Hebrew, "in" Indonesian, "nb"/"nn" Norwegian, "fil" Filipino).
std::string shorten(std::string s) {
    std::string out;
    for (char c : s) {
        if (!std::isalpha((unsigned char)c)) break;
        out += (char)std::tolower((unsigned char)c);
    }
    if (out == "iw") return "he";
    if (out == "in") return "id";
    if (out == "nb" || out == "nn") return "no";
    if (out == "fil") return "tl";
    if (out == "c" || out == "posix") return "";   // (no language set)
    return out.size() == 2 ? out : "";
}

std::string fromSystem() {
#if defined(__ANDROID__)
    if (SDL_Locale* l = SDL_GetPreferredLocales()) {
        std::string c = l[0].language ? shorten(l[0].language) : "";
        SDL_free(l);
        if (!c.empty()) return c;
    }
#elif defined(_WIN32)
    wchar_t buf[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) > 0) {
        std::string s;
        for (const wchar_t* p = buf; *p && *p < 128; ++p) s += (char)*p;
        if (std::string c = shorten(s); !c.empty()) return c;
    }
#elif defined(__APPLE__)
    if (CFArrayRef langs = CFLocaleCopyPreferredLanguages()) {
        std::string c;
        if (CFArrayGetCount(langs) > 0) {
            char buf[64];
            if (CFStringGetCString((CFStringRef)CFArrayGetValueAtIndex(langs, 0), buf, sizeof(buf), kCFStringEncodingUTF8))
                c = shorten(buf);
        }
        CFRelease(langs);
        if (!c.empty()) return c;
    }
#endif
    // Linux (and anything else): the usual environment variables, most important first.
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"})
        if (const char* v = std::getenv(var))
            if (std::string c = shorten(v); !c.empty()) return c;
    return "";
}
} // namespace

std::string code() {
    // (GB_LANG picks one by hand, for testing.)
    if (const char* v = std::getenv("GB_LANG"); v && *v) return shorten(v);
    // The language picked in Settings (the menus' language) wins over the computer's.
    if (const char* picked = GraphicsSettings::get().language; *picked) return shorten(picked);
    static const std::string system = fromSystem();
    return system;
}

const char* name(const std::string& code) {
    for (const auto& [c, n] : kNames) if (code == c) return n;
    return "";
}

bool translatable(const std::string& code) { return *name(code) != '\0'; }

} // namespace Lang
