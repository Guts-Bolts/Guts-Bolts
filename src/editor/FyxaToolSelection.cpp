#include "FyxaToolSelection.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <unordered_set>
#include <utility>

using nlohmann::json;

namespace FyxaToolSelection {
namespace {

std::string lower(std::string value) {
    for (char& c : value) c = (char)std::tolower((unsigned char)c);
    return value;
}

std::vector<std::string> tokens(const std::string& value) {
    static const std::unordered_set<std::string> stop = {
        "a","an","and","are","as","at","be","by","for","from","how","i","in","is","it","me",
        "my","of","on","or","please","the","this","to","us","we","what","with","you"
    };
    std::vector<std::string> out;
    std::string word;
    auto flush = [&] {
        if (word.size() >= 2 && !stop.count(word)) out.push_back(word);
        word.clear();
    };
    for (unsigned char c : value) {
        if (std::isalnum(c)) word.push_back((char)std::tolower(c));
        else flush();
    }
    flush();
    return out;
}

bool hasAny(const std::string& text, std::initializer_list<const char*> words) {
    for (const char* word : words) if (text.find(word) != std::string::npos) return true;
    return false;
}

std::unordered_set<std::string> domainsFor(const std::string& prompt) {
    const std::string p = lower(prompt);
    std::unordered_set<std::string> domains;
    if (hasAny(p, {"engine","capabilit","documentation","docs","how does","what can"})) domains.insert("DOCS");
    if (hasAny(p, {"scene","game tree","object tree","map","world","insert","spawn","create","make","build","delete","remove","find","where is","what is in"}))
        domains.insert("SCENE");
    if (hasAny(p, {"object","part","ball","color","colour","position","rotate","rotation","scale","move","property","anchor","collid","material","transparen","light","sound"}))
        domains.insert("OBJECTS");
    if (hasAny(p, {"script","lua","code","program","function","module","logic"})) domains.insert("SCRIPTING");
    if (hasAny(p, {"playtest","play test","multiplayer","runtime","run the game","start the game","stop the game","player window"}))
        domains.insert("RUNTIME");
    if (hasAny(p, {"error","bug","debug","diagnos","broken","doesn't work","does not work","why","output","lag","performance","crash"}))
        domains.insert("DEBUG");
    if (hasAny(p, {"undo","redo","checkpoint","restore","history","compare scenes"})) domains.insert("HISTORY");
    if (hasAny(p, {"mesh","accessory","texture","modeling","model editor"})) domains.insert("MODELING");
    if (hasAny(p, {"physics","explosion","force","gravity","collision","velocity","mass","bounce","falling"}))
        domains.insert("PHYSICS");
    if (hasAny(p, {"navmesh","pathfinding","path find","find path","walk character","navigation"}))
        domains.insert("NAVIGATION");
    if (hasAny(p, {"screenshot","render","camera","picture","image of","show me what"})) domains.insert("VIEW");
    if (hasAny(p, {"publish","publishing","release my game"})) { domains.insert("PUBLISH"); domains.insert("DEBUG"); }
    if (hasAny(p, {"game pass","gamepass","pass ","developer product","dev product","price","bolts","sales","stats","how is my game",
                   "how many players","visits","description","max players","genre","update note","tell players","game page","settings"}))
        domains.insert("PLATFORM");   // manage_game (EditorPlatform.cpp)
    if (hasAny(p, {"obby","tycoon","leaderboard","game system","rounds","collectible","shop","terrain","ui","make a game","build a game","create a game"}))
        domains.insert("GAME");
    if (!domains.empty() && (domains.count("SCENE") || domains.count("OBJECTS") || domains.count("GAME") ||
                             domains.count("SCRIPTING") || domains.count("RUNTIME") || domains.count("DEBUG"))) {
        domains.insert("SCENE");
    }
    return domains;
}

int lexicalScore(const std::string& query, const std::string& haystack) {
    const auto wanted = tokens(query);
    const auto available = tokens(haystack);
    int score = 0;
    for (const auto& term : wanted) {
        bool matched = false;
        for (const auto& candidate : available) {
            if (candidate == term) { score += 3; matched = true; break; }
            if (term.size() >= 3 && candidate.size() >= 3 &&
                (candidate.find(term) != std::string::npos || term.find(candidate) != std::string::npos)) {
                score += 1;
                matched = true;
                break;
            }
        }
        (void)matched;
    }
    return score;
}

struct Ranked {
    json value;
    int score = 0;
    size_t order = 0;
};

json bounded(std::vector<Ranked> ranked, size_t limit) {
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        return a.score > b.score;
    });
    json out = json::array();
    for (const auto& item : ranked) {
        if (out.size() >= limit) break;
        out.push_back(item.value);
    }
    return out;
}

