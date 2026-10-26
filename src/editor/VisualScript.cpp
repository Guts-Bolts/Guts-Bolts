#include "VisualScript.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <sstream>

using nlohmann::json;

namespace VisualScript {

const std::vector<Block>& blocks() {
    using F = Field;
    static const std::vector<Block> list = {
        // ---- Events: where a chain starts
        {"start", "Events", "When the game starts", "Runs once, as soon as the game starts.", {}, {"next"}, true},
        {"touched", "Events", "When touched", "Runs every time something touches this part (the script's parent).\n\"The part that touched\" and the player (if it was one) can be used below.",
            {}, {"next"}, true},
        {"clicked", "Events", "When clicked", "Runs when a player clicks this part (a ClickDetector is added for you).", {}, {"next"}, true},
        {"prompt", "Events", "When the prompt is used", "Shows \"press E\" near this part; runs when a player uses it.",
            {{F::Text, "action", "Prompt text", "Use", {}}}, {"next"}, true},
        {"joins", "Events", "When a player joins", "Runs for every player who joins (and the ones already there).", {}, {"next"}, true},
        {"every", "Events", "Every few seconds", "Runs again and again, forever.",
            {{F::Number, "seconds", "Seconds", "1", {}}}, {"next"}, true},

        // ---- Actions on objects
        {"color", "Actions", "Set colour", "Changes an object's colour.",
            {{F::Target, "target", "Object", "this", {}}, {F::Color, "color", "Colour", "255, 0, 0", {}}}, {"next"}},
        {"transparency", "Actions", "Set see-through", "0 = solid, 1 = invisible.",
            {{F::Target, "target", "Object", "this", {}}, {F::Number, "value", "Transparency", "0.5", {}}}, {"next"}},
        {"property", "Actions", "Set a property", "Any property, by the name scripts use: Anchored, CanCollide, Material, Name...\nNumbers and true / false work; anything else is text.",
            {{F::Target, "target", "Object", "this", {}}, {F::Text, "property", "Property", "Anchored", {}},
             {F::Text, "value", "Value", "false", {}}}, {"next"}},
        {"move", "Actions", "Move by", "Moves an object this far (x, y, z).",
            {{F::Target, "target", "Object", "this", {}}, {F::Text, "by", "By (x, y, z)", "0, 2, 0", {}}}, {"next"}},
        {"destroy", "Actions", "Destroy", "Removes an object from the game.",
            {{F::Target, "target", "Object", "this", {}}}, {"next"}},
        {"clone", "Actions", "Make a copy", "Puts a copy of an object next to it.",
            {{F::Target, "target", "Object", "this", {}}}, {"next"}},
        {"sound", "Actions", "Play a sound", "Plays a Sound object.",
            {{F::Target, "target", "Sound", "path:Workspace.Sound", {}}}, {"next"}},
        {"print", "Actions", "Print", "Writes a line in the Output. {player} is the player's name.",
            {{F::Text, "text", "Text", "Hello!", {}}}, {"next"}},
        {"wait", "Actions", "Wait", "Waits before the next block.",
            {{F::Number, "seconds", "Seconds", "1", {}}}, {"next"}},

        // ---- Players
        {"makeStat", "Players", "Make a leaderboard stat", "Adds a number to the player's leaderboard (use it in When a player joins).",
            {{F::Text, "stat", "Name", "Coins", {}}, {F::Number, "start", "Starts at", "0", {}}}, {"next"}},
        {"points", "Players", "Give points", "Adds to one of the player's leaderboard stats (take away with a minus number).",
            {{F::Text, "stat", "Stat", "Coins", {}}, {F::Number, "amount", "Amount", "1", {}}}, {"next"}},
        {"damage", "Players", "Hurt the player", "Takes health away from the player's character (100 = all of it).",
            {{F::Number, "amount", "Damage", "10", {}}}, {"next"}},
        {"teleport", "Players", "Teleport the player", "Moves the player's character on top of an object.",
            {{F::Target, "target", "To", "path:Workspace.SpawnLocation", {}}}, {"next"}},

        // ---- Control
        {"ifPlayer", "Control", "If it's a player", "Was it a player (not some other part)?", {}, {"then", "else", "next"}},
        {"ifTag", "Control", "If it has a tag", "Does the object have this tag (CollectionService)?",
            {{F::Target, "target", "Object", "other", {}}, {F::Text, "tag", "Tag", "Lava", {}}}, {"then", "else", "next"}},
        {"ifAttr", "Control", "If an attribute is", "Does the object's attribute have this value?",
            {{F::Target, "target", "Object", "this", {}}, {F::Text, "name", "Attribute", "Open", {}},
             {F::Text, "value", "Value", "true", {}}}, {"then", "else", "next"}},
        {"ifPoints", "Control", "If the player has points", "Does the player have at least this many of a stat?",
            {{F::Text, "stat", "Stat", "Coins", {}}, {F::Number, "amount", "At least", "10", {}}}, {"then", "else", "next"}},
        {"ifChance", "Control", "If lucky", "Goes on \"then\" this often (in percent).",
            {{F::Number, "percent", "Chance %", "50", {}}}, {"then", "else", "next"}},
        {"repeat", "Control", "Repeat", "Does the blocks in \"do\" this many times, then carries on.",
            {{F::Number, "times", "Times", "3", {}}}, {"do", "next"}},
        {"cooldown", "Control", "Only every few seconds", "The blocks after it run at most once in this many seconds\n(so a touch doesn't count 50 times).",
            {{F::Number, "seconds", "Seconds", "1", {}}}, {"next"}},
    };
    return list;
}

const Block* find(const std::string& type) {
    for (const Block& b : blocks()) if (type == b.type) return &b;
    return nullptr;
}

const char* targetLabel(const std::string& v) {
    static const std::pair<const char*, const char*> names[] = {
        {"this", "This part"},
        {"other", "The part that touched"},
        {"character", "The player's character"}};
    for (const auto& n : names) if (v == n.first) return n.second;
    return "An object (path)";
}

namespace {

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if ((unsigned char)c < 32) continue;
        else out += c;
    }
    return out + "\"";
}

