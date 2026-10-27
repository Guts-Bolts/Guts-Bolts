#include <algorithm>
#include <cmath>
#include "Serializer.h"
#include "EditMesh.h"
#include "RobloxFile.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Player.h"
#include "PlayerModel.h"
#include "../renderer/MeshLibrary.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <unordered_map>

using json = nlohmann::json;

namespace {

json vec(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

glm::vec3 vec(const json& j, const char* key, glm::vec3 fallback) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 3) return fallback;
    return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
}

template <typename T>
T get(const json& j, const char* key, T fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    try { return it->get<T>(); } catch (...) { return fallback; }
}

const char* kindName(NodeKind k) {
    switch (k) {
        case NodeKind::Model:  return "Model";
        case NodeKind::Script: return "Script";
        case NodeKind::Light:  return "Light";
        case NodeKind::ForceField: return "ForceField";
        case NodeKind::Sound:  return "Sound";
        case NodeKind::Attachment: return "Attachment";
        case NodeKind::Constraint: return "Constraint";
        case NodeKind::Tool:   return "Tool";
        case NodeKind::Value:  return "Value";
        case NodeKind::Decal:  return "Decal";
        case NodeKind::Animation: return "Animation";
        case NodeKind::Gui:    return "Gui";
        case NodeKind::FluidSystem:  return "FluidSystem";
        case NodeKind::FluidEmitter: return "FluidEmitter";
        case NodeKind::Mover:        return "Mover";
        case NodeKind::Remote:       return "Remote";
        case NodeKind::Prompt:       return "Prompt";
        case NodeKind::Highlight:    return "Highlight";
        case NodeKind::Trail:        return "Trail";
        case NodeKind::Beam:         return "Beam";
        case NodeKind::SoundGroup:   return "SoundGroup";
        case NodeKind::SoundEffect:  return "SoundEffect";
        case NodeKind::PostEffect:   return "PostEffect";
        case NodeKind::Sky:          return "Sky";
        case NodeKind::MaterialVariant: return "MaterialVariant";
        case NodeKind::Camera:       return "Camera";
        case NodeKind::ClickDetector: return "ClickDetector";
        case NodeKind::Particles:    return "Particles";
        default:               return "Part";
    }
}
NodeKind kindFrom(const std::string& s) {
    if (s == "Model")  return NodeKind::Model;
    if (s == "Script") return NodeKind::Script;
    if (s == "Light")  return NodeKind::Light;
    if (s == "ForceField") return NodeKind::ForceField;
    if (s == "Sound")  return NodeKind::Sound;
    if (s == "Attachment") return NodeKind::Attachment;
    if (s == "Constraint") return NodeKind::Constraint;
    if (s == "Tool")   return NodeKind::Tool;
    if (s == "Value")  return NodeKind::Value;
    if (s == "Decal")  return NodeKind::Decal;
    if (s == "Animation") return NodeKind::Animation;
    if (s == "Gui")    return NodeKind::Gui;
    if (s == "FluidSystem")  return NodeKind::FluidSystem;
    if (s == "FluidEmitter") return NodeKind::FluidEmitter;
    if (s == "Mover")        return NodeKind::Mover;
    if (s == "Remote")       return NodeKind::Remote;
    if (s == "Prompt")       return NodeKind::Prompt;
    if (s == "Highlight")    return NodeKind::Highlight;
    if (s == "Trail")        return NodeKind::Trail;
    if (s == "Beam")         return NodeKind::Beam;
    if (s == "SoundGroup")   return NodeKind::SoundGroup;
    if (s == "SoundEffect")  return NodeKind::SoundEffect;
    if (s == "PostEffect")   return NodeKind::PostEffect;
    if (s == "Sky")          return NodeKind::Sky;
    if (s == "MaterialVariant") return NodeKind::MaterialVariant;
    if (s == "Camera")       return NodeKind::Camera;
    if (s == "ClickDetector") return NodeKind::ClickDetector;
    if (s == "Particles")    return NodeKind::Particles;
    return NodeKind::Part;
}

const char* shapeName(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Cube:     return "Cube";
        case PrimitiveType::Sphere:   return "Sphere";
        case PrimitiveType::Plane:    return "Plane";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::Mesh:     return "Mesh";
        default:                      return "None";
    }
}
PrimitiveType shapeFrom(const std::string& s) {
    if (s == "Cube")     return PrimitiveType::Cube;
    if (s == "Sphere")   return PrimitiveType::Sphere;
    if (s == "Plane")    return PrimitiveType::Plane;
    if (s == "Cylinder") return PrimitiveType::Cylinder;
    if (s == "Mesh")     return PrimitiveType::Mesh;
    return PrimitiveType::None;
}

Material materialFrom(const std::string& s) {
    Material m = Material::Plastic;
    materialFromName(s, m);   // an unknown name (from a newer version?) stays Plastic
    return m;
}

} // namespace

namespace Serializer {
// Game UI properties (also sent to other players when they change).
nlohmann::json promptToJson(const PromptProps& p, bool enabled) {
    nlohmann::json j = {{"action", p.action}, {"key", p.key}, {"range", p.range}};
    if (!p.object.empty()) j["object"] = p.object;
    if (p.hold > 0.0f)     j["hold"] = p.hold;
    if (!p.lineOfSight)    j["los"] = false;
    if (!p.clickable)      j["click"] = false;
    if (!enabled)          j["on"] = false;
    return j;
}

void promptFromJson(const nlohmann::json& j, PromptProps& p, bool& enabled) {
    p.action      = j.value("action", std::string("Interact"));
    p.object      = j.value("object", std::string());
    p.key         = j.value("key", std::string("E"));
    p.hold        = std::clamp(j.value("hold", 0.0f), 0.0f, 60.0f);
    p.range       = std::clamp(j.value("range", 5.0f), 0.0f, 500.0f);
    p.lineOfSight = j.value("los", true);
    p.clickable   = j.value("click", true);
    enabled       = j.value("on", true);
}

nlohmann::json clickToJson(const ClickProps& c) {
    nlohmann::json j = {{"range", c.range}};
    if (!c.cursor.empty()) j["cursor"] = c.cursor;
    return j;
}

void clickFromJson(const nlohmann::json& j, ClickProps& c) {
    c.range  = std::clamp(j.value("range", 16.0f), 0.0f, 500.0f);
    c.cursor = j.value("cursor", std::string());
}

nlohmann::json highlightToJson(const HighlightProps& h, bool enabled) {
    nlohmann::json j = {{"fill", vec(h.fill)}, {"outline", vec(h.outline)},
                        {"fillT", h.fillTransparency}, {"outlineT", h.outlineTransparency}};
    if (!h.onTop)   j["occluded"] = true;
    if (h.adornee)  j["adornee"] = h.adornee;   // (a node ID, like a constraint's ref0)
    if (!enabled)   j["on"] = false;
    return j;
}

void highlightFromJson(const nlohmann::json& j, HighlightProps& h, bool& enabled) {
    auto color = [&](const char* key, glm::vec3 fallback) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array() || it->size() != 3) return fallback;
        for (auto& v : *it) if (!v.is_number()) return fallback;
        return glm::clamp(glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()), 0.0f, 1.0f);
    };
    h.fill                = color("fill", glm::vec3(1, 0, 0));
    h.outline             = color("outline", glm::vec3(1));
    h.fillTransparency    = std::clamp(j.value("fillT", 0.5f), 0.0f, 1.0f);
    h.outlineTransparency = std::clamp(j.value("outlineT", 0.0f), 0.0f, 1.0f);
    h.onTop               = !j.value("occluded", false);
    h.adornee             = j.value("adornee", (uint64_t)0);
    enabled               = j.value("on", true);
}

// Screen effects: only the settings their kind uses.
nlohmann::json postEffectToJson(const PostEffectProps& p, bool enabled) {
    json j = {{"type", kPostEffectClassNames[(int)p.type]}};
    switch (p.type) {
        case PostEffectType::ColorCorrection:
            j["brightness"] = p.brightness; j["contrast"] = p.contrast; j["saturation"] = p.saturation; j["tint"] = vec(p.tint);
            break;
        case PostEffectType::SunRays: j["intensity"] = p.intensity; j["spread"] = p.spread; break;
        case PostEffectType::DepthOfField:
            j["far"] = p.farIntensity; j["focus"] = p.focusDistance; j["radius"] = p.inFocusRadius; j["near"] = p.nearIntensity;
            break;
        case PostEffectType::Bloom: j["intensity"] = p.intensity; j["size"] = p.size; j["threshold"] = p.threshold; break;
        case PostEffectType::Blur:  j["size"] = p.size; break;
    }
    if (!enabled) j["on"] = false;
    return j;
}

void postEffectFromJson(const nlohmann::json& j, PostEffectProps& p, bool& enabled) {
    PostEffectType t = p.type;
    const std::string name = get<std::string>(j, "type", "");
    for (int i = 0; i < kPostEffectCount; ++i) if (name == kPostEffectClassNames[i]) t = (PostEffectType)i;
    const PostEffectProps d = postEffectDefaults(t);
    p.type          = t;
    p.brightness    = get<float>(j, "brightness", d.brightness);
    p.contrast      = get<float>(j, "contrast", d.contrast);
    p.saturation    = get<float>(j, "saturation", d.saturation);
    p.tint          = d.tint;
    if (auto it = j.find("tint"); it != j.end() && it->is_array() && it->size() == 3 && (*it)[0].is_number() &&
                                  (*it)[1].is_number() && (*it)[2].is_number())
        p.tint = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
    p.intensity     = get<float>(j, "intensity", d.intensity);
    p.spread        = get<float>(j, "spread", d.spread);
    p.size          = get<float>(j, "size", d.size);
    p.threshold     = get<float>(j, "threshold", d.threshold);
    p.farIntensity  = get<float>(j, "far", d.farIntensity);
    p.focusDistance = get<float>(j, "focus", d.focusDistance);
    p.inFocusRadius = get<float>(j, "radius", d.inFocusRadius);
    p.nearIntensity = get<float>(j, "near", d.nearIntensity);
    clampPostEffect(p);
    enabled = get<bool>(j, "on", true);
}

nlohmann::json skyToJson(const SkyProps& s) {
    json j = {{"stars", s.starCount}, {"sunSize", s.sunSize}, {"moonSize", s.moonSize}};
    if (!s.celestial) j["celestial"] = false;
    const std::pair<const char*, const std::string*> pics[] = {{"bk", &s.bk}, {"dn", &s.dn}, {"ft", &s.ft}, {"lf", &s.lf},
                                                               {"rt", &s.rt}, {"up", &s.up}, {"sun", &s.sun}, {"moon", &s.moon}};
    for (auto& [k, v] : pics) if (!v->empty()) j[k] = *v;
    return j;
}

void skyFromJson(const nlohmann::json& j, SkyProps& s) {
    const SkyProps d;
    s.starCount = get<int>(j, "stars", d.starCount);
    s.sunSize   = get<float>(j, "sunSize", d.sunSize);
    s.moonSize  = get<float>(j, "moonSize", d.moonSize);
    s.celestial = get<bool>(j, "celestial", true);
    s.bk = get<std::string>(j, "bk", ""); s.dn = get<std::string>(j, "dn", ""); s.ft = get<std::string>(j, "ft", "");
    s.lf = get<std::string>(j, "lf", ""); s.rt = get<std::string>(j, "rt", ""); s.up = get<std::string>(j, "up", "");
    s.sun = get<std::string>(j, "sun", ""); s.moon = get<std::string>(j, "moon", "");
    clampSky(s);
}

