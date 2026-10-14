// Running Studio's AI tools (see AiTools.h / AiTools.cpp). Every answer is
// structured JSON: what was asked, what really happened (read back from the
// engine), ids of what was touched, and for failures an error code, whether
// it can be fixed and what to try next.
#include "AiTools.h"
#include "Editor.h"
#include "MultiTest.h"
#include "panels/ViewportPanel.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Serializer.h"
#include "../scene/Physics.h"
#include "../scene/Effects.h"
#include "../scene/Blast.h"
#include "../scene/Player.h"
#include "../game/GameSession.h"
#include "../core/Log.h"

#include <imgui.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>

using nlohmann::json;

namespace {

const char* classOf(const SceneNode* n, const SceneNode* root) {
    if (n == root) return "Workspace";
    switch (n->kind) {
        case NodeKind::Model:  return "Model";
        case NodeKind::Script: return n->isModule ? "ModuleScript" : n->isLocal ? "LocalScript" : "Script";
        case NodeKind::Remote: return n->remoteFunction ? "RemoteFunction" : "RemoteEvent";
        case NodeKind::Prompt: return "ProximityPrompt";
        case NodeKind::Highlight: return "Highlight";
        case NodeKind::ClickDetector: return "ClickDetector";
        case NodeKind::Trail:     return "Trail";
        case NodeKind::Beam:      return "Beam";
        case NodeKind::Light:  return n->lightType == LightType::Spot ? "SpotLight" : "PointLight";
        case NodeKind::ForceField: return "ForceField";
        case NodeKind::Tool:       return "Tool";
        case NodeKind::Value:      return n->valueClass();
        case NodeKind::Decal:      return "Decal";
        case NodeKind::Animation:  return "Animation";
        case NodeKind::FluidSystem:  return "FluidSystem";
        case NodeKind::FluidEmitter: return "FluidEmitter";
        case NodeKind::Mover:        return kMoverClassNames[(int)n->mover.type];
        case NodeKind::Gui:        return kGuiClassNames[(int)n->gui.type];
        case NodeKind::Sound:      return "Sound";
        case NodeKind::Attachment: return "Attachment";
        case NodeKind::Constraint: return kConstraintClassNames[std::clamp((int)n->constraintType, 0, kConstraintTypeCount - 1)];
        default: return n->primitiveType == PrimitiveType::Mesh ? "MeshPart" : "Part";
    }
}

std::string luaQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') {}
        else out += c;
    }
    return out + "\"";
}

// A JSON value as Lua: numbers, true/false, strings; [x,y,z] becomes a
// Vector3 (or a Color3 for colour properties, 0-1 each).
std::string luaValue(const json& v, const std::string& property) {
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number()) { std::ostringstream o; o << v.get<double>(); return o.str(); }
    if (v.is_string()) return luaQuote(v.get<std::string>());
    if (v.is_array() && v.size() == 3 && v[0].is_number() && v[1].is_number() && v[2].is_number()) {
        std::ostringstream o;
        bool color = property.find("Color") != std::string::npos;
        o << (color ? "Color3.new(" : "Vector3.new(") << v[0].get<double>() << ", " << v[1].get<double>() << ", " << v[2].get<double>() << ")";
        return o.str();
    }
    if (v.is_null()) return "nil";
    return luaQuote(v.dump());
}

json vec(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
std::string idOf(const SceneNode* n) { return "#" + std::to_string(n->id); }

// Does this Lua compile? {ok} or {ok: false, error: "name:3: ..."}.
json syntaxOf(const std::string& source, const std::string& name) {
    lua_State* L = luaL_newstate();
    const std::string chunk = "=" + name;
    int r = luaL_loadbufferx(L, source.data(), source.size(), chunk.c_str(), "t");
    json out = {{"ok", r == LUA_OK}};
    if (r != LUA_OK) out["error"] = lua_tostring(L, -1) ? lua_tostring(L, -1) : "syntax error";
    lua_close(L);
    return out;
}

bool isPartLike(const SceneNode* n) { return n->kind == NodeKind::Part && n->mesh; }

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

} // namespace

// ---------------------------------------------------------------------------

SceneNode* Editor::aiFind(const std::string& ref) {
    if (ref.empty()) return nullptr;
    if (ref[0] == '#') {
        try { return m_scene->findById(std::stoull(ref.substr(1))); } catch (...) { return nullptr; }
    }
    if (ref.find_first_not_of("0123456789") == std::string::npos) {   // "42" = "#42"
        try { return m_scene->findById(std::stoull(ref)); } catch (...) { return nullptr; }
    }
    std::stringstream ss(ref);
    std::string part;
    SceneNode* n = m_scene->root();
    bool first = true;
    while (std::getline(ss, part, '.')) {
        if (first && (part == "Workspace" || part == "workspace" || part == "game")) { first = false; continue; }
        first = false;
        SceneNode* c = n->findChild(part);
        if (!c) return nullptr;
        n = c;
    }
    return n;
}

