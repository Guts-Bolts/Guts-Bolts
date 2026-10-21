#include "I18n.h"
#include "Settings.h"
#include "LangData.h"   // website/lang/*.json, baked in by CMakeLists.txt

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif
#ifdef GB_MOBILE
#include <SDL.h>
#endif

// The hooks inside ImGui (cmake/PatchImGui.cmake). Null = leave text alone.
extern bool (*GImGuiTextHook)(const char** text, const char** text_end, bool hide_after_hash);
extern const char* (*GImGuiFormatHook)(const char* fmt);

namespace I18n {

namespace {

std::string g_current = "en";
int g_pause = 0;

// The language that's on: every English line and its translation, kept in two
// big strings so the pointers handed to ImGui stay put.
std::string g_keys, g_values;
std::unordered_map<std::string_view, std::string_view> g_table;
// Lines with spaces around them ("  Play "), made once and kept.
std::unordered_map<std::string, std::string> g_padded;

const GbLangFile* findFile(const std::string& code) {
    for (const GbLangFile* f = kLangFiles; f->code; ++f)
        if (code == f->code) return f;
    return nullptr;
}

bool ours(const char* p) {
    return !g_values.empty() && p >= g_values.data() && p < g_values.data() + g_values.size();
}

std::string_view find(std::string_view key) {
    auto it = g_table.find(key);
    return it == g_table.end() ? std::string_view() : it->second;
}

// The translation of [text, end), or nothing.
const char* lookup(const char* text, const char* end, const char** outEnd) {
    const size_t n = (size_t)(end - text);
    if (n < 2 || n > 2000 || ours(text)) return nullptr;
    std::string_view key(text, n);
    std::string_view v = find(key);
    if (!v.empty()) { *outEnd = v.data() + v.size(); return v.data(); }
    // Spaces around it are just for looks: translate the words in the middle.
    size_t a = 0, b = n;
    while (a < b && (text[a] == ' ' || text[a] == '\n')) ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\n')) --b;
    if (a == 0 && b == n) return nullptr;
    v = find(key.substr(a, b - a));
    if (v.empty()) return nullptr;
    std::string& made = g_padded[std::string(key)];
    if (made.empty()) made = std::string(key.substr(0, a)) + std::string(v) + std::string(key.substr(b));
    *outEnd = made.data() + made.size();
    return made.data();
}

bool textHook(const char** text, const char** textEnd, bool hideAfterHash) {
    if (g_pause > 0 || !*text) return false;
    const char* end = *textEnd ? *textEnd : *text + std::strlen(*text);
    if (hideAfterHash) {   // "Play##btn" shows "Play"
        for (const char* p = *text; p + 1 < end; ++p)
            if (p[0] == '#' && p[1] == '#') { end = p; break; }
    }
    const char* outEnd = nullptr;
    const char* t = lookup(*text, end, &outEnd);
    if (!t) return false;
    *text = t;
    *textEnd = outEnd;
    return true;
}

// Text("%d friends", n): translate "%d friends" before the number goes in.
const char* formatHook(const char* fmt) {
    if (g_pause > 0 || !fmt) return fmt;
    std::string_view v = find(fmt);
    return v.empty() ? fmt : v.data();
}

// "%d friends" -> the kinds of thing that go in: "d". A translation with
// different ones would crash Text(), so it's left out.
std::string formatKinds(std::string_view s) {
    std::string kinds;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') continue;
        if (i + 1 < s.size() && s[i + 1] == '%') { ++i; continue; }
        size_t j = i + 1;
        while (j < s.size() && std::strchr("-+ #0123456789.*hlLzjtq", s[j])) ++j;
        if (j < s.size()) { kinds += std::string(s.substr(i + 1, j - i)); kinds += ','; }
        i = j;
    }
    return kinds;
}

void load(const std::string& code) {
    g_table.clear();
    g_padded.clear();
    g_keys.clear();
    g_values.clear();
    g_current = "en";
    const GbLangFile* f = code == "en" ? nullptr : findFile(code);
    if (!f) {
        GImGuiTextHook = nullptr;
        GImGuiFormatHook = nullptr;
        return;
    }
    nlohmann::json j = nlohmann::json::parse(f->data, f->data + f->size, nullptr, false);
    if (!j.is_object() || !j.contains("strings") || !j["strings"].is_object()) return;
    std::vector<std::pair<std::string, std::string>> pairs;
    size_t keyBytes = 0, valueBytes = 0;
    for (auto& [en, val] : j["strings"].items()) {
        if (!val.is_string()) continue;
        std::string t = val.get<std::string>();
        if (t.empty() || t == en || formatKinds(en) != formatKinds(t)) continue;
        keyBytes += en.size() + 1;
        valueBytes += t.size() + 1;
        pairs.emplace_back(en, std::move(t));
    }
    g_keys.reserve(keyBytes);
    g_values.reserve(valueBytes);
    g_table.reserve(pairs.size());
    for (auto& [en, t] : pairs) {
        const size_t k = g_keys.size(), v = g_values.size();
        g_keys += en; g_keys += '\0';
        g_values += t; g_values += '\0';
        g_table[std::string_view(g_keys.data() + k, en.size())] = std::string_view(g_values.data() + v, t.size());
    }
    g_current = code;
    GImGuiTextHook = textHook;
    GImGuiFormatHook = formatHook;
}

// "pt-BR" / "pt_BR.UTF-8" -> "pt"
std::string shortCode(std::string s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == '-' || s[i] == '_' || s[i] == '.' || s[i] == '@') { s.resize(i); break; }
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

} // namespace