nlohmann::json variantToJson(const MaterialVariantProps& v) {
    json j = {{"base", kMaterialNames[(int)v.base]}, {"studsPerTile", v.studsPerTile}, {"roughness", v.roughness},
              {"metalness", v.metalness}};
    if (v.glow > 0.0f) j["glow"] = v.glow;
    const std::pair<const char*, const std::string*> pics[] = {{"colorMap", &v.colorMap}, {"normalMap", &v.normalMap},
                                                               {"roughnessMap", &v.roughnessMap}, {"metalnessMap", &v.metalnessMap}};
    for (auto& [k, p] : pics) if (!p->empty()) j[k] = *p;
    if (!v.graph.empty()) {
        json g = json::parse(v.graph, nullptr, false);
        if (g.is_object()) j["graph"] = g;
    }
    return j;
}

void variantFromJson(const nlohmann::json& j, MaterialVariantProps& v) {
    const MaterialVariantProps d;
    materialFromName(get<std::string>(j, "base", "Plastic"), v.base);
    v.studsPerTile = get<float>(j, "studsPerTile", d.studsPerTile);
    v.roughness    = get<float>(j, "roughness", d.roughness);
    v.metalness    = get<float>(j, "metalness", d.metalness);
    v.glow         = get<float>(j, "glow", 0.0f);
    v.colorMap     = get<std::string>(j, "colorMap", "");
    v.normalMap    = get<std::string>(j, "normalMap", "");
    v.roughnessMap = get<std::string>(j, "roughnessMap", "");
    v.metalnessMap = get<std::string>(j, "metalnessMap", "");
    v.graph.clear();
    if (auto g = j.find("graph"); g != j.end() && g->is_object()) v.graph = g->dump();
    clampVariant(v);
}

nlohmann::json effectToJson(const EffectProps& e, bool enabled, bool beam) {
    json col = json::array(), tr = json::array();
    for (const ColorKey& k : e.color) col.push_back({k.t, k.c.r, k.c.g, k.c.b});
    for (const NumberKey& k : e.transparency) tr.push_back({k.t, k.v});
    json j = {{"a0", e.a0}, {"a1", e.a1}, {"color", col}, {"transparency", tr}};
    if (!e.texture.empty())     j["texture"] = e.texture;
    if (e.lightEmission > 0.0f) j["glow"] = e.lightEmission;
    if (e.faceCamera)           j["faceCamera"] = true;
    if (!enabled)               j["on"] = false;
    if (beam) {
        j["width"] = {e.width0, e.width1};
        j["curve"] = {e.curve0, e.curve1};
        j["segments"] = e.segments;
        j["texLength"] = e.textureLength;
        j["texSpeed"] = e.textureSpeed;
        if (e.textureWrap) j["texWrap"] = true;
    } else {
        json ws = json::array();
        for (const NumberKey& k : e.widthScale) ws.push_back({k.t, k.v});
        j["lifetime"] = e.lifetime;
        j["minLength"] = e.minLength;
        j["maxLength"] = e.maxLength;
        j["widthScale"] = ws;
    }
    return j;
}

void effectFromJson(const nlohmann::json& j, EffectProps& e, bool& enabled) {
    auto num = [&](const char* key, float fallback, float lo, float hi) {
        auto it = j.find(key);
        return it != j.end() && it->is_number() ? std::clamp(it->get<float>(), lo, hi) : fallback;
    };
    auto pair = [&](const char* key, float& a, float& b, float lo, float hi) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) return;
        a = std::clamp((*it)[0].get<float>(), lo, hi);
        b = std::clamp((*it)[1].get<float>(), lo, hi);
    };
    auto numbers = [&](const char* key, std::vector<NumberKey>& out, float lo, float hi) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array() || it->empty()) return;
        std::vector<NumberKey> keys;
        for (auto& k : *it)
            if (k.is_array() && k.size() == 2 && k[0].is_number() && k[1].is_number() && keys.size() < 20)
                keys.push_back({std::clamp(k[0].get<float>(), 0.0f, 1.0f), std::clamp(k[1].get<float>(), lo, hi)});
        if (!keys.empty()) out = keys;
    };
    e = EffectProps{};
    e.a0 = j.value("a0", (uint64_t)0);
    e.a1 = j.value("a1", (uint64_t)0);
    if (auto it = j.find("color"); it != j.end() && it->is_array() && !it->empty()) {
        std::vector<ColorKey> keys;
        for (auto& k : *it)
            if (k.is_array() && k.size() == 4 && k[0].is_number() && k[1].is_number() && k[2].is_number() && k[3].is_number() && keys.size() < 20)
                keys.push_back({std::clamp(k[0].get<float>(), 0.0f, 1.0f),
                                glm::clamp(glm::vec3(k[1].get<float>(), k[2].get<float>(), k[3].get<float>()), 0.0f, 1.0f)});
        if (!keys.empty()) e.color = keys;
    }
    numbers("transparency", e.transparency, 0.0f, 1.0f);
    numbers("widthScale", e.widthScale, 0.0f, 100.0f);
    if (auto it = j.find("texture"); it != j.end() && it->is_string()) e.texture = it->get<std::string>();
    e.lightEmission = num("glow", 0.0f, 0.0f, 1.0f);
    e.faceCamera = j.value("faceCamera", false);
    e.lifetime = num("lifetime", 2.0f, 0.0f, 20.0f);
    e.minLength = num("minLength", 0.1f, 0.0f, 1000.0f);
    e.maxLength = num("maxLength", 0.0f, 0.0f, 10000.0f);
    pair("width", e.width0, e.width1, 0.0f, 1000.0f);
    pair("curve", e.curve0, e.curve1, -1000.0f, 1000.0f);
    e.segments = (int)num("segments", 10.0f, 1.0f, 1000.0f);
    e.textureLength = num("texLength", 1.0f, 0.001f, 10000.0f);
    e.textureSpeed = num("texSpeed", 1.0f, -1000.0f, 1000.0f);
    e.textureWrap = j.value("texWrap", false);
    enabled = j.value("on", true);
}

nlohmann::json jointToJson(const JointProps& p) {
    return {{"limits", p.limitsEnabled}, {"upperAngle", p.upperAngle}, {"twistLimits", p.twistLimitsEnabled},
            {"twistLower", p.twistLowerAngle}, {"twistUpper", p.twistUpperAngle}, {"restitution", p.restitution},
            {"lower", p.lowerLimit}, {"upper", p.upperLimit}, {"actuator", kActuatorNames[(int)p.actuator]},
            {"motorForce", p.motorMaxForce}, {"velocity", p.velocity}, {"speed", p.speed},
            {"servoForce", p.servoMaxForce}, {"target", p.targetPosition},
            {"angActuator", kActuatorNames[(int)p.angularActuator]}, {"angVelocity", p.angularVelocity},
            {"motorTorque", p.motorMaxTorque}, {"angSpeed", p.angularSpeed}, {"targetAngle", p.targetAngle},
            {"servoTorque", p.servoMaxTorque}, {"angLimits", p.angularLimitsEnabled}, {"lowerAngle", p.lowerAngle},
            {"incline", p.inclinationAngle}};
}

void jointFromJson(const nlohmann::json& j, JointProps& p) {
    if (!j.is_object()) return;
    auto num = [&](const char* key, float fallback, float lo, float hi) {
        auto it = j.find(key);
        return it != j.end() && it->is_number() ? std::clamp(it->get<float>(), lo, hi) : fallback;
    };
    auto flag = [&](const char* key, bool fallback) {
        auto it = j.find(key);
        return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
    };
    auto actuator = [&](const char* key) {
        auto it = j.find(key);
        if (it != j.end() && it->is_string())
            for (int i = 0; i < 3; ++i) if (it->get<std::string>() == kActuatorNames[i]) return (Actuator)i;
        return Actuator::None;
    };
    const JointProps d;
    const float big = 1e9f;
    p.limitsEnabled      = flag("limits", false);
    p.upperAngle         = num("upperAngle", d.upperAngle, -180.0f, 180.0f);
    p.twistLimitsEnabled = flag("twistLimits", false);
    p.twistLowerAngle    = num("twistLower", d.twistLowerAngle, -180.0f, 180.0f);
    p.twistUpperAngle    = num("twistUpper", d.twistUpperAngle, -180.0f, 180.0f);
    p.restitution        = num("restitution", 0.0f, 0.0f, 1.0f);
    p.lowerLimit         = num("lower", d.lowerLimit, -big, big);
    p.upperLimit         = num("upper", d.upperLimit, -big, big);
    p.actuator           = actuator("actuator");
    p.motorMaxForce      = num("motorForce", 0.0f, 0.0f, big);
    p.velocity           = num("velocity", 0.0f, -big, big);
    p.speed              = num("speed", 0.0f, 0.0f, big);
    p.servoMaxForce      = num("servoForce", 0.0f, 0.0f, big);
    p.targetPosition     = num("target", 0.0f, -big, big);
    p.angularActuator    = actuator("angActuator");
    p.angularVelocity    = num("angVelocity", 0.0f, -big, big);
    p.motorMaxTorque     = num("motorTorque", 0.0f, 0.0f, big);
    p.angularSpeed       = num("angSpeed", 0.0f, 0.0f, big);
    p.targetAngle        = num("targetAngle", 0.0f, -180.0f, 180.0f);
    p.servoMaxTorque     = num("servoTorque", 0.0f, 0.0f, big);
    p.angularLimitsEnabled = flag("angLimits", false);
    p.lowerAngle         = num("lowerAngle", d.lowerAngle, -180.0f, 180.0f);
    p.inclinationAngle   = num("incline", 0.0f, -180.0f, 180.0f);
}

nlohmann::json particleToJson(const ParticleProps& p, bool enabled) {
    json j = {{"class", kParticleClassNames[(int)p.type]}};
    if (!enabled) j["on"] = false;
    if (p.emits)  j["emits"] = p.emits;     // (running counts: other players do the same Emit / Clear)
    if (p.clears) j["clears"] = p.clears;
    if (p.type != ParticleType::ParticleEmitter) {
        j["color"] = vec(p.color1);
        if (p.type == ParticleType::Fire)  { j["color2"] = vec(p.color2); j["heat"] = p.rise; j["size"] = p.fxSize; }
        if (p.type == ParticleType::Smoke) { j["rise"] = p.rise; j["size"] = p.fxSize; j["opacity"] = p.opacity; }
        return j;
    }
    json col = json::array(), sz = json::array(), tr = json::array();
    for (const ColorKey& k : p.color) col.push_back({k.t, k.c.r, k.c.g, k.c.b});
    for (const NumberKey& k : p.size) sz.push_back({k.t, k.v});
    for (const NumberKey& k : p.transparency) tr.push_back({k.t, k.v});
    auto range = [](glm::vec2 r) { return json::array({r.x, r.y}); };
    j["rate"] = p.rate;
    j["lifetime"] = range(p.lifetime);
    j["speed"] = range(p.speed);
    j["spread"] = range(p.spread);
    j["rotation"] = range(p.rotation);
    j["rotSpeed"] = range(p.rotSpeed);
    j["colors"] = col;
    j["sizes"] = sz;
    j["transparency"] = tr;
    if (!p.texture.empty())               j["texture"] = p.texture;
    if (p.lightEmission > 0.0f)           j["glow"] = p.lightEmission;
    if (p.acceleration != glm::vec3(0.0f)) j["accel"] = vec(p.acceleration);
    if (p.drag != 0.0f)                   j["drag"] = p.drag;
    if (p.direction != Face::Top)         j["direction"] = kFaceNames[(int)p.direction];
    if (p.locked)                         j["lockedToPart"] = true;
    return j;
}

