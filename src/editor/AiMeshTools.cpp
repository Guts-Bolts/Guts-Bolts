// Studio's AI making its own 3D things, like a hat: edit_mesh reshapes a part's mesh
// with Modeling mode's tools (extrude, inset, move, scale...), texture_mesh lays it
// out flat and paints a picture for it, and make_accessory puts it on the Accessory
// Mannequin as a hat so the person can press Upload. The tool descriptions are in
// AiTools.cpp; Editor::runAiTool sends these three here.
#include "Editor.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/EditMesh.h"
#include "../renderer/Textures.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../online/OnlineClient.h"

#include <stb_image_write.h>   // (its code is in renderer/Textures.cpp)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

using nlohmann::json;

namespace {

json vec(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
std::string idOf(const SceneNode* n) { return "#" + std::to_string(n->id); }

bool readVec3(const json& v, glm::vec3& out) {
    if (v.is_number()) { out = glm::vec3(v.get<float>()); return true; }
    if (!v.is_array() || v.size() != 3) return false;
    for (int i = 0; i < 3; ++i) { if (!v[i].is_number()) return false; out[i] = v[i].get<float>(); }
    return true;
}

// A colour: [r, g, b] (0-1, or 0-255 when a number is over 1), "#rrggbb" or a plain name.
bool readColor(const json& v, glm::vec3& out) {
    if (v.is_array() && v.size() == 3 && readVec3(v, out)) {
        if (out.x > 1.0f || out.y > 1.0f || out.z > 1.0f) out /= 255.0f;
        out = glm::clamp(out, glm::vec3(0.0f), glm::vec3(1.0f));
        return true;
    }
    if (!v.is_string()) return false;
    std::string s = v.get<std::string>();
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    if (s.size() == 7 && s[0] == '#' && s.find_first_not_of("0123456789abcdef", 1) == std::string::npos) {
        const unsigned long x = std::stoul(s.substr(1), nullptr, 16);
        out = glm::vec3((x >> 16) & 255, (x >> 8) & 255, x & 255) / 255.0f;
        return true;
    }
    static const std::map<std::string, glm::vec3> names = {
        {"white", {1, 1, 1}}, {"black", {0.07f, 0.07f, 0.07f}}, {"grey", {0.5f, 0.5f, 0.5f}}, {"gray", {0.5f, 0.5f, 0.5f}},
        {"red", {0.85f, 0.12f, 0.12f}}, {"dark red", {0.5f, 0.05f, 0.05f}}, {"orange", {0.95f, 0.5f, 0.1f}},
        {"yellow", {0.98f, 0.85f, 0.15f}}, {"gold", {0.85f, 0.65f, 0.15f}}, {"green", {0.2f, 0.65f, 0.25f}},
        {"dark green", {0.1f, 0.35f, 0.15f}}, {"lime", {0.55f, 0.9f, 0.2f}}, {"blue", {0.15f, 0.35f, 0.85f}},
        {"navy", {0.08f, 0.12f, 0.35f}}, {"light blue", {0.5f, 0.75f, 0.95f}}, {"cyan", {0.2f, 0.8f, 0.85f}},
        {"purple", {0.5f, 0.2f, 0.7f}}, {"pink", {0.95f, 0.5f, 0.7f}}, {"brown", {0.45f, 0.28f, 0.15f}},
        {"tan", {0.82f, 0.7f, 0.52f}}, {"beige", {0.9f, 0.85f, 0.72f}}, {"silver", {0.75f, 0.75f, 0.78f}},
    };
    auto it = names.find(s);
    if (it == names.end()) return false;
    out = it->second;
    return true;
}

int axisOf(const json& v, int fallback) {
    if (!v.is_string()) return fallback;
    const std::string s = v.get<std::string>();
    if (s == "x" || s == "X") return 0;
    if (s == "y" || s == "Y") return 1;
    if (s == "z" || s == "Z") return 2;
    return -1;
}

// "top" / "+y" -> the way it points (in the part's own space; front is -Z like a character's face).
bool sideDir(const std::string& s, glm::vec3& out) {
    static const std::map<std::string, glm::vec3> sides = {
        {"top", {0, 1, 0}}, {"+y", {0, 1, 0}}, {"up", {0, 1, 0}}, {"bottom", {0, -1, 0}}, {"-y", {0, -1, 0}}, {"down", {0, -1, 0}},
        {"right", {1, 0, 0}}, {"+x", {1, 0, 0}}, {"left", {-1, 0, 0}}, {"-x", {-1, 0, 0}},
        {"front", {0, 0, -1}}, {"-z", {0, 0, -1}}, {"back", {0, 0, 1}}, {"+z", {0, 0, 1}},
    };
    auto it = sides.find(s);
    if (it == sides.end()) return false;
    out = it->second;
    return true;
}

glm::vec3 newell(const EditMesh& m, const std::vector<uint32_t>& f) {
    glm::vec3 n(0.0f);
    for (size_t i = 0; i < f.size(); ++i) {
        const glm::vec3& a = m.verts[f[i]];
        const glm::vec3& b = m.verts[f[(i + 1) % f.size()]];
        n += glm::vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
    }
    return glm::length(n) > 1e-12f ? glm::normalize(n) : glm::vec3(0.0f);   // (a flat-as-nothing face faces no way)
}

// Which part of the mesh a step works on (or a paint layer covers):
//   "all", or {side, faces, vertices, axis + from/to (0 = the lowest point, 1 = the highest) or min/max (studs)}.
// Every condition given has to be true.
struct Pick {
    bool      all = true;
    bool      hasSide = false;
    glm::vec3 side{0.0f};
    float     sideDot = 0.7f;
    std::vector<int> faces, verts;
    int       axis = 1;
    bool      hasRange = false, fraction = false;
    float     lo = -1e30f, hi = 1e30f;

    bool read(const json& j, std::string& error) {
        if (j.is_null() || (j.is_string() && j.get<std::string>() == "all")) return true;
        if (j.is_string()) {   // just a side: "top"
            if (!sideDir(j.get<std::string>(), side)) { error = "select \"" + j.get<std::string>() + "\" isn't all or a side (top, bottom, left, right, front, back)."; return false; }
            all = false; hasSide = true;
            return true;
        }
        if (!j.is_object()) { error = "select is \"all\", a side like \"top\", or {side, faces, vertices, axis, from, to, min, max}."; return false; }
        all = false;
        for (auto& [k, v] : j.items())
            if (k != "side" && k != "faces" && k != "vertices" && k != "axis" && k != "from" && k != "to" && k != "min" && k != "max" && k != "spread") {
                error = "select doesn't know \"" + k + "\" (it takes side, faces, vertices, axis, from, to, min, max, spread).";
                return false;
            }
        if (j.contains("side")) {
            if (!j["side"].is_string() || !sideDir(j["side"].get<std::string>(), side)) { error = "side is top, bottom, left, right, front or back."; return false; }
            hasSide = true;
            if (j.contains("spread") && j["spread"].is_number()) sideDot = std::cos(glm::radians(std::clamp(j["spread"].get<float>(), 1.0f, 179.0f)));
        }
        auto ints = [&](const char* k, std::vector<int>& out) {
            if (!j.contains(k)) return true;
            if (!j[k].is_array()) return false;
            for (auto& i : j[k]) { if (!i.is_number_integer()) return false; out.push_back(i.get<int>()); }
            return true;
        };
        if (!ints("faces", faces) || !ints("vertices", verts)) { error = "faces and vertices are lists of numbers."; return false; }
        axis = axisOf(j.value("axis", json("y")), 1);
        if (axis < 0) { error = "axis is x, y or z."; return false; }
        if (j.contains("from") || j.contains("to")) {
            hasRange = fraction = true;
            lo = j.value("from", 0.0f); hi = j.value("to", 1.0f);
        } else if (j.contains("min") || j.contains("max")) {
            hasRange = true;
            lo = j.value("min", -1e30f); hi = j.value("max", 1e30f);
        }
        return true;
    }
    bool faceOk(const EditMesh& m, int fi) const {
        if (all) return true;
        if (!faces.empty() && std::find(faces.begin(), faces.end(), fi) == faces.end()) return false;
        if (hasSide && glm::dot(newell(m, m.faces[fi]), side) < sideDot) return false;
        return true;
    }
    bool faceRule() const { return !all && (hasSide || !faces.empty()); }
    // `p` in studs; `bmin` / `bmax` the mesh's box (for from / to).
    bool spotOk(const glm::vec3& p, const glm::vec3& bmin, const glm::vec3& bmax) const {
        if (all || !hasRange) return true;
        float a = lo, b = hi;
        if (fraction) { a = bmin[axis] + lo * (bmax[axis] - bmin[axis]); b = bmin[axis] + hi * (bmax[axis] - bmin[axis]); }
        const float eps = 1e-4f + 1e-4f * std::abs(bmax[axis] - bmin[axis]);
        return p[axis] >= std::min(a, b) - eps && p[axis] <= std::max(a, b) + eps;
    }
    // One flag per corner.
    MeshEdit::Selection select(const EditMesh& m) const {
        MeshEdit::Selection sel(m.verts.size(), all ? 1 : 0);
        if (all) return sel;
        glm::vec3 bmin(1e30f), bmax(-1e30f);
        for (auto& v : m.verts) { bmin = glm::min(bmin, v); bmax = glm::max(bmax, v); }
        std::vector<char> onFace(m.verts.size(), faceRule() ? 0 : 1);
        if (faceRule())
            for (size_t fi = 0; fi < m.faces.size(); ++fi)
                if (faceOk(m, (int)fi)) for (uint32_t v : m.faces[fi]) onFace[v] = 1;
        for (size_t i = 0; i < m.verts.size(); ++i) {
            if (!onFace[i]) continue;
            if (!verts.empty() && std::find(verts.begin(), verts.end(), (int)i) == verts.end()) continue;
            if (!spotOk(m.verts[i], bmin, bmax)) continue;
            sel[i] = 1;
        }
        return sel;
    }
};

// The middle (or bottom / top) of the picked corners: what scale and rotate turn around.
glm::vec3 pivotOf(const EditMesh& m, const MeshEdit::Selection& sel, const json& how) {
    glm::vec3 p;
    if (readVec3(how, p) && how.is_array()) return p;
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (size_t i = 0; i < m.verts.size(); ++i)
        if (sel[i]) { lo = glm::min(lo, m.verts[i]); hi = glm::max(hi, m.verts[i]); }
    glm::vec3 c = (lo + hi) * 0.5f;
    const std::string s = how.is_string() ? how.get<std::string>() : "center";
    if (s == "bottom") c.y = lo.y;
    else if (s == "top") c.y = hi.y;
    else if (s == "origin") c = glm::vec3(0.0f);
    return c;
}

bool shapeNamed(std::string s, MeshEdit::AddShape& out) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == ' ' || c == '_' || c == '-'; }), s.end());
    static const std::map<std::string, MeshEdit::AddShape> shapes = {
        {"cube", MeshEdit::AddShape::Cube}, {"box", MeshEdit::AddShape::Cube}, {"plane", MeshEdit::AddShape::Plane},
        {"circle", MeshEdit::AddShape::Circle}, {"cylinder", MeshEdit::AddShape::Cylinder}, {"cone", MeshEdit::AddShape::Cone},
        {"sphere", MeshEdit::AddShape::Sphere}, {"uvsphere", MeshEdit::AddShape::Sphere}, {"ball", MeshEdit::AddShape::Sphere},
        {"icosphere", MeshEdit::AddShape::IcoSphere}, {"torus", MeshEdit::AddShape::Torus}, {"ring", MeshEdit::AddShape::Torus},
        {"grid", MeshEdit::AddShape::Grid}, {"capsule", MeshEdit::AddShape::Capsule},
    };
    auto it = shapes.find(s);
    if (it == shapes.end()) return false;
    out = it->second;
    return true;
}
const char* const kShapeChoices[] = {"cube", "plane", "circle", "cylinder", "cone", "sphere", "icosphere", "torus", "grid", "capsule"};