AiToolResult Editor::runAiTool(const std::string& name, const json& args) {
    SceneNode* const root = m_scene->root();
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto label = [&](const SceneNode* n) { return n->name + " (" + classOf(n, root) + ") " + idOf(n); };
    auto brief = [&](const SceneNode* n) { return json{{"object_id", idOf(n)}, {"name", n->name}, {"class", classOf(n, root)}}; };

    // A short picture of an object's state: what an AI needs to check its work.
    auto state = [&](const SceneNode* n) {
        json s = brief(n);
        s["path"] = n == root ? "Workspace" : n->fullName();
        if (n->parent) s["parent_id"] = idOf(n->parent);
        if (isPartLike(n)) {
            s["position"] = vec(glm::vec3(n->worldMatrix()[3]));
            s["size"] = vec(n->transform.scale);
            s["orientation"] = vec(n->transform.rotation);
            s["color"] = vec(n->color);
            s["material"] = kMaterialNames[(int)n->material];
            s["transparency"] = n->transparency;
            s["anchored"] = n->anchored;
            s["can_collide"] = n->canCollide;
        } else if (n->isScript()) {
            s["enabled"] = n->enabled;
            s["lines"] = (int)std::count(n->source.begin(), n->source.end(), '\n') + (n->source.empty() ? 0 : 1);
        } else if (n != root) {
            s["enabled"] = n->enabled;
        }
        return s;
    };
    auto done = [&](json d) {
        d["success"] = true;
        d["operation"] = name;
        AiToolResult r;
        r.text = d.dump(1);
        r.data = std::move(d);
        return r;
    };
    auto fail = [&](const char* code, const std::string& message, const std::string& suggestion, bool recoverable = true) {
        json d = {{"success", false}, {"operation", name},
                  {"error", {{"code", code}, {"message", message}, {"recoverable", recoverable}, {"suggested_action", suggestion}}}};
        AiToolResult r;
        r.text = d.dump(1);
        r.error = true;
        r.data = std::move(d);
        return r;
    };
    auto notFound = [&](const char* key) {
        std::string ref = str(key);
        if (ref.empty()) return fail("INVALID_INPUT", std::string("`") + key + "` is missing.", "Give an object_id like \"#42\" (from get_game_tree or find_objects).");
        return fail("OBJECT_NOT_FOUND", "No object " + ref + " exists.",
                    "Search with find_objects or get_game_tree and use the object_id it returns.");
    };
    // Find the object a ref names. A path is refused when a name on it is used by
    // more than one object (the wrong one could be changed): ask for an id instead.
    AiToolResult refErr;
    auto resolve = [&](const std::string& ref, const char* key, SceneNode*& out) -> bool {
        out = aiFind(ref);
        if (!out) { refErr = notFound(key); return false; }
        if (ref[0] == '#' || ref.find_first_not_of("0123456789") == std::string::npos) return true;
        for (const SceneNode* n = out; n && n->parent; n = n->parent) {
            json same = json::array();
            for (auto& c : n->parent->children)
                if (c->name == n->name && !c->internal) same.push_back(json{{"object_id", idOf(c.get())}, {"path", c->fullName()}});
            if (same.size() > 1) {
                refErr = fail("AMBIGUOUS_NAME", std::to_string(same.size()) + " objects are called \"" + n->name + "\" in " +
                              n->parent->fullName() + ", so \"" + ref + "\" could mean more than one thing.",
                              "Use the object_id of the one you mean (see candidates).");
                refErr.data["error"]["candidates"] = same;
                refErr.text = refErr.data.dump(1);
                out = nullptr;
                return false;
            }
        }
        return true;
    };
    auto need = [&](const char* k, SceneNode*& out) -> bool { return resolve(str(k), k, out); };
    // An optional object argument: `fallback` when it's not given.
    auto optional = [&](const char* k, SceneNode* fallback, SceneNode*& out) -> bool {
        if (!args.contains(k)) { out = fallback; return true; }
        return need(k, out);
    };
    // Run Lua and collect what it printed.
    struct LuaRun { std::vector<std::string> lines; std::vector<std::string> errors; };
    auto lua = [&](const std::string& code) {
        size_t before = Log::entries().size();
        runCommand(code);
        LuaRun out;
        const auto& e = Log::entries();
        for (size_t i = before; i < e.size(); ++i) {
            if (i == before && e[i].text.rfind("> ", 0) == 0) continue;   // the echo of the command
            if (e[i].level == Log::Level::Error) out.errors.push_back(e[i].text);
            else out.lines.push_back((e[i].level == Log::Level::Warn ? "warning: " : "") + e[i].text);
        }
        return out;
    };
    auto levelName = [](Log::Level l) {
        return l == Log::Level::Error ? "error" : l == Log::Level::Warn ? "warning" : l == Log::Level::System ? "studio" : "info";
    };

    // diagnose_object's checks: likely problems, each with a way to fix it.
    auto diagnose = [&](SceneNode* top, bool recursive, json& findings) {
        auto add = [&](const SceneNode* n, const char* severity, const char* code, const std::string& msg, const std::string& fix) {
            findings.push_back({{"object_id", idOf(n)}, {"name", n->name}, {"severity", severity}, {"code", code},
                                {"message", msg}, {"fix", fix}});
        };
        auto inPart = [](const SceneNode* n) { return n->parent && isPartLike(n->parent); };
        std::function<void(SceneNode*)> check = [&](SceneNode* n) {
            if (n->internal || m_scene->isCharacterPart(n)) return;   // characters are the engine's own business
            if (isPartLike(n)) {
                if (n->anchored)
                    add(n, "info", "ANCHORED", "Anchored: it stays exactly where it is and physics won't move it (gravity, "
                        "explosions, pushes).", "If it should fall or be pushed, set_property Anchored false.");
                if (!n->anchored && !n->canCollide)
                    add(n, "warning", "FALLS_THROUGH", "Unanchored with CanCollide off: in play it will fall straight through the floor.",
                        "set_property CanCollide true, or Anchored true.");
                if (n->transparency >= 1.0f || !n->visible)
                    add(n, "warning", "INVISIBLE", "Can't be seen (Transparency 1 or not Visible).", "set_property Transparency 0.");
                const glm::vec3 s = n->transform.scale;
                if (s.x <= 0.0f || s.y <= 0.0f || s.z <= 0.0f)
                    add(n, "error", "BAD_SIZE", "Size has a zero or negative side.", "set_property Size with positive numbers.");
                if (glm::vec3(n->worldMatrix()[3]).y < m_scene->world().fallenPartsHeight)
                    add(n, "error", "BELOW_WORLD", "It's below FallenPartsHeight: it is destroyed as soon as the game starts.",
                        "Move it up with set_property Position.");
                if (n->negated)
                    add(n, "info", "NEGATED", "Negated: shown see-through pink and cut out of other parts by Union.",
                        "Un-negate it in Studio if it should be a normal part.");
            }
            if (n->isScript()) {
                json syn = syntaxOf(n->source, n->name);
                if (!syn["ok"].get<bool>())
                    add(n, "error", "SYNTAX_ERROR", "Lua syntax error: " + syn.value("error", std::string()) +
                        ". It won't run at all.", "read_script, then fix it with edit_script.");
                if (!n->enabled) add(n, "warning", "DISABLED", "The script is disabled: it won't run.", "set_property Enabled true.");
                if (n->source.find_first_not_of(" \t\r\n") == std::string::npos)
                    add(n, "warning", "EMPTY_SCRIPT", "The script is empty.", "Write its code with edit_script.");
                if (n->isModule)
                    add(n, "info", "MODULE", "A ModuleScript only runs when another script require()s it.", "");
            }
            if ((n->isLight() || n->isSound() || n->isPrompt() || n->isDecal() || n->isAttachment()) && !inPart(n)) {
                const char* why = n->isLight() ? "A light shines from the part it's in" :
                                  n->isSound() ? "A sound inside a part plays from there (elsewhere it plays everywhere)" :
                                  n->isPrompt() ? "A ProximityPrompt pops up near the part it's in" :
                                  n->isDecal() ? "A decal is drawn on a side of the part it's in" : "An attachment is a point on a part";
                add(n, n->isSound() ? "info" : "warning", "NOT_IN_PART", std::string(why) + ", but this one isn't inside a part.",
                    "set_property Parent to a part.");
            }
            if (n->isConstraint()) {
                const bool weld = n->constraintType == ConstraintType::Weld;
                for (uint64_t ref : {n->ref0, n->ref1})
                    if (!ref || !m_scene->findById(ref))
                        add(n, "error", "MISSING_ATTACHMENT", std::string("The constraint is missing its ") +
                            (weld ? "Part0 / Part1" : "Attachment0 / Attachment1") + ", so it does nothing.",
                            std::string("Set ") + (weld ? "Part0 and Part1" : "Attachment0 and Attachment1") + " with set_property.");
            }
            if (n->isClickDetector() && !inPart(n) && !(n->parent && n->parent->kind == NodeKind::Model && n->parent->parent))
                add(n, "warning", "NOT_IN_PART", "A ClickDetector makes the part (or model) it's in clickable, but this one isn't inside either.",
                    "set_property Parent to a part or model.");
            if (n->isEffect()) {
                for (uint64_t ref : {n->effect.a0, n->effect.a1})
                    if (!ref || !m_scene->findById(ref))
                        add(n, "error", "MISSING_ATTACHMENT", std::string("The ") + classOf(n, root) +
                            " needs two Attachments (Attachment0 and Attachment1) and doesn't have both, so it isn't drawn.",
                            "Insert Attachments in parts and set Attachment0 / Attachment1 with set_property.");
            }
            if (n->isTool()) {
                SceneNode* h = n->findChild("Handle");
                if (!h || !isPartLike(h))
                    add(n, "warning", "NO_HANDLE", "The tool has no part called Handle, so nothing shows in the character's hand.",
                        "Put a part named Handle inside the tool.");
            }
            if (n->isGuiObject()) {
                bool shown = false;
                for (const SceneNode* p = n->parent; p; p = p->parent)
                    if (p->isGui() && isGuiLayer(p->gui.type)) { shown = true; break; }
                if (!shown)
                    add(n, "warning", "GUI_NOT_IN_SCREENGUI", "UI only shows inside a ScreenGui, BillboardGui or SurfaceGui.",
                        "set_property Parent to a ScreenGui (insert_object ScreenGui first if there is none).");
            }
            if (recursive) for (auto& c : n->children) check(c.get());
        };
        check(top);
    };
    auto count = [](const json& findings, const char* severity) {
        int k = 0;
        for (const auto& f : findings) if (f.value("severity", "") == severity) ++k;
        return k;
    };

    // ------------------------------------------------------------------- DOCS
    if (name == "get_engine_info") {
        const std::string topic = str("topic");
        static const char* kTopics[] = {"overview", "capabilities", "concepts", "coordinates", "workflows", "examples", "classes"};
        auto one = [&](const std::string& t) {
            json d = {{"topic", t}};
            if (t == "classes") {
                d["classes"] = insertKinds();
                d["text"] = "Every kind insert_object can make (the names after the classes are ready-made things, each an "
                            "independent copy). Plus Team (goes in the Teams folder).";
            } else d["text"] = AiTools::engineDoc(t);
            if (t == "capabilities") d["manifest"] = AiTools::capabilities();
            return d;
        };
        if (topic == "all") {
            json pages = json::array();
            for (const char* t : kTopics) pages.push_back(one(t));
            return done({{"topic", "all"}, {"pages", pages}});
        }
        if (std::find(std::begin(kTopics), std::end(kTopics), topic) == std::end(kTopics))
            return fail("INVALID_INPUT", "Unknown topic \"" + topic + "\".",
                        "Use overview, capabilities, concepts, coordinates, workflows, examples, classes or all.");
        return done(one(topic));
    }

    // ------------------------------------------------------------------ SCENE
    if (name == "get_game_tree") {
        SceneNode* top;
        if (!optional("root", root, top)) return refErr;
        int depth = std::max(0, args.value("depth", 4));
        std::string out;
        int shown = 0;
        bool truncated = false;
        std::function<void(SceneNode*, int)> walk = [&](SceneNode* n, int d) {
            if (n->internal) return;
            if (shown >= 1500) { truncated = true; return; }
            out += std::string((size_t)d * 2, ' ') + label(n) + "\n";
            ++shown;
            if (d >= depth) {
                int kids = 0;
                for (auto& c : n->children) if (!c->internal) ++kids;
                if (kids) out += std::string((size_t)(d + 1) * 2, ' ') + "... " + std::to_string(kids) + " more inside\n";
                return;
            }
            for (auto& c : n->children) walk(c.get(), d + 1);
        };
        walk(top, 0);
        return done({{"object_id", idOf(top)}, {"tree", out}, {"object_count", shown}, {"truncated", truncated}});
    }
    if (name == "find_objects") {
        const std::string want = lower(str("name")), cls = str("class"), tag = str("tag");
        if (want.empty() && cls.empty() && tag.empty())
            return fail("INVALID_INPUT", "Give at least one of name, class or tag.", "e.g. {\"name\": \"barrel\"} or {\"class\": \"Script\"}.");
        SceneNode* top;
        if (!optional("under", root, top)) return refErr;
        const int limit = std::clamp(args.value("limit", 50), 1, 500);
        json matches = json::array();
        int total = 0;
        std::function<void(SceneNode*)> walk = [&](SceneNode* n) {
            if (n->internal) return;
            bool ok = n != root;
            if (ok && !want.empty()) ok = lower(n->name).find(want) != std::string::npos;
            if (ok && !cls.empty()) ok = cls == classOf(n, root);
            if (ok && !tag.empty()) ok = std::find(n->tags.begin(), n->tags.end(), tag) != n->tags.end();
            if (ok) {
                ++total;
                if ((int)matches.size() < limit) { json b = brief(n); b["path"] = n->fullName(); matches.push_back(b); }
            }
            for (auto& c : n->children) walk(c.get());
        };
        walk(top);
        return done({{"matches", matches}, {"count", total}, {"truncated", total > (int)matches.size()}});
    }
    if (name == "insert_object") {
        SceneNode* parent;
        if (!optional("parent", nullptr, parent)) return refErr;
        const std::string kind = str("kind");
        if (kind.empty()) return fail("INVALID_INPUT", "`kind` is missing.", "Give a class like Part, Model, Script or PointLight.");
        std::vector<uint64_t> before;
        m_scene->forEach([&](SceneNode* n) { before.push_back(n->id); });
        std::sort(before.begin(), before.end());
        insertObject(kind, parent);
        SceneNode* made = nullptr;
        json affected = json::array();
        m_scene->forEach([&](SceneNode* n) {
            if (std::binary_search(before.begin(), before.end(), n->id) || n->internal) return;
            if (!made) made = n;
            affected.push_back(idOf(n));
        });
        // Ready-made things come with parts inside: report the top one.
        while (made && made->parent && made->parent != root &&
               !std::binary_search(before.begin(), before.end(), made->parent->id)) made = made->parent;
        if (!made) return fail("UNKNOWN_CLASS", "Couldn't insert a \"" + kind + "\": that isn't something Studio can insert.",
                               "Use a class from the insert_object description, like Part, Model, Script or PointLight.");
        if (!str("name").empty()) made->name = str("name");
        json d = {{"object_id", idOf(made)}, {"name", made->name}, {"class", classOf(made, root)},
                  {"affected_ids", affected}, {"resulting_state", state(made)}};
        if (made->parent) d["parent_id"] = idOf(made->parent);
        if (parent && made->parent != parent)
            d["warnings"] = json::array({"It couldn't go inside " + label(parent) + " (scripts and character parts can't hold "
                                         "objects), so it went in " + (made->parent ? label(made->parent) : std::string("Workspace")) + "."});
        return done(d);
    }
    if (name == "delete_object") {
        SceneNode* n;
        if (!need("object", n)) return refErr;
        if (m_scene->isProtected(n) || n == root)
            return fail("PROTECTED", label(n) + " can't be deleted.", "Delete the objects inside it instead, if that's what was asked.", false);
        json gone = brief(n), affected = json::array();
        std::function<void(SceneNode*)> walk = [&](SceneNode* x) { affected.push_back(idOf(x)); for (auto& c : x->children) walk(c.get()); };
        walk(n);
        m_scene->deselect();
        m_scene->removeNode(n);
        return done({{"deleted", gone}, {"affected_ids", affected}, {"object_id", gone["object_id"]}});
    }

    // ---------------------------------------------------------------- OBJECTS
    if (name == "get_object") {
        SceneNode* n;
        if (!need("object", n)) return refErr;
        json props = json::parse(Serializer::nodeToString(*n), nullptr, false);
        if (props.is_object()) {
            props.erase("children");
            props.erase("mesh");            // big model data isn't useful here
            props.erase("editMesh");
            props.erase("id");
        }
        std::string dumped = props.dump();
        if (dumped.size() > 60000) props = {{"note", "too big to show; use read_script for a script's source"}};
        json kids = json::array();
        for (auto& c : n->children) if (!c->internal) kids.push_back(brief(c.get()));
        json d = state(n);
        d["children"] = kids;
        d["properties"] = props;
        d["state_source"] = m_playing ? "runtime (playtest running)" : "editor (saved game)";
        if (m_playing && isPartLike(n))
            d["runtime"] = {{"velocity", vec(n->velocity)}, {"angular_velocity", vec(n->angularVelocity)},
                            {"speed", glm::length(n->velocity)}, {"resting", n->sleepTime > 0.5f}};
        return done(d);
    }
    if (name == "set_property") {
        SceneNode* n;
        if (!need("object", n)) return refErr;
        std::string prop = str("property");
        if (prop.empty() || prop.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
            return fail("INVALID_INPUT", "\"" + prop + "\" isn't a property name.", "Use a property name like Position, Color or Anchored.");
        if (!args.contains("value")) return fail("INVALID_INPUT", "`value` is missing.", "Give the new value (number, true/false, text or [x, y, z]).");
        json value = args["value"];
        std::string v = luaValue(value, prop);
        if (prop == "Parent" && value.is_string()) {
            SceneNode* p;
            if (!resolve(value.get<std::string>(), "value", p)) return refErr;
            v = "__gb_byId(" + std::to_string(p->id) + ")";
        }
        const uint64_t id = n->id;
        LuaRun r = lua("local o = __gb_byId(" + std::to_string(id) + ")\no." + prop + " = " + v +
                       "\nlocal ok, now = pcall(function() return o." + prop + " end)\nprint(\"\\1GBREAD\" .. (ok and tostring(now) or \"?\"))");
        if (!r.errors.empty())
            return fail("LUA_ERROR", "Couldn't set " + prop + " of " + label(n) + ": " + r.errors.front(),
                        "Check the property name and value type with get_object (properties are Roblox-style, e.g. Color takes [r, g, b] 0-1).");
        std::string now;
        for (auto& l : r.lines) if (l.rfind("\1GBREAD", 0) == 0) now = l.substr(7);
        n = m_scene->findById(id);
        if (!n) return fail("OBJECT_NOT_FOUND", "The object disappeared after the change.", "Check get_game_tree.", false);
        return done({{"object_id", idOf(n)}, {"changed_properties", {{prop, {{"requested", value}, {"now", now}}}}},
                     {"resulting_state", state(n)}});
    }
    if (name == "select") {
        m_scene->deselect();
        json picked = json::array(), missing = json::array();
        if (args.contains("objects") && args["objects"].is_array())
            for (const auto& o : args["objects"])
                if (o.is_string()) {
                    if (SceneNode* f = aiFind(o.get<std::string>())) { m_scene->addToSelection(f); picked.push_back(idOf(f)); }
                    else missing.push_back(o);
                }
        json d = {{"selected", picked}};
        if (!missing.empty()) d["missing"] = missing;
        return done(d);
    }

    // -------------------------------------------------------------- SCRIPTING
    if (name == "create_script") {
        SceneNode* parent;
        if (!optional("parent", root, parent)) return refErr;
        const std::string type = str("type").empty() ? "Script" : str("type");
        if (type != "Script" && type != "LocalScript" && type != "ModuleScript")
            return fail("INVALID_INPUT", "type must be Script, LocalScript or ModuleScript.", "Leave it out for a normal Script.");
        auto s = std::make_unique<SceneNode>(str("name").empty() ? type : str("name"), NodeKind::Script);
        s->source = str("source");
        s->isModule = type == "ModuleScript";
        s->isLocal = type == "LocalScript";
        SceneNode* made = m_scene->insert(std::move(s), parent);
        json d = {{"object_id", idOf(made)}, {"name", made->name}, {"class", classOf(made, root)}, {"parent_id", idOf(parent)},
                  {"syntax", syntaxOf(made->source, made->name)}, {"resulting_state", state(made)}};
        if (!d["syntax"]["ok"].get<bool>()) d["warnings"] = json::array({"The script has a syntax error and won't run until it's fixed."});
        return done(d);
    }
    if (name == "read_script") {
        SceneNode* n;
        if (!need("object", n)) return refErr;
        if (!n->isScript()) return fail("NOT_A_SCRIPT", label(n) + " isn't a script.", "Use get_object for other objects.");
        return done({{"object_id", idOf(n)}, {"class", classOf(n, root)}, {"source", n->source},
                     {"lines", state(n)["lines"]}});
    }
    if (name == "edit_script") {
        SceneNode* n;
        if (!need("object", n)) return refErr;
        if (!n->isScript()) return fail("NOT_A_SCRIPT", label(n) + " isn't a script.", "find_objects with class Script to find it.");
        if (args.contains("source")) n->source = str("source");
        else {
            std::string old = str("old");
            if (old.empty()) return fail("INVALID_INPUT", "Give `old` and `new` (or `source`).", "Copy `old` exactly from read_script.");
            size_t at = n->source.find(old);
            if (at == std::string::npos)
                return fail("TEXT_NOT_FOUND", "`old` isn't in the script.", "read_script and copy the text exactly (spaces and line breaks matter).");
            if (n->source.find(old, at + 1) != std::string::npos)
                return fail("TEXT_NOT_UNIQUE", "`old` appears more than once.", "Include more of the lines around it so it's unique.");
            n->source.replace(at, old.size(), str("new"));
        }
        json d = {{"object_id", idOf(n)}, {"syntax", syntaxOf(n->source, n->name)}, {"resulting_state", state(n)}};
        json warn = json::array();
        if (!d["syntax"]["ok"].get<bool>()) warn.push_back("The script now has a syntax error and won't run until it's fixed.");
        if (m_playing) warn.push_back("A playtest is running: it keeps using the old code until it is stopped and started again.");
        if (!warn.empty()) d["warnings"] = warn;
        return done(d);
    }
    if (name == "run_lua") {
        LuaRun r = lua(str("code"));
        json d = {{"output", r.lines}, {"errors", r.errors}, {"ran_in", m_playing ? "the running playtest" : "the editor (saved game)"}};
        if (!r.errors.empty()) {
            AiToolResult res = fail("LUA_ERROR", r.errors.front(), "Fix the code and run it again (plain Lua 5.4, Roblox-style API).");
            res.data["output"] = r.lines;
            res.data["errors"] = r.errors;
            res.text = res.data.dump(1);
            return res;
        }
        return done(d);
    }

    // ---------------------------------------------------------------- RUNTIME
    if (name == "playtest") {
        const std::string action = str("action");
        json warn = json::array();
        if (action == "start" || action == "simulate") {
            if (m_playing) warn.push_back("It was already playtesting.");
            else startPlay(action == "simulate" ? 2 : 0);
        } else if (action == "stop") {
            if (!m_playing) warn.push_back("It wasn't playtesting.");
            else togglePlay();
        } else if (action == "pause" || action == "resume" || action == "step") {
            if (!m_playing) return fail("NOT_PLAYING", "There's no playtest to " + action + ".", "playtest start first.");
            m_state.simPaused = action != "resume";
            if (action == "step") m_state.simStep = true;
        } else {
            return fail("INVALID_INPUT", "action must be start, simulate, stop, pause, resume or step.", "");
        }
        json d = {{"playing", m_playing}, {"mode", !m_playing ? "editing" : m_session->runOnly() ? "simulate" : "play"},
                  {"paused", m_playing && m_state.simPaused}};
        d["note"] = !m_playing ? "Stopped. The game is back exactly how it was before Play."
                  : m_state.simPaused ? "Paused: physics and scripts are frozen (step moves one frame)."
                  : "Scripts and physics are running. Check get_errors, get_runtime_state, get_object and screenshot, then stop.";
        if (!warn.empty()) d["warnings"] = warn;
        return done(d);
    }

    if (name == "multiplayer_test") {
        MultiTest& mt = multiTest();
        const std::string action = str("action");
        json warn = json::array();
        bool withOutput = false;
        std::string err;
        if (action == "start") {
            const int players = std::clamp(args.value("players", 2), MultiTest::kMinPlayers, MultiTest::kMaxPlayers);
            if (mt.running()) warn.push_back("A multiplayer test was already running: it was restarted with the game as it is now.");
            if (m_playing) warn.push_back("The playtest was stopped first.");
            if (!startMultiTest(players, err))
                return fail("MULTIPLAYER_TEST_FAILED", err, "Check that GutsAndBoltsGameServer and GutsAndBoltsPlayer are next to Studio.");
        } else if (action == "stop") {
            if (!mt.running()) warn.push_back("There was no multiplayer test running.");
            stopMultiTest();
            withOutput = true;   // (what they printed before closing)
        } else if (action == "add_player") {
            if (!mt.addPlayer(err)) return fail("MULTIPLAYER_TEST_FAILED", err, mt.running() ? "" : "multiplayer_test start first.");
        } else if (action == "output" || action == "status") {
            withOutput = action == "output";
        } else {
            return fail("INVALID_INPUT", "action must be start, stop, status, output or add_player.", "");
        }
        json d = mt.status();
        if (withOutput || action == "status") {
            const int want = std::clamp(args.value("lines", withOutput ? 60 : 10), 1, 400);
            json lines = json::array();
            for (const auto& l : mt.output(want, str("from")))
                lines.push_back({{"from", l.from}, {"level", l.level}, {"text", l.text}, {"time", l.time}});
            d["output"] = lines;
        }
        d["note"] = !mt.running() ? (action == "stop" ? "Stopped: the test server and every player window are closed."
                                                      : "No multiplayer test is running.")
                  : !mt.serverReady() ? "The test server is loading the game; the player windows open when it's ready. "
                                        "Check again in a few seconds (status or output)."
                  : "Running. Server Scripts run on the test server, LocalScripts in each window. Read output, then stop.";
        if (!warn.empty()) d["warnings"] = warn;
        return done(d);
    }

    // ------------------------------------------------------------------ DEBUG
    if (name == "get_output") {
        int want = std::clamp(args.value("lines", 40), 1, 400);
        const auto& e = Log::entries();
        json lines = json::array();
        for (size_t i = e.size() > (size_t)want ? e.size() - (size_t)want : 0; i < e.size(); ++i)
            lines.push_back({{"time", e[i].time}, {"level", levelName(e[i].level)}, {"text", e[i].text}});
        return done({{"lines", lines}});
    }
    if (name == "get_errors") {
        int look = std::clamp(args.value("lines", 200), 1, 2000);
        bool warnings = args.value("include_warnings", true);
        const auto& e = Log::entries();
        json errs = json::array(), warns = json::array();
        for (size_t i = e.size() > (size_t)look ? e.size() - (size_t)look : 0; i < e.size(); ++i) {
            if (e[i].level == Log::Level::Error) errs.push_back({{"time", e[i].time}, {"text", e[i].text}});
            else if (warnings && e[i].level == Log::Level::Warn) warns.push_back({{"time", e[i].time}, {"text", e[i].text}});
        }
        return done({{"errors", errs}, {"warnings", warns}, {"error_count", errs.size()}, {"warning_count", warns.size()}});
    }
    if (name == "diagnose_object") {
        SceneNode* n;
        if (!need("object", n)) return refErr;
        json findings = json::array();
        diagnose(n, args.value("recursive", true), findings);
        return done({{"object_id", idOf(n)}, {"resulting_state", state(n)}, {"findings", findings},
                     {"error_count", count(findings, "error")}, {"warning_count", count(findings, "warning")}});
    }
    if (name == "validate_scene") {
        json all = json::array();
        diagnose(root, true, all);
        const bool info = args.value("include_info", false);
        const int limit = std::clamp(args.value("limit", 100), 1, 1000);
        json shown = json::array();
        for (const auto& f : all)
            if ((info || f.value("severity", "") != "info") && (int)shown.size() < limit) shown.push_back(f);
        int objects = 0, scripts = 0;
        m_scene->forEach([&](SceneNode* n) { if (!n->internal) { ++objects; if (n->isScript()) ++scripts; } });
        int outErrors = 0;
        const auto& e = Log::entries();
        for (size_t i = e.size() > 200 ? e.size() - 200 : 0; i < e.size(); ++i) if (e[i].level == Log::Level::Error) ++outErrors;
        json d = {{"object_count", objects}, {"script_count", scripts}, {"findings", shown},
                  {"error_count", count(all, "error")}, {"warning_count", count(all, "warning")},
                  {"recent_output_errors", outErrors}, {"ok", count(all, "error") == 0}};
        if (outErrors) d["warnings"] = json::array({"The Output has recent errors: read them with get_errors."});
        return done(d);
    }

    // ---------------------------------------------------------------- PHYSICS
    // [x, y, z] from an argument.
    auto vecArg = [&](const char* k, glm::vec3& out) -> bool {
        if (!args.contains(k) || !args[k].is_array() || args[k].size() != 3) return false;
        for (int i = 0; i < 3; ++i) { if (!args[k][i].is_number()) return false; out[i] = args[k][i].get<float>(); }
        return true;
    };
    // Where an object is: a part's centre, a character's HumanoidRootPart, else the middle of its parts.
    auto posOf = [&](SceneNode* n) {
        if (isPartLike(n)) return glm::vec3(n->worldMatrix()[3]);
        if (SceneNode* hrp = n->findChild("HumanoidRootPart")) return glm::vec3(hrp->worldMatrix()[3]);
        glm::vec3 sum(0.0f);
        int k = 0;
        std::function<void(SceneNode*)> walk = [&](SceneNode* x) {
            if (isPartLike(x)) { sum += glm::vec3(x->worldMatrix()[3]); ++k; }
            for (auto& c : x->children) walk(c.get());
        };
        walk(n);
        return k ? sum / (float)k : glm::vec3(n->worldMatrix()[3]);
    };
    // A point argument: [x, y, z] or an object.
    auto pointArg = [&](const char* k, glm::vec3& out) -> bool {
        if (vecArg(k, out)) return true;
        SceneNode* n;
        if (!args.contains(k) || !args[k].is_string()) {
            refErr = fail("INVALID_INPUT", std::string("`") + k + "` must be [x, y, z] or an object.", "e.g. [0, 3, 10] or \"#42\".");
            return false;
        }
        if (!need(k, n)) return false;
        out = posOf(n);
        return true;
    };
    auto navMesh = [&]() -> const NavMesh& {
        if (!m_aiNav) m_aiNav = std::make_unique<Physics>();
        m_aiNav->gather(*m_scene);
        return m_aiNav->navMesh();
    };

    if (name == "create_physical_object") {
        const std::string shape = str("shape").empty() ? "Part" : str("shape");
        if (shape != "Part" && shape != "Sphere" && shape != "Cylinder")
            return fail("INVALID_INPUT", "shape must be Part, Sphere or Cylinder.", "Leave it out for a box.");
        json insertArgs = {{"kind", shape}};
        if (args.contains("parent")) insertArgs["parent"] = args["parent"];
        if (!str("name").empty()) insertArgs["name"] = str("name");
        AiToolResult made = runAiTool("insert_object", insertArgs);
        if (made.error) { made.data["operation"] = name; made.text = made.data.dump(1); return made; }
        SceneNode* n = m_scene->findById(std::stoull(made.data["object_id"].get<std::string>().substr(1)));
        if (!n) return fail("INTERNAL", "The new part went missing.", "Check get_game_tree.", false);
        json warnings = made.data.value("warnings", json::array());
        glm::vec3 v;
        if (vecArg("size", v)) n->transform.scale = glm::max(v, glm::vec3(0.05f));
        else if (shape != "Sphere") n->transform.scale = glm::vec3(2.0f);
        if (vecArg("color", v)) n->color = glm::clamp(v, glm::vec3(0.0f), glm::vec3(1.0f));
        if (!str("material").empty()) {
            bool found = false;
            for (int i = 0; i < kMaterialCount; ++i)
                if (lower(kMaterialNames[i]) == lower(str("material"))) { n->material = (Material)i; found = true; }
            if (!found) warnings.push_back("There's no material \"" + str("material") + "\" (there are Plastic, Metal, Neon, Wood, "
                                           "Glass, Concrete, Ice); it stayed Plastic.");
        }
        n->anchored = args.value("anchored", false);
        n->canCollide = args.value("can_collide", true);
        if (args.contains("density") && args["density"].is_number()) n->density = std::max(0.01f, args["density"].get<float>());
        if (args.contains("friction") && args["friction"].is_number()) n->friction = std::clamp(args["friction"].get<float>(), 0.0f, 2.0f);
        if (args.contains("elasticity") && args["elasticity"].is_number()) n->elasticity = std::clamp(args["elasticity"].get<float>(), 0.0f, 1.0f);
        if (vecArg("position", v)) {
            glm::vec3 local = n->parent ? glm::vec3(glm::inverse(n->parent->worldMatrix()) * glm::vec4(v, 1.0f)) : v;
            n->transform.position = local;
        } else if (n->parent == root) {
            n->transform.position.y = n->transform.scale.y * 0.5f;   // standing on the ground near the camera
        }
        m_scene->markDirty();
        json findings = json::array();
        diagnose(n, false, findings);
        json d = {{"object_id", idOf(n)}, {"name", n->name}, {"class", classOf(n, root)}, {"resulting_state", state(n)},
                  {"findings", findings}, {"note", n->anchored ? "Anchored: it won't move." : "It falls and reacts to forces during a playtest."}};
        if (!warnings.empty()) d["warnings"] = warnings;
        return done(d);
    }
    if (name == "spawn_explosion") {
        if (!m_playing) return fail("NOT_PLAYING", "Explosions happen in the running game, and no playtest is running.",
                                    "playtest start first (or put an Explosion in a script for the real game).");
        glm::vec3 at;
        if (!vecArg("position", at)) return fail("INVALID_INPUT", "position must be [x, y, z].", "e.g. [0, 2, 0].");
        BlastOptions o;
        o.radius = std::clamp(args.value("radius", 8.0f), 0.5f, 500.0f);
        o.power = std::clamp(args.value("power", 1.0f), 0.0f, 100.0f);
        o.destroy = args.value("destroy", false);
        o.fire = std::max(0.0f, args.value("fire", 0.0f));
        o.visible = args.value("visible", true);
        o.hurts = args.value("hurts", true);
        auto hits = Effects::blast(*m_scene, at, o);
        json list = json::array();
        for (const auto& [part, dist] : hits) list.push_back({{"object_id", idOf(part)}, {"name", part->name}, {"distance", dist}});
        json d = {{"position", vec(at)}, {"radius", o.radius}, {"power", o.power}, {"hits", list}, {"hit_count", list.size()}};
        if (list.empty()) d["warnings"] = json::array({"Nothing was in range. Only parts within the radius are hit, and only unanchored ones move."});
        return done(d);
    }

    // ------------------------------------------------------------- NAVIGATION
    if (name == "bake_navmesh") {
        if (!m_aiNav) m_aiNav = std::make_unique<Physics>();
        m_aiNav->gather(*m_scene);
        m_aiNav->rebakeNavMesh();
        const NavMesh& nav = m_aiNav->navMesh();
        if (args.value("show", false)) { m_state.showNavMesh = true; m_state.bakeNavMesh = 1; }
        const NavMesh::Settings& st = nav.settings();
        json d = {{"baked", nav.baked()}, {"spans", nav.spanCount()}, {"bake_ms", nav.bakeMs()},
                  {"settings", {{"cell", st.cell}, {"max_climb", st.maxClimb}, {"max_slope", st.maxSlope}, {"jump_height", st.jumpHeight},
                                {"jump_gap", st.jumpGap}, {"max_drop", st.maxDrop}}},
                  {"shown_in_studio", m_state.showNavMesh}};
        if (nav.spanCount() == 0)
            d["warnings"] = json::array({"No walkable floor was found. Floors must be anchored parts with CanCollide on."});
        return done(d);
    }
    if (name == "find_path") {
        glm::vec3 from, to;
        if (!pointArg("from", from) || !pointArg("to", to)) return refErr;
        NavMesh::Agent agent;
        agent.radius = std::clamp(args.value("agent_radius", agent.radius), 0.1f, 10.0f);
        agent.height = std::clamp(args.value("agent_height", agent.height), 0.5f, 20.0f);
        agent.canJump = args.value("can_jump", true);
        const NavMesh& nav = navMesh();
        std::vector<NavMesh::Waypoint> wps;
        NavMesh::Status st = nav.findPath(from, to, agent, wps);
        json list = json::array();
        float length = 0.0f;
        for (size_t i = 0; i < wps.size(); ++i) {
            if (i) length += glm::length(wps[i].pos - wps[i - 1].pos);
            list.push_back({{"position", vec(wps[i].pos)}, {"action", wps[i].action == NavMesh::Action::Jump ? "Jump" : "Walk"}});
        }
        json d = {{"status", NavMesh::statusName(st)}, {"from", vec(from)}, {"to", vec(to)}, {"waypoints", list}, {"length", length}};
        if (st != NavMesh::Status::Success)
            d["warnings"] = json::array({"No full route. Check that both points are on (or just above) anchored, solid floors "
                                         "and that the way isn't blocked; jumps over " + std::to_string(nav.settings().jumpHeight) +
                                         " units high aren't planned."});
        return done(d);
    }
    if (name == "walk_character_to") {
        if (!m_playing) return fail("NOT_PLAYING", "Characters only walk in the running game.", "playtest start first.");
        SceneNode* ch;
        if (!need("character", ch)) return refErr;
        if (!ch->findChild("HumanoidRootPart"))
            return fail("NOT_A_CHARACTER", label(ch) + " isn't a character (a Model with HumanoidRootPart, Torso and Head).",
                        "find_objects for the character Model, or insert_object \"Rig\" / \"Zombie\".");
        glm::vec3 to;
        if (!pointArg("to", to)) return refErr;
        std::ostringstream code;
        code << "local m = __gb_byId(" << ch->id << ")\nlocal h = m and m.Humanoid\nif not h then error('no Humanoid', 0) end\n"
             << "local ok = h:PathfindStart(Vector3.new(" << to.x << ", " << to.y << ", " << to.z << "))\n"
             << "print(\"\\1GBREAD\" .. tostring(ok) .. \" \" .. tostring(h.PathfindStatus))";
        LuaRun r = lua(code.str());
        if (!r.errors.empty()) return fail("LUA_ERROR", "Couldn't start walking: " + r.errors.front(), "Check it's a character with a Humanoid.");
        std::string res;
        for (auto& l : r.lines) if (l.rfind("\1GBREAD", 0) == 0) res = l.substr(7);
        const bool started = res.rfind("true", 0) == 0;
        json d = {{"object_id", idOf(ch)}, {"to", vec(to)}, {"started", started},
                  {"status", res.find(' ') != std::string::npos ? res.substr(res.find(' ') + 1) : res}};
        if (!started) d["warnings"] = json::array({"No route there: find_path explains why."});
        return done(d);
    }

    // ---------------------------------------------------------------- RUNTIME
    if (name == "get_runtime_state") {
        const ImGuiIO& io = ImGui::GetIO();
        json d = {{"playing", m_playing}, {"paused", m_playing && m_state.simPaused},
                  {"mode", !m_playing ? "editing" : m_session->runOnly() ? "simulate" : "play"},
                  {"fps", io.Framerate}, {"frame_ms", io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f}};
        if (m_playing) {
            if (Player* p = m_scene->player(); p && p->root() && !m_session->runOnly())
                d["player"] = {{"object_id", idOf(p->root())}, {"position", vec(p->position())}, {"speed", glm::length(p->velocity())},
                               {"health", p->humanoid().health}, {"max_health", p->humanoid().maxHealth}, {"dead", p->isDead()}};
            const int limit = std::clamp(args.value("moving_limit", 20), 0, 200);
            json moving = json::array();
            int count = 0;
            m_scene->forEach([&](SceneNode* n) {
                if (!isPartLike(n) || n->anchored || n->internal || m_scene->isCharacterPart(n)) return;
                const float speed = glm::length(n->velocity);
                if (speed < 0.05f) return;
                ++count;
                if ((int)moving.size() < limit)
                    moving.push_back({{"object_id", idOf(n)}, {"name", n->name}, {"position", vec(glm::vec3(n->worldMatrix()[3]))}, {"speed", speed}});
            });
            d["moving_parts"] = moving;
            d["moving_count"] = count;
            json chars = json::array();
            static const char* kRoute[] = {"Idle", "Walking", "Arrived", "Failed"};
            for (const auto& npc : m_scene->npcs().all()) {
                SceneNode* model = m_scene->findById(npc->rootId);
                if (!model) continue;
                json c = {{"object_id", idOf(model)}, {"name", model->name}, {"position", vec(posOf(model))},
                          {"health", npc->humanoid.health}, {"dead", npc->dead}, {"pathfind", kRoute[(int)npc->route.state]}};
                if (npc->route.state == Npc::Route::Walking) c["going_to"] = vec(npc->route.goal);
                chars.push_back(c);
            }
            d["characters"] = chars;
        } else {
            d["note"] = "Not playtesting: nothing is simulating. playtest start (or simulate) to run the game.";
        }
        if (m_aiNav && m_aiNav->navMesh().baked())
            d["navmesh"] = {{"baked", true}, {"spans", m_aiNav->navMesh().spanCount()}, {"version", m_aiNav->navMesh().version()}};
        int errs = 0;
        const auto& e = Log::entries();
        for (size_t i = e.size() > 200 ? e.size() - 200 : 0; i < e.size(); ++i) if (e[i].level == Log::Level::Error) ++errs;
        d["recent_errors"] = errs;
        return done(d);
    }

    // ---------------------------------------------------------------- HISTORY
    if (name == "checkpoint") {
        const std::string action = str("action");
        const std::string cp = str("name").empty() ? "default" : str("name");
        if (action == "list") {
            json names = json::array();
            for (auto& [k, v] : m_aiCheckpoints) names.push_back(k);
            return done({{"checkpoints", names}});
        }
        if (action == "save") {
            if (m_playing) return fail("PLAYTEST_RUNNING", "Save points are of the saved game, and a playtest is running.", "playtest stop first.");
            m_aiCheckpoints[cp] = Serializer::saveScene(*m_scene);
            int objects = 0;
            m_scene->forEach([&](SceneNode* n) { if (!n->internal) ++objects; });
            return done({{"name", cp}, {"objects", objects}, {"note", "checkpoint restore with this name puts the game back to now."}});
        }
        auto it = m_aiCheckpoints.find(cp);
        if (action == "delete") {
            if (it == m_aiCheckpoints.end()) return fail("CHECKPOINT_NOT_FOUND", "No checkpoint called \"" + cp + "\".", "checkpoint list");
            m_aiCheckpoints.erase(it);
            return done({{"name", cp}, {"deleted", true}});
        }
        if (action == "restore") {
            if (m_playing) return fail("PLAYTEST_RUNNING", "Can't restore during a playtest.", "playtest stop first.");
            if (it == m_aiCheckpoints.end()) return fail("CHECKPOINT_NOT_FOUND", "No checkpoint called \"" + cp + "\".", "checkpoint list");
            trackChanges();                      // what's there now becomes one undo step
            m_undo.push_back(m_committed);
            if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
            m_redo.clear();
            restore(it->second);
            return done({{"name", cp}, {"restored", true}, {"note", "undo brings back the version from before the restore."}});
        }
        return fail("INVALID_INPUT", "action must be save, restore, list or delete.", "");
    }
    if (name == "redo") {
        if (m_playing) return fail("PLAYTEST_RUNNING", "Redo doesn't work during a playtest.", "playtest stop first.");
        if (m_redo.empty()) return fail("NOTHING_TO_REDO", "There's nothing to redo.", "", false);
        redo();
        return done({{"redone", true}, {"redo_steps_left", m_redo.size()}});
    }

    // ------------------------------------------------------------------- VIEW
    if (name == "screenshot") {
        std::string png = m_viewport->snapshotPng(960, 540, true);
        if (png.empty()) return fail("SCREENSHOT_FAILED", "Couldn't take a picture of the 3D view.", "Make sure Studio's 3D view is open, then try again.");
        AiToolResult r = done({{"width", 960}, {"height", 540}, {"view", m_playing ? "game camera (playtest)" : "editor camera"}});
        r.png = std::move(png);
        return r;
    }

    // ---------------------------------------------------------------- HISTORY
    if (name == "undo") {
        if (m_playing) return fail("PLAYTEST_RUNNING", "Undo doesn't work during a playtest.", "playtest stop (that already throws away playtest changes).");
        trackChanges();
        if (m_undo.empty()) return fail("NOTHING_TO_UNDO", "There's nothing to undo.", "", false);
        int steps = std::clamp(args.value("steps", 1), 1, 100), doneSteps = 0;
        while (doneSteps < steps && !m_undo.empty()) { undo(); ++doneSteps; }
        return done({{"undone", doneSteps}, {"undo_steps_left", m_undo.size()}});
    }

    return fail("UNKNOWN_TOOL", "There's no tool called " + name + ".", "Use one of the tools from tools/list. Don't invent tools.", false);
}
