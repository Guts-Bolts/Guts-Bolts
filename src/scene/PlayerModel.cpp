#include "PlayerModel.h"
#include "EditMesh.h"
#include "ObjImport.h"
#include "SceneNode.h"
#include "PlayerObj.h"   // generated: the bytes of assets/models/player.obj

#include <algorithm>
#include <map>

namespace PlayerModel {

namespace {

constexpr float kScale = 0.5f;   // the model is Roblox-sized; our characters are half that

struct Part {
    std::shared_ptr<EditMesh> mesh;
    std::shared_ptr<Mesh>     gpu;   // made the first time it's drawn
    glm::vec3 position{0.0f}, size{1.0f};
};

// The clothing template (the classic 585 x 559 shirt / pants picture): where each
// side of each body part goes, in pixels. Shirts use Torso and the arms; pants use
// Torso and the legs, drawn in the arms' places. tools/make_clothing_template.py
// draws the same layout.
struct Rect { float x, y, w, h; };
struct BoxLayout { Rect front, back, right, left, up, down; };
constexpr float kTemplateW = 585.0f, kTemplateH = 559.0f;
constexpr BoxLayout kTorso = {{231, 74, 128, 128}, {427, 74, 128, 128}, {165, 74, 64, 128},
                              {361, 74, 64, 128}, {231, 8, 128, 64}, {231, 204, 128, 64}};
// Right arm / right leg (bottom left of the picture) and left arm / left leg (bottom right).
constexpr BoxLayout kRightLimb = {{217, 355, 64, 128}, {85, 355, 64, 128}, {151, 355, 64, 128},
                                  {19, 355, 64, 128}, {217, 289, 64, 64}, {217, 485, 64, 64}};
constexpr BoxLayout kLeftLimb = {{308, 355, 64, 128}, {440, 355, 64, 128}, {506, 355, 64, 128},
                                 {374, 355, 64, 128}, {308, 289, 64, 64}, {308, 485, 64, 64}};

// Lay a body part out on the template: each face goes on the side of the box it
// faces, seen from outside, the same way up as the character. `lo`..`hi` is the box
// (-0.5..0.5 for the built-in parts). (In the model the character faces +Z, so its
// right side is -X.)
//
// Only faces at the very top or bottom of the part use the top / bottom boxes. On a
// curvy part (a bum, a chest, hips) a face in the middle, or a slope at the end, can
// point up or down too, and the top / bottom boxes are usually left empty on a shirt,
// so those faces would show as bare patches: they use the side they lean towards instead.
constexpr float kCapBand = 0.1f;    // how near the top / bottom (of the part's height) counts as "the end"
constexpr float kCapFlat = 0.95f;   // how flat a face there has to be (about 18 degrees from level)

void layOut(EditMesh& m, const BoxLayout& L, glm::vec3 lo = glm::vec3(-0.5f), glm::vec3 hi = glm::vec3(0.5f)) {
    const glm::vec3 span = glm::max(hi - lo, glm::vec3(1e-6f));
    m.uvs.clear();
    for (const auto& f : m.faces) {
        glm::vec3 n = MeshEdit::faceNormal(m, f), a = glm::abs(n);
        glm::vec3 mid(0.0f);   // the face's middle, -0.5..0.5 across the box
        for (uint32_t i : f) mid += (m.verts[i] - lo) / span - 0.5f;
        mid /= (float)std::max<size_t>(f.size(), 1);
        const bool cap = a.y >= kCapFlat && (n.y > 0 ? mid.y > 0.5f - kCapBand : mid.y < kCapBand - 0.5f);
        if (!cap && a.x < 1e-4f && a.z < 1e-4f) {   // flat, in the middle: go by where it sits
            a = glm::abs(glm::vec3(mid.x, 0.0f, mid.z));
            n = glm::vec3(mid.x, 0.0f, mid.z);
        }
        std::vector<glm::vec2> uv;
        for (uint32_t i : f) {
            glm::vec3 p = glm::clamp((m.verts[i] - lo) / span, glm::vec3(0.0f), glm::vec3(1.0f)) - 0.5f;
            Rect r; float s, t;   // s: 0 = left of the picture, t: 0 = top
            if (cap) {
                if (n.y > 0) { r = L.up;   s = p.x + 0.5f; t = p.z + 0.5f; }
                else         { r = L.down; s = p.x + 0.5f; t = 0.5f - p.z; }
            } else if (a.x >= a.z) {
                if (n.x < 0) { r = L.right; s = p.z + 0.5f; t = 0.5f - p.y; }
                else         { r = L.left;  s = 0.5f - p.z; t = 0.5f - p.y; }
            } else {
                if (n.z > 0) { r = L.front; s = p.x + 0.5f; t = 0.5f - p.y; }
                else         { r = L.back;  s = 0.5f - p.x; t = 0.5f - p.y; }
            }
            // Half a pixel in from the edges so neighbours don't bleed in.
            float x = r.x + 0.5f + s * (r.w - 1.0f), y = r.y + 0.5f + t * (r.h - 1.0f);
            uv.push_back({x / kTemplateW, 1.0f - y / kTemplateH});   // pictures load bottom row first
        }
        m.uvs.push_back(std::move(uv));
    }
}

std::map<std::string, Part>& parts() {
    static std::map<std::string, Part> p = [] {
        std::map<std::string, Part> out;
        std::string text(reinterpret_cast<const char*>(kPlayerObj), kPlayerObjSize);
        for (auto& [name, o] : ObjImport::parse(text)) {
            std::string rig = name;   // "Left_Arm" in the file -> "Left Arm" on the rig
            for (char& c : rig) if (c == '_') c = ' ';
            if (rig == "Torso") layOut(*o.mesh, kTorso);
            // By where they are, not their names: the limbs on the character's right (-X)
            // use the template's right-limb boxes.
            else if (rig != "Head" && o.center.x < 0.0f) layOut(*o.mesh, kRightLimb);
            else if (rig != "Head") layOut(*o.mesh, kLeftLimb);
            out[rig] = {o.mesh, nullptr, o.center * kScale, o.size * kScale};
        }
        return out;
    }();
    return p;
}

const BoxLayout* boxesFor(const std::string& rigPart) {
    if (rigPart == "Torso") return &kTorso;
    if (rigPart == "Right Arm" || rigPart == "Right Leg") return &kRightLimb;
    if (rigPart == "Left Arm" || rigPart == "Left Leg") return &kLeftLimb;
    return nullptr;
}

} // namespace

bool clothingUVs(EditMesh& m, const std::string& rigPart) {
    const BoxLayout* boxes = boxesFor(rigPart);
    if (!boxes || m.verts.empty() || m.faces.empty()) return false;
    // Stretch the mesh's own bounding box over the template boxes, so a part of any
    // shape or size gets the whole shirt / pants picture.
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const glm::vec3& p : m.verts) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    layOut(m, *boxes, lo, hi);
    return true;
}

std::shared_ptr<EditMesh> mesh(const std::string& rigPart) {
    auto it = parts().find(rigPart);
    return it == parts().end() ? nullptr : it->second.mesh;
}

bool placement(const std::string& rigPart, glm::vec3& position, glm::vec3& size) {
    auto it = parts().find(rigPart);
    if (it == parts().end()) return false;
    position = it->second.position;
    size = it->second.size;
    return true;
}

const char* nameOf(const EditMesh* m) {
    if (!m) return nullptr;
    for (auto& [name, p] : parts()) if (p.mesh.get() == m) return name.c_str();
    return nullptr;
}

bool apply(SceneNode& node) {
    auto it = parts().find(node.name);
    if (it == parts().end()) return false;
    Part& p = it->second;
    node.primitiveType = PrimitiveType::Mesh;
    node.editMesh = p.mesh;
    if (!p.gpu) {
        MeshEdit::refresh(node);
        p.gpu = node.mesh;
    }
    node.mesh = p.gpu;
    return true;
}

} // namespace PlayerModel