std::string fileSafe(std::string s) {
    for (char& c : s) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
    return s.empty() ? std::string("Mesh") : s;
}

// A small, smooth noise (the same every time), 0..1.
float hash3(int x, int y, int z) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / float(0xffffff);
}
float noise3(glm::vec3 p) {
    glm::vec3 f = glm::floor(p), t = p - f;
    t = t * t * (3.0f - 2.0f * t);
    const int x = (int)f.x, y = (int)f.y, z = (int)f.z;
    auto l = [](float a, float b, float k) { return a + (b - a) * k; };
    return l(l(l(hash3(x, y, z), hash3(x + 1, y, z), t.x), l(hash3(x, y + 1, z), hash3(x + 1, y + 1, z), t.x), t.y),
             l(l(hash3(x, y, z + 1), hash3(x + 1, y, z + 1), t.x), l(hash3(x, y + 1, z + 1), hash3(x + 1, y + 1, z + 1), t.x), t.y), t.z);
}

struct Layer {
    Pick      pick;
    std::string pattern = "solid";
    glm::vec3 color{1.0f}, color2{0.0f};
    float     scale = 0.5f;   // studs: one stripe + gap, one checker square, the dots' spacing, the noise's size
    float     amount = 0.35f; // noise: how strong
    int       axis = 1;       // stripes and gradient
    long long pixels = 0;
};

