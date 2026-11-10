#include "ScriptCheck.h"
#include "LuaApi.h"
#include "Luau.h"
#include "ScriptEngine.h"
#include "ScriptHelp.h"
#include "../scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <unordered_map>

namespace ScriptCheck {
namespace {

const std::set<std::string>& keywords() {
    static const std::set<std::string> k = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function",
        "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while", "continue"};
    return k;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// ---------------------------------------------------------------------------------------
// What a game script starts with, read from a real (private, empty) game's Lua.
// ---------------------------------------------------------------------------------------

// Built-in tables whose parts never change (math.floor, Vector3.new...): a name not in
// them is a mistake. (Not services or game: their parts are the game's objects.)
const char* const kLibraries[] = {"math", "string", "table", "task", "os", "coroutine", "utf8", "debug", "bit32",
    "Vector3", "Vector2", "CFrame", "Color3", "UDim2", "UDim", "Instance", "TweenInfo", "Ray", "BrickColor",
    "NumberRange", "NumberSequence", "ColorSequence", "Random", "Region3", "Rect", "buffer"};

// Roblox names worth suggesting when one is misspelt (only those this engine really has are kept).
const char* const kServiceNames[] = {"Players", "Workspace", "Lighting", "ReplicatedStorage", "ServerStorage",
    "ServerScriptService", "StarterGui", "StarterPack", "StarterPlayer", "Teams", "SoundService", "Chat",
    "TweenService", "RunService", "UserInputService", "ContextActionService", "Debris", "HttpService",
    "DataStoreService", "MemoryStoreService", "MarketplaceService", "BadgeService", "CollectionService",
    "PathfindingService", "PhysicsService", "TeleportService", "TextChatService", "TextService", "GuiService",
    "ProximityPromptService", "HapticService", "VRService", "InsertService", "MessagingService", "PolicyService",
    "SocialService", "LocalizationService", "AvatarEditorService", "GroupService", "AnalyticsService", "ContentProvider"};
const char* const kClassNames[] = {"Part", "WedgePart", "CornerWedgePart", "TrussPart", "SpawnLocation", "Seat",
    "VehicleSeat", "MeshPart", "UnionOperation", "Model", "Folder", "Script", "LocalScript", "ModuleScript",
    "Tool", "Humanoid", "Sound", "SoundGroup", "PointLight", "SpotLight", "SurfaceLight", "Fire", "Smoke",
    "Sparkles", "ParticleEmitter", "Beam", "Trail", "Attachment", "Weld", "WeldConstraint", "Motor6D",
    "HingeConstraint", "RopeConstraint", "SpringConstraint", "RodConstraint", "BallSocketConstraint",
    "PrismaticConstraint", "AlignPosition", "AlignOrientation", "LinearVelocity", "AngularVelocity",
    "VectorForce", "BodyVelocity", "BodyPosition", "BodyGyro", "BodyForce", "ClickDetector", "DragDetector",
    "ProximityPrompt", "Highlight", "Decal", "Texture", "SurfaceGui", "BillboardGui", "ScreenGui", "Frame",
    "TextLabel", "TextButton", "TextBox", "ImageLabel", "ImageButton", "ScrollingFrame", "ViewportFrame", "VideoFrame",
    "UICorner", "UIStroke", "UIGradient", "UIListLayout", "UIGridLayout", "UIPadding", "UIScale",
    "UIAspectRatioConstraint", "RemoteEvent", "RemoteFunction", "BindableEvent", "BindableFunction",
    "IntValue", "NumberValue", "StringValue", "BoolValue", "ObjectValue", "Vector3Value", "CFrameValue",
    "Color3Value", "Animation", "Animator", "Explosion", "Camera", "Sky", "Atmosphere", "BloomEffect",
    "BlurEffect", "ColorCorrectionEffect", "SunRaysEffect", "DepthOfFieldEffect", "Team", "IKControl",
    "HumanoidDescription", "SpecialMesh", "BlockMesh", "Accessory", "Shirt", "Pants", "ShirtGraphic",
    "Configuration", "SelectionBox", "BoxHandleAdornment"};

// Things to call on an object with a colon (part:Destroy()); with a dot it breaks.
const std::set<std::string>& colonMethods() {
    static const std::set<std::string> m = {"FindFirstChild", "WaitForChild", "GetChildren", "GetDescendants",
        "Destroy", "Clone", "IsA", "GetService", "FindFirstChildOfClass", "FindFirstChildWhichIsA",
        "FindFirstAncestor", "FindFirstAncestorOfClass", "FindFirstAncestorWhichIsA", "IsDescendantOf",
        "IsAncestorOf", "SetAttribute", "GetAttribute", "GetAttributes", "GetPropertyChangedSignal",
        "ClearAllChildren", "TakeDamage", "LoadAnimation", "GetPlayers", "GetPlayerFromCharacter",
        "FireServer", "FireClient", "FireAllClients", "InvokeServer", "InvokeClient", "SetPrimaryPartCFrame",
        "GetPivot", "PivotTo", "GetFullName", "ApplyImpulse", "ApplyAngularImpulse", "GetMass", "FireBullet", "GetInjury"};
    return m;
}

struct Known {
    ScriptEngine* engine = nullptr;
    std::set<std::string> globals;
    std::map<std::string, std::set<std::string>> tables;   // library -> its parts
    std::vector<std::string> services, classes;            // real ones, for suggestions
    std::unordered_map<std::string, bool> serviceOk, classOk;
    std::map<std::string, std::vector<std::string>> enumItems;   // category -> items (empty: anything goes)
    bool ready = false;
};

// Runs `fn(name)` (a Lua function kept as a global in the private game) and says if it gave true.
bool askLua(Known& k, const char* fn, const std::string& a, const std::string& b = "") {
    lua_State* L = k.engine ? k.engine->lua() : nullptr;
    if (!L) return true;   // (can't tell: never complain)
    const int top = lua_gettop(L);
    lua_getglobal(L, fn);
    lua_pushstring(L, a.c_str());
    lua_pushstring(L, b.c_str());
    const bool ok = lua_pcall(L, 2, 1, 0) == LUA_OK && lua_toboolean(L, -1);
    lua_settop(L, top);
    return ok;
}

bool isService(Known& k, const std::string& name) {
    auto it = k.serviceOk.find(name);
    if (it != k.serviceOk.end()) return it->second;
    return k.serviceOk[name] = askLua(k, "__gb_check_service", name);
}
bool isClass(Known& k, const std::string& name) {
    auto it = k.classOk.find(name);
    if (it != k.classOk.end()) return it->second;
    return k.classOk[name] = askLua(k, "__gb_check_class", name);
}

Known& known() {
    static Known* k = new Known;   // (kept for the whole run: building it means starting a game's Lua)
    if (k->ready) return *k;
    k->ready = true;
    static Scene* scene = new Scene;
    k->engine = new ScriptEngine(scene);
    k->engine->start(false);
    lua_State* L = k->engine->lua();
    if (!L) return *k;
    lua_pushglobaltable(L);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (lua_type(L, -2) == LUA_TSTRING) k->globals.insert(lua_tostring(L, -2));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    for (const char* n : {"script", "self", "_G", "_ENV", "game", "workspace", "Workspace", "plugin", "shared"}) k->globals.insert(n);
    for (const char* lib : kLibraries) {
        lua_getglobal(L, lib);
        if (lua_type(L, -1) == LUA_TTABLE && !lua_getmetatable(L, -1)) {
            std::set<std::string>& parts = k->tables[lib];
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_type(L, -2) == LUA_TSTRING) parts.insert(lua_tostring(L, -2));
                lua_pop(L, 1);
            }
            if (parts.empty()) k->tables.erase(lib);
        } else if (lua_type(L, -1) == LUA_TTABLE) {
            lua_pop(L, 1);   // (the metatable lua_getmetatable pushed)
        }
        lua_pop(L, 1);
    }
    const char* helpers = R"LUA(
        function __gb_check_service(name) return game:FindService(name) ~= nil end
        function __gb_check_class(name)
            local ok, o = pcall(Instance.new, name)
            if ok and o ~= nil then pcall(function() o:Destroy() end) end
            return ok and o ~= nil
        end
        function __gb_check_game(name)
            local ok, v = pcall(function() return game[name] end)
            return ok and v ~= nil
        end
        function __gb_check_enum(category)
            local ok, items = pcall(function() return Enum[category]:GetEnumItems() end)
            if not ok or type(items) ~= "table" then return "" end
            return table.concat(items, " ")
        end
    )LUA";
    if (luaL_dostring(L, helpers) != LUA_OK) lua_pop(L, 1);
    for (const char* s : kServiceNames) if (isService(*k, s)) k->services.push_back(s);
    for (const char* c : kClassNames) if (isClass(*k, c)) k->classes.push_back(c);
    return *k;
}

