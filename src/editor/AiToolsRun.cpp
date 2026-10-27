// Running Studio's AI tools (see AiTools.h / AiTools.cpp). Every answer is
// structured JSON: what was asked, what really happened (read back from the
// engine), ids of what was touched, and for failures an error code, whether
// it can be fixed and what to try next.
#include "AiTools.h"
#include "VisualScript.h"
#include "Editor.h"
#include "../scripting/ScriptHistory.h"
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
#include "../core/Paths.h"
#include "../online/OnlineClient.h"
#include "../scripting/Luau.h"

#include <imgui.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <sstream>

using nlohmann::json;

namespace {

const char* classOf(const SceneNode* n, const SceneNode* root) {
    if (n == root) return "Workspace";
    // A container service (StarterPack, ServerScriptService...) is a folder at the top.
    if (n->parent == root && n->kind == NodeKind::Model && Scene::isServiceName(n->name)) return n->name.c_str();
    switch (n->kind) {
        case NodeKind::Model:
            for (const Attribute& a : n->attributes)   // a Team, or an imported Roblox object we keep as a Model
                if (a.name == "RobloxClass" && a.type == Attribute::String) {
                    static thread_local std::string cls;
                    cls = a.s;
                    return cls.c_str();
                }
            return "Model";
        case NodeKind::Script: return n->isModule ? "ModuleScript" : n->isLocal ? "LocalScript" : "Script";
        case NodeKind::Remote: return n->remoteFunction ? "RemoteFunction" : "RemoteEvent";
        case NodeKind::Prompt: return "ProximityPrompt";
        case NodeKind::Highlight: return "Highlight";
        case NodeKind::PostEffect: return kPostEffectClassNames[(int)n->post.type];
        case NodeKind::Sky:       return "Sky";
        case NodeKind::MaterialVariant: return "MaterialVariant";
        case NodeKind::ClickDetector: return "ClickDetector";
        case NodeKind::Trail:     return "Trail";
        case NodeKind::Beam:      return "Beam";
        case NodeKind::Particles: return n->particleClass();
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
        case NodeKind::Camera:     return "Camera";
        case NodeKind::Sound:      return "Sound";
        case NodeKind::SoundGroup: return "SoundGroup";
        case NodeKind::SoundEffect: return SoundFx::kClassNames[(int)n->soundFx.type];
        case NodeKind::Attachment: return n->bone ? "Bone" : "Attachment";
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
    int r = Luau::load(L, source, chunk.c_str(), "t");   // (Luau extras compile too)
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

// A name the way people misspell it: "can_collide", "cancollide" and "CanCollide" are the same.
std::string canon(const std::string& s) {
    std::string out;
    for (char c : s) if (c != '_' && c != ' ' && c != '-') out += (char)std::tolower((unsigned char)c);
    return out;
}

// Every service an AI can name, in Roblox's Explorer order. Workspace, Players and
// Lighting are built in; the rest are folders at the top of the game, made the first
// time something goes in them (Scene::serviceNames).
const std::vector<std::string>& serviceList() {
    static const std::vector<std::string> list = {"Workspace", "Players", "Lighting", "ReplicatedFirst", "ReplicatedStorage",
        "ServerScriptService", "ServerStorage", "StarterGui", "StarterPack", "StarterPlayer", "Teams", "SoundService",
        "Chat", "TextChatService", "MaterialService"};
    return list;
}
// Where objects can go (Players fills itself while the game runs; Lighting holds settings).
json containerChoices() {
    json out = json::array();
    for (const std::string& s : serviceList())
        if (s != "Players" && s != "Lighting") out.push_back(s);
    out.push_back("StarterPlayer.StarterPlayerScripts");
    out.push_back("StarterPlayer.StarterCharacterScripts");
    return out;
}
// "Lighting" / "game.Players" -> that name; "" for anything else.
std::string virtualService(std::string ref) {
    if (ref.rfind("game.", 0) == 0) ref = ref.substr(5);
    return ref == "Lighting" || ref == "Players" ? ref : std::string();
}

// Every property name scripts can read on some object (LuaInstance.cpp). get_object and
// set_property ask the engine which of them the object really has (__gb_aiProps), so
// the tools always say and accept the same names as Lua.
const char* const kLuaProperties[] = {
    "Name", "ClassName", "Parent", "Position", "Size", "Orientation", "Rotation", "CFrame", "Color", "Color3",
    "Material", "Transparency", "Anchored", "CanCollide", "CastShadow", "Shape", "Visible", "Locked", "Mass",
    "Velocity", "AssemblyLinearVelocity", "RotVelocity", "AssemblyAngularVelocity", "AssemblyMass", "Density",
    "Friction", "Elasticity", "Enabled", "Disabled", "Source", "Brightness", "Range", "Angle", "Face", "Texture",
    "SoundId", "Volume", "PlaybackSpeed", "Pitch", "Looped", "Playing", "TimePosition", "Value", "ToolTip",
    "CanBeDropped", "RequiresHandle", "Grip", "GripPos", "GripForward", "GripRight", "GripUp", "ActionText",
    "ObjectText", "KeyboardKeyCode", "HoldDuration", "MaxActivationDistance", "RequiresLineOfSight", "ClickablePrompt",
    "Style", "FillColor", "OutlineColor", "FillTransparency", "OutlineTransparency", "DepthMode", "Adornee",
    "Attachment0", "Attachment1", "Part0", "Part1", "WorldPosition", "Length", "FreeLength", "Stiffness", "Damping",
    "AngularVelocity", "MotorMaxTorque", "ActuatorType", "Thickness", "LightEmission", "LightInfluence", "FaceCamera",
    "TextureMode", "TextureLength", "TextureSpeed", "Lifetime", "MinLength", "MaxLength", "WidthScale", "Width0",
    "Width1", "CurveSize0", "CurveSize1", "Segments", "ZOffset", "Viscosity", "SurfaceTension", "Rate", "FluidSystem",
    "FlowVelocity", "Clarity", "Drag", "WaveScale", "Gravity", "DeathStyle", "Gore", "FallDamage", "SafeFallSpeed",
    "FallDamageScale", "PlayerCollisions", "VoiceChatEnabled", "Orthographic", "OrthographicSize", "BloodColor",
    "BloodAmount", "MaxFluidParticles", "ClockTime", "GlobalShadows", "FogEnabled", "FogDensity", "FogColor",
    "Ambient", "SunColor", "Weather", "WeatherIntensity", "Text", "TextColor3", "TextSize", "TextScaled", "TextWrapped", "TextTransparency",
    "TextStrokeColor3", "TextStrokeTransparency", "TextXAlignment", "TextYAlignment", "TextEditable", "Font", "FontSize",
    "PlaceholderText", "PlaceholderColor3", "ClearTextOnFocus", "MultiLine", "ContentText", "BackgroundColor3",
    "BackgroundTransparency", "BorderColor3", "BorderSizePixel", "AnchorPoint", "ZIndex", "LayoutOrder", "Active",
    "AutoButtonColor", "ClipsDescendants", "Selectable", "Rotation", "Image", "ImageColor3", "ImageTransparency",
    "ScaleType", "CornerRadius", "ApplyStrokeMode", "LineJoinMode", "Blur", "CanvasSize", "CanvasPosition",
    "AutomaticCanvasSize", "ScrollBarThickness", "ScrollBarImageColor3", "ScrollBarImageTransparency",
    "ScrollingDirection", "ScrollingEnabled", "FillDirection", "HorizontalAlignment", "VerticalAlignment", "SortOrder",
    "Padding", "CellPadding", "CellSize", "StartCorner", "FillDirectionMaxCells", "DisplayOrder", "IgnoreGuiInset",
    "ResetOnSpawn", "ZIndexBehavior", "AlwaysOnTop", "MaxDistance", "StudsOffset", "StudsOffsetWorldSpace",
    "ExtentsOffset", "ExtentsOffsetWorldSpace", "SizeOffset", "PixelsPerStud", "SizingMode", "PlayerToHideFrom",
    "Force", "MaxForce", "MaxTorque", "MaxVelocity", "MaxAngularVelocity", "Torque", "VectorVelocity", "P", "D",
    "Responsiveness", "RigidityEnabled", "ReactionForceEnabled", "ReactionTorqueEnabled", "ApplyAtCenterOfMass",
    "RelativeTo", "Mode", "AlignType", "PrimaryAxisOnly", "ForceLimitMode", "ForceLimitsEnabled",
    "VelocityConstraintMode", "Location", "AnimationId", "Priority", "Occupant", "Disabled", "PrimaryPart", "BreakForce", "Broken",
    "MaterialVariant", "BaseMaterial", "ColorMap", "NormalMap", "RoughnessMap", "MetalnessMap", "StudsPerTile",
    "Roughness", "Metalness", "Glow",
};

// The names as a Lua table, for __gb_aiProps.
const std::string& luaPropertyTable() {
    static const std::string t = [] {
        std::string s = "{";
        for (const char* n : kLuaProperties) { s += '"'; s += n; s += "\","; }
        return s + "}";
    }();
    return t;
}

// Enum-like properties and the values they take (Enum.Material.Neon or "Neon").
const std::vector<std::string>* enumChoices(const std::string& property) {
    static const std::vector<std::string> materials = [] {
        std::vector<std::string> v(std::begin(kMaterialNames), std::end(kMaterialNames));
        for (const char* alias : {"SmoothPlastic", "DiamondPlate", "Foil", "CorrodedMetal", "WoodPlanks"}) v.push_back(alias);
        return v;
    }();
    static const std::vector<std::string> shapes = {"Block", "Ball", "Cylinder", "Plane"};
    static const std::vector<std::string> faces(std::begin(kFaceNames), std::end(kFaceNames));
    static const std::vector<std::string> depth = {"AlwaysOnTop", "Occluded"};
    static const std::vector<std::string> priority = {"Core", "Idle", "Movement", "Action"};
    if (property == "Material") return &materials;
    if (property == "Shape") return &shapes;
    if (property == "Face") return &faces;
    if (property == "DepthMode") return &depth;
    if (property == "Priority") return &priority;
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------

SceneNode* Editor::aiFind(const std::string& ref, bool create) {
    if (ref.empty()) return nullptr;
    if (ref[0] == '#') {
        try { return m_scene->findById(std::stoull(ref.substr(1))); } catch (...) { return nullptr; }
    }
    if (ref.find_first_not_of("0123456789") == std::string::npos) {   // "42" = "#42"
        try { return m_scene->findById(std::stoull(ref)); } catch (...) { return nullptr; }
    }
    std::vector<std::string> parts;
    std::stringstream ss(ref);
    for (std::string p; std::getline(ss, p, '.');) parts.push_back(p);
    size_t i = 0;
    if (i < parts.size() && parts[i] == "game") ++i;
    SceneNode* const root = m_scene->root();
    SceneNode* n = root;
    if (i < parts.size()) {
        const std::string& top = parts[i];
        if (top == "Workspace" || top == "workspace") ++i;
        else if (Scene::isServiceName(top)) {
            // StarterPack, ServerScriptService...: a folder made the first time something goes in.
            n = m_scene->serviceFolder(top, create);
            if (!n) return nullptr;
            ++i;
            if (top == "StarterPlayer" && i < parts.size()) {
                if (parts[i] == "StarterCharacterScripts") {
                    // Guts&Bolts' StarterCharacterScripts is the character itself: what's in it
                    // is in every player's character (like the Animate script).
                    Player* p = m_scene->player();
                    n = p ? p->root() : nullptr;
                    if (!n) return nullptr;
                    ++i;
                } else if (parts[i] == "StarterPlayerScripts" && !n->findChild(parts[i]) && create) {
                    auto f = std::make_unique<SceneNode>("StarterPlayerScripts", NodeKind::Model);
                    f->visible = false;
                    n = m_scene->insert(std::move(f), n);
                    ++i;
                }
            }
        }
    }
    for (; i < parts.size(); ++i) {
        SceneNode* c = n->findChild(parts[i]);
        if (!c) return nullptr;
        n = c;
    }
    return n;
}

AiToolResult Editor::runAiTool(const std::string& name, const json& args) {
    SceneNode* const root = m_scene->root();
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto label = [&](const SceneNode* n) { return n->name + " (" + classOf(n, root) + ") " + idOf(n); };
    // Where an object is, the way scripts reach it: "Workspace.Castle.Door", "StarterPack.Sword",
    // "ServerScriptService.Main" (service folders are their own top-level services).
    auto pathOf = [&](const SceneNode* n) {
        if (n == root) return std::string("Workspace");
        std::string p = n->name;
        const SceneNode* top = n;
        for (const SceneNode* a = n->parent; a && a != root; a = a->parent) { p = a->name + "." + p; top = a; }
        return m_scene->isServiceFolder(top) ? p : "Workspace." + p;
    };
    auto brief = [&](const SceneNode* n) { return json{{"object_id", idOf(n)}, {"name", n->name}, {"class", classOf(n, root)}}; };

    // A short picture of an object's state: what an AI needs to check its work. The keys
    // are the property names scripts use (Position, CanCollide...), so set_property takes them back.
    auto state = [&](const SceneNode* n) {
        json s = brief(n);
        s["path"] = pathOf(n);
        if (n->parent) s["parent_id"] = idOf(n->parent);
        if (isPartLike(n)) {
            s["Position"] = vec(glm::vec3(n->worldMatrix()[3]));
            s["Size"] = vec(n->transform.scale);
            s["Orientation"] = vec(n->transform.rotation);
            s["Color"] = vec(n->color);
            s["Material"] = kMaterialNames[(int)n->material];
            s["Transparency"] = n->transparency;
            s["Anchored"] = n->anchored;
            s["CanCollide"] = n->canCollide;
            if (n->collisionGroup != "Default") s["CollisionGroup"] = n->collisionGroup;
        } else if (n->isScript()) {
            s["Enabled"] = n->enabled;
            s["lines"] = (int)std::count(n->source.begin(), n->source.end(), '\n') + (n->source.empty() ? 0 : 1);
        } else if (n != root && !m_scene->isServiceFolder(n)) {
            s["Enabled"] = n->enabled;
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
    // A failure with the names that would have worked (error.valid_choices).
    auto failWith = [&](const char* code, const std::string& message, const std::string& suggestion, json choices) {
        AiToolResult r = fail(code, message, suggestion);
        r.data["error"]["valid_choices"] = std::move(choices);
        r.text = r.data.dump(1);
        return r;
    };
    auto notFound = [&](const char* key, const std::string& ref) {
        if (ref.empty()) return fail("INVALID_INPUT", std::string("`") + key + "` is missing.", "Give an object_id like \"#42\" (from get_game_tree or find_objects).");
        if (ref[0] != '#' && ref.find_first_not_of("0123456789") != std::string::npos) {
            // A path: is its first name a service or something in the Workspace?
            std::string top = ref.rfind("game.", 0) == 0 ? ref.substr(5) : ref;
            top = top.substr(0, top.find('.'));
            const bool known = top == "Workspace" || top == "workspace" || Scene::isServiceName(top) || root->findChild(top);
            if (!known)
                return failWith("SERVICE_NOT_FOUND", "\"" + top + "\" isn't a service, and nothing in the Workspace is called that.",
                                "Start the path with a service (see valid_choices) or Workspace, or use an object_id.",
                                json(serviceList()));
            if (top == "StarterPlayer" && ref.find(".StarterCharacterScripts") != std::string::npos)
                return fail("OBJECT_NOT_FOUND", "StarterCharacterScripts is the character, and there's no character in this game.",
                            "Put the script in StarterPlayer.StarterPlayerScripts, or in a character Model.");
            if (Scene::isServiceName(top) && !m_scene->serviceFolder(top, false))
                return fail("OBJECT_NOT_FOUND", top + " is empty, so there's no " + ref + ".",
                            "insert_object / create_script with parent \"" + top + "\" puts things there.");
        }
        return fail("OBJECT_NOT_FOUND", "No object " + ref + " exists.",
                    "Search with find_objects or get_game_tree and use the object_id it returns.");
    };
    // Find the object a ref names. A path is refused when a name on it is used by
    // more than one object (the wrong one could be changed): ask for an id instead.
    AiToolResult refErr;
    auto resolve = [&](const std::string& ref, const char* key, SceneNode*& out, bool create = false) -> bool {
        if (const std::string v = virtualService(ref); !v.empty()) {
            refErr = failWith("NOT_A_CONTAINER", v == "Lighting"
                                  ? "Lighting holds the sky, sun and fog settings, not objects."
                                  : "Players fills itself with the people in the game while it runs; nothing can be put in it.",
                              v == "Lighting" ? "Change its settings with set_property {object: \"Lighting\"}; put objects in one of valid_choices."
                                              : "Use one of valid_choices (StarterPlayer.StarterPlayerScripts for per-player LocalScripts).",
                              containerChoices());
            out = nullptr;
            return false;
        }
        out = aiFind(ref, create);
        if (!out) { refErr = notFound(key, ref); return false; }
        if (ref[0] == '#' || ref.find_first_not_of("0123456789") == std::string::npos) return true;
        for (const SceneNode* n = out; n && n->parent; n = n->parent) {
            if (m_scene->isCharacterRoot(n->id)) continue;   // StarterPlayer.StarterCharacterScripts (named on purpose)
            json same = json::array();
            for (auto& c : n->parent->children)
                if (c->name == n->name && !c->internal) same.push_back(json{{"object_id", idOf(c.get())}, {"path", pathOf(c.get())}});
            if (same.size() > 1) {
                refErr = fail("AMBIGUOUS_NAME", std::to_string(same.size()) + " objects are called \"" + n->name + "\" in " +
                              pathOf(n->parent) + ", so \"" + ref + "\" could mean more than one thing.",
                              "Use the object_id of the one you mean (see candidates).");
                refErr.data["error"]["candidates"] = same;
                refErr.text = refErr.data.dump(1);
                out = nullptr;
                return false;
            }
        }
        return true;
    };
    auto need = [&](const char* k, SceneNode*& out, bool create = false) -> bool { return resolve(str(k), k, out, create); };
    // An optional object argument: `fallback` when it's not given. `create`: a service named
    // here (StarterPack...) gets its folder now, because something is about to go in it.
    auto optional = [&](const char* k, SceneNode* fallback, SceneNode*& out, bool create = false) -> bool {
        if (!args.contains(k)) { out = fallback; return true; }
        return need(k, out, create);
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
    struct QuietRun { std::vector<std::string> lines, errors; };
    auto quietLua = [&](const std::string& code) {
        std::vector<Log::Entry> got;
        Log::setCapture(&got);
        runCommand(code);
        Log::setCapture(nullptr);
        QuietRun out;
        for (size_t i = 0; i < got.size(); ++i) {
            if (i == 0 && got[i].text.rfind("> ", 0) == 0) continue;   // the echo of the command
            (got[i].level == Log::Level::Error ? out.errors : out.lines).push_back(got[i].text);
        }
        return out;
    };
    // Every property `expr` (Lua: an object, or Lighting) has, by its Lua name, read from the engine.
    auto readProps = [&](const std::string& expr) {
        QuietRun r = quietLua("print(\"\\1GBPROPS\" .. __gb_aiProps(" + expr + ", " + luaPropertyTable() + "))");
        for (auto& l : r.lines)
            if (l.rfind("\1GBPROPS", 0) == 0) {
                json j = json::parse(l.substr(8), nullptr, false);
                if (j.is_object()) return j;
            }
        return json::object();
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
            if (n->isParticles() && (!n->parent || (!isPartLike(n->parent) && !n->parent->isAttachment())))
                add(n, "warning", "PARTICLES_NOT_IN_PART", std::string("The ") + classOf(n, root) +
                    " isn't inside a part or an Attachment, so no particles come out of it.",
                    "Move it into the part (or Attachment) the particles should come from.");
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
                json kinds = insertKinds();
                kinds.push_back("Team");
                d["classes"] = kinds;
                d["services"] = containerChoices();
                d["text"] = "Every kind insert_object can make (the names after the classes are ready-made things, each an "
                            "independent copy; Team goes in Teams by itself), and the services they can be put in.";
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
        int depth = std::max(0, args.value("depth", 4));
        std::string out;
        int shown = 0;
        bool truncated = false;
        // `skipServices`: the Workspace's own listing leaves out the service folders and,
        // outside playtests, the character (they're listed under their services below it,
        // like Studio's Explorer).
        auto elsewhere = [&](const SceneNode* c) {
            return m_scene->isServiceFolder(c) || (!m_playing && m_scene->isCharacterRoot(c->id));
        };
        std::function<void(SceneNode*, int, bool)> walk = [&](SceneNode* n, int d, bool skipServices) {
            if (n->internal) return;
            if (shown >= 1500) { truncated = true; return; }
            out += std::string((size_t)d * 2, ' ') + label(n) + "\n";
            ++shown;
            int kids = 0;
            for (auto& c : n->children) if (!c->internal && !(skipServices && elsewhere(c.get()))) ++kids;
            if (d >= depth) {
                if (kids) out += std::string((size_t)(d + 1) * 2, ' ') + "... " + std::to_string(kids) + " more inside\n";
                return;
            }
            for (auto& c : n->children)
                if (!(skipServices && elsewhere(c.get()))) walk(c.get(), d + 1, false);
        };
        if (args.contains("root")) {
            // One part of the game. An empty service (nothing put in it yet) is just empty.
            const std::string ref = str("root");
            std::string svc = ref.rfind("game.", 0) == 0 ? ref.substr(5) : ref;
            if (svc == "Lighting" || svc == "Players") {
                out = svc + " (" + svc + ")\n";
                return done({{"tree", out}, {"object_count", 0}, {"truncated", false},
                             {"note", svc == "Lighting" ? "Lighting holds settings: get_object {object: \"Lighting\"} reads them."
                                                        : "Players lists the people in the game while it runs (get_runtime_state)."}});
            }
            if (Scene::isServiceName(svc) && !m_scene->serviceFolder(svc, false))
                return done({{"tree", svc + " (" + svc + ")\n"}, {"object_count", 0}, {"truncated", false},
                             {"note", svc + " is empty. insert_object / create_script with parent \"" + svc + "\" puts things in it."}});
            SceneNode* top;
            if (!need("root", top)) return refErr;
            walk(top, 0, top == root);
            return done({{"object_id", idOf(top)}, {"path", pathOf(top)}, {"tree", out}, {"object_count", shown}, {"truncated", truncated}});
        }
        // The whole game: the Workspace, then every service.
        json services = json::array();
        walk(root, 0, true);
        services.push_back({{"name", "Workspace"}, {"class", "Workspace"}, {"object_id", idOf(root)}, {"can_insert", true}});
        for (const std::string& svc : serviceList()) {
            if (svc == "Workspace") continue;
            json info = {{"name", svc}, {"class", svc}, {"can_insert", svc != "Players" && svc != "Lighting"}};
            if (svc == "Players") {
                std::vector<std::string> who;
                if (m_playing) {
                    if (Player* p = m_scene->player(); p && p->root() && !m_session->runOnly()) who.push_back(p->root()->name);
                    for (auto& rc : m_scene->remotes()) who.push_back(rc.name);
                }
                out += "Players (Players)" + std::string(who.empty() ? "  [nobody: it fills while the game runs]" : "") + "\n";
                for (auto& w : who) out += "  " + w + " (Player)\n";
                info["players"] = who;
                services.push_back(info);
                continue;
            }
            if (svc == "Lighting") {
                out += "Lighting (Lighting)  [sky, sun and fog settings: get_object / set_property \"Lighting\"]\n";
                services.push_back(info);
                continue;
            }
            SceneNode* f = m_scene->serviceFolder(svc, false);
            if (f) {
                if (shown >= 1500) truncated = true;
                else walk(f, 0, false);
                info["object_id"] = idOf(f);
            } else {
                out += svc + " (" + svc + ")  [empty]\n";
                info["object_id"] = nullptr;
            }
            int kids = 0;
            if (f) for (auto& c : f->children) if (!c->internal) ++kids;
            info["child_count"] = kids;
            if (svc == "StarterPlayer") {
                Player* p = m_scene->player();
                SceneNode* ch = p ? p->root() : nullptr;
                if (ch && !m_playing) {
                    // The character everyone spawns as (it's in the Workspace while the game runs).
                    if (shown < 1500) walk(ch, 1, false);
                    out += "  StarterCharacterScripts -> the character " + label(ch) + ": scripts in it run in every player's character\n";
                }
                if (ch) info["starter_character"] = idOf(ch);
                if (!f || !f->findChild("StarterPlayerScripts"))
                    out += "  StarterPlayerScripts  [empty: LocalScripts here run for every player]\n";
            }
            services.push_back(info);
        }
        return done({{"object_id", idOf(root)}, {"tree", out}, {"object_count", shown}, {"truncated", truncated}, {"services", services},
                     {"note", "Paths start with a service: Workspace.Castle.Door, StarterPack.Sword, ServerScriptService.Main."}});
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
                if ((int)matches.size() < limit) { json b = brief(n); b["path"] = pathOf(n); matches.push_back(b); }
            }
            for (auto& c : n->children) walk(c.get());
        };
        walk(top);
        return done({{"matches", matches}, {"count", total}, {"truncated", total > (int)matches.size()}});
    }
    if (name == "insert_object") {
        std::string kind = str("kind");
        if (kind.empty()) return fail("INVALID_INPUT", "`kind` is missing.", "Give a class like Part, Model, Script or PointLight.");
        // The class: exactly, or the same name typed differently ("pointlight" -> PointLight).
        std::vector<std::string> kinds = insertKinds();
        kinds.push_back("Team");
        if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) {
            std::string fixed;
            for (const std::string& k : kinds) if (canon(k) == canon(kind)) fixed = k;
            if (fixed.empty()) {
                json similar = json::array();
                for (const std::string& k : kinds)
                    if (canon(k).find(canon(kind)) != std::string::npos || canon(kind).find(canon(k)) != std::string::npos) similar.push_back(k);
                AiToolResult r = failWith("UNKNOWN_CLASS", "\"" + kind + "\" isn't something Studio can insert.",
                                          similar.empty() ? "Use one of valid_choices (the same list as Studio's Insert Object window)."
                                                          : "Did you mean " + similar[0].get<std::string>() + "? valid_choices has every kind.",
                                          json(kinds));
                if (!similar.empty()) { r.data["error"]["did_you_mean"] = similar; r.text = r.data.dump(1); }
                return r;
            }
            kind = fixed;
        }
        SceneNode* parent;
        if (!optional("parent", nullptr, parent, true)) return refErr;
        const bool script = kind == "Script" || kind == "LocalScript" || kind == "ModuleScript";
        json warn = json::array();
        // StarterPlayer.StarterCharacterScripts is the character: scripts go straight in
        // (Studio's Insert Object keeps other things out of the character).
        if (parent && m_scene->isCharacterRoot(parent->id) && script) {
            AiToolResult made = runAiTool("create_script", {{"parent", idOf(parent)}, {"type", kind},
                {"name", str("name").empty() ? kind : str("name")},
                {"source", "-- In the character: script.Parent is the character it runs in.\n"}});
            made.data["operation"] = name;
            if (!made.error) made.data["note"] = "In the character (StarterCharacterScripts): it runs in every player's character.";
            made.text = made.data.dump(1);
            return made;
        }
        std::vector<uint64_t> before;
        m_scene->forEach([&](SceneNode* n) { before.push_back(n->id); });
        std::sort(before.begin(), before.end());
        std::vector<Log::Entry> said;   // what Insert Object said (e.g. "Select a part first")
        Log::setCapture(&said);
        insertObject(kind, parent);
        Log::setCapture(nullptr);
        for (auto& e : said) if (e.level != Log::Level::Info) warn.push_back(e.text);
        SceneNode* made = nullptr;
        json affected = json::array();
        m_scene->forEach([&](SceneNode* n) {
            if (std::binary_search(before.begin(), before.end(), n->id) || n->internal) return;
            if (!made) made = n;
            affected.push_back(idOf(n));
        });
        // Ready-made things come with parts inside: report the top one (not a service
        // folder made on the way, like Teams or StarterGui).
        while (made && made->parent && made->parent != root && !m_scene->isServiceFolder(made->parent) &&
               !std::binary_search(before.begin(), before.end(), made->parent->id)) made = made->parent;
        if (made && m_scene->isServiceFolder(made) && made->children.size() == 1) made = made->children.front().get();
        if (!made) {
            std::string why = warn.empty() ? "nothing was made" : warn.front().get<std::string>();
            return fail("COULD_NOT_INSERT", "Couldn't insert a " + kind + ": " + why,
                        "Give a parent it can go in (e.g. a part for lights, prompts and decals).");
        }
        if (!str("name").empty()) made->name = str("name");
        json d = {{"object_id", idOf(made)}, {"name", made->name}, {"class", classOf(made, root)}, {"path", pathOf(made)},
                  {"affected_ids", affected}, {"resulting_state", state(made)}};
        if (made->parent) d["parent_id"] = idOf(made->parent);
        if (parent && made->parent != parent) {
            if (m_scene->isCharacterPart(parent) || parent->isScript())
                warn.push_back("It couldn't go inside " + label(parent) + " (scripts and character parts can't hold "
                               "objects; only scripts go in StarterCharacterScripts), so it went in " +
                               (made->parent ? label(made->parent) : std::string("Workspace")) + ".");
            else
                warn.push_back("A " + kind + " always goes in " + (made->parent ? pathOf(made->parent) : std::string("Workspace")) + ".");
        }
        if (parent && parent->name == "StarterPlayerScripts" && kind == "Script")
            warn.push_back("A Script in StarterPlayerScripts runs once on the server; LocalScripts are what normally go here.");
        if (!warn.empty()) d["warnings"] = warn;
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
        // Lighting is a service of settings (no object_id): its properties, like a script sees them.
        if (const std::string v = virtualService(str("object")); !v.empty()) {
            if (v == "Players") {
                json who = json::array();
                if (m_playing) {
                    if (Player* p = m_scene->player(); p && p->root() && !m_session->runOnly()) who.push_back(brief(p->root()));
                    for (auto& rc : m_scene->remotes()) who.push_back({{"name", rc.name}, {"object_id", "#" + std::to_string(rc.rootId)}});
                }
                return done({{"name", "Players"}, {"class", "Players"}, {"path", "Players"}, {"players", who},
                             {"note", "Players fills itself while the game runs; it can't be changed."}});
            }
            return done({{"name", "Lighting"}, {"class", "Lighting"}, {"path", "Lighting"}, {"properties", readProps("Lighting")},
                         {"note", "Change these with set_property {object: \"Lighting\", property, value}."}});
        }
        SceneNode* n;
        if (!need("object", n)) return refErr;
        json saved = json::parse(Serializer::nodeToString(*n), nullptr, false);
        if (saved.is_object()) {
            saved.erase("children");
            saved.erase("mesh");            // big model data isn't useful here
            saved.erase("editMesh");
            saved.erase("id");
            saved.erase("source");          // read_script has it
        }
        if (saved.dump().size() > 60000) saved = {{"note", "too big to show"}};
        json kids = json::array();
        for (auto& c : n->children) if (!c->internal) kids.push_back(brief(c.get()));
        json d = state(n);
        d["children"] = kids;
        // The same names and values a script reads (part.CanCollide, gui.BackgroundColor3...):
        // set_property takes exactly these names back.
        json props = readProps("__gb_byId(" + std::to_string(n->id) + ")");
        if (n->isScript()) props.erase("Source");   // read_script has it
        d["properties"] = props;
        json attrs = json::object();
        for (const Attribute& a : n->attributes) {
            switch (a.type) {
                case Attribute::Bool:    attrs[a.name] = a.b; break;
                case Attribute::String:  attrs[a.name] = a.s; break;
                case Attribute::Vector3:
                case Attribute::Color3:  attrs[a.name] = vec(a.v); break;
                default:                 attrs[a.name] = a.n; break;
            }
        }
        if (!attrs.empty()) d["attributes"] = attrs;
        if (!n->tags.empty()) d["tags"] = n->tags;
        d["saved_fields"] = saved;   // how the game file stores it (internal names; for reference)
        d["state_source"] = m_playing ? "runtime (playtest running)" : "editor (saved game)";
        if (m_playing && isPartLike(n))
            d["runtime"] = {{"velocity", vec(n->velocity)}, {"angular_velocity", vec(n->angularVelocity)},
                            {"speed", glm::length(n->velocity)}, {"resting", n->sleepTime > 0.5f}};
        return done(d);
    }
    if (name == "set_property") {
        std::string prop = str("property");
        if (prop.empty() || prop.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
            return fail("INVALID_INPUT", "\"" + prop + "\" isn't a property name.", "Use a property name like Position, Color or Anchored.");
        if (!args.contains("value")) return fail("INVALID_INPUT", "`value` is missing.", "Give the new value (number, true/false, text or [x, y, z]).");
        json value = args["value"];
        // Which object: one by id / path, or the Lighting service.
        const bool lighting = virtualService(str("object")) == "Lighting";
        SceneNode* n = nullptr;
        if (!lighting) {
            if (virtualService(str("object")) == "Players")
                return fail("NOT_SETTABLE", "Players has no properties to change here.", "Change a player's character during a playtest, or StarterPlayer.");
            if (!need("object", n)) return refErr;
        }
        const uint64_t id = n ? n->id : 0;
        const std::string expr = lighting ? std::string("Lighting") : "__gb_byId(" + std::to_string(id) + ")";
        const std::string who = lighting ? std::string("Lighting") : label(n);
        // A script's code goes through edit_script (it checks the Lua and keeps the editor in step).
        if (n && n->isScript() && canon(prop) == "source") {
            if (!value.is_string()) return fail("INVALID_INPUT", "Source must be text (the whole Lua code).", "Use edit_script {object, source} or {object, old, new}.");
            AiToolResult r = runAiTool("edit_script", {{"object", idOf(n)}, {"source", value}});
            r.data["operation"] = name;
            r.data["routed_to"] = "edit_script";
            r.data["note"] = "Script.Source is changed with edit_script; this call was passed on to it. Next time use edit_script "
                             "(old/new swaps a small part without rewriting the whole script).";
            r.text = r.data.dump(1);
            return r;
        }
        // The property's real (Lua) name: "can_collide", "cancollide" and "CanCollide" all mean CanCollide.
        json have = readProps(expr);
        json aliasNote;
        if (!have.contains(prop)) {
            std::string fixed;
            for (auto& [k, v] : have.items()) if (canon(k) == canon(prop)) fixed = k;
            if (fixed.empty() && canon(prop) == "color" && have.contains("Color3")) fixed = "Color3";      // a Decal's colour
            if (fixed.empty() && canon(prop) == "rotation" && have.contains("Orientation")) fixed = "Orientation";
            if (!fixed.empty()) { aliasNote = prop + " is called " + fixed + " (the name scripts use)."; prop = fixed; }
        }
        // Enum values: Enum.Material.Neon, "Neon" and "neon" are the same; anything else lists the choices.
        if (value.is_string()) {
            std::string sv = value.get<std::string>();
            if (sv.rfind("Enum.", 0) == 0 && sv.rfind('.') != std::string::npos) sv = sv.substr(sv.rfind('.') + 1);
            if (const std::vector<std::string>* choices = enumChoices(prop); choices && (have.contains(prop) || n == nullptr)) {
                std::string fixed;
                for (const std::string& c : *choices) if (canon(c) == canon(sv)) fixed = c;
                if (fixed.empty())
                    return failWith("INVALID_ENUM_VALUE", "\"" + value.get<std::string>() + "\" isn't a " + prop + ".",
                                    "Use one of valid_choices.", json(*choices));
                sv = fixed;
            }
            value = sv;
        }
        std::string v = luaValue(value, prop);
        // Objects as values: Parent, Adornee, Part0, Attachment0... take an object ("#42" or a path).
        static const char* refProps[] = {"Parent", "Adornee", "Part0", "Part1", "Attachment0", "Attachment1", "FluidSystem",
                                         "PrimaryPart", "PlayerToHideFrom"};
        const bool refProp = std::find_if(std::begin(refProps), std::end(refProps), [&](const char* r) { return prop == r; }) != std::end(refProps);
        if (value.is_string() && (refProp || (have.contains(prop) && have[prop].is_string() &&
                                              have[prop].get<std::string>().rfind('#', 0) == 0))) {
            SceneNode* p;
            if (!resolve(value.get<std::string>(), "value", p, prop == "Parent")) return refErr;
            v = "__gb_byId(" + std::to_string(p->id) + ")";
        }
        // Lists: [x, y] Vector2, [[xs, xo], [ys, yo]] or [xs, xo, ys, yo] UDim2 (UI sizes and positions).
        if (value.is_array()) {
            auto num = [](const json& j) { return j.is_number(); };
            std::ostringstream o;
            if (value.size() == 2 && value[0].is_array() && value[1].is_array() && value[0].size() == 2 && value[1].size() == 2) {
                o << "UDim2.new(" << value[0][0].get<double>() << ", " << value[0][1].get<double>() << ", "
                  << value[1][0].get<double>() << ", " << value[1][1].get<double>() << ")";
                v = o.str();
            } else if (value.size() == 4 && std::all_of(value.begin(), value.end(), num)) {
                o << "UDim2.new(" << value[0].get<double>() << ", " << value[1].get<double>() << ", " << value[2].get<double>()
                  << ", " << value[3].get<double>() << ")";
                v = o.str();
            } else if (value.size() == 2 && std::all_of(value.begin(), value.end(), num)) {
                o << "Vector2.new(" << value[0].get<double>() << ", " << value[1].get<double>() << ")";
                v = o.str();
            }
        } else if (value.is_object() && value.contains("position") && value["position"].is_array() && value["position"].size() == 3) {
            const json& pp = value["position"];
            std::ostringstream o;
            o << "CFrame.new(" << pp[0].get<double>() << ", " << pp[1].get<double>() << ", " << pp[2].get<double>() << ")";
            v = o.str();
        }
        QuietRun r = quietLua("local o = " + expr + "\no." + prop + " = " + v);
        if (!r.errors.empty()) {
            const std::string& e = r.errors.front();
            const bool unknown = !have.contains(prop) &&
                (e.find("can't be set") != std::string::npos || e.find("not a valid member") != std::string::npos);
            json choices = json::array();
            for (auto& [k, val] : have.items()) if (k != "ClassName") choices.push_back(k);
            if (unknown)
                return failWith("UNKNOWN_PROPERTY", who + " has no property \"" + prop + "\".",
                                "Use one of valid_choices (the names scripts use; get_object shows their values).", choices);
            return failWith("LUA_ERROR", "Couldn't set " + prop + " of " + who + ": " + e,
                            "Check the value's type with get_object (Color takes [r, g, b] 0-1, Position [x, y, z]).", choices);
        }
        json now = readProps(expr);
        if (!lighting) {
            n = m_scene->findById(id);
            if (!n) return fail("OBJECT_NOT_FOUND", "The object disappeared after the change.", "Check get_game_tree.", false);
        }
        json d = {{"changed_properties", {{prop, {{"requested", args["value"]}, {"now", now.contains(prop) ? now[prop] : json()}}}}}};
        if (n) { d["object_id"] = idOf(n); d["resulting_state"] = state(n); }
        else d["object"] = "Lighting";
        if (!aliasNote.is_null()) d["warnings"] = json::array({aliasNote});
        return done(d);
    }
    if (name == "find_and_change") {
        // Studio's Find and Change window does the work (EditorBulkEdit.cpp); the window keeps its own search.
        const BulkQuery keep = m_bulk;
        auto text = [&](const json& j, const char* k) {
            if (!j.is_object() || !j.contains(k)) return std::string();
            return j[k].is_string() ? j[k].get<std::string>() : j[k].dump();
        };
        const json where = args.value("where", json::object()), change = args.value("change", json::object());
        m_bulk = BulkQuery{};
        m_bulk.cls = str("kind");
        m_bulk.nameHas = str("name_contains");
        m_bulk.tag = str("has_tag");
        m_bulk.notTag = str("not_tag");
        m_bulk.whereProp = text(where, "property");
        m_bulk.whereOp = text(where, "op").empty() ? "=" : text(where, "op");
        m_bulk.whereVal = text(where, "value");
        m_bulk.setProp = text(change, "property");
        m_bulk.op = text(change, "op").empty() ? "Set to" : text(change, "op");
        m_bulk.value = text(change, "value");
        m_bulk.inSelection = args.value("inside_selection", false);
        const bool apply = args.value("apply", false);
        static const char* ops[] = {"Set to", "Add", "Multiply by", "Add tag", "Remove tag"};
        const bool okOp = std::find_if(std::begin(ops), std::end(ops), [&](const char* o) { return m_bulk.op == o; }) != std::end(ops);
        if (!okOp) { m_bulk = keep; return failWith("INVALID_INPUT", "change.op \"" + text(change, "op") + "\" isn't an operation.",
                                                    "Use one of valid_choices.", json(std::vector<std::string>(std::begin(ops), std::end(ops)))); }
        if (apply && m_playing) { m_bulk = keep; return fail("PLAYTEST_RUNNING", "Can't change the saved game during a playtest.", "playtest stop first."); }
        bulkRun(apply);
        json results = json::array();
        int bad = 0;
        for (const BulkRow& r : m_bulkRows) {
            bad += !r.error.empty();
            if (results.size() >= 200) continue;
            SceneNode* n = m_scene->findById(r.id);
            json row = {{"object_id", "#" + std::to_string(r.id)}, {"name", n ? n->name : std::string()}, {"now", r.old}};
            if (!m_bulk.setProp.empty() || m_bulk.op == "Add tag" || m_bulk.op == "Remove tag") row["becomes"] = r.now;
            if (!r.error.empty()) row["error"] = r.error;
            results.push_back(row);
        }
        json d = {{"count", m_bulkRows.size()}, {"results", results}, {"applied", apply}};
        if (apply) d["changed"] = (int)m_bulkRows.size() - bad;
        if (!m_bulkStatus.empty() && m_bulkRows.empty()) d["note"] = m_bulkStatus;
        if (apply) trackChanges();   // one undo step
        m_bulk = keep;
        return done(d);
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
        if (!optional("parent", root, parent, true)) return refErr;
        const std::string type = str("type").empty() ? "Script" : str("type");
        if (type != "Script" && type != "LocalScript" && type != "ModuleScript")
            return fail("INVALID_INPUT", "type must be Script, LocalScript or ModuleScript.", "Leave it out for a normal Script.");
        auto s = std::make_unique<SceneNode>(str("name").empty() ? type : str("name"), NodeKind::Script);
        s->source = str("source");
        s->isModule = type == "ModuleScript";
        s->isLocal = type == "LocalScript";
        SceneNode* made = m_scene->insert(std::move(s), parent);
        json d = {{"object_id", idOf(made)}, {"name", made->name}, {"class", classOf(made, root)}, {"parent_id", idOf(parent)},
                  {"path", pathOf(made)}, {"syntax", syntaxOf(made->source, made->name)}, {"resulting_state", state(made)}};
        if (!d["syntax"]["ok"].get<bool>()) d["warnings"] = json::array({"The script has a syntax error and won't run until it's fixed."});
        return done(d);
    }
    if (name == "visual_script") {
        const std::string action = str("action");
        if (action == "blocks") {
            json list = json::array();
            for (const auto& b : VisualScript::blocks()) {
                json fields = json::array();
                const char* kinds[] = {"text", "number", "color (r, g, b)", "target", "choice"};
                for (const auto& f : b.fields) fields.push_back({{"key", f.key}, {"label", f.label}, {"kind", kinds[f.kind]}, {"default", f.def}});
                list.push_back({{"type", b.type}, {"category", b.category}, {"title", b.title}, {"tip", b.tip},
                                {"fields", fields}, {"outs", b.outs}, {"event", b.event}});
            }
            return done({{"blocks", list}});
        }
        if (action == "read") {
            SceneNode* n;
            if (!need("object", n)) return refErr;
            if (!n->isScript() || !VisualScript::isBlocks(n->source))
                return fail("NOT_BLOCKS", label(n) + " isn't a script made with blocks.", "Use read_script for normal scripts.");
            bool edited = false;
            json g = VisualScript::graphOf(n->source, &edited);
            std::vector<std::string> problems;
            const std::string lua = VisualScript::toLua(g, problems);
            return done({{"object_id", idOf(n)}, {"graph", g}, {"lua", lua}, {"problems", problems}, {"edited", edited}});
        }
        if (action != "write") return fail("INVALID_INPUT", "action must be blocks, write or read.", "Start with blocks to see them.");
        if (!args.contains("graph") || !args["graph"].is_object())
            return fail("INVALID_INPUT", "Give `graph`: {nodes: [...], links: [...]}.", "visual_script blocks lists the block types.");
        const json& g = args["graph"];
        if (!g.contains("nodes") || !g["nodes"].is_array())
            return fail("INVALID_INPUT", "graph.nodes must be a list.", "Each node: {id, type, x, y, f}.");
        for (const auto& node : g["nodes"]) {
            const std::string t = node.is_object() ? node.value("type", std::string()) : std::string();
            if (!VisualScript::find(t))
                return fail("UNKNOWN_BLOCK", "There's no block called \"" + t + "\".", "visual_script blocks lists them.");
            if (!node.contains("id") || !node["id"].is_number_integer())
                return fail("INVALID_INPUT", "Every node needs a whole-number id.", "Like {\"id\": 1, \"type\": \"start\"}.");
        }
        SceneNode* n = nullptr;
        if (!str("object").empty()) {
            if (!need("object", n)) return refErr;
            if (!n->isScript() || !VisualScript::isBlocks(n->source))
                return fail("NOT_BLOCKS", label(n) + " isn't a script made with blocks.", "Leave out object to make a new block script.");
        } else {
            SceneNode* parent;
            if (!optional("parent", root, parent, true)) return refErr;
            auto s = std::make_unique<SceneNode>(str("name").empty() ? "BlockScript" : str("name"), NodeKind::Script);
            n = m_scene->insert(std::move(s), parent);
        }
        json keep = {{"nodes", g["nodes"]}, {"links", g.contains("links") && g["links"].is_array() ? g["links"] : json::array()}};
        std::vector<std::string> problems;
        n->source = VisualScript::scriptSource(keep, problems);
        std::vector<std::string> again;
        json d = {{"object_id", idOf(n)}, {"path", pathOf(n)}, {"lua", VisualScript::toLua(keep, again)}, {"problems", problems},
                  {"syntax", syntaxOf(n->source, n->name)}};
        if (m_playing) d["warnings"] = json::array({"A playtest is running: it keeps using the old code until it is stopped and started again."});
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
            AiToolResult res = fail("LUA_ERROR", r.errors.front(), "Fix the code and run it again (Lua 5.4 or Luau, Roblox-style API).");
            res.data["output"] = r.lines;
            res.data["errors"] = r.errors;
            res.text = res.data.dump(1);
            return res;
        }
        return done(d);
    }

    // ---------------------------------------------------------------- RUNTIME
    if (name == "script_history") {
        const std::string action = str("action").empty() ? "read" : str("action");
        if (action == "record") ScriptHistory::setRecording(true);
        else if (action == "stop") ScriptHistory::setRecording(false);
        else if (action == "clear") ScriptHistory::clear();
        else if (action != "read") return fail("INVALID_INPUT", "action must be read, record, stop or clear.", "");
        uint64_t only = 0;
        if (!str("object").empty()) {
            SceneNode* n = nullptr;
            if (!need("object", n)) return refErr;
            only = n->id;
        }
        std::string script = str("script"), kind = str("kind");
        const int limit = std::clamp(args.value("limit", 50), 1, 500);
        const auto& all = ScriptHistory::entries();
        json list = json::array();
        for (auto it = all.rbegin(); it != all.rend() && (int)list.size() < limit; ++it) {
            const ScriptHistory::Entry& e = *it;
            const bool set = e.kind == ScriptHistory::Entry::Set;
            if (only && e.object != only) continue;
            if (!script.empty() && e.scriptName.find(script) == std::string::npos) continue;
            if ((kind == "set" && !set) || (kind == "event" && set)) continue;
            json j = {{"time", e.time}, {"kind", set ? "set" : "event"}, {"object", e.objectName}, {"script", e.scriptName},
                      {"what", e.what}};
            if (e.object) j["object_id"] = "#" + std::to_string(e.object);
            if (e.script) j["script_id"] = "#" + std::to_string(e.script);
            if (set) { j["line"] = e.line; j["before"] = e.before; j["after"] = e.after; }
            list.push_back(j);
        }
        return done({{"recording", ScriptHistory::recording()}, {"total", all.size()}, {"entries", list}});
    }
    if (name == "compare_scenes") {
        const std::string action = str("action");
        // A saved game's name (in the games folder) or a path.
        auto gamePath = [&](const std::string& s) {
            if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos || s.find('.') != std::string::npos) return s;
            return (Paths::gamesFolder() / (s + Paths::kExtension)).string();
        };
        if (action == "save_copy") {
            std::string n = str("name");
            n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return c == '/' || c == '\\' || c == ':' || c == '.'; }), n.end());
            if (n.empty()) return fail("INVALID_INPUT", "Give the copy a name.", "name: \"Before the bridge\"");
            const std::string path = gamePath(n);
            if (!Serializer::writeFile(path, Serializer::saveScene(*m_scene, true)))
                return fail("WRITE_FAILED", "Couldn't write " + path + ".", "");
            return done({{"path", path}});
        }
        if (action == "compare") {
            if (str("other").empty()) return fail("INVALID_INPUT", "other is needed: a saved game's name or a file path.", "");
            const std::string base = str("base").empty() ? std::string() : gamePath(str("base"));
            json d = compareWith(gamePath(str("other")), base.empty() ? nullptr : &base);
            if (d.contains("error")) return fail("CANT_COMPARE", d["error"].get<std::string>(), "Check the name or path.");
            m_showCompare = true;
            m_compareOther = gamePath(str("other"));
            m_compareUseBase = !base.empty();
            m_compareBase = base;
            return done(d);
        }
        if (action == "merge") {
            if (m_playing) return fail("PLAYING", "Stop the playtest first.", "playtest stop.");
            if (m_compare.changes.empty()) return fail("NOTHING_TO_MERGE", "There are no differences to bring in.", "compare_scenes compare first.");
            std::vector<int> chosen;
            if (args.contains("changes") && args["changes"].is_array()) {
                for (const auto& i : args["changes"])
                    if (i.is_number_integer()) {
                        if (i.get<int>() < 0 || i.get<int>() >= (int)m_compare.changes.size())
                            return fail("INVALID_INPUT", "There's no change " + i.dump() + ".", "Use the indexes compare gave.");
                        chosen.push_back(i.get<int>());
                    }
            } else {
                for (size_t i = 0; i < m_compare.changes.size(); ++i) if (!m_compare.changes[i].conflict) chosen.push_back((int)i);
            }
            return done(mergeChosen(chosen));
        }
        return fail("INVALID_INPUT", "action must be save_copy, compare or merge.", "");
    }
    if (name == "physics_replay") {
        const std::string action = str("action");
        auto frameArg = [&]() {
            if (args.contains("frame")) return args.value("frame", 0);
            return (int)std::lround(args.value("time", m_replay.seconds()) / m_replay.step);
        };
        auto testList = [&](std::vector<SceneNode*> tests) {
            json list = json::array();
            for (SceneNode* t : tests) {
                PhysicsReplay::Test pt;
                const bool ok = PhysicsReplay::testFromJson(json::parse(t->value.s, nullptr, false), pt);
                list.push_back({{"name", t->name}, {"object_id", idOf(t)}, {"seconds", ok ? pt.seconds : 0.0f},
                                {"tolerance", ok ? pt.tolerance : 0.0f}, {"parts", ok ? (int)pt.expected.ids.size() : 0}});
            }
            return list;
        };
        if (action == "record") {
            json d = recordPhysics(std::clamp(args.value("seconds", 5.0f), 0.1f, 30.0f));
            if (d.value("parts", 0) == 0)
                d["warnings"] = json::array({"No loose parts: everything is anchored, so nothing moves."});
            return done(d);
        }
        if (action == "frame" || action == "keep") {
            if (m_replay.empty()) return fail("NO_RECORDING", "There's no recording yet.", "physics_replay record first.");
            const int f = frameArg();
            if (f < 0 || f >= (int)m_replay.frames.size())
                return fail("INVALID_INPUT", "That frame isn't in the recording (0 to " + std::to_string(m_replay.frames.size() - 1) + ").", "");
            const PhysicsReplay::Frame& fr = m_replay.frames[f];
            if (action == "keep") {
                if (m_playing) return fail("PLAYING", "Stop the playtest first.", "playtest stop.");
                PhysicsReplay::apply(*m_scene, fr);
                return done({{"frame", f}, {"time", fr.time}, {"moved", (int)fr.parts.size()}});
            }
            m_replayFrame = f;
            m_replayPlaying = false;
            m_showReplay = true;
            json parts = json::array();
            for (const auto& p : fr.parts) {
                if (parts.size() >= 100) break;
                parts.push_back({{"object_id", "#" + std::to_string(p.id)}, {"name", m_replay.nameOf(p.id)}, {"position", vec(p.position)},
                                 {"rotation", vec(p.rotation)}, {"velocity", vec(p.velocity)}, {"asleep", p.sleepTime >= 0.6f}});
            }
            return done({{"frame", f}, {"time", fr.time}, {"parts", parts}, {"fell_out", (int)(m_replay.ids.size() - fr.parts.size())}});
        }
        if (action == "save_test") {
            if (m_playing) return fail("PLAYING", "Stop the playtest first: tests are made from the scene in the editor.", "playtest stop.");
            std::string error;
            SceneNode* t = savePhysicsTest(str("name"), args.value("seconds", 5.0f), args.value("tolerance", 0.1f), &error);
            if (!t) return fail("NOTHING_TO_TEST", error, "Make some parts unanchored first.");
            return done({{"object_id", idOf(t)}, {"path", pathOf(t)}, {"tests", testList(physicsTests())}});
        }
        if (action == "list_tests") return done({{"tests", testList(physicsTests())}});
        if (action == "run_tests") {
            std::vector<SceneNode*> tests = physicsTests();
            const std::string which = str("test");
            if (!which.empty()) {
                std::vector<SceneNode*> one;
                for (SceneNode* t : tests) if (t->name == which || idOf(t) == which) one.push_back(t);
                if (one.empty()) {
                    json names = json::array();
                    for (SceneNode* t : tests) names.push_back(t->name);
                    return failWith("NOT_FOUND", "There's no physics test called \"" + which + "\".", "list_tests shows them.", names);
                }
                tests = one;
            }
            json results = json::array();
            int passed = 0;
            for (SceneNode* t : tests) {
                json r = runPhysicsTest(t);
                r["object_id"] = idOf(t);
                if (r.value("passed", false)) ++passed;
                results.push_back(r);
            }
            m_showReplay = true;
            return done({{"passed", passed}, {"failed", (int)tests.size() - passed}, {"results", results}});
        }
        return fail("INVALID_INPUT", "action must be record, frame, keep, save_test, run_tests or list_tests.", "");
    }
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
    // Which play session's lines: "all" (default), "latest" (the newest run, running or
    // finished), "current" (the running one), "editing" (outside play), or a run number.
    int runWanted = -2;   // -2 all, -1 editing only, else that run (0 = none yet)
    std::string runError;
    auto pickRun = [&]() {
        runWanted = -2;
        if (!args.contains("run")) return true;
        const json& r = args["run"];
        if (r.is_number_integer()) { runWanted = std::max(0, r.get<int>()); return true; }
        const std::string s = r.is_string() ? r.get<std::string>() : std::string();
        if (s == "all") return true;
        if (s == "latest") { runWanted = Log::lastRun(); return true; }
        if (s == "current") { runWanted = Log::currentRun(); return true; }
        if (s == "editing") { runWanted = -1; return true; }
        runError = "run must be \"all\", \"latest\", \"current\", \"editing\" or a run number.";
        return false;
    };
    auto inRun = [&](const Log::Entry& e) { return runWanted == -2 || (runWanted == -1 ? e.run == 0 : e.run == runWanted); };
    auto runInfo = [&](json& d) {
        d["latest_run"] = Log::lastRun();
        d["current_run"] = Log::currentRun();
        d["playing"] = m_playing;
        if (runWanted >= 0) d["run"] = runWanted;
        else d["run"] = runWanted == -1 ? "editing" : "all";
        if (runWanted == 0) d["note"] = Log::lastRun() ? "No play session is running now (see latest_run)." : "Nothing has been playtested yet.";
    };
    if (name == "get_output") {
        if (!pickRun()) return fail("INVALID_INPUT", runError, "Leave run out for everything.");
        int want = std::clamp(args.value("lines", 40), 1, 400);
        const auto& e = Log::entries();
        std::vector<size_t> pick;
        for (size_t i = e.size(); i-- > 0 && (int)pick.size() < want;) if (inRun(e[i])) pick.push_back(i);
        json lines = json::array();
        for (size_t k = pick.size(); k-- > 0;) {
            const Log::Entry& x = e[pick[k]];
            lines.push_back({{"time", x.time}, {"level", levelName(x.level)}, {"text", x.text}, {"run", x.run}});
        }
        json d = {{"lines", lines}};
        runInfo(d);
        return done(d);
    }
    if (name == "get_errors") {
        if (!pickRun()) return fail("INVALID_INPUT", runError, "Leave run out for everything.");
        int look = std::clamp(args.value("lines", 200), 1, 2000);
        bool warnings = args.value("include_warnings", true);
        const auto& e = Log::entries();
        json errs = json::array(), warns = json::array();
        int seen = 0;
        size_t from = e.size();
        while (from > 0 && seen < look) { --from; if (inRun(e[from])) ++seen; }
        for (size_t i = from; i < e.size(); ++i) {
            if (!inRun(e[i])) continue;
            if (e[i].level == Log::Level::Error) errs.push_back({{"time", e[i].time}, {"text", e[i].text}, {"run", e[i].run}});
            else if (warnings && e[i].level == Log::Level::Warn) warns.push_back({{"time", e[i].time}, {"text", e[i].text}, {"run", e[i].run}});
        }
        json d = {{"errors", errs}, {"warnings", warns}, {"error_count", errs.size()}, {"warning_count", warns.size()}};
        runInfo(d);
        return done(d);
    }
    if (name == "diagnose_object") {
        SceneNode* n;
        if (!need("object", n)) return refErr;
        json findings = json::array();
        diagnose(n, args.value("recursive", true), findings);
        return done({{"object_id", idOf(n)}, {"resulting_state", state(n)}, {"findings", findings},
                     {"error_count", count(findings, "error")}, {"warning_count", count(findings, "warning")}});
    }
    if (name == "analyze_scene_performance") {
        int objects = 0, parts = 0, dynamicParts = 0, lights = 0, emitters = 0;
        int scripts = 0, enabledScripts = 0;
        size_t totalScriptBytes = 0;
        json scriptSizes = json::array();
        m_scene->forEach([&](SceneNode* n) {
            if (n->internal) return;
            ++objects;
            if (isPartLike(n)) {
                ++parts;
                if (!n->anchored) ++dynamicParts;
            }
            if (n->isLight() && n->enabled) ++lights;
            if (n->isParticles() && n->enabled) ++emitters;
            if (n->isScript()) {
                ++scripts;
                if (n->enabled && !n->isModule) ++enabledScripts;
                totalScriptBytes += n->source.size();
                scriptSizes.push_back({{"path", n->fullName()}, {"object_id", idOf(n)},
                                       {"bytes", n->source.size()}, {"enabled", n->enabled},
                                       {"type", n->isModule ? "ModuleScript" : (n->isLocal ? "LocalScript" : "Script")}});
            }
        });
        std::vector<json> ranked;
        for (const auto& item : scriptSizes) ranked.push_back(item);
        std::sort(ranked.begin(), ranked.end(), [](const json& x, const json& y) {
            return x.value("bytes", size_t(0)) > y.value("bytes", size_t(0));
        });
        json largest = json::array();
        for (size_t i = 0; i < ranked.size() && i < 10; ++i) largest.push_back(ranked[i]);
        json findings = json::array();
        auto finding = [&](const char* severity, const char* code, const std::string& message, const std::string& recommendation) {
            findings.push_back({{"severity", severity}, {"code", code}, {"message", message}, {"recommendation", recommendation}});
        };
        if (parts >= 1000) finding("warning", "HIGH_PART_COUNT", std::to_string(parts) + " parts are present in the scene.",
                                   "Profile the scene in Play mode; reduce unnecessary geometry and consider grouping or simplifying repeated detail.");
        if (dynamicParts >= 250) finding("warning", "MANY_DYNAMIC_BODIES", std::to_string(dynamicParts) + " parts are unanchored and may participate in physics.",
                                          "Anchor static scenery and reduce the number of simultaneously simulated bodies where gameplay permits.");
        if (lights >= 40) finding("warning", "MANY_ENABLED_LIGHTS", std::to_string(lights) + " enabled lights are present.",
                                  "Check shadow-casting and overlapping lights, then compare frame time with lights disabled in a test copy.");
        if (emitters >= 75) finding("warning", "MANY_PARTICLE_EMITTERS", std::to_string(emitters) + " enabled particle emitters are present.",
                                    "Reduce emitter count or particle rates where effects overlap; verify the result visually.");
        if (totalScriptBytes >= 100000) finding("info", "LARGE_SCRIPT_SOURCE_TOTAL", "Scripts contain " + std::to_string(totalScriptBytes) + " bytes of source in total.",
                                                 "Review large scripts for duplicated logic; source size alone does not prove runtime cost.");
        for (const auto& s : ranked) {
            if (s.value("bytes", size_t(0)) >= 20000) {
                finding("info", "LARGE_SCRIPT_SOURCE", s.value("path", std::string("Script")) + " contains " +
                        std::to_string(s.value("bytes", size_t(0))) + " bytes of source.",
                        "Consider splitting large scripts into focused modules. Large source is a maintainability signal, not a runtime performance measurement.");
            }
        }
        return done({{"metrics", {{"object_count", objects}, {"part_count", parts}, {"unanchored_part_count", dynamicParts},
                                  {"enabled_light_count", lights}, {"enabled_particle_emitter_count", emitters},
                                  {"script_count", scripts}, {"enabled_non_module_script_count", enabledScripts},
                                  {"total_script_source_bytes", totalScriptBytes}}},
                     {"findings", findings}, {"largest_scripts", largest},
                     {"note", "Static heuristic only. Counts do not measure actual CPU/GPU time, draw calls, memory use or frame rate. Validate suspected hotspots with Play-mode measurements."}});
    }
    if (name == "analyze_script_dependencies") {
        // Static-only dependency scan. Never execute scripts or evaluate require expressions.
        std::vector<SceneNode*> scripts;
        std::vector<SceneNode*> modules;
        m_scene->forEach([&](SceneNode* n) {
            if (!n->isScript() || n->internal) return;
            scripts.push_back(n);
            if (n->isModule) modules.push_back(n);
        });
        auto trim = [](std::string s) {
            const size_t first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return std::string();
            const size_t last = s.find_last_not_of(" \t\r\n");
            return s.substr(first, last - first + 1);
        };
        auto pathOfNode = [&](const SceneNode* n) { return n->fullName(); };
        json dependencies = json::array();
        json unresolved = json::array();
        std::map<uint64_t, int> incoming;
        for (SceneNode* source : scripts) {
            const std::string& code = source->source;
            size_t scan = 0;
            while (scan < code.size()) {
                // Skip Lua comments and quoted strings so text in comments / messages
                // cannot create fake dependency edges.
                if (code[scan] == '-' && scan + 1 < code.size() && code[scan + 1] == '-') {
                    scan += 2;
                    if (scan < code.size() && code[scan] == '[') {
                        size_t eq = scan + 1;
                        while (eq < code.size() && code[eq] == '=') ++eq;
                        if (eq < code.size() && code[eq] == '[') {
                            const std::string close = "]" + std::string(eq - scan - 1, '=') + "]";
                            const size_t closeAt = code.find(close, eq + 1);
                            scan = closeAt == std::string::npos ? code.size() : closeAt + close.size();
                            continue;
                        }
                    }
                    const size_t nl = code.find('\n', scan);
                    scan = nl == std::string::npos ? code.size() : nl + 1;
                    continue;
                }
                if (code[scan] == '[') {
                    size_t eq = scan + 1;
                    while (eq < code.size() && code[eq] == '=') ++eq;
                    if (eq < code.size() && code[eq] == '[') {
                        const std::string close = "]" + std::string(eq - scan - 1, '=') + "]";
                        const size_t closeAt = code.find(close, eq + 1);
                        scan = closeAt == std::string::npos ? code.size() : closeAt + close.size();
                        continue;
                    }
                }
                if (code[scan] == '"' || code[scan] == '\'') {
                    const char quote = code[scan++];
                    while (scan < code.size()) {
                        if (code[scan] == '\\') { scan = std::min(scan + 2, code.size()); continue; }
                        if (code[scan++] == quote) break;
                    }
                    continue;
                }
                if (!(std::isalpha((unsigned char)code[scan]) || code[scan] == '_')) { ++scan; continue; }
                const size_t wordStart = scan++;
                while (scan < code.size() && (std::isalnum((unsigned char)code[scan]) || code[scan] == '_')) ++scan;
                if (code.compare(wordStart, scan - wordStart, "require") != 0) continue;
                size_t open = scan;
                while (open < code.size() && std::isspace((unsigned char)code[open])) ++open;
                if (open >= code.size() || code[open] != '(') continue;
                size_t argStart = open + 1, end = argStart;
                char quote = 0;
                while (end < code.size()) {
                    const char ch = code[end];
                    if (quote) {
                        if (ch == '\\') { end = std::min(end + 2, code.size()); continue; }
                        if (ch == quote) quote = 0;
                    } else if (ch == '"' || ch == '\'') quote = ch;
                    else if (ch == ')') break;
                    ++end;
                }
                if (end >= code.size()) continue;
                std::string expr = trim(code.substr(argStart, end - argStart));
                scan = end + 1;
                if (expr.empty()) continue;
                // Resolve simple quoted module names as well as bare paths. More
                // complex expressions are intentionally reported as unresolved.
                if (expr.size() >= 2 && ((expr.front() == '"' && expr.back() == '"') ||
                                         (expr.front() == '\'' && expr.back() == '\''))) {
                    expr = expr.substr(1, expr.size() - 2);
                }
                SceneNode* target = nullptr;
                for (SceneNode* candidate : modules) {
                    const std::string full = pathOfNode(candidate);
                    const std::string name = candidate->name;
                    if (expr == name || expr == full || expr == ("game." + full) ||
                        expr == ("script.Parent." + name)) {
                        if (target) { target = nullptr; break; } // ambiguous: don't guess
                        target = candidate;
                    }
                }
                if (target) {
                    dependencies.push_back({{"from", pathOfNode(source)}, {"to", pathOfNode(target)}, {"expression", expr}});
                    ++incoming[target->id];
                } else {
                    unresolved.push_back({{"from", pathOfNode(source)}, {"expression", expr}});
                }
            }
        }
        json unused = json::array();
        for (SceneNode* module : modules) {
            if (incoming[module->id] == 0)
                unused.push_back({{"path", pathOfNode(module)}, {"object_id", idOf(module)},
                                  {"name", module->name}, {"note", "No static require(...) reference was found. Dynamic or external use may still exist."}});
        }
        return done({{"script_count", scripts.size()}, {"module_count", modules.size()},
                     {"dependencies", dependencies}, {"dependency_count", dependencies.size()},
                     {"unresolved", unresolved}, {"unresolved_count", unresolved.size()},
                     {"unreferenced_modules", unused},
                     {"note", "Static heuristic only: scripts were not executed. Dynamic require expressions, string-built paths and runtime-only references may be missed."}});
    }
    if (name == "lua_debugger") {
        ScriptEngine& scripts = m_session->scripts();
        const std::string action = str("action");
        if (action == "add_breakpoint" || action == "remove_breakpoint") {
            SceneNode* n;
            if (!need("object", n)) return refErr;
            if (!n->isScript()) return fail("NOT_A_SCRIPT", label(n) + " isn't a script.", "find_objects with class Script to find it.");
            const int line = args.value("line", 0);
            if (line < 1) return fail("INVALID_INPUT", "Give the line number (1 or more).", "read_script shows the lines.");
            scripts.debugSetBreakpoint(n->id, line, action == "add_breakpoint");
        } else if (action == "clear_breakpoints") {
            scripts.debugClearBreakpoints();
        } else if (action == "continue" || action == "step_over" || action == "step_into" || action == "step_out") {
            if (!m_playing) return fail("NOT_PLAYING", "There's no playtest.", "Add a breakpoint, then playtest start.");
            if (!scripts.debugPaused()) return fail("NOT_PAUSED", "No script is paused at a breakpoint.", "lua_debugger status shows the breakpoints.");
            if (action == "continue") scripts.debugContinue();
            else if (action == "step_over") scripts.debugStepOver();
            else if (action == "step_into") scripts.debugStepInto();
            else scripts.debugStepOut();
        } else if (action != "status") {
            return fail("INVALID_INPUT", "action must be status, add_breakpoint, remove_breakpoint, clear_breakpoints, continue, "
                                         "step_over, step_into or step_out.", "");
        }
        json d = {{"playing", m_playing}, {"paused", m_playing && scripts.debugPaused()}, {"pauses", scripts.debugPauses()}};
        if (m_playing && scripts.debugPaused()) {
            SceneNode* at = m_scene->findById(scripts.debugScript());
            d["at"] = {{"object_id", at ? idOf(at) : std::string()}, {"source", scripts.debugSourceForUi()},
                       {"line", scripts.debugLine()}, {"reason", scripts.debugReason()}};
            d["call_stack"] = scripts.debugCallStack();
            d["locals"] = scripts.debugLocals();
        }
        json bps = json::array();
        for (const auto& bp : scripts.debugBreakpoints()) {
            SceneNode* n = m_scene->findById(bp.first);
            bps.push_back({{"object_id", n ? idOf(n) : std::string()}, {"name", n ? n->fullName() : std::string("[deleted script]")},
                           {"line", bp.second}});
        }
        d["breakpoints"] = bps;
        if (m_playing && !scripts.debugPaused() && !bps.empty())
            d["note"] = "Running: a script pauses when it reaches a breakpoint. Call status again to see.";
        return done(d);
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
        // Accept the property names scripts use (Size, Color, CanCollide...) as well as size / can_collide.
        bool renamed = false;
        json norm = json::object();
        for (auto& [k, v] : args.items()) {
            std::string key = canon(k) == "cancollide" ? "can_collide" : lower(k);
            if (key != k) renamed = true;
            norm[key] = v;
        }
        if (renamed) return runAiTool(name, norm);
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
            std::string m = str("material");
            if (m.rfind("Enum.", 0) == 0) m = m.substr(m.rfind('.') + 1);
            for (int i = 0; i < kMaterialCount; ++i)
                if (lower(kMaterialNames[i]) == lower(m)) { n->material = (Material)i; found = true; }
            if (!found) warnings.push_back("There's no material \"" + str("material") + "\" (there are " +
                                           std::string(materialNameList()) + "); it stayed Plastic.");
        }
        n->anchored = args.value("anchored", false);
        n->canCollide = args.value("can_collide", true);
        if (args.contains("density") && args["density"].is_number()) n->density = std::max(0.01f, args["density"].get<float>());
        if (args.contains("friction") && args["friction"].is_number()) n->friction = std::clamp(args["friction"].get<float>(), 0.0f, 2.0f);
        if (args.contains("elasticity") && args["elasticity"].is_number()) n->elasticity = std::clamp(args["elasticity"].get<float>(), 0.0f, 1.0f);
        if (vecArg("position", v)) {
            glm::vec3 local = n->parent ? glm::vec3(glm::inverse(n->parentFrame()) * glm::vec4(v, 1.0f)) : v;
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
    if (name == "generate_level") {
        LevelGen::Settings ls;
        const std::string style = str("style");
        if (!style.empty() && style != "Dungeon" && style != "Obby")
            return failWith("INVALID_ENUM_VALUE", "style must be Dungeon or Obby.", "Leave it out for a Dungeon.", json::array({"Dungeon", "Obby"}));
        ls.style = style == "Obby" ? LevelGen::Style::Obby : LevelGen::Style::Dungeon;
        ls.seed = (uint32_t)std::max(0, args.value("seed", 1));
        ls.count = args.value("count", 8);
        ls.roomMin = args.value("room_min", ls.roomMin);
        ls.roomMax = args.value("room_max", std::max(ls.roomMax, ls.roomMin));
        ls.wallHeight = args.value("wall_height", ls.wallHeight);
        ls.props = args.value("props", true);
        ls.coins = args.value("coins", true);
        SceneNode* old = nullptr;
        if (!str("replace").empty() && !need("replace", old)) return refErr;
        glm::vec3 at;
        const bool hasAt = vecArg("at", at);
        const bool dry = args.value("dry_run", false);
        if (!dry && m_playing) return fail("PLAYING", "Stop the playtest first: levels are made in the editor.", "playtest stop.");
        SceneNode* made = nullptr;
        json d = generateLevel(ls, args.value("tries", 1), !dry, hasAt ? &at : nullptr, old, &made);
        if (made) { d["object_id"] = idOf(made); d["path"] = pathOf(made); }
        if (!d.value("ok", false))
            d["warnings"] = json::array({"No seed passed every check: see tried[].problems. Try more seeds (tries) or fewer rooms."});
        return done(d);
    }
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
    if (name == "ai_activity") {
        const std::string action = str("action").empty() ? "report" : str("action");
        if (action == "keep") { aiKeepChanges(); return done({{"kept", true}, {"note", "The report starts over from now."}}); }
        if (action == "undo_all") {
            if (m_playing) return fail("PLAYTEST_RUNNING", "Can't undo during a playtest.", "playtest stop first.");
            if (!aiUndoChanges()) return fail("NOTHING_TO_UNDO", "There are no AI changes waiting in the report.", "ai_activity report");
            return done({{"undone", true}, {"note", "The game is back how it was before the AI's changes. undo brings them back."}});
        }
        if (action == "log") {
            loadAiLog();
            const int limit = std::clamp(args.value("limit", 30), 1, 300);
            json list = json::array();
            for (auto it = m_aiLog.rbegin(); it != m_aiLog.rend() && (int)list.size() < limit; ++it)
                list.push_back({{"time", it->time}, {"source", it->source}, {"tool", it->tool}, {"args", it->args}, {"result", it->result},
                                {"added", it->added}, {"removed", it->removed}, {"changed", it->changed}, {"settings", it->settings}});
            return done({{"entries", list}, {"ask_first", m_aiAskFirst}});
        }
        if (action != "report") return fail("INVALID_INPUT", "action must be report, log, keep or undo_all.", "");
        if (m_playing) return fail("PLAYTEST_RUNNING", "The report is about the saved game, and a playtest is running.", "playtest stop first.");
        return done(aiChangeReport());
    }
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

    // ---------------------------------------------------------------- PUBLISH
    // The same publish as File > Publish to Guts&Bolts: the signed-in account, the same
    // request, and the server's own checks and limits decide. Nothing here gets around them.
    if (name == "publish_game") {
        const std::string action = str("action").empty() ? "publish" : str("action");
        if (action == "status") {
            json d = m_aiPublish.is_object() ? m_aiPublish : json{{"state", "none"}};
            d["published_id"] = m_scene->info().publishedId;
            return done(d);
        }
        if (action != "publish") return failWith("INVALID_INPUT", "action must be publish or status.", "", json::array({"publish", "status"}));
        if (!Online::configured() || !Online::online())
            return fail("NOT_SIGNED_IN", "Studio isn't signed in to Guts&Bolts (" +
                        (Online::configured() ? Online::statusText() : std::string("no server")) + "), so it can't publish.",
                        "Ask the person to sign in (File > Guts&Bolts Server... / their profile), then try again. "
                        "Publishing only ever happens as the account they signed in with.", false);
        if (Online::isGuest())
            return fail("NOT_SIGNED_IN", "Guests can't publish games.", "Ask the person to sign up or sign in first.", false);
        if (m_playing) return fail("PLAYTEST_RUNNING", "Can't publish during a playtest (it would publish the running game).", "playtest stop first.");
        if (m_onlineBusy || (m_aiPublish.is_object() && m_aiPublish.value("state", "") == "publishing"))
            return fail("BUSY", "A publish (or another upload) is still going.", "Wait, then publish_game {action: \"status\"}.");
        if (!args.value("confirm", false))
            return fail("CONFIRMATION_REQUIRED", "Publishing puts the game on Guts&Bolts for everyone to find and play.",
                        "Only publish when the person asked for it, then call again with confirm: true.");
        const std::string pubName = str("name").empty() ? m_scene->info().title : str("name");
        const std::string pubDesc = args.contains("description") ? str("description") : m_scene->info().description;
        if (pubName.find_first_not_of(" \t") == std::string::npos)
            return fail("INVALID_INPUT", "The game needs a name.", "Give name (the title people see on the site).");
        const std::string mode = str("mode").empty() ? "auto" : str("mode");
        if (mode != "auto" && mode != "new" && mode != "update")
            return failWith("INVALID_INPUT", "mode must be auto, new or update.", "", json::array({"auto", "new", "update"}));
        const bool published = !m_scene->info().publishedId.empty();
        if (mode == "update" && !published)
            return fail("NOT_PUBLISHED", "This game hasn't been published yet, so there's nothing to update.", "Use mode \"new\" (or auto).");
        const bool update = mode == "update" || (mode == "auto" && published);
        m_aiPublish = {{"state", "publishing"}, {"name", pubName}, {"update", update}};
        Log::system("AI tools: publishing \"" + pubName + "\" to Guts&Bolts...");
        publishGame(pubName, pubDesc, update, [this](const json& r) {
            if (r.value("ok", false)) {
                m_aiPublish["state"] = "published";
                m_aiPublish["published_id"] = m_scene->info().publishedId;
            } else {
                m_aiPublish["state"] = "failed";
                m_aiPublish["error"] = r.value("error", std::string("Publishing didn't work."));
                Log::warn("Publishing didn't work: " + m_aiPublish["error"].get<std::string>());
            }
        });
        return done({{"state", "publishing"}, {"name", pubName}, {"update", update},
                     {"note", "Sent to the server; its checks decide. Check the result with publish_game {action: \"status\"}."}});
    }

    return fail("UNKNOWN_TOOL", "There's no tool called " + name + ".", "Use one of the tools from tools/list. Don't invent tools.", false);
}