glm::vec3 shade(const Layer& L, glm::vec3 p, const glm::vec3& bmin, const glm::vec3& bmax) {
    const float s = std::max(L.scale, 1e-3f);
    if (L.pattern == "stripes") return std::fmod(std::fmod(p[L.axis] / s, 1.0f) + 1.0f, 1.0f) < 0.5f ? L.color : L.color2;
    if (L.pattern == "checker") {
        const int k = (int)std::floor(p.x / s) + (int)std::floor(p.y / s) + (int)std::floor(p.z / s);
        return (k & 1) ? L.color2 : L.color;
    }
    if (L.pattern == "dots") {
        glm::vec3 q = p / s - glm::floor(p / s) - 0.5f;
        return glm::length(q) < 0.3f ? L.color2 : L.color;
    }
    if (L.pattern == "noise") {
        const float n = noise3(p / s) * 0.65f + noise3(p / s * 3.1f) * 0.35f;
        return glm::mix(L.color, L.color2, std::clamp(n * L.amount * 2.0f, 0.0f, 1.0f));
    }
    if (L.pattern == "gradient") {
        const float span = bmax[L.axis] - bmin[L.axis];
        return glm::mix(L.color, L.color2, span > 1e-6f ? std::clamp((p[L.axis] - bmin[L.axis]) / span, 0.0f, 1.0f) : 0.0f);
    }
    return L.color;
}

} // namespace