const std::vector<std::string>& enumItems(Known& k, const std::string& category) {
    auto it = k.enumItems.find(category);
    if (it != k.enumItems.end()) return it->second;
    std::vector<std::string>& items = k.enumItems[category];
    lua_State* L = k.engine ? k.engine->lua() : nullptr;
    if (!L) return items;
    const int top = lua_gettop(L);
    lua_getglobal(L, "__gb_check_enum");
    lua_pushstring(L, category.c_str());
    if (lua_pcall(L, 1, 1, 0) == LUA_OK && lua_isstring(L, -1)) {
        std::string all = lua_tostring(L, -1), word;
        for (char c : all + " ") {
            if (c == ' ') { if (!word.empty()) items.push_back(word); word.clear(); }
            else word += c;
        }
    }
    lua_settop(L, top);
    return items;
}

// ---------------------------------------------------------------------------------------
// Reading the script
// ---------------------------------------------------------------------------------------

enum class T { Name, Str, Num, Sym };
struct Tok { std::string text; int line; int col; T kind; };

// Lua source -> its pieces (comments left out; strings keep what's inside them).
std::vector<Tok> tokens(const std::string& s) {
    std::vector<Tok> out;
    int line = 1;
    size_t lineStart = 0, i = 0, n = s.size();
    auto newline = [&](size_t at) { ++line; lineStart = at + 1; };
    auto longBracket = [&](size_t at) -> int {   // "[[", "[=[" ... -> its level, or -1
        if (at >= n || s[at] != '[') return -1;
        size_t j = at + 1;
        while (j < n && s[j] == '=') ++j;
        return j < n && s[j] == '[' ? (int)(j - at - 1) : -1;
    };
    auto skipLong = [&](int level) -> std::string {   // i is at the opening bracket
        const size_t open = i + (size_t)level + 2;
        const std::string close = "]" + std::string((size_t)level, '=') + "]";
        const size_t end = s.find(close, i);
        const size_t stop = end == std::string::npos ? n : end + close.size();
        std::string inside = s.substr(std::min(open, n), (end == std::string::npos ? n : end) - std::min(open, n));
        for (; i < stop; ++i) if (s[i] == '\n') newline(i);
        return inside;
    };
    while (i < n) {
        const char c = s[i];
        const int col = (int)(i - lineStart);
        if (c == '\n') { newline(i); ++i; continue; }
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (c == '-' && i + 1 < n && s[i + 1] == '-') {   // comment
            i += 2;
            if (int lv = longBracket(i); lv >= 0) skipLong(lv);
            else while (i < n && s[i] != '\n') ++i;
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            std::string inside;
            for (++i; i < n && s[i] != c && s[i] != '\n'; ++i) {
                if (s[i] == '\\' && i + 1 < n) { inside += s[++i]; continue; }
                inside += s[i];
            }
            ++i;
            out.push_back({inside, line, col, T::Str});
            continue;
        }
        if (int lv = longBracket(i); lv >= 0) {
            const int startLine = line;
            std::string inside = skipLong(lv);
            out.push_back({inside, startLine, col, T::Str});
            continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1]))) {
            while (i < n && (std::isalnum((unsigned char)s[i]) || s[i] == '.' || s[i] == '_')) ++i;
            out.push_back({"0", line, col, T::Num});
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) ++j;
            out.push_back({s.substr(i, j - i), line, col, T::Name});
            i = j;
            continue;
        }
        if (i + 2 < n && s.compare(i, 3, "...") == 0) { out.push_back({"...", line, col, T::Sym}); i += 3; continue; }
        if (i + 1 < n && ((c == '=' && s[i + 1] == '=') || (c == '~' && s[i + 1] == '=') || (c == '<' && s[i + 1] == '=') ||
                          (c == '>' && s[i + 1] == '=') || (c == '.' && s[i + 1] == '.') || (c == ':' && s[i + 1] == ':'))) {
            out.push_back({s.substr(i, 2), line, col, T::Sym});
            i += 2;
            continue;
        }
        out.push_back({std::string(1, c), line, col, T::Sym});
        ++i;
    }
    return out;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines(1);
    for (char c : s) {
        if (c == '\n') lines.emplace_back();
        else lines.back() += c;
    }
    return lines;
}