bool isNumber(const std::string& s) {
    if (s.empty()) return false;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

std::string number(const std::string& s, const char* def = "0") { return isNumber(s) ? s : def; }

std::vector<float> numbers(const std::string& s, size_t want) {
    std::vector<float> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        out.push_back(isNumber(part) ? std::stof(part) : 0.0f);
    }
    out.resize(want, 0.0f);
    return out;
}

std::string fmt(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

struct Gen {
    std::map<int, const json*> nodes;
    std::map<std::pair<int, std::string>, int> links;
    std::vector<std::string>& problems;
    std::set<std::string> helpers;   // "find", "points"
    std::string top;                 // lines before everything (cooldown flags)
    struct Ctx { bool other = false, player = false; };

    explicit Gen(std::vector<std::string>& p) : problems(p) {}

    std::string field(const json& n, const char* key) {
        const Block* b = find(n.value("type", std::string()));
        if (n.contains("f") && n["f"].is_object() && n["f"].contains(key) && n["f"][key].is_string()) return n["f"][key].get<std::string>();
        if (b) for (const Field& f : b->fields) if (std::string(f.key) == key) return f.def;
        return "";
    }
    std::string title(const json& n) {
        const Block* b = find(n.value("type", std::string()));
        return b ? b->title : n.value("type", std::string("?"));
    }
    std::string target(const json& n, const char* key, const Ctx& c) {
        const std::string v = field(n, key);
        if (v == "this") return "script.Parent";
        if (v == "other") {
            if (!c.other) { problems.push_back(title(n) + ": \"The part that touched\" only works under When touched."); return ""; }
            return "other";
        }
        if (v == "character") {
            if (!c.player) { problems.push_back(title(n) + ": there's no player here (use it under When touched, clicked, prompt or joins)."); return ""; }
            return "(player and player.Character)";
        }
        std::string path = v.rfind("path:", 0) == 0 ? v.substr(5) : v;
        if (path.empty()) { problems.push_back(title(n) + ": pick an object."); return ""; }
        helpers.insert("find");
        return "find(" + quote(path) + ")";
    }
    std::string value(const std::string& s) {
        if (isNumber(s) || s == "true" || s == "false") return s;
        return quote(s);
    }
    bool needPlayer(const json& n, const Ctx& c) {
        if (c.player) return true;
        problems.push_back(title(n) + ": there's no player here (use it under When touched, clicked, prompt or joins).");
        return false;
    }
    // "Hello {player}!" -> "Hello " .. (player and player.Name or "") .. "!"
    std::string text(const std::string& s, const Ctx& c) {
        const std::string tag = "{player}";
        if (!c.player || s.find(tag) == std::string::npos) return quote(s);
        std::string out;
        size_t at = 0, hit;
        while ((hit = s.find(tag, at)) != std::string::npos) {
            if (hit > at) out += (out.empty() ? "" : " .. ") + quote(s.substr(at, hit - at));
            out += std::string(out.empty() ? "" : " .. ") + "(player and player.Name or \"\")";
            at = hit + tag.size();
        }
        if (at < s.size()) out += " .. " + quote(s.substr(at));
        return out;
    }

    void line(std::string& out, int depth, const std::string& s) { out += std::string(depth * 4, ' ') + s + "\n"; }

    // One object to change: `local t = <expr>` and the body when it's there.
    void onTarget(std::string& out, int depth, const std::string& expr, const std::string& body) {
        if (expr.empty()) return;
        line(out, depth, "do");
        line(out, depth + 1, "local t = " + expr);
        line(out, depth + 1, "if t then " + body + " end");
        line(out, depth, "end");
    }

    int next(int id, const char* pin) {
        auto it = links.find({id, pin});
        return it == links.end() ? 0 : it->second;
    }

    void chain(std::string& out, int id, int depth, const Ctx& c, std::set<int> seen) {
        while (id) {
            if (seen.count(id)) { problems.push_back("Some blocks go round in a circle; that part is left out."); return; }
            seen.insert(id);
            auto it = nodes.find(id);
            if (it == nodes.end()) return;
            const json& n = *it->second;
            const std::string type = n.value("type", std::string());
            const Block* b = find(type);
            if (!b || b->event) { if (b) problems.push_back(title(n) + " can only start a chain."); return; }
            if (type == "color") {
                auto v = numbers(field(n, "color"), 3);
                onTarget(out, depth, target(n, "target", c),
                         "t.Color = Color3.fromRGB(" + fmt(v[0]) + ", " + fmt(v[1]) + ", " + fmt(v[2]) + ")");
            } else if (type == "transparency") {
                onTarget(out, depth, target(n, "target", c), "t.Transparency = " + number(field(n, "value")));
            } else if (type == "property") {
                const std::string prop = field(n, "property");
                bool okName = !prop.empty() && std::all_of(prop.begin(), prop.end(), [](char ch) { return std::isalnum((unsigned char)ch) || ch == '_'; });
                if (!okName) problems.push_back("Set a property: \"" + prop + "\" isn't a property name.");
                else onTarget(out, depth, target(n, "target", c), "t." + prop + " = " + value(field(n, "value")));
            } else if (type == "move") {
                auto v = numbers(field(n, "by"), 3);
                onTarget(out, depth, target(n, "target", c),
                         "t.Position = t.Position + Vector3.new(" + fmt(v[0]) + ", " + fmt(v[1]) + ", " + fmt(v[2]) + ")");
            } else if (type == "destroy") {
                onTarget(out, depth, target(n, "target", c), "t:Destroy()");
            } else if (type == "clone") {
                onTarget(out, depth, target(n, "target", c), "local copy = t:Clone() copy.Parent = t.Parent");
            } else if (type == "sound") {
                onTarget(out, depth, target(n, "target", c), "t:Play()");
            } else if (type == "print") {
                line(out, depth, "print(" + text(field(n, "text"), c) + ")");
            } else if (type == "wait") {
                line(out, depth, "task.wait(" + number(field(n, "seconds"), "1") + ")");
            } else if (type == "makeStat") {
                if (needPlayer(n, c)) {
                    helpers.insert("points");
                    line(out, depth, "stat(player, " + quote(field(n, "stat")) + ", " + number(field(n, "start")) + ")");
                }
            } else if (type == "points") {
                if (needPlayer(n, c)) {
                    helpers.insert("points");
                    line(out, depth, "do local s = stat(player, " + quote(field(n, "stat")) + ") if s then s.Value = s.Value + " +
                                         number(field(n, "amount"), "1") + " end end");
                }
            } else if (type == "damage") {
                if (needPlayer(n, c)) {
                    line(out, depth, "do");
                    line(out, depth + 1, "local h = player and player.Character and player.Character:FindFirstChildOfClass(\"Humanoid\")");
                    line(out, depth + 1, "if h then h:TakeDamage(" + number(field(n, "amount"), "10") + ") end");
                    line(out, depth, "end");
                }
            } else if (type == "teleport") {
                if (needPlayer(n, c)) {
                    const std::string to = target(n, "target", c);
                    if (!to.empty()) {
                        line(out, depth, "do");
                        line(out, depth + 1, "local ch, to = player and player.Character, " + to);
                        line(out, depth + 1, "if ch and to then ch:PivotTo(CFrame.new(to.Position + Vector3.new(0, 3, 0))) end");
                        line(out, depth, "end");
                    }
                }
            } else if (type.rfind("if", 0) == 0) {
                std::string cond;
                if (type == "ifPlayer") { if (needPlayer(n, c)) cond = "player ~= nil"; }
                else if (type == "ifTag") {
                    const std::string t = target(n, "target", c);
                    if (!t.empty()) cond = "(function() local t = " + t + " return t ~= nil and game:GetService(\"CollectionService\"):HasTag(t, " +
                                           quote(field(n, "tag")) + ") end)()";
                } else if (type == "ifAttr") {
                    const std::string t = target(n, "target", c);
                    if (!t.empty()) cond = "(function() local t = " + t + " return t ~= nil and t:GetAttribute(" + quote(field(n, "name")) +
                                           ") == " + value(field(n, "value")) + " end)()";
                } else if (type == "ifPoints") {
                    if (needPlayer(n, c)) {
                        helpers.insert("points");
                        cond = "(function() local s = stat(player, " + quote(field(n, "stat")) + ") return s ~= nil and s.Value >= " +
                               number(field(n, "amount"), "0") + " end)()";
                    }
                } else if (type == "ifChance") {
                    cond = "math.random() * 100 < " + number(field(n, "percent"), "50");
                }
                if (!cond.empty()) {
                    line(out, depth, "if " + cond + " then");
                    chain(out, next(id, "then"), depth + 1, c, seen);
                    if (int e = next(id, "else")) {
                        line(out, depth, "else");
                        chain(out, e, depth + 1, c, seen);
                    }
                    line(out, depth, "end");
                }
            } else if (type == "repeat") {
                line(out, depth, "for _ = 1, " + number(field(n, "times"), "1") + " do");
                chain(out, next(id, "do"), depth + 1, c, seen);
                line(out, depth, "end");
            } else if (type == "cooldown") {
                const std::string flag = "ready" + std::to_string(id);
                top += "local " + flag + " = true\n";
                line(out, depth, "if " + flag + " then");
                line(out, depth + 1, flag + " = false");
                line(out, depth + 1, "task.delay(" + number(field(n, "seconds"), "1") + ", function() " + flag + " = true end)");
                chain(out, next(id, "next"), depth + 1, c, seen);
                line(out, depth, "end");
                return;   // (the rest ran inside)
            }
            id = next(id, "next");
        }
    }
};

} // namespace