void particleFromJson(const nlohmann::json& j, ParticleProps& p, bool& enabled, bool update) {
    ParticleType type = ParticleType::ParticleEmitter;
    const std::string cls = j.value("class", std::string());
    for (int i = 0; i < kParticleTypeCount; ++i) if (cls == kParticleClassNames[i]) type = (ParticleType)i;
    // (Counts carry on; what's still to do here is only what's new since last time.)
    const int emits = p.emits, clears = p.clears, pendingEmit = p.pendingEmit;
    const bool pendingClear = p.pendingClear;
    const bool same = update && p.type == type;
    p = particleDefaults(type);
    auto num = [&](const char* key, float fallback, float lo, float hi) {
        auto it = j.find(key);
        return it != j.end() && it->is_number() ? std::clamp(it->get<float>(), lo, hi) : fallback;
    };
    auto color = [&](const char* key, glm::vec3 fallback) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array() || it->size() != 3) return fallback;
        for (auto& v : *it) if (!v.is_number()) return fallback;
        return glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    };
    auto range = [&](const char* key, glm::vec2 fallback, float lo, float hi) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) return fallback;
        glm::vec2 r(std::clamp((*it)[0].get<float>(), lo, hi), std::clamp((*it)[1].get<float>(), lo, hi));
        if (r.y < r.x) std::swap(r.x, r.y);
        return r;
    };
    auto numbers = [&](const char* key, std::vector<NumberKey>& out, float lo, float hi) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array() || it->empty()) return;
        std::vector<NumberKey> keys;
        for (auto& k : *it)
            if (k.is_array() && k.size() == 2 && k[0].is_number() && k[1].is_number() && keys.size() < 20)
                keys.push_back({std::clamp(k[0].get<float>(), 0.0f, 1.0f), std::clamp(k[1].get<float>(), lo, hi)});
        if (!keys.empty()) out = keys;
    };
    enabled = j.value("on", true);
    p.emits = j.value("emits", 0);
    p.clears = j.value("clears", 0);
    if (same) {   // the same object again (another player's update): do what they did since
        p.pendingEmit = std::clamp(pendingEmit + std::max(0, p.emits - emits), 0, kMaxSpritesPerEmitter);
        p.pendingClear = pendingClear || p.clears != clears;
    }
    if (type != ParticleType::ParticleEmitter) {
        p.color1  = glm::clamp(color("color", p.color1), 0.0f, 1.0f);
        p.color2  = glm::clamp(color("color2", p.color2), 0.0f, 1.0f);
        p.rise    = num(type == ParticleType::Fire ? "heat" : "rise", p.rise, -25.0f, 25.0f);
        p.fxSize  = num("size", p.fxSize, type == ParticleType::Fire ? 2.0f : 0.1f, type == ParticleType::Fire ? 30.0f : 100.0f);
        p.opacity = num("opacity", p.opacity, 0.0f, 1.0f);
        return;
    }
    p.rate     = num("rate", p.rate, 0.0f, 1e6f);
    p.lifetime = range("lifetime", p.lifetime, 0.0f, 1000.0f);
    p.speed    = range("speed", p.speed, -1e4f, 1e4f);
    p.rotation = range("rotation", p.rotation, -36000.0f, 36000.0f);
    p.rotSpeed = range("rotSpeed", p.rotSpeed, -36000.0f, 36000.0f);
    if (auto it = j.find("spread"); it != j.end() && it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number())
        p.spread = {std::clamp((*it)[0].get<float>(), -360.0f, 360.0f), std::clamp((*it)[1].get<float>(), -360.0f, 360.0f)};   // (X, Y: not a range)
    if (auto it = j.find("colors"); it != j.end() && it->is_array() && !it->empty()) {
        std::vector<ColorKey> keys;
        for (auto& k : *it)
            if (k.is_array() && k.size() == 4 && k[0].is_number() && k[1].is_number() && k[2].is_number() && k[3].is_number() && keys.size() < 20)
                keys.push_back({std::clamp(k[0].get<float>(), 0.0f, 1.0f),
                                glm::clamp(glm::vec3(k[1].get<float>(), k[2].get<float>(), k[3].get<float>()), 0.0f, 1.0f)});
        if (!keys.empty()) p.color = keys;
    }
    numbers("sizes", p.size, 0.0f, 1000.0f);
    numbers("transparency", p.transparency, 0.0f, 1.0f);
    if (auto it = j.find("texture"); it != j.end() && it->is_string()) p.texture = it->get<std::string>();
    p.lightEmission = num("glow", 0.0f, 0.0f, 1.0f);
    p.acceleration  = color("accel", glm::vec3(0.0f));
    p.drag          = num("drag", 0.0f, -100.0f, 100.0f);
    const std::string dir = j.value("direction", std::string("Top"));
    for (int i = 0; i < 6; ++i) if (dir == kFaceNames[i]) p.direction = (Face)i;
    p.locked = j.value("lockedToPart", false);
}

nlohmann::json guiToJson(const GuiProps& g) {
    auto ud = [](const UDim2& u) { return json::array({u.xs, u.xo, u.ys, u.yo}); };
    json j = {{"class", kGuiClassNames[(int)g.type]}, {"pos", ud(g.pos)}, {"size", ud(g.size)},
              {"anchor", json::array({g.anchor.x, g.anchor.y})}, {"bg", vec(g.bg)}, {"bgT", g.bgTransparency},
              {"borderColor", vec(g.borderColor)}, {"border", g.border}, {"z", g.zIndex}, {"clips", g.clips}};
    if (g.layoutOrder) j["order2"] = g.layoutOrder;
    if (guiHasText(g.type)) {
        j["text"] = g.text; j["textColor"] = vec(g.textColor); j["textSize"] = g.textSize;
        j["scaled"] = g.textScaled; j["wrapped"] = g.textWrapped; j["bold"] = g.bold;
        j["xAlign"] = g.xAlign; j["yAlign"] = g.yAlign; j["textT"] = g.textTransparency;
        j["strokeColor"] = vec(g.strokeColor); j["strokeT"] = g.strokeTransparency;
    }
    if (g.type == GuiType::ImageLabel || g.type == GuiType::ImageButton) {
        j["image"] = g.image; j["imageColor"] = vec(g.imageColor); j["imageT"] = g.imageTransparency;
    }
    if (g.type == GuiType::TextButton || g.type == GuiType::ImageButton) j["autoColor"] = g.autoButtonColor;
    if (g.type == GuiType::ScreenGui) j["order"] = g.displayOrder;
    if (g.type == GuiType::UICorner) {
        j["corner"] = ud(g.corner);
        if (g.corners != glm::vec4(-1.0f)) {
            j["corners"] = json::array({g.corners.x, g.corners.y, g.corners.z, g.corners.w});
            j["cornerScales"] = json::array({g.cornerScales.x, g.cornerScales.y, g.cornerScales.z, g.cornerScales.w});
        }
    }
    if (g.type == GuiType::UIShadow) {
        j["shadowOffset"] = json::array({g.shadowOffset.x, g.shadowOffset.y});
        j["shadowBlur"] = g.shadowBlur; j["shadowSpread"] = g.shadowSpread;
    }
    if (g.type == GuiType::UIBlur) j["blurSize"] = g.blurSize;
    if (g.type == GuiType::UIStroke) j["thickness"] = g.thickness;
    if (g.type == GuiType::UIGradient) {
        json col = json::array(), tr = json::array();
        for (const ColorKey& k : g.gradColor) col.push_back({k.t, k.c.r, k.c.g, k.c.b});
        for (const NumberKey& k : g.gradTransparency) tr.push_back({k.t, k.v});
        j["gradColor"] = col; j["gradT"] = tr;
        j["gradRot"] = g.gradRotation; j["gradOffset"] = json::array({g.gradOffset.x, g.gradOffset.y});
    }
    if (g.type == GuiType::UIScale) j["scale"] = g.scale;
    if (g.type == GuiType::UIAspectRatioConstraint) {
        j["aspect"] = g.aspectRatio; j["aspectType"] = g.aspectType; j["dominantAxis"] = g.dominantAxis;
    }
    if (g.type == GuiType::ViewportFrame) {
        if (g.camera) j["camera"] = g.camera;   // (a node ID, like a BillboardGui's adornee)
        j["ambient"] = vec(g.ambient); j["lightColor"] = vec(g.lightColor); j["lightDir"] = vec(g.lightDir);
        j["imageColor"] = vec(g.imageColor); j["imageT"] = g.imageTransparency;
    }
    if (g.type == GuiType::TextBox) {
        j["placeholder"] = g.placeholder; j["placeholderColor"] = vec(g.placeholderColor);
        j["clearOnFocus"] = g.clearOnFocus; j["editable"] = g.editable; j["multiLine"] = g.multiLine;
    }
    if (g.type == GuiType::ScrollingFrame) {
        j["canvasSize"] = ud(g.canvasSize); j["canvasPos"] = json::array({g.canvasPos.x, g.canvasPos.y});
        j["scrollBar"] = g.scrollBar; j["scrollColor"] = vec(g.scrollColor); j["scrollT"] = g.scrollTransparency;
        j["scrollDir"] = g.scrollDir; j["autoCanvas"] = g.autoCanvas; j["scrolling"] = g.scrolling;
    }
    if (isGuiLayout(g.type)) {
        j["fill"] = g.fill; j["padding"] = ud(g.padding); j["hAlign"] = g.hAlign; j["vAlign"] = g.vAlign;
        j["sortByName"] = g.sortByName;
        if (g.type == GuiType::UIGridLayout) { j["cellSize"] = ud(g.cellSize); j["maxCells"] = g.maxCells; }
    }
    if (g.type == GuiType::BillboardGui || g.type == GuiType::SurfaceGui) {
        j["onTop"] = g.alwaysOnTop;
        if (g.adornee) j["adornee"] = g.adornee;   // (node IDs, like a constraint's ref0)
        if (g.maxDistance > 0) j["maxDistance"] = g.maxDistance;
    }
    if (g.type == GuiType::BillboardGui) { j["studsOffset"] = vec(g.studsOffset); j["worldOffset"] = vec(g.worldOffset); }
    if (g.type == GuiType::SurfaceGui) {
        j["face"] = g.face; j["perStud"] = g.perStud; j["pps"] = g.pixelsPerStud;
        j["surfaceCanvas"] = json::array({g.surfaceCanvas.x, g.surfaceCanvas.y});
    }
    if (g.type == GuiType::UIPadding) {
        j["padScale"] = json::array({g.padScale.x, g.padScale.y, g.padScale.z, g.padScale.w});
        j["padPx"] = json::array({g.padPx.x, g.padPx.y, g.padPx.z, g.padPx.w});
    }
    return j;
}