bool wordChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// Where `text` is on line `line` of the script as typed (the Luau rewrite we read can shift
// things a little along a line): the place nearest `col`, or `col` if it's not there.
int placeOn(const std::vector<std::string>& lines, int line, const std::string& text, int col) {
    if (line < 1 || line > (int)lines.size() || text.empty()) return col;
    const std::string& l = lines[(size_t)line - 1];
    const bool word = wordChar(text.front()) && wordChar(text.back());
    int best = -1;
    for (size_t at = l.find(text); at != std::string::npos; at = l.find(text, at + 1)) {
        if (word && ((at > 0 && wordChar(l[at - 1])) || (at + text.size() < l.size() && wordChar(l[at + text.size()])))) continue;
        if (best < 0 || std::abs((int)at - col) < std::abs(best - col)) best = (int)at;
    }
    return best >= 0 ? best : std::clamp(col, 0, (int)l.size());
}

int distance(const std::string& a, const std::string& b) {
    std::vector<int> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = (int)j;
    for (size_t i = 1; i <= a.size(); ++i) {
        int diag = row[0];
        row[0] = (int)i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const int up = row[j];
            const int cost = std::tolower((unsigned char)a[i - 1]) == std::tolower((unsigned char)b[j - 1]) ? 0 : 1;
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + cost});
            // (two letters swapped round, "pirnt", counts as one slip)
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) row[j] = std::min(row[j], diag);
            diag = up;
        }
    }
    return row[b.size()];
}