std::string toLua(const json& graph, std::vector<std::string>& problems) {
    Gen g(problems);
    std::vector<const json*> events;
    if (graph.contains("nodes") && graph["nodes"].is_array())
        for (const auto& n : graph["nodes"]) {
            if (!n.is_object() || !n.contains("id") || !n["id"].is_number_integer()) continue;
            g.nodes[n["id"].get<int>()] = &n;
            const Block* b = find(n.value("type", std::string()));
            if (b && b->event) events.push_back(&n);
        }
    if (graph.contains("links") && graph["links"].is_array())
        for (const auto& l : graph["links"])
            if (l.is_object() && l.contains("from") && l.contains("to") && l["from"].is_number_integer() && l["to"].is_number_integer())
                g.links[{l["from"].get<int>(), l.value("pin", std::string("next"))}] = l["to"].get<int>();
    // Top to bottom, then left to right, like reading.
    std::sort(events.begin(), events.end(), [](const json* a, const json* b) {
        return std::make_pair(a->value("y", 0.0f), a->value("x", 0.0f)) < std::make_pair(b->value("y", 0.0f), b->value("x", 0.0f));
    });

    std::string body;
    for (const json* ev : events) {
        const json& n = *ev;
        const int id = n["id"].get<int>();
        const std::string type = n.value("type", std::string());
        std::string inner;
        Gen::Ctx c;
        int depth = 1;
        std::string head, tail;
        if (type == "start") {
            head = "-- When the game starts\ntask.spawn(function()\n";
            tail = "end)\n";
        } else if (type == "touched") {
            c.other = c.player = true;
            head = "-- When touched\nscript.Parent.Touched:Connect(function(other)\n"
                   "    local player = game.Players:GetPlayerFromCharacter(other.Parent)\n";
            tail = "end)\n";
        } else if (type == "clicked") {
            c.player = true;
            head = "-- When clicked\ndo\n"
                   "    local detector = script.Parent:FindFirstChildOfClass(\"ClickDetector\")\n"
                   "    if not detector then\n        detector = Instance.new(\"ClickDetector\")\n        detector.Parent = script.Parent\n    end\n"
                   "    detector.MouseClick:Connect(function(player)\n";
            tail = "    end)\nend\n";
            depth = 2;
        } else if (type == "prompt") {
            c.player = true;
            head = "-- When the prompt is used\ndo\n"
                   "    local prompt = script.Parent:FindFirstChildOfClass(\"ProximityPrompt\")\n"
                   "    if not prompt then\n        prompt = Instance.new(\"ProximityPrompt\")\n        prompt.Parent = script.Parent\n    end\n"
                   "    prompt.ActionText = " + quote(g.field(n, "action")) + "\n"
                   "    prompt.Triggered:Connect(function(player)\n";
            tail = "    end)\nend\n";
            depth = 2;
        } else if (type == "joins") {
            c.player = true;
            head = "-- When a player joins\ndo\n    local function joined(player)\n";
            tail = "    end\n    game.Players.PlayerAdded:Connect(joined)\n"
                   "    for _, player in ipairs(game.Players:GetPlayers()) do task.spawn(joined, player) end\nend\n";
            depth = 2;
        } else if (type == "every") {
            head = "-- Every " + number(g.field(n, "seconds"), "1") + " seconds\ntask.spawn(function()\n    while true do\n"
                   "        task.wait(" + number(g.field(n, "seconds"), "1") + ")\n";
            tail = "    end\nend)\n";
            depth = 2;
        }
        g.chain(inner, g.next(id, "next"), depth, c, {});
        body += head + inner + tail + "\n";
    }

    std::string out;
    if (g.helpers.count("find"))
        out += "-- Finds an object by its path, like \"Workspace.Door\"\n"
               "local function find(path)\n"
               "    local object = game\n"
               "    for name in string.gmatch(path, \"[^%.]+\") do\n"
               "        if object == game and name == \"Workspace\" then object = workspace\n"
               "        elseif object then object = object:FindFirstChild(name) end\n"
               "    end\n"
               "    return object\n"
               "end\n\n";
    if (g.helpers.count("points"))
        out += "-- A player's leaderboard stat (made the first time, if a start is given)\n"
               "local function stat(player, name, start)\n"
               "    local board = player:FindFirstChild(\"leaderstats\")\n"
               "    if not board and start == nil then return nil end\n"
               "    if not board then\n"
               "        board = Instance.new(\"Folder\")\n"
               "        board.Name = \"leaderstats\"\n"
               "        board.Parent = player\n"
               "    end\n"
               "    local s = board:FindFirstChild(name)\n"
               "    if not s and start ~= nil then\n"
               "        s = Instance.new(\"IntValue\")\n"
               "        s.Name = name\n"
               "        s.Value = start\n"
               "        s.Parent = board\n"
               "    end\n"
               "    return s\n"
               "end\n\n";
    if (!g.top.empty()) out += g.top + "\n";
    out += body;
    if (events.empty()) out += "-- (Add an event block, like \"When touched\", to start.)\n";
    return out;
}