void guiFromJson(GuiProps& g, const nlohmann::json& j) {
    auto ud = [&](const char* k, UDim2 d) {
        if (!j.contains(k) || !j[k].is_array() || j[k].size() != 4) return d;
        try { return UDim2{j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>(), j[k][3].get<float>()}; }
        catch (...) { return d; }
    };
    std::string cls = get<std::string>(j, "class", std::string("Frame"));
    for (int i = 0; i < kGuiTypeCount; ++i) if (cls == kGuiClassNames[i]) g.type = (GuiType)i;
    g.pos = ud("pos", g.pos);
    g.size = ud("size", g.size);
    if (j.contains("anchor") && j["anchor"].is_array() && j["anchor"].size() == 2)
        try { g.anchor = {j["anchor"][0].get<float>(), j["anchor"][1].get<float>()}; } catch (...) {}
    g.bg = vec(j, "bg", g.bg);
    g.bgTransparency = get<float>(j, "bgT", g.bgTransparency);
    g.borderColor = vec(j, "borderColor", g.borderColor);
    g.border = get<int>(j, "border", g.border);
    g.zIndex = get<int>(j, "z", g.zIndex);
    g.clips = get<bool>(j, "clips", g.clips);
    g.text = get<std::string>(j, "text", g.text);
    g.textColor = vec(j, "textColor", g.textColor);
    g.textSize = get<float>(j, "textSize", g.textSize);
    g.textScaled = get<bool>(j, "scaled", g.textScaled);
    g.textWrapped = get<bool>(j, "wrapped", g.textWrapped);
    g.bold = get<bool>(j, "bold", g.bold);
    g.xAlign = std::clamp(get<int>(j, "xAlign", g.xAlign), 0, 2);
    g.yAlign = std::clamp(get<int>(j, "yAlign", g.yAlign), 0, 2);
    g.textTransparency = get<float>(j, "textT", g.textTransparency);
    g.strokeColor = vec(j, "strokeColor", g.strokeColor);
    g.strokeTransparency = get<float>(j, "strokeT", g.strokeTransparency);
    g.image = get<std::string>(j, "image", g.image);
    g.imageColor = vec(j, "imageColor", g.imageColor);
    g.imageTransparency = get<float>(j, "imageT", g.imageTransparency);
    g.autoButtonColor = get<bool>(j, "autoColor", g.autoButtonColor);
    g.displayOrder = get<int>(j, "order", g.displayOrder);
    g.corner = ud("corner", g.corner);
    g.thickness = get<float>(j, "thickness", g.thickness);
    auto v4 = [&](const char* k, glm::vec4& out) {
        if (j.contains(k) && j[k].is_array() && j[k].size() == 4)
            try { out = {j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>(), j[k][3].get<float>()}; } catch (...) {}
    };
    v4("corners", g.corners);
    v4("cornerScales", g.cornerScales);
    if (j.contains("shadowOffset") && j["shadowOffset"].is_array() && j["shadowOffset"].size() == 2)
        try { g.shadowOffset = {j["shadowOffset"][0].get<float>(), j["shadowOffset"][1].get<float>()}; } catch (...) {}
    g.shadowBlur = std::clamp(get<float>(j, "shadowBlur", g.shadowBlur), 0.0f, 100.0f);
    g.shadowSpread = std::clamp(get<float>(j, "shadowSpread", g.shadowSpread), -100.0f, 100.0f);
    g.blurSize = std::clamp(get<float>(j, "blurSize", g.blurSize), 0.0f, 100.0f);
    g.layoutOrder = get<int>(j, "order2", g.layoutOrder);
    g.placeholder = get<std::string>(j, "placeholder", g.placeholder);
    g.placeholderColor = vec(j, "placeholderColor", g.placeholderColor);
    g.clearOnFocus = get<bool>(j, "clearOnFocus", g.clearOnFocus);
    g.editable = get<bool>(j, "editable", g.editable);
    g.multiLine = get<bool>(j, "multiLine", g.multiLine);
    g.canvasSize = ud("canvasSize", g.canvasSize);
    if (j.contains("canvasPos") && j["canvasPos"].is_array() && j["canvasPos"].size() == 2)
        try { g.canvasPos = {j["canvasPos"][0].get<float>(), j["canvasPos"][1].get<float>()}; } catch (...) {}
    g.scrollBar = std::clamp(get<int>(j, "scrollBar", g.scrollBar), 0, 100);
    g.scrollColor = vec(j, "scrollColor", g.scrollColor);
    g.scrollTransparency = get<float>(j, "scrollT", g.scrollTransparency);
    g.scrollDir = std::clamp(get<int>(j, "scrollDir", g.scrollDir), 1, 3);
    g.autoCanvas = std::clamp(get<int>(j, "autoCanvas", g.autoCanvas), 0, 3);
    g.scrolling = get<bool>(j, "scrolling", g.scrolling);
    g.fill = std::clamp(get<int>(j, "fill", g.fill), 0, 1);
    g.padding = ud("padding", g.padding);
    g.cellSize = ud("cellSize", g.cellSize);
    g.hAlign = std::clamp(get<int>(j, "hAlign", g.hAlign), 0, 2);
    g.vAlign = std::clamp(get<int>(j, "vAlign", g.vAlign), 0, 2);
    g.sortByName = get<bool>(j, "sortByName", g.sortByName);
    g.maxCells = std::max(0, get<int>(j, "maxCells", g.maxCells));
    v4("padScale", g.padScale);
    v4("padPx", g.padPx);
    g.alwaysOnTop = get<bool>(j, "onTop", g.alwaysOnTop);
    g.adornee = get<uint64_t>(j, "adornee", g.adornee);
    g.maxDistance = std::max(0.0f, get<float>(j, "maxDistance", g.maxDistance));
    g.studsOffset = vec(j, "studsOffset", g.studsOffset);
    g.worldOffset = vec(j, "worldOffset", g.worldOffset);
    g.face = std::clamp(get<int>(j, "face", g.face), 0, 5);
    g.perStud = get<bool>(j, "perStud", g.perStud);
    g.pixelsPerStud = std::clamp(get<float>(j, "pps", g.pixelsPerStud), 1.0f, 1000.0f);
    if (j.contains("surfaceCanvas") && j["surfaceCanvas"].is_array() && j["surfaceCanvas"].size() == 2)
        try { g.surfaceCanvas = glm::max(glm::vec2(1.0f), glm::vec2(j["surfaceCanvas"][0].get<float>(), j["surfaceCanvas"][1].get<float>())); } catch (...) {}
    // UIGradient (sequences: [t, r, g, b] / [t, v], at most 20 keys, like a Trail's)
    if (auto it = j.find("gradColor"); it != j.end() && it->is_array() && !it->empty()) {
        std::vector<ColorKey> keys;
        for (auto& k : *it)
            if (k.is_array() && k.size() == 4 && k[0].is_number() && k[1].is_number() && k[2].is_number() && k[3].is_number() && keys.size() < 20)
                keys.push_back({std::clamp(k[0].get<float>(), 0.0f, 1.0f),
                                glm::clamp(glm::vec3(k[1].get<float>(), k[2].get<float>(), k[3].get<float>()), 0.0f, 1.0f)});
        if (!keys.empty()) g.gradColor = keys;
    }
    if (auto it = j.find("gradT"); it != j.end() && it->is_array() && !it->empty()) {
        std::vector<NumberKey> keys;
        for (auto& k : *it)
            if (k.is_array() && k.size() == 2 && k[0].is_number() && k[1].is_number() && keys.size() < 20)
                keys.push_back({std::clamp(k[0].get<float>(), 0.0f, 1.0f), std::clamp(k[1].get<float>(), 0.0f, 1.0f)});
        if (!keys.empty()) g.gradTransparency = keys;
    }
    g.gradRotation = get<float>(j, "gradRot", g.gradRotation);
    if (j.contains("gradOffset") && j["gradOffset"].is_array() && j["gradOffset"].size() == 2)
        try { g.gradOffset = {j["gradOffset"][0].get<float>(), j["gradOffset"][1].get<float>()}; } catch (...) {}
    g.scale = std::clamp(get<float>(j, "scale", g.scale), 0.0f, 100.0f);
    g.aspectRatio = std::clamp(get<float>(j, "aspect", g.aspectRatio), 0.001f, 1000.0f);
    g.aspectType = std::clamp(get<int>(j, "aspectType", g.aspectType), 0, 1);
    g.dominantAxis = std::clamp(get<int>(j, "dominantAxis", g.dominantAxis), 0, 1);
    g.camera = get<uint64_t>(j, "camera", g.camera);
    g.ambient = glm::clamp(vec(j, "ambient", g.ambient), 0.0f, 1.0f);
    g.lightColor = glm::clamp(vec(j, "lightColor", g.lightColor), 0.0f, 1.0f);
    g.lightDir = vec(j, "lightDir", g.lightDir);
}

nlohmann::json collisionGroupsToJson(const CollisionGroups& g) {
    json out = json::array();
    for (int i : g.slots()) out.push_back({{"name", g.names[i]}, {"id", i}, {"mask", g.masks[i]}});
    return out;
}

CollisionGroups collisionGroupsFromJson(const nlohmann::json& j) {
    CollisionGroups g;
    if (!j.is_array()) return g;
    for (const auto& e : j) {
        if (!e.is_object()) continue;
        const int id = get<int>(e, "id", -1);
        std::string name = get<std::string>(e, "name", std::string()).substr(0, CollisionGroups::kMaxNameLength);
        if (id < 0 || id >= CollisionGroups::kMax || name.empty()) continue;
        if (id == 0) name = CollisionGroups::kDefault;            // (slot 0 is always Default)
        else if (name == CollisionGroups::kDefault || g.find(name) >= 0) continue;
        g.names[id] = name;
        g.masks[id] = get<uint32_t>(e, "mask", ~0u);
    }
    // Both sides of every pair agree (a hand-edited file could say otherwise).
    for (int a = 0; a < CollisionGroups::kMax; ++a)
        for (int b = 0; b < a; ++b) {
            const bool both = ((g.masks[a] >> b) & 1u) && ((g.masks[b] >> a) & 1u);
            g.set(a, b, both);
        }
    return g;
}
// Sounds, SoundGroups and sound effects (saving and multiplayer updates).
nlohmann::json soundToJson(const SceneNode& n) {
    if (n.isSound()) {
        nlohmann::json j = {{"volume", n.volume}, {"pitch", n.pitch}, {"looped", n.looped}};
        if (n.soundGroup) j["group"] = n.soundGroup;   // (a node ID, like a constraint's ref0)
        return j;
    }
    if (n.isSoundGroup()) return {{"volume", n.volume}};
    if (n.isSoundEffect()) {
        const SoundFxProps& f = n.soundFx;
        nlohmann::json v = nlohmann::json::array();
        for (size_t i = 0; i < SoundFx::params(f.type).size(); ++i) v.push_back(f.v[i]);
        nlohmann::json j = {{"type", SoundFx::kClassNames[(int)f.type]}, {"v", v}};
        if (f.priority) j["priority"] = f.priority;
        if (!n.enabled) j["on"] = false;
        return j;
    }
    return nullptr;
}

void soundFromJson(SceneNode& n, const nlohmann::json& j) {
    if (!j.is_object()) return;
    if (n.isSound()) {
        n.volume = std::clamp(get<float>(j, "volume", n.volume), 0.0f, 10.0f);
        n.pitch = get<float>(j, "pitch", n.pitch);
        n.looped = get<bool>(j, "looped", n.looped);
        n.soundGroup = get<uint64_t>(j, "group", 0);
    } else if (n.isSoundGroup()) {
        n.volume = std::clamp(get<float>(j, "volume", 0.5f), 0.0f, 10.0f);
    } else if (n.isSoundEffect()) {
        bool found = false;
        const SoundFx::Type t = SoundFx::typeFromClass(get<std::string>(j, "type", "").c_str(), found);
        n.soundFx = soundFxDefaults(found ? t : SoundFx::Type::Reverb);
        const auto& params = SoundFx::params(n.soundFx.type);
        if (auto v = j.find("v"); v != j.end() && v->is_array())
            for (size_t i = 0; i < v->size() && i < params.size(); ++i)
                if ((*v)[i].is_number()) n.soundFx.v[i] = std::clamp((*v)[i].get<float>(), params[i].lo, params[i].hi);
        n.soundFx.priority = get<int>(j, "priority", 0);
        n.enabled = get<bool>(j, "on", true);
    }
}
} // namespace Serializer