// Friendlier words for the mistakes Lua finds most.
std::string explain(const std::string& msg) {
    auto has = [&](const char* s) { return msg.find(s) != std::string::npos; };
    if (has("'then' expected near '='")) return "To check if two things are the same, use == (two equals signs).";
    if (has("'do' expected near '='")) return "To check if two things are the same, use == (two equals signs).";
    if (has("'end' expected")) return "Something here is missing its \"end\". Every function, if, for and while needs one.";
    if (has("unfinished string")) return "This text is missing its closing quote mark.";
    if (has("unfinished long string") || has("unfinished long comment")) return "This is missing its closing ]].";
    if (has("')' expected")) return "A bracket ( is missing its ).";
    if (has("'}' expected")) return "A { is missing its }.";
    if (has("'=' expected")) return "Lua expected an = here. Is a word misspelt, or a bracket missing?";
    if (has("unexpected symbol")) return "Lua didn't expect this here.";
    return "";
}

} // namespace

std::string closest(const std::string& word, const std::vector<std::string>& names) {
    if (word.size() < 3) return "";
    const int allowed = word.size() <= 4 ? 1 : 2;
    std::string best;
    int bestD = allowed + 1;
    for (const std::string& n : names) {
        if (n == word || n.rfind("__", 0) == 0) continue;
        if (std::abs((int)n.size() - (int)word.size()) > allowed) continue;
        int d = lower(n) == lower(word) ? 0 : distance(word, n);
        if (d < bestD) { bestD = d; best = n; }
    }
    return best;
}

