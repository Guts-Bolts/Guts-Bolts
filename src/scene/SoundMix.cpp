#include "SoundMix.h"
#include "Scene.h"
#include "SceneNode.h"
#include "../core/Audio.h"

#include <algorithm>
#include <set>

namespace SoundMix {

SceneNode* groupOf(Scene& scene, const SceneNode* sound) {
    if (!sound || !sound->isSound() || !sound->soundGroup) return nullptr;
    SceneNode* g = scene.findById(sound->soundGroup);
    return g && g->isSoundGroup() ? g : nullptr;
}

SceneNode* parentGroup(const SceneNode* group) {
    return group && group->parent && group->parent->isSoundGroup() ? group->parent : nullptr;
}

float volume(Scene& scene, const SceneNode* sound) {
    float v = sound ? std::max(0.0f, sound->volume) : 0.0f;
    int depth = 0;
    for (SceneNode* g = groupOf(scene, sound); g && depth < 32; g = parentGroup(g), ++depth) v *= std::max(0.0f, g->volume);
    return v;
}

std::vector<SoundFx::Settings> effectsIn(const SceneNode* container) {
    std::vector<std::pair<int, SoundFx::Settings>> list;
    if (container)
        for (const auto& c : container->children) {
            if (!c->isSoundEffect() || !c->enabled) continue;
            SoundFx::Settings s;
            s.id = c->id;
            s.type = c->soundFx.type;
            for (int i = 0; i < SoundFx::kMostParams; ++i) s.v[i] = c->soundFx.v[i];
            list.push_back({c->soundFx.priority, s});
        }
    // Higher Priority first; the same Priority keeps the order they're in.
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<SoundFx::Settings> out;
    for (auto& [p, s] : list) out.push_back(s);
    return out;
}

std::vector<SoundFx::Settings> chainOf(Scene& scene, const SceneNode* sound) {
    std::vector<SoundFx::Settings> out = effectsIn(sound);
    int depth = 0;
    for (SceneNode* g = groupOf(scene, sound); g && depth < 32; g = parentGroup(g), ++depth) {
        auto more = effectsIn(g);
        out.insert(out.end(), more.begin(), more.end());
    }
    return out;
}

namespace {
// Make sure a group's bus (and its parent groups' buses) exist and are up to date.
void busFor(SceneNode* group, std::set<uint64_t>& used) {
    for (int depth = 0; group && depth < 32 && !used.count(group->id); group = parentGroup(group), ++depth) {
        SceneNode* parent = parentGroup(group);
        Audio::setBus(group->id, parent ? parent->id : 0, effectsIn(group));
        used.insert(group->id);
    }
}

void point(Scene& scene, const SceneNode* sound, int handle, std::set<uint64_t>& used) {
    SceneNode* g = groupOf(scene, sound);
    busFor(g, used);
    Audio::setVolume(handle, volume(scene, sound));
    Audio::setEffects(handle, effectsIn(sound));
    Audio::setRoute(handle, g ? g->id : 0);
}
} // namespace

void apply(Scene& scene, const SceneNode* sound, int handle) {
    if (!handle || !sound) return;
    std::set<uint64_t> used;
    point(scene, sound, handle, used);
}

void update(Scene& scene) {
    std::set<uint64_t> used;
    scene.forEach([&](SceneNode* n) {
        if (n->isSound() && n->audioHandle) point(scene, n, n->audioHandle, used);
    });
    Audio::keepBuses(used);
}

} // namespace SoundMix