std::string nativeName(const json& tool) { return tool.value("name", std::string()); }
std::string externalName(const json& tool) { return tool.value("qualifiedName", tool.value("name", std::string())); }

} // namespace

json selectNative(const json& catalog, const std::string& prompt,
                  const std::vector<std::string>& discovered, size_t limit) {
    json out = json::array();
    if (!catalog.is_array() || limit == 0) return out;
    const auto domains = domainsFor(prompt);
    const std::unordered_set<std::string> explicitNames(discovered.begin(), discovered.end());
    std::vector<Ranked> ranked;
    size_t order = 0;
    for (const auto& tool : catalog) {
        if (!tool.is_object()) continue;
        const std::string name = nativeName(tool);
        if (name.empty()) continue;
        const std::string domain = tool.value("_meta", json::object()).value("gutsbolts/domain", std::string());
        const bool core = name == "get_engine_info";
        const bool selected = domains.count(domain) != 0;
        const bool explicitSelection = explicitNames.count(name) != 0;
        if (!core && !selected && !explicitSelection) { ++order; continue; }
        int score = core ? 1000 : selected ? 100 : 0;
        if (explicitSelection) score += 2000;
        score += lexicalScore(prompt, name + " " + tool.value("title", std::string()) + " " +
                              tool.value("description", std::string()));
        ranked.push_back({{{"name", name},
                           {"description", tool.value("description", std::string())},
                           {"input_schema", tool.value("inputSchema", json::object())},
                           {"domain", domain}}, score, order++});
    }
    return bounded(std::move(ranked), limit);
}

json selectExternal(const json& catalog, const std::string& prompt,
                    const std::vector<std::string>& discovered, size_t limit) {
    json out = json::array();
    if (!catalog.is_array() || limit == 0) return out;
    const std::unordered_set<std::string> explicitNames(discovered.begin(), discovered.end());
    std::vector<Ranked> ranked;
    size_t order = 0;
    for (const auto& tool : catalog) {
        if (!tool.is_object()) continue;
        const std::string name = externalName(tool);
        if (name.empty()) continue;
        const bool explicitSelection = explicitNames.count(name) != 0;
        const int lexical = lexicalScore(prompt, name + " " + tool.value("description", std::string()));
        if (!explicitSelection && lexical == 0) { ++order; continue; }
        ranked.push_back({{{"name", name},
                           {"description", tool.value("description", std::string())},
                           {"input_schema", tool.value("inputSchema", json::object())},
                           {"domain", "EXTERNAL"}},
                          lexical + (explicitSelection ? 2000 : 0), order++});
    }
    return bounded(std::move(ranked), limit);
}

json discover(const json& nativeCatalog, const json& externalCatalog, const std::string& query, size_t limit) {
    json out = json::array();
    if (query.empty() || limit == 0) return out;
    std::vector<Ranked> ranked;
    size_t order = 0;
    if (nativeCatalog.is_array()) for (const auto& tool : nativeCatalog) {
        if (!tool.is_object()) continue;
        const std::string name = nativeName(tool);
        const std::string domain = tool.value("_meta", json::object()).value("gutsbolts/domain", std::string());
        const std::string title = tool.value("title", name);
        const std::string description = tool.value("description", std::string());
        const int score = lexicalScore(query, name + " " + title + " " + domain + " " + description);
        if (score > 0) ranked.push_back({{{"name", name}, {"title", title}, {"description", description}, {"domain", domain}}, score, order});
        ++order;
    }
    if (externalCatalog.is_array()) for (const auto& tool : externalCatalog) {
        if (!tool.is_object()) continue;
        const std::string name = externalName(tool);
        const std::string description = tool.value("description", std::string());
        const int score = lexicalScore(query, name + " " + description);
        if (score > 0) ranked.push_back({{{"name", name}, {"title", name}, {"description", description}, {"domain", "EXTERNAL"}}, score, order});
        ++order;
    }
    return bounded(std::move(ranked), limit);
}

} // namespace FyxaToolSelection