namespace {
using Serializer::guiToJson;
using Serializer::guiFromJson;

json toJson(const SceneNode& n) {
    json j;
    j["id"]   = n.id;
    j["name"] = n.name;
    j["kind"] = kindName(n.kind);
    if (n.isBone()) j["bone"] = true;
    if (n.robloxEmbeddedMeshBone) j["embeddedMeshBone"] = true;
    // (A part posed in the Animation Editor is saved where it really is.)
    j["pos"]  = vec(n.restPose ? n.restPose->position : n.transform.position);
    j["rot"]  = vec(n.restPose ? n.restPose->rotation : n.transform.rotation);
    j["size"] = vec(n.transform.scale);
    if (n.kind == NodeKind::Part) {
        j["shape"]        = shapeName(n.primitiveType);
        if (const char* body = n.primitiveType == PrimitiveType::Mesh ? PlayerModel::nameOf(n.editMesh.get()) : nullptr) {
            j["body"] = body;   // the default character's shape: no need to save its points
        } else if (n.primitiveType == PrimitiveType::Mesh && n.editMesh) {
            // A custom mesh: "v" = x,y,z,x,y,z..., "f" = lists of corner numbers.
            json v = json::array(), f = json::array();
            for (const auto& p : n.editMesh->verts) { v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); }
            for (const auto& face : n.editMesh->faces) f.push_back(face);
            j["mesh"] = {{"v", v}, {"f", f}, {"smooth", n.editMesh->smooth}};
            // Source vertex attributes must survive a save/reload round-trip.
            if (n.editMesh->normals.size() == n.editMesh->verts.size()) {
                json values = json::array();
                for (const auto& x : n.editMesh->normals) { values.push_back(x.x); values.push_back(x.y); values.push_back(x.z); }
                j["mesh"]["n"] = std::move(values);
            }
            if (n.editMesh->colors.size() == n.editMesh->verts.size()) {
                json values = json::array();
                for (const auto& x : n.editMesh->colors) { values.push_back(x.r); values.push_back(x.g); values.push_back(x.b); values.push_back(x.a); }
                j["mesh"]["c"] = std::move(values);
            }
            if (n.editMesh->tangents.size() == n.editMesh->verts.size()) {
                json values = json::array();
                for (const auto& x : n.editMesh->tangents) { values.push_back(x.x); values.push_back(x.y); values.push_back(x.z); values.push_back(x.w); }
                j["mesh"]["t"] = std::move(values);
            }
            // Texture coordinates (imported models): u,v for each corner of each face.
            if (n.editMesh->uvs.size() == n.editMesh->faces.size()) {
                json uv = json::array();
                for (const auto& face : n.editMesh->uvs) {
                    json c = json::array();
                    for (const auto& t : face) { c.push_back(t.x); c.push_back(t.y); }
                    uv.push_back(std::move(c));
                }
                j["mesh"]["uv"] = std::move(uv);
            }
            // Bound to an armature: bone names, where each was when bound, and each vertex's weights.
            if (n.editMesh->skinned()) {
                const EditMesh& m = *n.editMesh;
                json bind = json::array(), w = json::array();
                for (const glm::mat4& b : m.bind) {
                    json f = json::array();
                    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) f.push_back(b[c][r]);
                    bind.push_back(std::move(f));
                }
                for (const auto& vw : m.weights) {
                    json one = json::array();
                    for (const auto& x : vw) { one.push_back(x.bone); one.push_back(x.w); }
                    w.push_back(std::move(one));
                }
                j["mesh"]["skin"] = {{"bones", m.bones}, {"bind", std::move(bind)}, {"w", std::move(w)}};
            }
            // Seams for Unwrap: pairs of corner numbers.
            if (!n.editMesh->seams.empty()) {
                json sm = json::array();
                for (auto [x, y] : n.editMesh->seams) { sm.push_back(x); sm.push_back(y); }
                j["mesh"]["seams"] = std::move(sm);
            }
        }
        j["color"]        = vec(n.color);
        j["transparency"] = n.transparency;
        j["material"]     = kMaterialNames[(int)n.material];
        if (!n.materialVariant.empty()) j["materialVariant"] = n.materialVariant;
        j["anchored"]     = n.anchored;
        j["canCollide"]   = n.canCollide;
        if (n.collisionGroup != CollisionGroups::kDefault) j["collisionGroup"] = n.collisionGroup;
        j["castShadow"]   = n.castShadow;
        if (n.negated) j["negate"] = {{"color", vec(n.negColor)}, {"transparency", n.negTransparency}, {"canCollide", n.negCollide}};
        if (!n.unionSource.empty()) j["union"] = json::parse(n.unionSource, nullptr, false);
        if (n.density >= 0)    j["density"]    = n.density;
        if (n.friction >= 0)   j["friction"]   = n.friction;
        if (n.elasticity >= 0) j["elasticity"] = n.elasticity;
        if (n.frictionWeight != 1.0f)   j["frictionWeight"]   = n.frictionWeight;
        if (n.elasticityWeight != 1.0f) j["elasticityWeight"] = n.elasticityWeight;
        // SpawnLocation: Neutral, TeamColor, AllowTeamChangeOnTouch (only if not Roblox's defaults)
        if (!n.spawnNeutral || n.teamColor != kDefaultTeamColor || n.allowTeamChange)
            j["spawn"] = {{"neutral", n.spawnNeutral}, {"teamColor", vec(n.teamColor)}, {"allowTeamChange", n.allowTeamChange}};
    }
    if (n.kind == NodeKind::Model && n.primaryPart) j["primaryPart"] = n.primaryPart;   // (a node ID, like a constraint's ref0)
    if (n.kind == NodeKind::Constraint) {
        j["type"] = kConstraintNames[(int)n.constraintType];
        j["ref0"] = n.ref0; j["ref1"] = n.ref1;
        j["length"] = n.length; j["stiffness"] = n.stiffness; j["damping"] = n.damping;
        j["motorSpeed"] = n.motorSpeed; j["motorTorque"] = n.motorTorque;
        j["thickness"] = n.thickness; j["color"] = vec(n.color); j["enabled"] = n.enabled;
        if (n.breakForce > 0.0f) j["breakForce"] = n.breakForce;
        if (usesJointProps(n.constraintType)) j["joint"] = Serializer::jointToJson(n.joint);
    }
    if (n.kind == NodeKind::Mover) {
        const MoverProps& m = n.mover;
        j["type"] = kMoverClassNames[(int)m.type];
        j["ref0"] = n.ref0; j["ref1"] = n.ref1; j["enabled"] = n.enabled;
        j["value"] = vec(m.value); j["maxAxes"] = vec(m.maxAxes); j["maxForce"] = m.maxForce;
        j["p"] = m.p; j["d"] = m.d; j["rot"] = vec(m.rotation); j["location"] = vec(m.location);
        j["resp"] = m.responsiveness; j["maxVel"] = m.maxVelocity;
        j["relative"] = m.relativeToAttachment; j["atCenter"] = m.atCenterOfMass; j["rigid"] = m.rigid;
    }
    if (n.kind == NodeKind::Animation) j["source"] = n.source;
    if (n.kind == NodeKind::Script) {
        j["source"]  = n.source;
        j["enabled"] = n.enabled;
    }
    if (n.kind == NodeKind::Gui) {
        j["gui"] = guiToJson(n.gui);
        j["enabled"] = n.enabled;
    }
    if (n.kind == NodeKind::Camera) j["fov"] = n.fieldOfView;
    if (n.isPart() && !n.texture.empty()) j["texture"] = n.texture;   // clothing pictures on a character
    if (n.kind == NodeKind::Decal) {
        j["texture"] = n.texture;
        j["face"] = kFaceNames[(int)n.face];
        j["color"] = vec(n.color);
        j["transparency"] = n.transparency;
    }
    if (n.kind == NodeKind::Value) {
        json v = {{"t", (int)n.value.type}, {"int", n.intValue}};
        switch (n.value.type) {
            case Attribute::Bool:   v["v"] = n.value.b; break;
            case Attribute::Number: v["v"] = n.value.n; break;
            case Attribute::String: v["v"] = n.value.s; break;
            default:                v["v"] = vec(n.value.v); break;
        }
        j["value"] = v;
    }
    if (n.kind == NodeKind::Tool) {
        j["enabled"] = n.enabled;
        j["toolTip"] = n.toolTip;
        j["canBeDropped"] = n.canBeDropped;
        j["starterTool"] = n.starterTool;
        j["gripPos"] = vec(n.gripPos);
        if (n.gripRot != kDefaultGripRot)
            j["gripRot"] = {n.gripRot[0][0], n.gripRot[0][1], n.gripRot[0][2], n.gripRot[1][0], n.gripRot[1][1],
                            n.gripRot[1][2], n.gripRot[2][0], n.gripRot[2][1], n.gripRot[2][2]};
    }
    if (n.kind == NodeKind::FluidSystem) {
        j["color"] = vec(n.color); j["viscosity"] = n.viscosity; j["surfaceTension"] = n.surfaceTension;
    }
    if (n.kind == NodeKind::FluidEmitter) {
        j["rate"] = n.fluidRate; j["velocity"] = vec(n.fluidVelocity); j["system"] = n.fluidSystem;
        j["enabled"] = n.enabled;
    }
    if (n.kind == NodeKind::Sound) {
        j["soundId"] = n.soundId; j["volume"] = n.volume; j["pitch"] = n.pitch;
        j["looped"] = n.looped; j["autoplay"] = n.autoplay;
        if (n.soundGroup) j["soundGroup"] = n.soundGroup;   // (a node ID, like a constraint's ref0)
    }
    if (n.isSoundGroup()) j["volume"] = n.volume;
    if (n.isSoundEffect()) j["soundFx"] = Serializer::soundToJson(n);
    if (n.kind == NodeKind::Light) {
        j["lightType"]  = n.lightType == LightType::Spot ? "Spot" : "Point";
        j["color"]      = vec(n.color);
        j["brightness"] = n.brightness;
        j["range"]      = n.range;
        j["spotAngle"]  = n.spotAngle;
        j["enabled"]    = n.enabled;
    }
    if (!n.visible) j["visible"]  = false;
    if (n.internal) j["internal"] = true;
    if (n.isModule) j["module"]   = true;
    if (n.isLocal)  j["local"]    = true;
    if (n.remoteFunction) j["function"] = true;
    if (n.isPrompt()) j["prompt"] = Serializer::promptToJson(n.prompt, n.enabled);
    if (n.isClickDetector()) j["click"] = Serializer::clickToJson(n.click);
    if (n.isHighlight()) j["highlight"] = Serializer::highlightToJson(n.highlight, n.enabled);
    if (n.isEffect()) j["effect"] = Serializer::effectToJson(n.effect, n.enabled, n.kind == NodeKind::Beam);
    if (n.isPostEffect()) j["post"] = Serializer::postEffectToJson(n.post, n.enabled);
    if (n.isSky()) j["sky"] = Serializer::skyToJson(n.sky);
    if (n.isMaterialVariant()) j["variant"] = Serializer::variantToJson(n.variant);
    if (n.isParticles()) j["particles"] = Serializer::particleToJson(n.particle, n.enabled);
    if (n.locked)   j["locked"]   = true;
    if (!n.tags.empty()) j["tags"] = n.tags;
    if (!n.attributes.empty()) {
        json attrs = json::array();
        for (const Attribute& a : n.attributes) {
            json e = {{"n", a.name}, {"t", (int)a.type}};
            switch (a.type) {
                case Attribute::Bool:   e["v"] = a.b; break;
                case Attribute::Number: e["v"] = a.n; break;
                case Attribute::String: e["v"] = a.s; break;
                default:                e["v"] = vec(a.v); break;
            }
            attrs.push_back(e);
        }
        j["attrs"] = attrs;
    }

    json kids = json::array();
    for (auto& c : n.children) kids.push_back(toJson(*c));
    if (!kids.empty()) j["children"] = std::move(kids);
    return j;
}