AiToolResult Editor::runMeshAiTool(const std::string& name, const json& args) {
    auto done = [&](json d) {
        d["success"] = true;
        d["operation"] = name;
        AiToolResult r;
        r.text = d.dump(1);
        r.data = std::move(d);
        return r;
    };
    auto fail = [&](const char* code, const std::string& message, const std::string& suggestion, json choices = nullptr) {
        json d = {{"success", false}, {"operation", name},
                  {"error", {{"code", code}, {"message", message}, {"recoverable", true}, {"suggested_action", suggestion}}}};
        if (!choices.is_null()) d["error"]["valid_choices"] = std::move(choices);
        AiToolResult r;
        r.text = d.dump(1);
        r.error = true;
        r.data = std::move(d);
        return r;
    };
    if (m_playing) return fail("PLAYING", "Stop the playtest first: shapes are made while editing.", "playtest stop.");
    const std::string ref = args.value("object", std::string());
    SceneNode* node = aiFind(ref);
    if (!node) return fail("OBJECT_NOT_FOUND", ref.empty() ? "object is missing." : "No object " + ref + " exists.",
                           "insert_object {kind: \"Part\"} makes a cube to start from; use the object_id it returns.");
    if (!canEdit(node) || m_scene->isCharacterPart(node) || m_scene->isProtected(node))
        return fail("NOT_EDITABLE", node->name + " can't be changed here (it's part of the character, or locked).",
                    "insert_object a new Part and shape that instead.");

    // ------------------------------------------------------------- make_accessory
    if (name == "make_accessory") {
        const std::string kind = args.value("kind", std::string("hat"));
        glm::vec3 offset(0.0f);
        if (args.contains("offset") && !readVec3(args["offset"], offset))
            return fail("INVALID_INPUT", "offset is [x, y, z] in studs.", "offset: [0, 0.1, 0]");
        SceneNode* model = node;
        while (model && model->kind != NodeKind::Model && model->parent && model->parent != m_scene->root()) model = model->parent;
        if (node->isPart() && (!model || model->kind != NodeKind::Model)) {
            // One part: put it in a Model of its own (like Ctrl+G), named after the hat.
            auto group = std::make_unique<SceneNode>(args.value("name", node->name), NodeKind::Model);
            group->transform.position = glm::vec3(node->worldMatrix()[3]);
            SceneNode* parent = node->parent ? node->parent : m_scene->root();
            if (parent != m_scene->root()) group->transform.position = glm::vec3(glm::inverse(parent->worldMatrix()) * glm::vec4(group->transform.position, 1.0f));
            model = m_scene->insert(std::move(group), parent);
            if (!model || !m_scene->reparent(node, model)) return fail("CANT_GROUP", "Couldn't put the part in a Model.", "Group it yourself (Ctrl+G) and try again.");
        }
        if (!model || model->kind != NodeKind::Model || model->name == "Accessory Mannequin")
            return fail("INVALID_INPUT", "object should be the hat: a part, or a Model with its parts in it.", "make_accessory {object: \"#42\"}");
        if (args.contains("name") && args["name"].is_string() && !args["name"].get<std::string>().empty()) model->name = args["name"].get<std::string>();
        std::string error;
        json d = aiPlaceAccessory(model, kind, offset, error);
        if (!error.empty())
            return fail("INVALID_INPUT", error, "kind: \"hat\"", json::array({"hat", "hair", "faceacc", "neck", "shoulder", "waist"}));
        d["object_id"] = idOf(model);
        d["name"] = model->name;
        d["next"] = "It's on the Accessory Mannequin and its position is saved. Look with screenshot; nudge it with "
                    "make_accessory {offset}. To sell or wear it, the person presses Upload in AVATAR > Accessories "
                    "(the window is open; it needs a signed-in Verified creator). Never upload for them.";
        if (!(Online::online() && (Online::verified() || Online::staff())))
            d["warnings"] = json::array({"This account isn't a signed-in Verified creator, so the Upload button stays off until it is."});
        Log::system("The AI made \"" + model->name + "\" a " + kind + " on the Accessory Mannequin.");
        return done(d);
    }

    if (!node->isPart()) return fail("NOT_A_PART", node->name + " isn't a part.", "Use a Part (insert_object {kind: \"Part\"}) or a MeshPart.");
    const glm::vec3 S0 = glm::max(glm::abs(node->transform.scale), glm::vec3(1e-4f));

    // ------------------------------------------------------------------ edit_mesh
    if (name == "edit_mesh") {
        if (!args.contains("steps") || !args["steps"].is_array() || args["steps"].empty())
            return fail("INVALID_INPUT", "steps is a list of things to do, like [{op: \"extrude\", select: \"top\", distance: 1}].",
                        "steps: [{op: \"start\", shape: \"cylinder\"}]");
        if (args["steps"].size() > 64) return fail("INVALID_INPUT", "At most 64 steps in one call.", "Split it into more calls.");
        // Work on a copy in studs (the part's size taken out), so nothing changes if a step fails.
        EditMesh work = node->editMesh && node->primitiveType == PrimitiveType::Mesh ? *node->editMesh
                                                                                     : *MeshEdit::fromPrimitive(node->primitiveType == PrimitiveType::None ? PrimitiveType::Cube : node->primitiveType);
        glm::vec3 S = S0;
        for (auto& v : work.verts) v *= S;
        json report = json::array();
        int index = 0;
        for (const json& step : args["steps"]) {
            const std::string where = "Step " + std::to_string(index) + ": ";
            if (!step.is_object() || !step.contains("op") || !step["op"].is_string())
                return fail("INVALID_INPUT", where + "each step is {op: ..., ...}.", "{op: \"scale\", select: \"top\", by: 0.5}");
            const std::string op = step["op"].get<std::string>();
            Pick pick;
            std::string error;
            if (!pick.read(step.contains("select") ? step["select"] : json(nullptr), error)) return fail("INVALID_INPUT", where + error, "select: \"top\"");
            MeshEdit::Selection sel = pick.select(work);
            const int picked = MeshEdit::countSelected(sel);
            const int pickedFaces = (int)MeshEdit::selectedFaces(work, sel).size();
            auto needVerts = [&]() { return picked > 0; };
            const EditMesh before = work;
            bool ok = true;
            if (op == "start") {
                MeshEdit::AddShape sh = MeshEdit::AddShape::Cube;
                if (!shapeNamed(step.value("shape", std::string("cube")), sh))
                    return fail("INVALID_INPUT", where + "unknown shape.", "shape: \"cylinder\"", json(std::vector<std::string>(std::begin(kShapeChoices), std::end(kShapeChoices))));
                glm::vec3 size;
                if (step.contains("size")) {
                    if (!readVec3(step["size"], size)) return fail("INVALID_INPUT", where + "size is [x, y, z] in studs.", "size: [2, 1, 2]");
                    S = glm::max(glm::abs(size), glm::vec3(0.05f));
                }
                work = *MeshEdit::shape(sh);
                for (auto& v : work.verts) v *= S;
                work.uvs.clear();
                work.seams.clear();
            } else if (op == "add_shape") {
                MeshEdit::AddShape sh;
                if (!shapeNamed(step.value("shape", std::string()), sh))
                    return fail("INVALID_INPUT", where + "unknown shape.", "shape: \"torus\"", json(std::vector<std::string>(std::begin(kShapeChoices), std::end(kShapeChoices))));
                glm::vec3 pos(0.0f), size(1.0f), rot(0.0f);
                if ((step.contains("position") && !readVec3(step["position"], pos)) || (step.contains("size") && !readVec3(step["size"], size)) ||
                    (step.contains("rotation") && !readVec3(step["rotation"], rot)))
                    return fail("INVALID_INPUT", where + "position, size and rotation are [x, y, z] (studs, degrees).", "position: [0, 1, 0], size: [1, 1, 1]");
                glm::mat4 place = glm::translate(glm::mat4(1.0f), pos);
                place = glm::rotate(place, glm::radians(rot.z), glm::vec3(0, 0, 1));
                place = glm::rotate(place, glm::radians(rot.y), glm::vec3(0, 1, 0));
                place = glm::rotate(place, glm::radians(rot.x), glm::vec3(1, 0, 0));
                place = glm::scale(place, size);
                MeshEdit::Selection all(work.verts.size(), 0);
                MeshEdit::append(work, all, *MeshEdit::shape(sh), place);
            } else if (op == "move" || op == "scale" || op == "rotate") {
                if (!needVerts()) return fail("NOTHING_PICKED", where + "select picked no corners.", "Check select (try \"all\", or a side like \"top\").");
                const glm::vec3 c = pivotOf(work, sel, step.contains("pivot") ? step["pivot"] : json("center"));
                glm::vec3 v;
                const char* key = op == "move" ? "by" : op == "scale" ? "by" : "degrees";
                if (!step.contains(key) || !readVec3(step[key], v))
                    return fail("INVALID_INPUT", where + op + " needs " + key + (op == "scale" ? " (a number, or [x, y, z])." : " ([x, y, z])."),
                                op == "move" ? "by: [0, 1, 0]" : op == "scale" ? "by: 0.5" : "degrees: [0, 45, 0]");
                glm::mat4 R(1.0f);
                if (op == "rotate") {
                    R = glm::rotate(R, glm::radians(v.z), glm::vec3(0, 0, 1));
                    R = glm::rotate(R, glm::radians(v.y), glm::vec3(0, 1, 0));
                    R = glm::rotate(R, glm::radians(v.x), glm::vec3(1, 0, 0));
                }
                for (size_t i = 0; i < work.verts.size(); ++i) {
                    if (!sel[i]) continue;
                    glm::vec3& p = work.verts[i];
                    if (op == "move") p += v;
                    else if (op == "scale") p = c + (p - c) * v;
                    else p = c + glm::vec3(R * glm::vec4(p - c, 0.0f));
                }
            } else if (op == "extrude") {
                if (pickedFaces == 0) return fail("NOTHING_PICKED", where + "extrude needs whole faces picked (select a side, like \"top\").", "select: \"top\"");
                const float dist = step.value("distance", 1.0f);
                ok = MeshEdit::extrude(work, sel);
                if (ok) {
                    // Each new corner goes out along the faces it's on (like Blender's Extrude Region).
                    std::vector<glm::vec3> push(work.verts.size(), glm::vec3(0.0f));
                    for (int fi : MeshEdit::selectedFaces(work, sel)) {
                        glm::vec3 n = newell(work, work.faces[fi]);
                        for (uint32_t v : work.faces[fi]) push[v] += n;
                    }
                    for (size_t i = 0; i < work.verts.size(); ++i)
                        if (sel[i] && glm::length(push[i]) > 1e-6f) work.verts[i] += glm::normalize(push[i]) * dist;
                }
            } else if (op == "inset") {
                if (pickedFaces == 0) return fail("NOTHING_PICKED", where + "inset needs whole faces picked.", "select: \"top\"");
                ok = MeshEdit::inset(work, sel, std::clamp(step.value("amount", 0.2f), 0.01f, 0.99f));
            } else if (op == "subdivide") {
                const int times = std::clamp(step.value("times", 1), 1, 3);
                for (int k = 0; k < times && ok; ++k) {
                    if (work.faces.size() > 20000) return fail("TOO_BIG", where + "the mesh would get too big (over 20000 faces).", "Subdivide less.");
                    MeshEdit::Selection s = pick.select(work);
                    ok = MeshEdit::subdivide(work, s);
                }
            } else if (op == "smooth_subdivide") {
                const int times = std::clamp(step.value("times", 1), 1, 3);
                for (int k = 0; k < times && ok; ++k) {
                    if (work.faces.size() > 20000) return fail("TOO_BIG", where + "the mesh would get too big (over 20000 faces).", "Subdivide less.");
                    MeshEdit::Selection s = pick.select(work);
                    ok = MeshEdit::subdivideSmooth(work, s);
                }
            } else if (op == "smooth") {
                if (!needVerts()) return fail("NOTHING_PICKED", where + "select picked no corners.", "select: \"all\"");
                const int times = std::clamp(step.value("times", 1), 1, 10);
                for (int k = 0; k < times && ok; ++k) ok = MeshEdit::smoothVerts(work, sel, std::clamp(step.value("amount", 0.5f), 0.0f, 1.0f));
            } else if (op == "delete") {
                if (pickedFaces == 0) return fail("NOTHING_PICKED", where + "delete needs whole faces picked.", "select: \"bottom\"");
                ok = MeshEdit::remove(work, sel, 2);
                if (ok) MeshEdit::compact(work, sel);
            } else if (op == "flip") {
                ok = MeshEdit::flip(work, sel);
            } else if (op == "fix_normals") {
                for (char& c : sel) c = 1;
                ok = MeshEdit::recalcNormals(work, sel, false);
            } else if (op == "merge_close") {
                for (char& c : sel) if (pick.all) c = 1;
                int removed = 0;
                ok = MeshEdit::mergeByDistance(work, sel, std::max(step.value("distance", 0.001f), 0.0f), &removed);
                ok = true;
            } else if (op == "shading") {
                const std::string how = step.value("shading", step.value("mode", std::string("smooth")));
                if (how != "smooth" && how != "flat") return fail("INVALID_INPUT", where + "shading is \"smooth\" or \"flat\".", "shading: \"smooth\"");
                work.smooth = how == "smooth";
            } else {
                return fail("INVALID_INPUT", where + "unknown op \"" + op + "\".", "See valid_choices.",
                            json::array({"start", "add_shape", "move", "scale", "rotate", "extrude", "inset", "subdivide", "smooth_subdivide",
                                         "smooth", "delete", "flip", "fix_normals", "merge_close", "shading"}));
            }
            if (!ok) return fail("STEP_FAILED", where + op + " couldn't be done on what was picked.", "Pick something else, or try another op.");
            if (work.verts.empty() || work.faces.empty()) return fail("STEP_FAILED", where + "that would leave no mesh at all.", "Delete less.");
            if (work.faces.size() > 30000) return fail("TOO_BIG", where + "the mesh is too big now (over 30000 faces).", "Use fewer subdivides.");
            // Keep the texture layout where the faces are the same.
            if (before.uvs.size() == before.faces.size() && op != "start") MeshEdit::carryUVs(before, work);
            report.push_back({{"op", op}, {"picked_corners", picked}, {"picked_faces", pickedFaces},
                              {"vertices", work.verts.size()}, {"faces", work.faces.size()}});
            ++index;
        }
        // Back into the part: corners in its box again, the box wrapped round them.
        for (auto& v : work.verts) v /= S;
        node->transform.scale = S;
        if (node->primitiveType != PrimitiveType::Mesh) MeshEdit::attach(*node, std::make_shared<EditMesh>(std::move(work)));
        else { MeshEdit::own(*node) = std::move(work); MeshEdit::refresh(*node); }
        MeshEdit::fit(*node);
        if (m_state.modeling.node == node->id) {   // being edited in Modeling mode: its picks start over
            m_state.modeling.sel.assign(node->editMesh->verts.size(), 0);
            m_state.modeling.hidden.assign(node->editMesh->verts.size(), 0);
        }
        m_scene->markDirty();
        const EditMesh& m = *node->editMesh;
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (auto& v : m.verts) { lo = glm::min(lo, v * node->transform.scale); hi = glm::max(hi, v * node->transform.scale); }
        json d = {{"object_id", idOf(node)}, {"class", "MeshPart"}, {"vertices", m.verts.size()}, {"faces", m.faces.size()},
                  {"Size", vec(node->transform.scale)}, {"Position", vec(glm::vec3(node->worldMatrix()[3]))}, {"steps", report},
                  {"smooth_shading", m.smooth}, {"bounds_min", vec(lo)}, {"bounds_max", vec(hi)}};
        if (m.uvs.size() != m.faces.size() && !node->texture.empty())
            d["warnings"] = json::array({"The shape changed under its texture: run texture_mesh again to paint the new faces."});
        return done(d);
    }

    // --------------------------------------------------------------- texture_mesh
    if (name == "texture_mesh") {
        if (node->primitiveType != PrimitiveType::Mesh || !node->editMesh)
            MeshEdit::attach(*node, MeshEdit::fromPrimitive(node->primitiveType == PrimitiveType::None ? PrimitiveType::Cube : node->primitiveType));
        EditMesh& m = MeshEdit::own(*node);
        // Lay it out flat: the layout it has if it's good, else Smart UV Project.
        const std::string how = args.value("unwrap", std::string("auto"));
        static const std::map<std::string, MeshEdit::Unwrap> ways = {
            {"smart", MeshEdit::Unwrap::Smart}, {"cube", MeshEdit::Unwrap::Cube}, {"cylinder", MeshEdit::Unwrap::Cylinder},
            {"sphere", MeshEdit::Unwrap::Sphere}, {"unwrap", MeshEdit::Unwrap::Unwrap}};
        bool fits = m.uvs.size() == m.faces.size();
        for (size_t f = 0; fits && f < m.uvs.size(); ++f)
            for (auto& t : m.uvs[f]) if (t.x < -1e-3f || t.y < -1e-3f || t.x > 1.001f || t.y > 1.001f) { fits = false; break; }
        std::string used = "kept";
        if (how != "keep" || !fits) {
            if (how != "auto" && how != "keep" && !ways.count(how))
                return fail("INVALID_INPUT", "unwrap is auto, keep, smart, cube, cylinder, sphere or unwrap.", "unwrap: \"smart\"",
                            json::array({"auto", "keep", "smart", "cube", "cylinder", "sphere", "unwrap"}));
            if (how == "auto" && fits) used = "kept";
            else {
                const MeshEdit::Unwrap u = ways.count(how) ? ways.at(how) : MeshEdit::Unwrap::Smart;
                MeshEdit::Selection none(m.verts.size(), 0);
                MeshEdit::unwrap(m, none, u, S0);
                if (u == MeshEdit::Unwrap::Cylinder || u == MeshEdit::Unwrap::Sphere) {   // (these can go past the square's edge)
                    std::vector<int> all(m.faces.size());
                    for (size_t i = 0; i < all.size(); ++i) all[i] = (int)i;
                    MeshEdit::packIslands(m, all, S0);
                }
                used = ways.count(how) ? how : "smart";
            }
        }
        // The layers: the base colour everywhere, then each layer on top where it picks.
        std::vector<Layer> layers(1);
        if (!readColor(args.contains("color") ? args["color"] : json("white"), layers[0].color))
            return fail("INVALID_INPUT", "color is [r, g, b] (0-1), \"#rrggbb\" or a name like \"red\".", "color: \"#3366cc\"");
        if (args.contains("layers")) {
            if (!args["layers"].is_array() || args["layers"].size() > 16) return fail("INVALID_INPUT", "layers is a list (up to 16).", "layers: [{select: \"top\", color: \"red\"}]");
            int li = 0;
            for (const json& lj : args["layers"]) {
                const std::string where = "Layer " + std::to_string(li++) + ": ";
                if (!lj.is_object()) return fail("INVALID_INPUT", where + "each layer is {select, color, pattern, color2, scale}.", "");
                Layer L;
                std::string error;
                if (!L.pick.read(lj.contains("select") ? lj["select"] : json(nullptr), error)) return fail("INVALID_INPUT", where + error, "select: {axis: \"y\", from: 0, to: 0.2}");
                if (!readColor(lj.contains("color") ? lj["color"] : json("white"), L.color) ||
                    !readColor(lj.contains("color2") ? lj["color2"] : json("black"), L.color2))
                    return fail("INVALID_INPUT", where + "color / color2 are [r, g, b] (0-1), \"#rrggbb\" or a name.", "color: \"navy\"");
                L.pattern = lj.value("pattern", std::string("solid"));
                if (L.pattern != "solid" && L.pattern != "stripes" && L.pattern != "checker" && L.pattern != "dots" && L.pattern != "noise" && L.pattern != "gradient")
                    return fail("INVALID_INPUT", where + "unknown pattern.", "pattern: \"stripes\"", json::array({"solid", "stripes", "checker", "dots", "noise", "gradient"}));
                L.scale = std::clamp(lj.value("scale", 0.5f), 0.01f, 100.0f);
                L.amount = std::clamp(lj.value("amount", 0.35f), 0.0f, 1.0f);
                L.axis = axisOf(lj.value("axis", json("y")), 1);
                if (L.axis < 0) return fail("INVALID_INPUT", where + "axis is x, y or z.", "axis: \"y\"");
                layers.push_back(L);
            }
        }
        const int size = std::clamp(args.value("size", 512), 64, 2048);
        // In studs, so patterns are the same size on every side.
        std::vector<glm::vec3> P(m.verts.size());
        for (size_t i = 0; i < m.verts.size(); ++i) P[i] = m.verts[i] * S0;
        EditMesh studs;
        studs.verts = P;
        studs.faces = m.faces;
        glm::vec3 bmin(1e30f), bmax(-1e30f);
        for (auto& p : P) { bmin = glm::min(bmin, p); bmax = glm::max(bmax, p); }
        std::vector<unsigned char> px((size_t)size * size * 4, 0);
        std::vector<char> covered((size_t)size * size, 0);
        for (size_t fi = 0; fi < m.faces.size(); ++fi) {
            const auto& f = m.faces[fi];
            const auto& uv = m.uvs[fi];
            if (f.size() < 3 || uv.size() != f.size()) continue;
            std::vector<int> faceLayers;   // which layers can paint this face at all
            for (size_t li = 0; li < layers.size(); ++li) if (layers[li].pick.faceOk(studs, (int)fi)) faceLayers.push_back((int)li);
            auto toPx = [&](glm::vec2 t) { return glm::vec2(t.x * size, (1.0f - t.y) * size); };   // row 0 = the top (v = 1)
            for (size_t t = 1; t + 1 < f.size(); ++t) {
                const glm::vec2 a = toPx(uv[0]), b = toPx(uv[t]), c = toPx(uv[t + 1]);
                const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
                if (std::abs(area) < 1e-8f) continue;
                const int x0 = std::max(0, (int)std::floor(std::min({a.x, b.x, c.x})) - 1), x1 = std::min(size - 1, (int)std::ceil(std::max({a.x, b.x, c.x})) + 1);
                const int y0 = std::max(0, (int)std::floor(std::min({a.y, b.y, c.y})) - 1), y1 = std::min(size - 1, (int)std::ceil(std::max({a.y, b.y, c.y})) + 1);
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) {
                        const glm::vec2 p(x + 0.5f, y + 0.5f);
                        const float w0 = ((b.x - p.x) * (c.y - p.y) - (c.x - p.x) * (b.y - p.y)) / area;
                        const float w1 = ((c.x - p.x) * (a.y - p.y) - (a.x - p.x) * (c.y - p.y)) / area;
                        const float w2 = 1.0f - w0 - w1;
                        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                        const glm::vec3 pos = P[f[0]] * w0 + P[f[t]] * w1 + P[f[t + 1]] * w2;
                        glm::vec3 col = layers[0].color;
                        int top = 0;
                        for (int li : faceLayers) if (li > 0 && layers[li].pick.spotOk(pos, bmin, bmax)) { col = shade(layers[li], pos, bmin, bmax); top = li; }
                        if (top > 0) layers[top].pixels++;
                        unsigned char* o = &px[((size_t)y * size + x) * 4];
                        o[0] = (unsigned char)std::lround(col.r * 255.0f);
                        o[1] = (unsigned char)std::lround(col.g * 255.0f);
                        o[2] = (unsigned char)std::lround(col.b * 255.0f);
                        o[3] = 255;
                        covered[(size_t)y * size + x] = 1;
                    }
            }
        }
        // Spread the edges out a little, so no background shows along the seams.
        for (int pass = 0; pass < 4; ++pass) {
            std::vector<char> next = covered;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    if (covered[(size_t)y * size + x]) continue;
                    for (auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= size || ny >= size || !covered[(size_t)ny * size + nx]) continue;
                        std::copy_n(&px[((size_t)ny * size + nx) * 4], 4, &px[((size_t)y * size + x) * 4]);
                        next[(size_t)y * size + x] = 1;
                        break;
                    }
                }
            covered.swap(next);
        }
        // Whatever no face uses: the base colour.
        for (size_t i = 0; i < covered.size(); ++i)
            if (!covered[i]) {
                px[i * 4 + 0] = (unsigned char)std::lround(layers[0].color.r * 255.0f);
                px[i * 4 + 1] = (unsigned char)std::lround(layers[0].color.g * 255.0f);
                px[i * 4 + 2] = (unsigned char)std::lround(layers[0].color.b * 255.0f);
                px[i * 4 + 3] = 255;
            }
        std::error_code ec;
        const std::filesystem::path dir = Paths::gamesFolder() / "textures";
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path file = dir / (fileSafe(node->name) + "-" + std::to_string(node->id) + ".png");
        if (!stbi_write_png(file.string().c_str(), size, size, 4, px.data(), size * 4))
            return fail("WRITE_FAILED", "Couldn't save the picture to " + file.string() + ".", "Check the games folder can be written to.");
        const std::string tex = Paths::relativeToGames(file);
        Textures::forget(tex);
        Textures::forget(file.string());
        node->texture = tex;
        node->color = layers[0].color;   // (what it looks like before the picture loads)
        MeshEdit::refresh(*node);
        m_scene->markDirty();
        json painted = json::array();
        for (size_t li = 1; li < layers.size(); ++li) painted.push_back({{"layer", li - 1}, {"pattern", layers[li].pattern}, {"pixels", layers[li].pixels}});
        json d = {{"object_id", idOf(node)}, {"Texture", tex}, {"path", file.string()}, {"size", size}, {"unwrap", used},
                  {"faces", m.faces.size()}, {"layers", painted}};
        json warn = json::array();
        for (size_t li = 1; li < layers.size(); ++li)
            if (layers[li].pixels == 0) warn.push_back("Layer " + std::to_string(li - 1) + " painted nothing: its select matched no surface.");
        if (!warn.empty()) d["warnings"] = warn;
        Log::system("The AI painted a texture for '" + node->name + "' (" + tex + ").");
        return done(d);
    }

    return fail("UNKNOWN_TOOL", "No such mesh tool.", "");
}
