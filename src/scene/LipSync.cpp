#include "LipSync.h"
#include "../core/Audio.h"
#include "Scene.h"
#include "SceneNode.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace LipSync {

namespace {

struct Mouth { float open = 0.0f; float jawRest = 0.0f; uint64_t jaw = 0; bool seen = false; };
std::unordered_map<uint64_t, Mouth>& mouths() {   // by head id
    static std::unordered_map<uint64_t, Mouth> m;
    return m;
}

const Attribute* mouthAttr(SceneNode& model) {
    if (const Attribute* a = model.findAttribute("MouthOpen"); a && a->type == Attribute::Number) return a;
    for (auto& c : model.children)
        if (c->name == "Humanoid")
            if (const Attribute* a = c->findAttribute("MouthOpen"); a && a->type == Attribute::Number) return a;
    return nullptr;
}

SceneNode* findJaw(SceneNode& head) {
    std::vector<SceneNode*> stack{&head};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n != &head && n->name == "Jaw" && n->isBone()) return n;
        for (auto& c : n->children) stack.push_back(c.get());
    }
    return nullptr;
}

} // namespace

float target(Scene&, SceneNode& model, const std::function<float(const std::string&)>& voiceLevel) {
    if (const Attribute* a = mouthAttr(model)) return std::clamp((float)a->n, 0.0f, 1.0f);
    float loud = 0.0f;
    // Sounds inside the character (an NPC talking).
    std::vector<SceneNode*> stack{&model};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n->isSound() && n->audioHandle) loud = std::max(loud, Audio::loudness(n->audioHandle) * 8.0f);
        for (auto& c : n->children) stack.push_back(c.get());
    }
    if (voiceLevel) loud = std::max(loud, voiceLevel(model.name));
    // A quiet hum barely moves it; talking opens it most of the way.
    return std::clamp((loud - 0.04f) * 1.6f, 0.0f, 1.0f);
}

void update(Scene& scene, float dt, const std::function<float(const std::string&)>& voiceLevel) {
    for (auto& [id, m] : mouths()) m.seen = false;
    scene.forEach([&](SceneNode* n) {
        if (!n->isPart() || n->name != "Head" || !n->parent || n->parent->kind != NodeKind::Model) return;
        SceneNode& model = *n->parent;
        Mouth& m = mouths()[n->id];
        m.seen = true;
        const float want = target(scene, model, voiceLevel);
        // Opens fast, shuts a bit slower (like a real mouth between sounds).
        const float speed = want > m.open ? 28.0f : 14.0f;
        m.open += (want - m.open) * (1.0f - std::exp(-speed * std::max(0.0f, dt)));
        if (m.open < 0.002f) m.open = 0.0f;
        // A Jaw bone in the head turns down with it.
        SceneNode* jaw = m.jaw ? scene.findById(m.jaw) : nullptr;
        if (!jaw || !n->isAncestorOf(jaw)) {
            jaw = findJaw(*n);
            m.jaw = jaw ? jaw->id : 0;
            if (jaw) m.jawRest = jaw->transform.rotation.x;
        }
        if (jaw) jaw->transform.rotation.x = m.jawRest + m.open * kJawDegrees;
    });
    for (auto it = mouths().begin(); it != mouths().end();) {
        if (!it->second.seen) it = mouths().erase(it);
        else ++it;
    }
}

float mouth(uint64_t headId) {
    auto it = mouths().find(headId);
    return it == mouths().end() ? 0.0f : it->second.open;
}

void clear() {
    mouths().clear();
}

} // namespace LipSync
