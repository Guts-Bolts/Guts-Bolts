// Blood that shows up, and is the same for everyone (multiplayer).
//  * A fast drop that falls through the top of the floor in one step still lands ON the
//    floor (it used to stick sideways inside it, hidden).
//  * The host writes down every blood call (GoreEvent) with its dice roll: a joined
//    player doing the same call gets exactly the same drops.
//  * A joined player's own blood calls do nothing (the host decides), and its drops
//    leave no stains of their own: the host's stains (NetSplat) are copied over.
//  * Body stains land on the same place of the same body part, wherever it is.
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/Particles.h"
#include "scene/Physics.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

SceneNode* addPart(Scene& scene, const std::string& name, glm::vec3 at, glm::vec3 size) {
    SceneNode* p = scene.addNode(name, PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    p->transform.position = at;
    p->transform.scale = size;
    p->anchored = true;
    return p;
}

int count(const ParticleSystem& ps, Particle::Kind kind, bool worldOnly = false) {
    int n = 0;
    for (const Particle& p : ps.items()) if (p.kind == kind && (!worldOnly || !p.onPart)) ++n;
    return n;
}

void settle(ParticleSystem& ps, Physics& phys, float seconds) {
    for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f) ps.update(1.0f / 60.0f, 196.2f, phys);
}
} // namespace

int main() {
    Mesh::headless = true;
    Scene scene;
    addPart(scene, "Floor", {0, -0.5f, 0}, {60, 1, 60});
    SceneNode* arm = addPart(scene, "Arm", {20, 5, 0}, {1, 2, 1});
    Physics phys;
    phys.gather(scene);

    // --- A fast drop through the top of the floor lands on top of it ---
    {
        ParticleSystem ps;
        ps.spray(GoreKind::Blood, {0, 6, 0}, {0, -1, 0}, 120, 30.0f);
        settle(ps, phys, 3.0f);
        int onTop = 0, buried = 0;
        for (const Particle& p : ps.items()) {
            if (p.kind != Particle::Splat) continue;
            if (p.pos.y > 0.0f && p.normal.y > 0.9f) ++onTop;
            if (p.pos.y < -0.01f) ++buried;
        }
        check(onTop > 0, "fast drops land as pools on top of the floor (" + std::to_string(onTop) + ")");
        check(buried == 0, "and none stick sideways inside it (" + std::to_string(buried) + ")");
    }

    // --- The host's calls, done again on a joined player, make the same drops ---
    ParticleSystem host, replica;
    host.setRole(ParticleSystem::Role::Host);
    replica.setRole(ParticleSystem::Role::Replica);
    host.spray(GoreKind::Blood, {2, 4, 1}, {0.3f, 1, 0}, 40, 5.0f);
    host.spurt(GoreKind::Blood, {-2, 3, 0}, {1, 0.2f, 0}, {0, 0, 0}, 1.0f);
    std::vector<GoreEvent> events = host.takeEvents();
    check(events.size() == 2, "the host writes down each blood call once (" + std::to_string(events.size()) + ")");
    check(host.takeEvents().empty(), "and hands them over only once");

    replica.spray(GoreKind::Blood, {0, 4, 0}, {0, 1, 0}, 40, 5.0f);
    check(replica.items().empty(), "a joined player's own blood does nothing (the host decides)");
    for (const GoreEvent& e : events) replica.replay(e, nullptr);
    bool same = host.items().size() == replica.items().size() && !host.items().empty();
    for (size_t i = 0; same && i < host.items().size(); ++i)
        same = glm::length(host.items()[i].pos - replica.items()[i].pos) < 1e-5f &&
               glm::length(host.items()[i].vel - replica.items()[i].vel) < 1e-5f;
    check(same, "the joined player gets exactly the same drops (" + std::to_string(replica.items().size()) + ")");

    // --- Stains: only the host's land; they're copied over ---
    settle(host, phys, 3.0f);
    settle(replica, phys, 3.0f);
    check(count(host, Particle::Splat, true) > 0, "the host's drops make pools");
    check(count(replica, Particle::Splat, true) == 0, "the joined player's copies of them don't make pools of their own");
    std::vector<NetSplat> changed;
    std::vector<uint32_t> gone;
    host.syncSplats(changed, gone, 1000);
    check((int)changed.size() == count(host, Particle::Splat, true), "every pool is sent (" + std::to_string(changed.size()) + ")");
    replica.applySplats(changed, gone);
    bool match = count(replica, Particle::Splat, true) == count(host, Particle::Splat, true);
    for (const NetSplat& s : changed) {
        bool found = false;
        for (const Particle& p : replica.items()) found = found || (p.net == s.id && glm::length(p.pos - s.pos) < 1e-5f);
        match = match && found;
    }
    check(match, "and the joined player has the same pools in the same places");
    changed.clear(); gone.clear();
    host.syncSplats(changed, gone, 1000);
    check(changed.empty() && gone.empty(), "nothing is sent again while nothing changes");

    // A pool that goes away goes away for everyone.
    for (const Particle& p : host.items()) if (p.kind == Particle::Splat && p.net) { const_cast<Particle&>(p).life = 0.0f; break; }
    host.update(1.0f / 60.0f, 196.2f, phys);
    changed.clear(); gone.clear();
    host.syncSplats(changed, gone, 1000);
    check(gone.size() == 1, "a pool that's gone is said to be gone");
    const int before = count(replica, Particle::Splat, true);
    replica.applySplats(changed, gone);
    replica.update(1.0f / 60.0f, 196.2f, phys);
    check(count(replica, Particle::Splat, true) == before - 1, "and goes on the joined player's screen too");

    // A new joiner gets all of them at once.
    std::vector<NetSplat> all;
    host.allSplats(all);
    check((int)all.size() == count(host, Particle::Splat, true), "someone joining gets every pool there is");

    // --- Body stains: the same place on the same part, wherever the part is here ---
    {
        ParticleSystem h, r;
        h.setRole(ParticleSystem::Role::Host);
        r.setRole(ParticleSystem::Role::Replica);
        h.stain(GoreKind::Blood, arm, {20.6f, 5.5f, 0.1f}, 0.5f);
        std::vector<GoreEvent> ev = h.takeEvents();
        check(ev.size() == 1 && ev[0].part == arm->id, "a stain on a body part names the part");
        const glm::vec3 hostLocal = h.items().empty() ? glm::vec3(-9) : h.items().back().localPos;
        arm->transform.position = {-7, 2, 3};   // (it's somewhere else on this screen)
        r.replay(ev[0], [&](uint64_t id) -> const SceneNode* { return id == arm->id ? arm : nullptr; });
        check(!r.items().empty() && r.items().back().onPart == arm->id &&
              glm::length(r.items().back().localPos - hostLocal) < 1e-4f,
              "and the joined player's stain is on the same spot of that part");
        r.clear();
        r.replay(ev[0], [](uint64_t) -> const SceneNode* { return nullptr; });
        check(r.items().empty(), "a part that isn't here any more is just skipped");
    }

    std::cout << (failures ? "FAILED: " : "all passed") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}