std::unique_ptr<SceneNode> fromJson(const json& j, bool freshIds) {
    auto n = std::make_unique<SceneNode>(get<std::string>(j, "name", "Part"),
                                         kindFrom(get<std::string>(j, "kind", "Part")));
    if (!freshIds && j.contains("id")) {
        n->id = j["id"].get<uint64_t>();
        SceneNode::reserveId(n->id);
    }
    n->bone = n->kind == NodeKind::Attachment && get<bool>(j, "bone", false);
    n->robloxEmbeddedMeshBone = n->bone && get<bool>(j, "embeddedMeshBone", false);
    n->transform.position = vec(j, "pos",  {0, 0, 0});
    n->transform.rotation = vec(j, "rot",  {0, 0, 0});
    n->transform.scale    = vec(j, "size", {1, 1, 1});
    if (n->kind == NodeKind::Part) {
        n->primitiveType = shapeFrom(get<std::string>(j, "shape", "None"));
        n->mesh          = MeshLibrary::get(n->primitiveType);
        if (n->primitiveType == PrimitiveType::Mesh && j.contains("body")) {
            std::string body = get<std::string>(j, "body", std::string());
            std::string keep = n->name;
            n->name = body;
            if (!PlayerModel::apply(*n)) MeshEdit::attach(*n, MeshEdit::fromPrimitive(PrimitiveType::Cube));
            n->name = keep;
        } else if (n->primitiveType == PrimitiveType::Mesh) {
            auto m = std::make_shared<EditMesh>();
            if (auto it = j.find("mesh"); it != j.end() && it->is_object()) {
                const json& v = (*it)["v"];
                for (size_t i = 0; i + 2 < v.size(); i += 3)
                    m->verts.push_back({v[i].get<float>(), v[i + 1].get<float>(), v[i + 2].get<float>()});
                const json* uv = it->contains("uv") && (*it)["uv"].is_array() ? &(*it)["uv"] : nullptr;
                size_t fi = 0;
                for (const auto& face : (*it)["f"]) {
                    std::vector<uint32_t> fv;
                    for (const auto& k : face) if (k.get<uint32_t>() < m->verts.size()) fv.push_back(k.get<uint32_t>());
                    if (fv.size() >= 3) {
                        if (uv && fi < uv->size() && (*uv)[fi].size() == fv.size() * 2 && m->uvs.size() == m->faces.size()) {
                            std::vector<glm::vec2> c;
                            for (size_t k = 0; k < fv.size(); ++k) c.push_back({(*uv)[fi][k * 2].get<float>(), (*uv)[fi][k * 2 + 1].get<float>()});
                            m->uvs.push_back(std::move(c));
                        }
                        m->faces.push_back(std::move(fv));
                    }
                    ++fi;
                }
                if (m->uvs.size() != m->faces.size()) m->uvs.clear();
                auto readVec3 = [&](const char* key, std::vector<glm::vec3>& output) {
                    auto values = it->find(key);
                    if (values == it->end() || !values->is_array() || values->size() != m->verts.size() * 3) return;
                    std::vector<glm::vec3> parsed;
                    parsed.reserve(m->verts.size());
                    for (size_t i = 0; i < values->size(); i += 3) {
                        float x[3];
                        for (int k = 0; k < 3; ++k) {
                            if (!(*values)[i + k].is_number()) return;
                            x[k] = (*values)[i + k].get<float>();
                            if (!std::isfinite(x[k]) || std::abs(x[k]) > 1.0e6f) return;
                        }
                        parsed.emplace_back(x[0], x[1], x[2]);
                    }
                    output = std::move(parsed);
                };
                auto readVec4 = [&](const char* key, std::vector<glm::vec4>& output) {
                    auto values = it->find(key);
                    if (values == it->end() || !values->is_array() || values->size() != m->verts.size() * 4) return;
                    std::vector<glm::vec4> parsed;
                    parsed.reserve(m->verts.size());
                    for (size_t i = 0; i < values->size(); i += 4) {
                        float x[4];
                        for (int k = 0; k < 4; ++k) {
                            if (!(*values)[i + k].is_number()) return;
                            x[k] = (*values)[i + k].get<float>();
                            if (!std::isfinite(x[k]) || std::abs(x[k]) > 1.0e6f) return;
                        }
                        parsed.emplace_back(x[0], x[1], x[2], x[3]);
                    }
                    output = std::move(parsed);
                };
                readVec3("n", m->normals);
                readVec4("c", m->colors);
                readVec4("t", m->tangents);
                for (auto& color : m->colors) color = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));
                for (auto& tangent : m->tangents) {
                    glm::vec3 direction(tangent);
                    if (glm::length(direction) > 1e-6f) direction = glm::normalize(direction);
                    else direction = glm::vec3(1, 0, 0);
                    tangent = glm::vec4(direction, tangent.w < 0.0f ? -1.0f : 1.0f);
                }
                if (auto sk = it->find("skin"); sk != it->end() && sk->is_object()) {
                    const json& bones = (*sk)["bones"];
                    const json& bind = (*sk)["bind"];
                    const json& w = (*sk)["w"];
                    if (bones.is_array() && bind.is_array() && w.is_array() && bones.size() == bind.size() &&
                        w.size() == m->verts.size() && bones.size() < 65535) {
                        bool good = true;
                        for (size_t b = 0; b < bones.size() && good; ++b) {
                            if (!bones[b].is_string() || !bind[b].is_array() || bind[b].size() != 16) { good = false; break; }
                            m->bones.push_back(bones[b].get<std::string>());
                            glm::mat4 f;
                            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) {
                                const json& x = bind[b][c * 4 + r];
                                f[c][r] = x.is_number() ? x.get<float>() : 0.0f;
                            }
                            m->bind.push_back(f);
                        }
                        for (size_t v = 0; v < w.size() && good; ++v) {
                            std::vector<EditMesh::Weight> one;
                            if (!w[v].is_array()) { good = false; break; }
                            for (size_t k = 0; k + 1 < w[v].size() && one.size() < 4; k += 2) {
                                if (!w[v][k].is_number_unsigned() || !w[v][k + 1].is_number()) continue;
                                uint32_t b = w[v][k].get<uint32_t>();
                                float wt = w[v][k + 1].get<float>();
                                if (b < m->bones.size() && std::isfinite(wt)) one.push_back({(uint16_t)b, std::clamp(wt, 0.0f, 1.0f)});
                            }
                            m->weights.push_back(std::move(one));
                        }
                        if (!good) { m->bones.clear(); m->bind.clear(); m->weights.clear(); }
                    }
                }
                if (auto sm = it->find("seams"); sm != it->end() && sm->is_array())
                    for (size_t i = 0; i + 1 < sm->size(); i += 2) {
                        if (!(*sm)[i].is_number_unsigned() || !(*sm)[i + 1].is_number_unsigned()) continue;
                        uint32_t x = (*sm)[i].get<uint32_t>(), y = (*sm)[i + 1].get<uint32_t>();
                        if (x < m->verts.size() && y < m->verts.size() && x != y) m->seams.push_back({std::min(x, y), std::max(x, y)});
                    }
                std::sort(m->seams.begin(), m->seams.end());
                m->seams.erase(std::unique(m->seams.begin(), m->seams.end()), m->seams.end());
                m->smooth = get<bool>(*it, "smooth", false);
            }
            if (m->faces.empty()) m = MeshEdit::fromPrimitive(PrimitiveType::Cube);
            MeshEdit::attach(*n, m);
        }
        n->color         = vec(j, "color", n->color);
        n->transparency  = get<float>(j, "transparency", 0.0f);
        n->material      = materialFrom(get<std::string>(j, "material", "Plastic"));
        n->materialVariant = get<std::string>(j, "materialVariant", "");
        n->anchored      = get<bool>(j, "anchored", true);
        n->canCollide    = get<bool>(j, "canCollide", true);
        n->collisionGroup = get<std::string>(j, "collisionGroup", CollisionGroups::kDefault).substr(0, CollisionGroups::kMaxNameLength);
        if (n->collisionGroup.empty()) n->collisionGroup = CollisionGroups::kDefault;
        n->castShadow    = get<bool>(j, "castShadow", true);
        if (auto it = j.find("negate"); it != j.end() && it->is_object()) {
            n->negated = true;
            n->negColor = vec(*it, "color", glm::vec3(0.64f));
            n->negTransparency = get<float>(*it, "transparency", 0.0f);
            n->negCollide = get<bool>(*it, "canCollide", true);
        }
        if (auto it = j.find("union"); it != j.end() && it->is_object()) n->unionSource = it->dump();
        n->density       = get<float>(j, "density", -1.0f);
        n->friction      = get<float>(j, "friction", -1.0f);
        n->elasticity    = get<float>(j, "elasticity", -1.0f);
        n->frictionWeight   = get<float>(j, "frictionWeight", 1.0f);
        n->elasticityWeight = get<float>(j, "elasticityWeight", 1.0f);
        if (auto sp = j.find("spawn"); sp != j.end() && sp->is_object()) {
            n->spawnNeutral    = get<bool>(*sp, "neutral", true);
            n->teamColor       = vec(*sp, "teamColor", n->teamColor);
            n->allowTeamChange = get<bool>(*sp, "allowTeamChange", false);
        }
    }
    if (n->kind == NodeKind::Model) n->primaryPart = get<uint64_t>(j, "primaryPart", 0);
    if (n->kind == NodeKind::Constraint) {
        std::string type = get<std::string>(j, "type", "Rope");
        for (int i = 0; i < kConstraintTypeCount; ++i) if (type == kConstraintNames[i]) n->constraintType = (ConstraintType)i;
        if (auto it = j.find("joint"); it != j.end()) Serializer::jointFromJson(*it, n->joint);
        n->ref0 = get<uint64_t>(j, "ref0", 0);
        n->ref1 = get<uint64_t>(j, "ref1", 0);
        n->length = get<float>(j, "length", -1.0f);
        n->stiffness = get<float>(j, "stiffness", 200.0f);
        n->damping = get<float>(j, "damping", 5.0f);
        n->motorSpeed = get<float>(j, "motorSpeed", 0.0f);
        n->motorTorque = get<float>(j, "motorTorque", 0.0f);
        n->thickness = get<float>(j, "thickness", 0.1f);
        n->breakForce = get<float>(j, "breakForce", 0.0f);
        n->color = vec(j, "color", {0.45f, 0.32f, 0.2f});
    }
    if (n->kind == NodeKind::Mover) {
        const std::string type = get<std::string>(j, "type", "BodyVelocity");
        for (int i = 0; i < kMoverTypeCount; ++i) if (type == kMoverClassNames[i]) n->mover = moverDefaults((MoverType)i);
        MoverProps& m = n->mover;
        n->ref0 = get<uint64_t>(j, "ref0", 0);
        n->ref1 = get<uint64_t>(j, "ref1", 0);
        n->enabled = get<bool>(j, "enabled", true);
        m.value = vec(j, "value", m.value);
        m.maxAxes = vec(j, "maxAxes", m.maxAxes);
        m.maxForce = get<float>(j, "maxForce", m.maxForce);
        m.p = get<float>(j, "p", m.p);
        m.d = get<float>(j, "d", m.d);
        m.rotation = vec(j, "rot", m.rotation);
        m.location = vec(j, "location", m.location);
        m.responsiveness = get<float>(j, "resp", m.responsiveness);
        m.maxVelocity = get<float>(j, "maxVel", m.maxVelocity);
        m.relativeToAttachment = get<bool>(j, "relative", m.relativeToAttachment);
        m.atCenterOfMass = get<bool>(j, "atCenter", m.atCenterOfMass);
        m.rigid = get<bool>(j, "rigid", m.rigid);
    }
    if (n->kind == NodeKind::Light) {
        n->lightType  = get<std::string>(j, "lightType", "Point") == "Spot" ? LightType::Spot : LightType::Point;
        n->color      = vec(j, "color", {1, 1, 1});
        n->brightness = get<float>(j, "brightness", 2.0f);
        n->range      = get<float>(j, "range", 14.0f);
        n->spotAngle  = get<float>(j, "spotAngle", 60.0f);
    }
    if (n->kind == NodeKind::Gui) {
        if (j.contains("gui") && j["gui"].is_object()) guiFromJson(n->gui, j["gui"]);
        n->enabled = get<bool>(j, "enabled", true);
    }
    if (n->kind == NodeKind::Camera) n->fieldOfView = std::clamp(get<float>(j, "fov", 70.0f), 1.0f, 120.0f);
    if (n->isPart()) n->texture = get<std::string>(j, "texture", std::string());
    if (n->kind == NodeKind::Decal) {
        n->texture = get<std::string>(j, "texture", std::string());
        std::string f = get<std::string>(j, "face", std::string("Front"));
        for (int i = 0; i < 6; ++i) if (f == kFaceNames[i]) n->face = (Face)i;
        n->color = vec(j, "color", {1, 1, 1});
        n->transparency = get<float>(j, "transparency", 0.0f);
    }
    if (n->kind == NodeKind::Value && j.contains("value") && j["value"].is_object()) {
        const json& vj = j["value"];
        n->value.type = (Attribute::Type)std::clamp(vj.value("t", 1), 0, 4);
        n->intValue = vj.value("int", false);
        const json v = vj.contains("v") ? vj["v"] : json();
        if (n->value.type == Attribute::Bool && v.is_boolean()) n->value.b = v.get<bool>();
        else if (n->value.type == Attribute::Number && v.is_number()) n->value.n = v.get<double>();
        else if (n->value.type == Attribute::String && v.is_string()) n->value.s = v.get<std::string>();
        else if (v.is_array() && v.size() == 3) n->value.v = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
    }
    if (n->kind == NodeKind::Tool) {
        n->toolTip      = get<std::string>(j, "toolTip", std::string());
        n->canBeDropped = get<bool>(j, "canBeDropped", true);
        n->starterTool  = get<bool>(j, "starterTool", false);
        n->gripPos      = vec(j, "gripPos", {0, 0, 0});
        if (auto g = j.find("gripRot"); g != j.end() && g->is_array() && g->size() == 9)
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r) n->gripRot[c][r] = (*g)[(size_t)(c * 3 + r)].get<float>();
    }
    if (n->kind == NodeKind::FluidSystem) {
        n->color          = vec(j, "color", {0.12f, 0.56f, 1.0f});
        n->viscosity      = get<float>(j, "viscosity", 0.015f);
        n->surfaceTension = get<float>(j, "surfaceTension", 0.0f);
    }
    if (n->kind == NodeKind::FluidEmitter) {
        n->fluidRate     = get<float>(j, "rate", 500.0f);
        n->fluidVelocity = vec(j, "velocity", {0, -10, 0});
        n->fluidSystem   = get<uint64_t>(j, "system", 0);
    }
    if (n->kind == NodeKind::Sound) {
        n->soundId  = get<std::string>(j, "soundId", "coin");
        n->volume   = get<float>(j, "volume", 0.6f);
        n->pitch    = get<float>(j, "pitch", 1.0f);
        n->looped   = get<bool>(j, "looped", false);
        n->autoplay = get<bool>(j, "autoplay", false);
        n->soundGroup = get<uint64_t>(j, "soundGroup", 0);
    }
    if (n->isSoundGroup()) n->volume = std::clamp(get<float>(j, "volume", 0.5f), 0.0f, 10.0f);
    n->source        = get<std::string>(j, "source", "");
    n->enabled = get<bool>(j, "enabled", true);
    n->visible       = get<bool>(j, "visible", true);
    n->internal      = get<bool>(j, "internal", false);
    n->isModule      = get<bool>(j, "module", false);
    n->isLocal       = get<bool>(j, "local", false);
    n->remoteFunction = get<bool>(j, "function", false);
    if (auto p = j.find("prompt"); p != j.end() && p->is_object()) Serializer::promptFromJson(*p, n->prompt, n->enabled);
    if (auto c = j.find("click"); c != j.end() && c->is_object()) Serializer::clickFromJson(*c, n->click);
    if (auto h = j.find("highlight"); h != j.end() && h->is_object()) Serializer::highlightFromJson(*h, n->highlight, n->enabled);
    if (auto e = j.find("effect"); e != j.end() && e->is_object()) Serializer::effectFromJson(*e, n->effect, n->enabled);
    if (auto f = j.find("soundFx"); f != j.end() && f->is_object() && n->isSoundEffect()) Serializer::soundFromJson(*n, *f);
    if (auto e = j.find("post"); n->isPostEffect() && e != j.end() && e->is_object()) Serializer::postEffectFromJson(*e, n->post, n->enabled);
    if (auto e = j.find("sky"); n->isSky() && e != j.end() && e->is_object()) Serializer::skyFromJson(*e, n->sky);
    if (auto e = j.find("variant"); n->isMaterialVariant() && e != j.end() && e->is_object()) Serializer::variantFromJson(*e, n->variant);
    if (auto e = j.find("particles"); e != j.end() && e->is_object() && n->isParticles()) Serializer::particleFromJson(*e, n->particle, n->enabled);
    n->locked        = get<bool>(j, "locked", false);
    if (auto t = j.find("tags"); t != j.end() && t->is_array())
        for (auto& v : *t) if (v.is_string()) n->tags.push_back(v.get<std::string>());
    if (auto at = j.find("attrs"); at != j.end() && at->is_array())
        for (auto& e : *at) {
            if (!e.is_object() || !e.contains("n") || !e.contains("v")) continue;
            Attribute a;
            a.name = e["n"].get<std::string>();
            a.type = (Attribute::Type)std::clamp(e.value("t", 1), 0, 4);
            const json& v = e["v"];
            if (a.type == Attribute::Bool && v.is_boolean()) a.b = v.get<bool>();
            else if (a.type == Attribute::Number && v.is_number()) a.n = v.get<double>();
            else if (a.type == Attribute::String && v.is_string()) a.s = v.get<std::string>();
            else if (v.is_array() && v.size() == 3) a.v = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
            n->attributes.push_back(a);
        }
    // Spawns saved before SpawnLocation had team properties used a TeamColor attribute.
    if (n->kind == NodeKind::Part && !j.contains("spawn") && n->isSpawnLocation())
        if (const Attribute* tc = n->findAttribute("TeamColor"); tc && tc->type == Attribute::Color3) {
            n->teamColor = tc->v;
            n->spawnNeutral = false;
        }

    if (auto it = j.find("children"); it != j.end() && it->is_array())
        for (const auto& c : *it) n->addChild(fromJson(c, freshIds));
    return n;
}

