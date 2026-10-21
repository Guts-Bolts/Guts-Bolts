// In-game chat translation: when someone writes in another language than yours, your app asks
// the Guts&Bolts server for it in yours (chat.translate, worker/server.js), and the chat shows
// the translation with "(translated)" next to it (hover it to see what was really said).
// Everyone's app translates for itself, so a Russian and an English player both read their own language.
#include "NetGame.h"
#include "../core/Language.h"
#include "../core/Settings.h"
#include "../core/TextFilter.h"
#include "../online/OnlineClient.h"

using json = nlohmann::json;

std::string ChatLog::myLanguage() { return cleanLanguage(Lang::code()); }

std::string ChatLog::cleanLanguage(const std::string& lang) { return Lang::translatable(lang) ? lang : std::string(); }

void ChatLog::translateNew() {
    if (!GraphicsSettings::get().translateChat || !Online::online()) return;
    const std::string mine = myLanguage();
    if (mine.empty()) return;
    for (Line& l : lines) {
        if (l.asked) continue;
        l.asked = true;
        if (l.system || l.lang.empty() || l.lang == mine || l.from == Online::playerName()) continue;
        std::weak_ptr<int> alive = m_alive;
        Online::request("chat.translate", {{"text", l.text}, {"from", l.lang}, {"to", mine}},
                        [this, alive, id = l.id](const json& r) {
                            if (alive.expired() || !r.value("ok", false) || !r.contains("text") || !r["text"].is_string()) return;
                            const std::string t = TextFilter::filter(r["text"].get<std::string>());   // (the server filters it too)
                            for (Line& l : lines) {
                                if (l.id != id) continue;
                                if (t.empty() || t == l.text) return;
                                // The speech bubble over their head too, if it's still showing this line.
                                if (!l.whisper)
                                    if (auto b = bubbles.find(l.from); b != bubbles.end() && b->second.first == l.text) b->second.first = t;
                                l.original = l.text;
                                l.text = t;
                                l.translated = true;
                                return;
                            }
                        }, 10);
    }
}