const std::vector<Language>& languages() {
    static std::vector<Language> list = [] {
        std::vector<Language> l{{"en", "English"}};
        for (const GbLangFile* f = kLangFiles; f->code; ++f) {
            if (std::string(f->code) == "en") continue;
            // The name is near the top ("name": "Español"); no need to read the whole file.
            std::string_view head((const char*)f->data, std::min<size_t>(f->size, 400));
            std::string name = f->code;
            size_t at = head.find("\"name\"");
            if (at != std::string_view::npos) {
                size_t q1 = head.find('"', head.find(':', at) + 1);
                size_t q2 = q1 == std::string_view::npos ? q1 : head.find('"', q1 + 1);
                if (q2 != std::string_view::npos) name = std::string(head.substr(q1 + 1, q2 - q1 - 1));
            }
            l.push_back({f->code, name});
        }
        return l;
    }();
    return list;
}

std::string systemLanguage() {
    std::string code;
#if defined(_WIN32)
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0)
        for (wchar_t* p = name; *p && *p < 128; ++p) code += (char)*p;
#elif defined(__APPLE__)
    if (CFArrayRef langs = CFLocaleCopyPreferredLanguages()) {
        if (CFArrayGetCount(langs) > 0) {
            char buf[32] = {};
            if (CFStringGetCString((CFStringRef)CFArrayGetValueAtIndex(langs, 0), buf, sizeof(buf), kCFStringEncodingUTF8))
                code = buf;
        }
        CFRelease(langs);
    }
#endif
#ifdef GB_MOBILE
    if (code.empty())
        if (SDL_Locale* l = SDL_GetPreferredLocales()) {
            if (l[0].language) code = l[0].language;
            SDL_free(l);
        }
#endif
    if (code.empty())
        for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"})
            if (const char* v = std::getenv(var); v && *v && std::strcmp(v, "C") && std::strcmp(v, "POSIX")) { code = v; break; }
    code = shortCode(code);
    return findFile(code) ? code : "en";
}

void init() {
    const std::string want = GraphicsSettings::get().language;
    load(want.empty() ? systemLanguage() : shortCode(want));
}

void setLanguage(const std::string& code) {
    GraphicsSettings& s = GraphicsSettings::get();
    std::snprintf(s.language, sizeof s.language, "%s", code.c_str());
    s.save();
    init();
}

const std::string& current() { return g_current; }

const char* tr(const char* english) {
    if (!english || g_table.empty()) return english;
    std::string_view v = find(english);
    return v.empty() ? english : v.data();
}

Pause::Pause() { ++g_pause; }
Pause::~Pause() { --g_pause; }

} // namespace I18n
