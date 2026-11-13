#include "Impacts.h"
#include "Realism.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Terrain.h"
#include "../core/Audio.h"
#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace Impacts {

namespace {
int g_last = 0;
double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
std::unordered_map<uint64_t, double>& lastHeard() { static std::unordered_map<uint64_t, double> m; return m; }
uint32_t g_seed = 0x2545F491u;
float wobble() { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5; return 0.92f + 0.16f * ((g_seed & 0xffff) / 65535.0f); }

Realism::Feel feelOf(Scene& scene, const SceneNode* n, const glm::vec3& at) {
    if (!n) return Realism::feelOf(scene.terrain().materialAt(at.x, at.z));
    if (scene.isCharacterPart(n)) return Realism::Feel::Flesh;
    return Realism::feelOf(n->material);
}
} // namespace

int lastCount() { return g_last; }

void play(Scene& scene, const std::vector<Physics::Knock>& knocks, const std::vector<Physics::Smash>& smashes) {
    g_last = 0;
    if (!scene.world().impactSounds) return;
    const double now = nowSeconds();
    auto& heard = lastHeard();
    if (heard.size() > 4096) heard.clear();
    int played = 0;
    for (const Physics::Smash& s : smashes) {
        const SceneNode* n = scene.findById(s.id);
        if (!n) continue;
        Audio::play(Realism::breakSound(Realism::feelOf(n->material)), 0.85f, wobble(), false, &s.at);
        heard[s.id] = now;
        ++played;
    }
    // The hardest knocks first, and only a few a frame.
    std::vector<const Physics::Knock*> order;
    for (const Physics::Knock& k : knocks) order.push_back(&k);
    std::sort(order.begin(), order.end(), [](const Physics::Knock* x, const Physics::Knock* y) { return x->speed > y->speed; });
    for (const Physics::Knock* k : order) {
        if (played >= 6) break;
        const float vol = Realism::hitVolume(k->speed, k->mass);
        if (vol <= 0.0f) continue;
        double& last = heard[k->a];
        if (now - last < 0.12) continue;   // (rattling: not every frame)
        last = now;
        const SceneNode* a = scene.findById(k->a);
        const SceneNode* b = k->b ? scene.findById(k->b) : nullptr;
        // The harder of the two rings out: a crate on metal sounds of metal.
        Realism::Feel fa = feelOf(scene, a, k->at), fb = feelOf(scene, b, k->at);
        auto ring = [](Realism::Feel f) {
            switch (f) { case Realism::Feel::Metal: return 4; case Realism::Feel::Glass: return 3; case Realism::Feel::Stone: return 2;
                         case Realism::Feel::Wood: return 1; default: return 0; }
        };
        const Realism::Feel f = ring(fb) > ring(fa) ? fb : fa;
        Audio::play(Realism::hitSound(f), vol, wobble(), false, &k->at);
        ++played;
        ++g_last;
    }
}

} // namespace Impacts