// Every Environment field, listed once so saving and loading can't drift apart.
#define ENV_FIELDS(X)                                                              \
    X(clockTime) X(sunAzimuth) X(sunElevation) X(sunColor) X(sunIntensity) X(sunSize)            \
    X(shadows) X(shadowSoftness) X(shadowStrength) X(shadowDistance)                \
    X(ambientColor) X(groundAmbient) X(ambientIntensity) X(reflections)             \
    X(showSky) X(skyZenith) X(skyHorizon) X(skyGround) X(skyBrightness)             \
    X(clouds) X(cloudCover) X(cloudSpeed) X(cloudColor) X(stars)                    \
    X(fogEnabled) X(fogColor) X(fogDensity) X(fogSunGlow)                           \
    X(exposure) X(bloomIntensity) X(bloomThreshold) X(aoIntensity) X(contrast)      \
    X(saturation) X(vignette) X(tint) X(weather) X(weatherAmount)

json toJsonValue(float v) { return v; }
json toJsonValue(bool v) { return v; }
json toJsonValue(int v) { return v; }
json toJsonValue(const glm::vec3& v) { return vec(v); }
void fromJsonValue(const json& j, const char* k, float& v) { v = get<float>(j, k, v); }
void fromJsonValue(const json& j, const char* k, bool& v)  { v = get<bool>(j, k, v); }
void fromJsonValue(const json& j, const char* k, int& v)   { v = get<int>(j, k, v); }
void fromJsonValue(const json& j, const char* k, glm::vec3& v) { v = vec(j, k, v); }

json envToJson(const Environment& e) {
    json j;
#define SAVE_FIELD(f) j[#f] = toJsonValue(e.f);
    ENV_FIELDS(SAVE_FIELD)
#undef SAVE_FIELD
    return j;
}

Environment envFromJson(const json& j) {
    Environment e;
#define LOAD_FIELD(f) fromJsonValue(j, #f, e.f);
    ENV_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
    return e;
}

} // namespace