namespace {
std::string hashOf(const std::string& s) {
    uint64_t h = 1469598103934665603ull;   // FNV-1a
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}
constexpr const char* kOpen = "--[==[blocks";
constexpr const char* kClose = "]==]";
} // namespace

std::string scriptSource(const json& graph, std::vector<std::string>& problems) {
    const std::string lua = toLua(graph, problems);
    json keep = graph;
    keep["hash"] = hashOf(lua);
    std::string data = keep.dump();
    // (JSON never has "]==]" outside its strings; inside them, write the "=" another way.)
    for (size_t at; (at = data.find(kClose)) != std::string::npos;) data.replace(at, 4, "]\\u003d=]");
    return std::string(kOpen) + "\n" + data + "\n" + kClose + "\n"
           "-- Made with Blocks (VIEW > Visual Scripting). Change the blocks there:\n"
           "-- changes made to this code by hand are lost when the blocks change.\n\n" + lua;
}

json graphOf(const std::string& source, bool* edited) {
    if (edited) *edited = false;
    if (!isBlocks(source)) return nullptr;
    const size_t start = source.find('\n');
    const size_t end = source.find(kClose);
    if (start == std::string::npos || end == std::string::npos || end < start) return nullptr;
    json g = json::parse(source.substr(start + 1, end - start - 1), nullptr, false);
    if (!g.is_object()) return nullptr;
    if (edited) {
        // The Lua after the comment lines, compared with what the blocks made.
        size_t lua = source.find("\n\n", end);
        std::string code = lua == std::string::npos ? std::string() : source.substr(lua + 2);
        *edited = g.value("hash", std::string()) != hashOf(code);
    }
    g.erase("hash");
    return g;
}

} // namespace VisualScript