std::vector<Mark> check(const std::string& source) {
    std::vector<Mark> marks;
    const std::vector<std::string> lines = splitLines(source);
    auto lineEnd = [&](int line) { return line >= 1 && line <= (int)lines.size() ? (int)lines[(size_t)line - 1].size() : 0; };
    auto wholeLine = [&](int line, Mark m) {
        const std::string l = line >= 1 && line <= (int)lines.size() ? lines[(size_t)line - 1] : "";
        const size_t first = l.find_first_not_of(" \t");
        m.line = line;
        m.col = first == std::string::npos ? 0 : (int)first;
        m.len = std::max(1, lineEnd(line) - m.col);
        return m;
    };

    // --- Can Lua read it at all? ---
    std::string err;
    int errLine = 0;
    if (!ScriptEngine::checkSyntax(source, err, errLine)) {
        Mark m;
        m.error = true;
        const std::string friendly = explain(err);
        m.text = friendly.empty() ? err : friendly + " (" + err + ")";
        int line = std::clamp(errLine, 1, (int)lines.size());
        // "'end' expected (to close 'function' at line 3)": point at that function.
        const size_t close = err.find("to close '");
        if (close != std::string::npos) {
            const size_t q = err.find('\'', close + 10), at = err.find(" at line ", close);
            if (q != std::string::npos && at != std::string::npos) {
                const std::string what = err.substr(close + 10, q - close - 10);
                const int opener = std::atoi(err.c_str() + at + 9);
                if (opener >= 1 && opener <= (int)lines.size()) {
                    line = opener;
                    m.line = line;
                    m.col = placeOn(lines, line, what, 0);
                    m.len = (int)what.size();
                    marks.push_back(m);
                    return marks;
                }
            }
        }
        // "... near 'x'": underline the x.
        const size_t near = err.rfind("near '");
        if (near != std::string::npos) {
            const size_t q = err.find('\'', near + 6);
            const std::string what = q == std::string::npos ? "" : err.substr(near + 6, q - near - 6);
            const std::string& l = line <= (int)lines.size() ? lines[(size_t)line - 1] : "";
            if (!what.empty() && l.find(what) != std::string::npos) {
                m.line = line;
                m.col = placeOn(lines, line, what, (int)l.size());
                m.len = (int)what.size();
                marks.push_back(m);
                return marks;
            }
        }
        marks.push_back(wholeLine(line, m));
        return marks;
    }

    Known& k = known();
    // (Luau extras read as the Lua they run as: same lines, type names blanked out)
    const std::vector<Tok> t = tokens(Luau::toLua(source));
    auto is = [&](size_t i, const char* s) { return i < t.size() && t[i].kind != T::Str && t[i].text == s; };
    auto name = [&](size_t i) { return i < t.size() && t[i].kind == T::Name && !keywords().count(t[i].text); };
    auto add = [&](const Tok& at, const std::string& text, bool error, const std::string& what, const std::string& fix) {
        Mark m;
        m.line = at.line;
        m.col = placeOn(lines, at.line, what, at.col);
        m.len = std::max(1, (int)what.size());
        m.error = error;
        m.text = text;
        m.fix = fix;
        for (const Mark& o : marks) if (o.line == m.line && o.col == m.col) return;   // (one mark per place)
        marks.push_back(m);
    };
    // Is token i the first name of a chain (a.b.c), not a part of one?
    auto chainStart = [&](size_t i) { return !(i > 0 && (is(i - 1, ".") || is(i - 1, ":"))); };

    // Pass 1: every name the script defines anywhere (locals, functions, parameters, loop
    // variables, globals it sets), and parts it adds to libraries (function string.trim()).
    // No scopes on purpose: a name defined anywhere is never underlined.
    std::set<std::string> defined, addedParts;
    for (size_t i = 0; i < t.size(); ++i) {
        if (is(i, "local") || is(i, "for")) {
            size_t j = i + 1;
            if (is(j, "function")) ++j;
            while (name(j)) {
                defined.insert(t[j].text);
                if (is(j + 1, ",")) j += 2; else break;
            }
        }
        if (is(i, "function")) {
            size_t j = i + 1;
            if (name(j)) {
                defined.insert(t[j].text);
                if ((is(j + 1, ".") || is(j + 1, ":")) && name(j + 2)) addedParts.insert(t[j].text + "." + t[j + 2].text);
            }
            while (j < t.size() && !is(j, "(")) ++j;
            for (++j; j < t.size() && !is(j, ")"); ++j) if (name(j)) defined.insert(t[j].text);
        }
        if (t[i].kind == T::Name && is(i + 1, "=")) {
            if (i >= 2 && is(i - 1, ".") && t[i - 2].kind == T::Name) addedParts.insert(t[i - 2].text + "." + t[i].text);
            else if (!(i > 0 && (is(i - 1, ".") || is(i - 1, ":") || is(i - 1, "{") || is(i - 1, ",")))) defined.insert(t[i].text);
        }
        // a, b = ...  (several globals at once)
        if (t[i].kind == T::Name && is(i + 1, ",") && chainStart(i) && !(i > 0 && (is(i - 1, "{") || is(i - 1, ",") || is(i - 1, "(")))) {
            size_t j = i;
            while (name(j) && is(j + 1, ",")) j += 2;
            if (name(j) && is(j + 1, "=")) for (size_t q = i; q <= j; q += 2) defined.insert(t[q].text);
        }
    }

    std::vector<std::string> everyName(k.globals.begin(), k.globals.end());
    everyName.insert(everyName.end(), defined.begin(), defined.end());
    std::set<std::string> told;
    for (size_t i = 0; i < t.size(); ++i) {
        const Tok& tok = t[i];
        if (tok.kind != T::Name || keywords().count(tok.text)) continue;
        const bool start = chainStart(i);

        // --- A name nothing defines ---
        if (start && !defined.count(tok.text) && !k.globals.count(tok.text) && !is(i - 1, "goto") && !is(i - 1, "::") &&
            !(is(i + 1, "=") && i > 0 && (is(i - 1, "{") || is(i - 1, ","))) && told.insert(tok.text).second) {
            std::string fix = ScriptHelp::rightCase(tok.text);
            if (fix.empty()) fix = closest(tok.text, everyName);
            add(tok, fix.empty() ? "'" + tok.text + "' isn't defined anywhere. Is it misspelt?"
                                 : "'" + tok.text + "' isn't defined anywhere. Did you mean " + fix + "?",
                false, tok.text, fix);
            continue;
        }
        if (!start || defined.count(tok.text)) continue;   // (a script's own "math" is its own business)

        // --- math.flor, task.wiat ---
        auto lib = k.tables.find(tok.text);
        if (lib != k.tables.end() && (is(i + 1, ".") || is(i + 1, ":")) && name(i + 2) && !is(i + 3, "=")) {
            const std::string& part = t[i + 2].text;
            if (!lib->second.count(part) && !addedParts.count(tok.text + "." + part) && !(i > 0 && is(i - 1, "function"))) {
                std::vector<std::string> parts(lib->second.begin(), lib->second.end());
                const std::string fix = closest(part, parts);
                add(t[i + 2], tok.text + " has no '" + part + "'." + (fix.empty() ? "" : " Did you mean " + tok.text + "." + fix + "?"),
                    false, part, fix);
            }
        }

        // --- game:GetService("Plyers") and game.Plyers ---
        if ((tok.text == "game" || tok.text == "Game") && k.globals.count("game")) {
            if (is(i + 1, ":") && i + 4 < t.size() && (t[i + 2].text == "GetService" || t[i + 2].text == "service") && is(i + 3, "(") &&
                t[i + 4].kind == T::Str && !isService(k, t[i + 4].text)) {
                const std::string fix = closest(t[i + 4].text, k.services);
                add(t[i + 4], "There's no service called \"" + t[i + 4].text + "\"." + (fix.empty() ? "" : " Did you mean \"" + fix + "\"?"),
                    false, t[i + 4].text, fix);
            }
            if (is(i + 1, ".") && name(i + 2) && !is(i + 3, "=") && !is(i + 3, "(") &&
                !askLua(k, "__gb_check_game", t[i + 2].text)) {
                const std::string fix = closest(t[i + 2].text, k.services);
                add(t[i + 2], "game has no '" + t[i + 2].text + "'." + (fix.empty() ? "" : " Did you mean game." + fix + "?"),
                    false, t[i + 2].text, fix);
            }
        }

        // --- Instance.new("Prat") ---
        if (tok.text == "Instance" && is(i + 1, ".") && i + 4 < t.size() && t[i + 2].text == "new" && is(i + 3, "(") &&
            t[i + 4].kind == T::Str && !isClass(k, t[i + 4].text)) {
            const std::string fix = closest(t[i + 4].text, k.classes);
            add(t[i + 4], "Guts&Bolts can't make a \"" + t[i + 4].text + "\"." + (fix.empty() ? "" : " Did you mean \"" + fix + "\"?"),
                false, t[i + 4].text, fix);
        }

        // --- Enum.KeyCode.Spase, Enum.Material.Neone ---
        if (tok.text == "Enum" && is(i + 1, ".") && name(i + 2) && is(i + 3, ".") && name(i + 4)) {
            const std::string& cat = t[i + 2].text;
            const std::string& item = t[i + 4].text;
            if (cat == "KeyCode") {
                if (!LuaApi::keyFromName(item)) {
                    static const std::vector<std::string> keys = {"Space", "Return", "Escape", "Tab", "LeftShift",
                        "RightShift", "LeftControl", "RightControl", "LeftAlt", "Backspace", "Delete", "Up", "Down",
                        "Left", "Right", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Zero",
                        "ButtonA", "ButtonB", "ButtonX", "ButtonY", "ButtonL1", "ButtonR1", "ButtonL2", "ButtonR2"};
                    std::string fix = closest(item, keys);
                    add(t[i + 4], "Enum.KeyCode has no '" + item + "'." + (fix.empty() ? "" : " Did you mean " + fix + "?"),
                        false, item, fix);
                }
            } else if (cat == "Material" && item != "GetEnumItems") {   // (other lists are only examples: anything goes)
                const std::vector<std::string>& items = enumItems(k, cat);
                if (!items.empty() && std::find(items.begin(), items.end(), item) == items.end()) {
                    const std::string fix = closest(item, items);
                    add(t[i + 4], "Enum." + cat + " has no '" + item + "'." + (fix.empty() ? "" : " Did you mean " + fix + "?"),
                        false, item, fix);
                }
            }
        }
    }

    // --- part.Destroy() where it needs part:Destroy() ---
    for (size_t i = 1; i + 2 < t.size(); ++i) {
        if (!is(i, ".") || t[i + 1].kind != T::Name || !colonMethods().count(t[i + 1].text) || !is(i + 2, "(")) continue;
        if (t[i - 1].kind != T::Name && !is(i - 1, ")") && !is(i - 1, "]")) continue;
        size_t s = i - 1;   // the start of the chain: "function a.Destroy(" defines one, that's fine
        while (s >= 2 && t[s].kind == T::Name && (is(s - 1, ".") || is(s - 1, ":"))) s -= 2;
        if (s > 0 && is(s - 1, "function")) continue;
        if (t[s].kind == T::Name && k.tables.count(t[s].text)) continue;   // (a library's own function)
        if (addedParts.count(t[i - 1].text + "." + t[i + 1].text)) continue;   // (the script made that one itself)
        add(t[i], "Use a colon here: " + t[i - 1].text + ":" + t[i + 1].text + "(...), not a dot.", false,
            "." + t[i + 1].text, ":" + t[i + 1].text);
    }

    std::sort(marks.begin(), marks.end(), [](const Mark& a, const Mark& b) { return a.line != b.line ? a.line < b.line : a.col < b.col; });
    return marks;
}

} // namespace ScriptCheck
