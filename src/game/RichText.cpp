#include "RichText.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <tuple>

namespace RichText {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// The value of name="..." (or '...') inside a tag; "" if it isn't there.
std::string attr(const std::string& tag, const char* name) {
    const std::string t = lower(tag);
    size_t at = 0;
    const std::string want = name;
    while ((at = t.find(want, at)) != std::string::npos) {
        size_t i = at + want.size();
        const bool start = at == 0 || std::isspace((unsigned char)t[at - 1]);
        while (i < t.size() && std::isspace((unsigned char)t[i])) ++i;
        if (!start || i >= t.size() || t[i] != '=') { at += want.size(); continue; }
        ++i;
        while (i < t.size() && std::isspace((unsigned char)t[i])) ++i;
        if (i >= t.size()) return "";
        const char q = t[i];
        if (q != '"' && q != '\'') return "";
        const size_t end = t.find(q, i + 1);
        if (end == std::string::npos) return "";
        return tag.substr(i + 1, end - i - 1);   // (from the original: keep its case)
    }
    return "";
}

bool parseColor(const std::string& v, glm::vec3& out) {
    std::string s = lower(v);
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
    if (s.size() == 7 && s[0] == '#') {
        char* end = nullptr;
        const unsigned long n = std::strtoul(s.c_str() + 1, &end, 16);
        if (*end) return false;
        out = glm::vec3((n >> 16) & 255, (n >> 8) & 255, n & 255) / 255.0f;
        return true;
    }
    if (s.rfind("rgb(", 0) == 0 && s.back() == ')') {
        float c[3];
        const char* p = s.c_str() + 4;
        for (int i = 0; i < 3; ++i) {
            char* end = nullptr;
            c[i] = std::strtof(p, &end);
            if (end == p) return false;
            p = end;
            if (i < 2) { if (*p != ',') return false; ++p; }
        }
        out = glm::clamp(glm::vec3(c[0], c[1], c[2]) / 255.0f, 0.0f, 1.0f);
        return true;
    }
    return false;
}

// &lt; &gt; &quot; &apos; &amp; -> the character (anything else stays as it is).
size_t entity(const std::string& s, size_t i, std::string& out) {
    static const std::pair<const char*, char> names[] = {{"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&amp;", '&'}};
    for (const auto& [name, ch] : names) {
        const size_t n = std::char_traits<char>::length(name);
        if (s.compare(i, n, name) == 0) { out += ch; return n; }
    }
    out += '&';
    return 1;
}

} // namespace

std::vector<Run> parse(const std::string& s) {
    std::vector<Run> runs;
    std::vector<std::pair<std::string, Style>> stack;   // open tags: name, the style before it
    Style cur;
    std::string text;
    auto flush = [&] {
        if (text.empty()) return;
        if (cur.upper) for (char& c : text) c = (char)std::toupper((unsigned char)c);
        if (!runs.empty() && runs.back().text != "\n" && text != "\n" &&
            std::tie(runs.back().style.bold, runs.back().style.underline, runs.back().style.strike, runs.back().style.hasColor,
                     runs.back().style.size, runs.back().style.transparency) ==
                std::tie(cur.bold, cur.underline, cur.strike, cur.hasColor, cur.size, cur.transparency) &&
            (!cur.hasColor || runs.back().style.color == cur.color))
            runs.back().text += text;
        else
            runs.push_back({text, cur});
        text.clear();
    };
    for (size_t i = 0; i < s.size();) {
        if (s.compare(i, 4, "<!--") == 0) {   // a comment
            const size_t end = s.find("-->", i + 4);
            i = end == std::string::npos ? s.size() : end + 3;
            continue;
        }
        if (s[i] == '&') { i += entity(s, i, text); continue; }
        if (s[i] != '<') { text += s[i++]; continue; }
        const size_t end = s.find('>', i);
        if (end == std::string::npos) { text += s.substr(i); break; }   // (no closing '>': just text)
        std::string tag = s.substr(i + 1, end - i - 1);
        i = end + 1;
        const bool closing = !tag.empty() && tag[0] == '/';
        if (closing) tag.erase(0, 1);
        std::string name;
        for (char c : tag) { if (std::isspace((unsigned char)c) || c == '/') break; name += (char)std::tolower((unsigned char)c); }
        if (name == "br") { flush(); runs.push_back({"\n", cur}); continue; }
        if (closing) {
            // Close the newest open tag with this name (and anything opened inside it).
            for (size_t k = stack.size(); k-- > 0;)
                if (stack[k].first == name) {
                    flush();
                    cur = stack[k].second;
                    stack.resize(k);
                    break;
                }
            continue;
        }
        Style next = cur;
        if (name == "b") next.bold = true;
        else if (name == "i") next.italic = true;
        else if (name == "u") next.underline = true;
        else if (name == "s") next.strike = true;
        else if (name == "uppercase" || name == "uc") next.upper = true;
        else if (name == "font") {
            glm::vec3 c;
            if (parseColor(attr(tag, "color"), c)) { next.hasColor = true; next.color = c; }
            if (const std::string v = attr(tag, "size"); !v.empty()) next.size = std::clamp(std::strtof(v.c_str(), nullptr), 1.0f, 200.0f);
            if (const std::string v = attr(tag, "transparency"); !v.empty())
                next.transparency = std::clamp(std::strtof(v.c_str(), nullptr), 0.0f, 1.0f);
            if (const std::string w = lower(attr(tag, "weight")); w == "bold" || w == "heavy" || w == "extrabold" || w == "semibold")
                next.bold = true;
        } else if (name != "stroke" && name != "smallcaps" && name != "sc" && name != "mark") {
            continue;   // a tag we don't know: left out (and nothing to close)
        }
        flush();
        stack.push_back({name == "uc" ? "uc" : name, cur});
        cur = next;
    }
    flush();
    return runs;
}

std::string strip(const std::string& markup) {
    std::string out;
    for (const Run& r : parse(markup)) out += r.text;
    return out;
}

} // namespace RichText