namespace Serializer {

namespace {
json settingsJson(Scene& scene) {
    json j;
    j["format"]  = "GutsAndBolts";
    j["version"] = 2;
    j["info"] = {{"title", scene.info().title}, {"description", scene.info().description},
                 {"author", scene.info().author}};
    if (!scene.info().publishedId.empty()) j["info"]["published"] = scene.info().publishedId;
    j["environment"] = envToJson(scene.environment());
    if (!scene.terrain().empty()) j["terrain"] = scene.terrain().toJson();
    const WorldSettings& ws = scene.world();
    j["world"] = {{"gravity", ws.gravity}, {"fallenPartsHeight", ws.fallenPartsHeight},
                  {"deathStyle", (int)ws.deathStyle}, {"gore", (int)ws.gore},
                  {"dismemberment", ws.dismemberment}, {"knockdowns", ws.knockdowns}, {"fallDamage", ws.fallDamage},
                  {"fallDamageSpeed", ws.fallDamageSpeed}, {"spawnForceField", ws.spawnForceField},
                  {"fallDamageScale", ws.fallDamageScale}, {"bloodColor", vec(ws.bloodColor)},
                  {"bloodAmount", ws.bloodAmount}, {"bloodStay", ws.bloodStay},
                  {"playerCollisions", ws.playerCollisions}, {"voiceChat", ws.voiceChat}, {"maxFluidParticles", ws.maxFluidParticles},
                  {"orthographic", ws.orthographic}, {"orthographicSize", ws.orthographicSize}};
    if (!ws.collisionGroups.isDefault())   // (only games that use them)
        j["world"]["collisionGroups"] = Serializer::collisionGroupsToJson(ws.collisionGroups);
    if (ws.playerCamera != CameraOptions{})   // (only games that changed them)
        j["world"]["playerCamera"] = {{"lockFirstPerson", ws.playerCamera.lockFirstPerson},
                                      {"minZoom", ws.playerCamera.minZoom}, {"maxZoom", ws.playerCamera.maxZoom}};
    if (Player* p = scene.player()) {
        const Humanoid& h = p->humanoid();
        j["player"] = {
            {"rootId", p->rootId()}, {"spawn", vec(p->spawn())}, {"hat", (int)p->hat()},
            {"humanoid", {{"walkSpeed", h.walkSpeed}, {"jumpPower", h.jumpPower},
                          {"jumpHeight", h.jumpHeight}, {"useJumpPower", h.useJumpPower},
                          {"health", h.health}, {"maxHealth", h.maxHealth},
                          {"autoRotate", h.autoRotate}}},
        };
    }
    return j;
}

void applySettings(Scene& scene, const json& j) {
    GameInfo info;
    if (j.contains("info")) {
        info.title       = get<std::string>(j["info"], "title", info.title);
        info.description = get<std::string>(j["info"], "description", info.description);
        info.author      = get<std::string>(j["info"], "author", info.author);
        info.publishedId = get<std::string>(j["info"], "published", std::string());
    }
    scene.info() = info;

    scene.environment() = j.contains("environment") ? envFromJson(j["environment"]) : Environment{};
    // Terrain: only rebuilt when it's different (undo and Team Create re-apply
    // the settings often, and the terrain can be big).
    {
        const json t = j.contains("terrain") ? j["terrain"] : json();
        if (t.is_null() ? !scene.terrain().empty() : t != scene.terrain().toJson()) scene.terrain().fromJson(t);
    }
    WorldSettings w;
    if (j.contains("world")) {
        w.gravity           = get<float>(j["world"], "gravity", w.gravity);
        w.fallenPartsHeight = get<float>(j["world"], "fallenPartsHeight", w.fallenPartsHeight);
        w.deathStyle        = (DeathStyle)get<int>(j["world"], "deathStyle", (int)w.deathStyle);
        w.gore              = (GoreLevel)get<int>(j["world"], "gore", (int)w.gore);
        w.dismemberment     = get<bool>(j["world"], "dismemberment", w.dismemberment);
        w.knockdowns        = get<bool>(j["world"], "knockdowns", w.knockdowns);
        w.fallDamage        = get<bool>(j["world"], "fallDamage", w.fallDamage);
        w.fallDamageSpeed   = get<float>(j["world"], "fallDamageSpeed", w.fallDamageSpeed);
        w.spawnForceField   = get<float>(j["world"], "spawnForceField", w.spawnForceField);
        w.fallDamageScale   = get<float>(j["world"], "fallDamageScale", w.fallDamageScale);
        w.playerCollisions  = get<bool>(j["world"], "playerCollisions", w.playerCollisions);
        w.voiceChat         = get<bool>(j["world"], "voiceChat", w.voiceChat);
        w.orthographic      = get<bool>(j["world"], "orthographic", w.orthographic);
        w.orthographicSize  = std::clamp(get<float>(j["world"], "orthographicSize", w.orthographicSize), 0.0f, 2000.0f);
        w.maxFluidParticles = std::clamp(get<int>(j["world"], "maxFluidParticles", w.maxFluidParticles), 0, 1 << 20);
        w.bloodColor        = vec(j["world"], "bloodColor", w.bloodColor);
        w.bloodAmount       = get<float>(j["world"], "bloodAmount", w.bloodAmount);
        w.bloodStay         = get<float>(j["world"], "bloodStay", w.bloodStay);
        if (j["world"].contains("collisionGroups")) w.collisionGroups = Serializer::collisionGroupsFromJson(j["world"]["collisionGroups"]);
        if (j["world"].contains("playerCamera") && j["world"]["playerCamera"].is_object()) {
            const json& pc = j["world"]["playerCamera"];
            CameraOptions& o = w.playerCamera;
            o.lockFirstPerson = get<bool>(pc, "lockFirstPerson", o.lockFirstPerson);
            o.minZoom = std::clamp(get<float>(pc, "minZoom", o.minZoom), 0.0f, CameraOptions::kLimit);
            o.maxZoom = std::clamp(get<float>(pc, "maxZoom", o.maxZoom), 0.0f, CameraOptions::kLimit);
        }
    }
    scene.world() = w;

    // Characters and NPC rigs saved with the old 3D face (eye and smile parts): the flat picture instead.
    {
        std::vector<SceneNode*> heads;
        scene.forEach([&](SceneNode* n) {
            if (n->name != "Head" || !n->isPart()) return;
            for (auto& c : n->children)
                if (c->name.rfind("Eye", 0) == 0 || c->name.rfind("Smile", 0) == 0) { heads.push_back(n); return; }
        });
        for (SceneNode* h : heads) {
            Player::removeOldFace(scene, h);
            if (h->texture.empty()) Player::addFace(h);
        }
    }

    if (Player* p = scene.player()) {
        p->resetSettings();
        p->setRootId(0);
        if (j.contains("player")) {
            const json& pj = j["player"];
            p->setRootId(get<uint64_t>(pj, "rootId", 0));
            Player::upgradeRig(p->root());   // saved with the old blocky character?
            p->upgradeFace();
            p->setSpawn(vec(pj, "spawn", {0, 0, 0}));
            p->rememberHat((HatStyle)get<int>(pj, "hat", 0));
            if (pj.contains("humanoid")) {
                const json& hj = pj["humanoid"];
                Humanoid& h  = p->humanoid();
                h.walkSpeed  = get<float>(hj, "walkSpeed", h.walkSpeed);
                h.jumpPower  = get<float>(hj, "jumpPower", h.jumpPower);
                if (hj.contains("useJumpPower")) {
                    h.useJumpPower = get<bool>(hj, "useJumpPower", false);
                    h.jumpHeight   = get<float>(hj, "jumpHeight", h.jumpHeight);
                } else if (std::abs(h.jumpPower - 8.5f) < 0.01f) {
                    h.jumpPower = Humanoid{}.jumpPower;   // games saved with the old, too-low default: jump properly now
                } else {
                    h.useJumpPower = true;   // the game picked its own Jump Power: keep it
                }
                h.maxHealth  = get<float>(hj, "maxHealth", h.maxHealth);
                h.health     = get<float>(hj, "health", h.maxHealth);
                h.autoRotate = get<bool>(hj, "autoRotate", h.autoRotate);
            }
        }
    }
}
} // namespace

std::string saveScene(Scene& scene, bool pretty) {
    json j = settingsJson(scene);
    j["workspace"] = toJson(*scene.root());
    return pretty ? j.dump(2) : j.dump();
}

bool loadScene(Scene& scene, const std::string& text, std::string* error) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("workspace")) {
        if (error) *error = "This doesn't look like a Guts and Bolts scene file.";
        return false;
    }
    try {
        auto root = fromJson(j["workspace"], false);
        root->kind = NodeKind::Model;
        scene.replaceRoot(std::move(root));
        applySettings(scene, j);
        if (Player* p = scene.player())
            if (!p->root()) p->build();   // older files / missing character
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

std::string settingsToString(Scene& scene) { return settingsJson(scene).dump(); }

void settingsFromString(Scene& scene, const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (j.is_object()) {
        try { applySettings(scene, j); } catch (...) {}
    }
}

std::string nodeShallowToString(const SceneNode& node) {
    json j = toJson(node);
    j.erase("children");
    return j.dump();
}

std::unique_ptr<SceneNode> nodeShallowFromString(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return nullptr;
    j.erase("children");
    try { return fromJson(j, false); } catch (...) { return nullptr; }
}

void applyNodeShallow(SceneNode& dst, const std::string& text) {
    auto src = nodeShallowFromString(text);
    if (!src) return;
    // Copy every saved property, keeping the node's place in the tree.
    dst.name = src->name;           dst.kind = src->kind;
    dst.transform = src->transform; dst.primitiveType = src->primitiveType;
    dst.mesh = src->mesh;           dst.color = src->color;
    dst.editMesh = src->editMesh;
    dst.visible = src->visible;     dst.internal = src->internal;
    dst.transparency = src->transparency; dst.material = src->material;
    dst.anchored = src->anchored;   dst.canCollide = src->canCollide; dst.castShadow = src->castShadow;
    dst.collisionGroup = src->collisionGroup;
    dst.negated = src->negated;     dst.negColor = src->negColor;
    dst.negTransparency = src->negTransparency; dst.negCollide = src->negCollide;
    dst.unionSource = src->unionSource;
    dst.source = src->source;       dst.enabled = src->enabled;
    dst.isModule = src->isModule;   dst.locked = src->locked;
    dst.isLocal = src->isLocal;     dst.remoteFunction = src->remoteFunction;
    dst.prompt = src->prompt;       dst.highlight = src->highlight;
    dst.click = src->click;
    dst.effect = src->effect;       dst.particle = src->particle;
    dst.post = src->post;           dst.sky = src->sky;
    dst.variant = src->variant;     dst.materialVariant = src->materialVariant;
    dst.tags = src->tags;           dst.attributes = src->attributes;
    dst.lightType = src->lightType; dst.brightness = src->brightness;
    dst.range = src->range;         dst.spotAngle = src->spotAngle;
    dst.soundId = src->soundId;     dst.volume = src->volume; dst.pitch = src->pitch;
    dst.looped = src->looped;       dst.autoplay = src->autoplay;
    dst.soundGroup = src->soundGroup; dst.soundFx = src->soundFx;
    dst.toolTip = src->toolTip;     dst.canBeDropped = src->canBeDropped;
    dst.value = src->value;         dst.intValue = src->intValue;
    dst.texture = src->texture;     dst.face = src->face;
    dst.starterTool = src->starterTool; dst.gripPos = src->gripPos; dst.gripRot = src->gripRot;
    dst.density = src->density;     dst.friction = src->friction; dst.elasticity = src->elasticity;
    dst.frictionWeight = src->frictionWeight; dst.elasticityWeight = src->elasticityWeight;
    dst.primaryPart = src->primaryPart;
    dst.spawnNeutral = src->spawnNeutral; dst.teamColor = src->teamColor; dst.allowTeamChange = src->allowTeamChange;
    dst.constraintType = src->constraintType; dst.ref0 = src->ref0; dst.ref1 = src->ref1;
    dst.length = src->length;       dst.stiffness = src->stiffness; dst.damping = src->damping;
    dst.breakForce = src->breakForce;
    dst.motorSpeed = src->motorSpeed; dst.motorTorque = src->motorTorque; dst.thickness = src->thickness;
    dst.joint = src->joint;
    dst.viscosity = src->viscosity; dst.surfaceTension = src->surfaceTension;
    dst.fluidRate = src->fluidRate; dst.fluidVelocity = src->fluidVelocity; dst.fluidSystem = src->fluidSystem;
    dst.mover = src->mover;
    dst.fieldOfView = src->fieldOfView;
}

std::string nodeToString(const SceneNode& node) { return toJson(node).dump(); }

std::string environmentToString(const Environment& env) { return envToJson(env).dump(); }

void environmentFromString(Environment& env, const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (j.is_object()) env = envFromJson(j);
}

std::unique_ptr<SceneNode> nodeFromString(const std::string& text, bool freshIds) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return nullptr;
    try {
        if (!freshIds) return fromJson(j, false);
        auto original = fromJson(j, false);
        return clone(*original);
    } catch (...) { return nullptr; }
}

namespace {
void collectIds(const SceneNode& a, const SceneNode& b, std::unordered_map<uint64_t, uint64_t>& map) {
    map[a.id] = b.id;
    for (size_t i = 0; i < a.children.size() && i < b.children.size(); ++i)
        collectIds(*a.children[i], *b.children[i], map);
}
void remapRefs(SceneNode& n, const std::unordered_map<uint64_t, uint64_t>& map) {
    if (n.isConstraint() || n.isMover()) {
        if (auto it = map.find(n.ref0); it != map.end()) n.ref0 = it->second;
        if (auto it = map.find(n.ref1); it != map.end()) n.ref1 = it->second;
    }
    if (n.isGui() && n.gui.adornee)   // (a copied BillboardGui sits on the copied part, if it came along)
        if (auto it = map.find(n.gui.adornee); it != map.end()) n.gui.adornee = it->second;
    if (n.isGui() && n.gui.camera)   // (a copied ViewportFrame looks through the copied Camera, if it came along)
        if (auto it = map.find(n.gui.camera); it != map.end()) n.gui.camera = it->second;
    if (n.isHighlight() && n.highlight.adornee)   // (the same for a copied Highlight)
        if (auto it = map.find(n.highlight.adornee); it != map.end()) n.highlight.adornee = it->second;
    if (n.isEffect()) {   // (a copied Trail / Beam uses the copied attachments)
        if (auto it = map.find(n.effect.a0); it != map.end()) n.effect.a0 = it->second;
        if (auto it = map.find(n.effect.a1); it != map.end()) n.effect.a1 = it->second;
    }
    if (n.isSound() && n.soundGroup)   // (a copied sound plays through the copied group, if it came along)
        if (auto it = map.find(n.soundGroup); it != map.end()) n.soundGroup = it->second;
    if (n.kind == NodeKind::Model && n.primaryPart)   // (a copied model's PrimaryPart is its own copy)
        if (auto it = map.find(n.primaryPart); it != map.end()) n.primaryPart = it->second;
    if (n.kind == NodeKind::FluidEmitter)   // (a copied emitter uses the copied liquid, if it came along)
        if (auto it = map.find(n.fluidSystem); it != map.end()) n.fluidSystem = it->second;
    for (auto& c : n.children) remapRefs(*c, map);
}
} // namespace

std::unique_ptr<SceneNode> clone(const SceneNode& node) {
    auto copy = fromJson(toJson(node), true);
    // Ropes / hinges inside the copy connect to the copied attachments.
    std::unordered_map<uint64_t, uint64_t> map;
    collectIds(node, *copy, map);
    remapRefs(*copy, map);
    return copy;
}

bool loadGameFile(Scene& scene, const std::string& path, std::string* error) {
    if (RobloxFile::isPlace(path)) {
        RobloxFile::Report report;
        std::string err;
        if (!RobloxFile::importPlace(scene, path, report, err)) { if (error) *error = err; return false; }
        return true;
    }
    std::string text;
    if (!readFile(path, text)) { if (error) *error = "couldn't read the file"; return false; }
    return loadScene(scene, text, error);
}

bool writeFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << text;
    return (bool)f;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

} // namespace Serializer
