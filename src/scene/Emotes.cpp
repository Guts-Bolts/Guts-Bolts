#include "Emotes.h"
#include "EmotesData.h"   // assets/emotes/emotes.gbemotes, baked in (cmake/Embed.cmake)

#include <cctype>

using json = nlohmann::json;

namespace Emotes {

namespace {
const json& data() {
    static const json j = json::parse(std::string((const char*)kEmotesData, kEmotesDataSize), nullptr, false);
    return j;
}
std::shared_ptr<const Anim::Clip> clipOf(const json& c) {
    return std::make_shared<const Anim::Clip>(Anim::parse(c.dump()));
}
} // namespace

std::string normalize(const std::string& name) {
    std::string out;
    for (unsigned char c : name)
        if (std::isalnum(c)) out += (char)std::tolower(c);
    if (out == "dance1") out = "dance";
    return out;
}

const std::vector<Emote>& builtIn() {
    static const std::vector<Emote> list = [] {
        std::vector<Emote> v;
        const json& d = data();
        if (!d.is_object() || !d.contains("builtin")) return v;
        for (const json& e : d["builtin"]) {
            if (!e.is_object() || !e.contains("clip")) continue;
            v.push_back({e.value("name", std::string()), e.value("title", std::string()), clipOf(e["clip"]),
                         e.value("picture", 0.0f)});
        }
        return v;
    }();
    return list;
}

const Emote* findBuiltIn(const std::string& name) {
    const std::string n = normalize(name);
    for (const Emote& e : builtIn()) if (e.name == n) return &e;
    return nullptr;
}

const std::vector<StarterItem>& starterItems() {
    static const std::vector<StarterItem> list = [] {
        std::vector<StarterItem> v;
        const json& d = data();
        if (!d.is_object() || !d.contains("catalog")) return v;
        for (const json& c : d["catalog"]) {
            if (!c.is_object()) continue;
            StarterItem it;
            it.id = c.value("id", std::string());
            it.kind = c.value("kind", std::string());
            it.name = c.value("name", std::string());
            it.description = c.value("description", std::string());
            it.price = c.value("price", 0LL);
            it.picture = c.value("picture", json());
            if (it.kind == "emote") it.file = {{"format", "gbanim"}, {"clip", c.value("clip", json::object())}};
            else it.file = {{"format", "gbanimpack"}, {"clips", c.value("clips", json::object())}};
            v.push_back(std::move(it));
        }
        return v;
    }();
    return list;
}

bool parseEmoteFile(const std::string& text, Anim::Clip& out) {
    json j = json::parse(text, nullptr, false);
    if (!j.is_object() || j.value("format", std::string()) != "gbanim" || !j.contains("clip") || !j["clip"].is_object())
        return false;
    out = Anim::parse(j["clip"].dump());
    return !out.keys.empty();
}

bool parsePackFile(const std::string& text, Pack& out) {
    out.clear();
    json j = json::parse(text, nullptr, false);
    if (!j.is_object() || j.value("format", std::string()) != "gbanimpack" || !j.contains("clips") || !j["clips"].is_object())
        return false;
    for (const char* state : kPackStates) {
        if (!j["clips"].contains(state) || !j["clips"][state].is_object()) continue;
        auto c = clipOf(j["clips"][state]);
        if (!c->keys.empty()) out[state] = c;
    }
    return !out.empty();
}

Loadout& mine() {
    static Loadout l;
    return l;
}

std::vector<MenuEntry> menu() {
    std::vector<MenuEntry> out;
    const Loadout& l = mine();
    if (l.anyEmote()) {
        for (const Slot& s : l.slots) out.push_back(s.clip ? MenuEntry{normalize(s.name), s.name} : MenuEntry{});
        return out;
    }
    for (const Emote& e : builtIn()) out.push_back({e.name, e.title});
    return out;
}

} // namespace Emotes
