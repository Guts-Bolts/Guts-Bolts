#include "TextFilter.h"

#include <algorithm>
#include <regex>
#include <vector>

namespace TextFilter {
namespace {

// Slurs, ROT13'd so they don't sit in the code as plain words (the same list as
// worker/textfilter.js). A word only counts on its own, with letters repeated,
// look-alike numbers and symbols, or a space or dot between letters.
const char* const kWords[] = {"avttre", "avttn", "fnaqavttre", "snttbg", "xvxr", "fcvp", "puvax", "jrgonpx", "genaal", "tbbx", "ornare", "enturnq", "gbjryurnq", "cnxv", "cbepuzbaxrl", "wvtnobb", "pbba"};

std::string rot13(std::string w) {
    for (char& c : w) if (c >= 'a' && c <= 'z') c = (char)('a' + (c - 'a' + 13) % 26);
    return w;
}

std::string looks(char c) {
    switch (c) {
        case 'a': return "a4@";  case 'b': return "b8";  case 'e': return "e3";  case 'g': return "g69";
        case 'i': return "i1!|"; case 'o': return "o0";  case 's': return "s5$"; case 't': return "t7";
        default: return std::string(1, c);
    }
}

std::string wordPattern(const std::string& word) {
    std::string p;
    for (size_t i = 0; i < word.size(); ++i) {
        if (i) p += "[ ._*-]?";
        p += "[";
        for (char c : looks(word[i])) { if (c == '\\' || c == ']' || c == '^' || c == '-') p += '\\'; p += c; }
        p += "]+";
    }
    return p;
}

struct Rules {
    std::regex slurs, link, email, phone, allowedLink;
    Rules() {
        std::string alt;
        for (const char* w : kWords) { if (!alt.empty()) alt += "|"; alt += wordPattern(rot13(w)); }
        slurs = std::regex("(^|[^a-z0-9])((?:" + alt + ")(?:e?s)?)(?![a-z])");
        link = std::regex(R"((?:https?://|www\.)[^\s]+|[a-z0-9-]+(?:\.[a-z0-9-]+)*\.(?:com|net|org|gg|io|xyz|ru|tk|ml|ga|cf|gq|link|site|online|top|click|me|co|us|info|biz|shop|app|dev|ly)(?![a-z0-9])(?:/[^\s]*)?)");
        email = std::regex(R"([a-z0-9._%+-]+@[a-z0-9.-]+\.[a-z]{2,})");
        phone = std::regex(R"(\+?\(?[0-9](?:[ .()-]*[0-9]){9,14})");
        allowedLink = std::regex(R"(^(?:https?://)?(?:www\.)?gutsandbolts\.net(?:[/?#].*)?$)");
    }
};
const Rules& rules() { static Rules r; return r; }

// Lua code isn't a link: in workspace.Shop.Door or script.Parent.Top:Destroy() the "shop" and
// "top" carry on into a Name with a capital letter, a :Method or a ( [, which a web address never
// does. (Only for bare names: anything with http://, www. or a /path is still a link.)
// `after` is the original text right after the match. (worker/textfilter.js has the same.)
bool isLuaCode(const std::string& match, const std::string& after) {
    if (match.rfind("http://", 0) == 0 || match.rfind("https://", 0) == 0 || match.rfind("www.", 0) == 0) return false;
    if (match.find('/') != std::string::npos || after.empty()) return false;
    const auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    const auto letter = [&](char c) { return upper(c) || (c >= 'a' && c <= 'z'); };
    if (after[0] == '(' || after[0] == '[') return true;
    return after.size() >= 2 && ((after[0] == '.' && upper(after[1])) || (after[0] == ':' && letter(after[1])));
}

} // namespace

static std::string cover(const std::string& text) {
    if (text.empty()) return text;
    std::string low = text;
    for (char& c : low) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    std::vector<bool> mask(text.size(), false);
    auto cover = [&](size_t from, size_t to) { for (size_t i = from; i < to && i < mask.size(); ++i) mask[i] = true; };
    const Rules& r = rules();
    for (std::sregex_iterator it(low.begin(), low.end(), r.email), end; it != end; ++it)
        cover((size_t)it->position(0), (size_t)(it->position(0) + it->length(0)));
    for (std::sregex_iterator it(low.begin(), low.end(), r.link), end; it != end; ++it)
        if (!std::regex_match(it->str(0), r.allowedLink) &&
            !isLuaCode(it->str(0), text.substr((size_t)(it->position(0) + it->length(0)), 2)))
            cover((size_t)it->position(0), (size_t)(it->position(0) + it->length(0)));
    for (std::sregex_iterator it(low.begin(), low.end(), r.phone), end; it != end; ++it)
        cover((size_t)it->position(0), (size_t)(it->position(0) + it->length(0)));
    for (std::sregex_iterator it(low.begin(), low.end(), r.slurs), end; it != end; ++it)
        cover((size_t)it->position(2), (size_t)(it->position(2) + it->length(2)));
    std::string out = text;
    for (size_t i = 0; i < out.size(); ++i) {
        if (!mask[i] || out[i] == ' ' || out[i] == '\n') continue;
        // A whole letter becomes one # (not one per byte).
        const unsigned char c = (unsigned char)out[i];
        if ((c & 0xC0) == 0x80) { out[i] = 0; continue; }
        out[i] = '#';
    }
    out.erase(std::remove(out.begin(), out.end(), '\0'), out.end());
    return out;
}

std::string filter(const std::string& text) {
    return cover(text) == text ? text : kContentDeleted;
}

bool nameHasHateWord(const std::string& name) {
    static const std::regex inName = [] {
        std::string alt;
        for (const char* w : kWords) {
            if (!alt.empty()) alt += "|";
            for (char c : rot13(w)) { alt += c; alt += '+'; }
        }
        return std::regex(alt);
    }();
    std::string plain;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        switch (c) {
            case '4': case '@': c = 'a'; break;  case '8': c = 'b'; break;  case '3': c = 'e'; break;
            case '6': case '9': c = 'g'; break;  case '1': case '!': case '|': c = 'i'; break;
            case '0': c = 'o'; break;  case '5': case '$': c = 's'; break;  case '7': c = 't'; break;
            default: break;
        }
        if (c >= 'a' && c <= 'z') plain += c;
    }
    return std::regex_search(plain, inName);
}

} // namespace TextFilter
