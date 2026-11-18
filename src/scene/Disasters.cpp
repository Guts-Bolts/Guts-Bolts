#include "Disasters.h"
#include "Effects.h"
#include "Blast.h"
#include "Npc.h"
#include "Player.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Shatter.h"
#include "Storms.h"
#include "Terrain.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

using nlohmann::json;

namespace Disasters {
namespace {

float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float rnd(State& st) {
    st.rng ^= st.rng << 13;
    st.rng ^= st.rng >> 17;
    st.rng ^= st.rng << 5;
    return (float)(st.rng & 0xFFFFFF) / (float)0x1000000;
}

glm::vec3 flat(const glm::vec3& v) { return glm::vec3(v.x, 0.0f, v.z); }
glm::vec3 unitFlat(const glm::vec3& v, const glm::vec3& fallback = glm::vec3(1, 0, 0)) {
    const glm::vec3 f = flat(v);
    const float l = glm::length(f);
    return l > 1e-4f ? f / l : fallback;
}

float groundAt(const Scene& scene, const glm::vec3& p) {
    float h = 0.0f;
    if (!scene.terrain().empty() && scene.terrain().heightAt(p.x, p.z, h)) return h;
    return p.y;
}

// How much of it is there now: grows in, fades out when stopping.
float fadeOf(const Disaster& d) {
    float grow = 1.0f, fadeTime = 1.0f;
    switch (d.kind) {
        case Kind::Tornado:    grow = smooth(0.0f, 3.0f, d.age); fadeTime = 3.0f; break;
        case Kind::Tsunami:    grow = smooth(0.0f, 1.0f, d.age); fadeTime = 2.0f; break;
        case Kind::Earthquake: grow = 1.0f; fadeTime = 2.0f; break;
        case Kind::Lightning:  grow = 1.0f; fadeTime = 0.2f; break;
    }
    if (d.stopping && d.lifeLeft >= 0.0f) grow *= std::clamp(d.lifeLeft / fadeTime, 0.0f, 1.0f);
    return grow;
}

float fadeTimeOf(Kind k) {
    switch (k) {
        case Kind::Tornado: return 3.0f;
        case Kind::Tsunami: return 2.0f;
        case Kind::Earthquake: return 2.0f;
        default: return 0.0f;
    }
}

bool released(const Disaster& d, uint64_t root) {
    return root && std::find(d.released.begin(), d.released.end(), root) != d.released.end();
}

// The push of one disaster on a character at `p` moving at `v` (studs / second²).
glm::vec3 pushOf(const Scene& scene, const Disaster& d, const glm::vec3& p, const glm::vec3& v) {
    const float g = scene.world().gravity;
    const float k = d.fade;
    if (k <= 0.0f) return glm::vec3(0.0f);
    if (d.kind == Kind::Tornado) {
        const glm::vec3 off = flat(p - d.pos);
        const float r = glm::length(off);
        const float core = tornadoCore(d), reach = tornadoReach(d);
        const float up = p.y - d.pos.y;
        if (r > reach || up < -6.0f || up > Storms::kFunnelHeight) return glm::vec3(0.0f);
        const glm::vec3 out = r > 1e-3f ? off / r : glm::vec3(1, 0, 0);
        const glm::vec3 around(-out.z, 0.0f, out.x);
        const float orbit = core * 0.8f;
        // Round and round: like a wheel inside, slower further out.
        const float spinSpeed = (18.0f + 7.0f * d.strength) * d.spin;
        const float turn = r < orbit ? spinSpeed * r / orbit : spinSpeed * orbit / r;
        // Sucked in towards the funnel (not past where it whirls you round).
        const float in = (10.0f + 4.0f * d.strength) * d.pull * smooth(orbit * 0.8f, orbit * 1.6f, r) * (1.0f - smooth(reach * 0.6f, reach, r));
        glm::vec3 want = around * turn - out * in;
        const float caught = 1.0f - smooth(core * 1.1f, core * 2.2f, r);   // in the funnel: lifted
        const float influence = 1.0f - smooth(reach * 0.55f, reach, r);
        glm::vec3 push(0.0f);
        push.x = (want.x - v.x) * 3.5f * influence;
        push.z = (want.z - v.z) * 3.5f * influence;
        if (caught > 0.0f) {
            // Up to the carry height, then held there, bobbing.
            const float carry = tornadoCarry(d);
            const float vyWant = std::clamp((carry - up) * 1.2f, -8.0f, (8.0f + 2.0f * d.strength) * d.lift);
            push.y = g * caught + (vyWant - v.y) * 4.0f * caught;
        } else if (r < core * 3.0f && up < 2.0f) {
            push.y = g * 1.15f * (1.0f - smooth(core * 2.2f, core * 3.0f, r)) * std::min(1.0f, d.pull);   // swept off your feet near it
        }
        return push * k;
    }
    if (d.kind == Kind::Tsunami) {
        const float top = waterTop(d, p);
        if (p.y > top + 0.5f) return glm::vec3(0.0f);
        const glm::vec3 want = d.dir * d.speed * 1.05f;
        glm::vec3 push((want.x - v.x) * 4.0f, 0.0f, (want.z - v.z) * 4.0f);
        // Floated up to near the surface and tumbled along with it.
        const float vyWant = std::clamp((top - 1.2f - p.y) * 2.5f, -6.0f, 14.0f);
        push.y = g + (vyWant - v.y) * 3.0f;
        return push * k;
    }
    return glm::vec3(0.0f);
}

// ---- The host's view of the characters -------------------------------------------

struct Body {
    uint64_t  root;
    glm::vec3 pos, vel;
    int       who;       // 0 this computer's player, 1 another player, 2 an NPC
};

glm::vec3 bodyPos(SceneNode* root) {
    if (SceneNode* hrp = root->findChild("HumanoidRootPart")) return glm::vec3(hrp->worldMatrix()[3]);
    if (SceneNode* torso = root->findChild("Torso")) return glm::vec3(torso->worldMatrix()[3]);
    return root->transform.position + glm::vec3(0.0f, 2.5f, 0.0f);
}

std::vector<Body> bodies(Scene& scene, State& st, float dt) {
    std::vector<Body> out;
    for (State::Track& t : st.tracks) t.fresh = false;
    auto add = [&](uint64_t id, int who) {
        SceneNode* root = scene.findById(id);
        if (!root) return;
        const glm::vec3 p = bodyPos(root);
        State::Track* tr = nullptr;
        for (State::Track& t : st.tracks) if (t.root == id) { tr = &t; break; }
        if (!tr) { st.tracks.push_back({id, p, glm::vec3(0.0f), true}); tr = &st.tracks.back(); }
        else if (dt > 1e-4f) {
            const glm::vec3 v = (p - tr->pos) / dt;
            tr->vel += (v - tr->vel) * 0.5f;
        }
        tr->pos = p;
        tr->fresh = true;
        // (Measured from where the body really goes: a ragdoll's own velocity isn't the character's.)
        out.push_back({id, p, tr->vel, who});
    };
    if (Player* me = scene.player(); me && me->root() && !me->isDead()) add(me->rootId(), 0);
    for (RemoteCharacter& rc : scene.remotes()) if (rc.alive && rc.humanoid.health > 0.0f) add(rc.rootId, 1);
    for (const auto& n : scene.npcs().all()) if (n && !n->dead && n->humanoid.health > 0.0f) add(n->rootId, 2);
    st.tracks.erase(std::remove_if(st.tracks.begin(), st.tracks.end(), [](const State::Track& t) { return !t.fresh; }), st.tracks.end());
    return out;
}

bool shielded(Scene& scene, uint64_t root) {
    SceneNode* n = scene.findById(root);
    return n && n->hasForceField();
}

void hurt(Scene& scene, const Body& b, float damage, float force, const glm::vec3& impulse) {
    if (damage <= 0.0f) return;
    if (b.who == 0) { scene.player()->hurt(damage, force, impulse); return; }
    if (shielded(scene, b.root)) return;
    if (b.who == 1) {
        if (RemoteCharacter* rc = scene.findRemote(b.root)) {
            rc->humanoid.health = std::max(0.0f, rc->humanoid.health - damage);
            rc->humanoidDirty = true;
            if (rc->humanoid.health <= 0.0f) rc->kills.push_back({force, impulse});
        }
        return;
    }
    if (Npc* n = scene.npcs().find(b.root)) {
        n->humanoid.health = std::max(0.0f, n->humanoid.health - damage);
        if (n->humanoid.health <= 0.0f) scene.npcs().kill(b.root, force, impulse);
    }
}

void killBody(Scene& scene, const Body& b, float force, const glm::vec3& impulse) {
    if (shielded(scene, b.root)) return;
    scene.killCharacter(b.root, force, impulse);
}

void trip(Scene& scene, const Body& b, float seconds) {
    if (b.who == 0) { scene.player()->trip(seconds, glm::vec3(180.0f, 0.0f, 0.0f)); return; }
    if (b.who == 1)
        if (RemoteCharacter* rc = scene.findRemote(b.root)) {
            RemoteCharacter::Kill k{-1.0f, glm::vec3(0.0f)};
            k.trip = seconds;
            rc->kills.push_back(k);
        }
}

void launch(Scene& scene, const Body& b, const glm::vec3& v) {
    if (b.who == 0) { scene.player()->launch(v); return; }
    if (b.who == 1) {
        if (RemoteCharacter* rc = scene.findRemote(b.root)) rc->kills.push_back({-1.0f, v - b.vel});
        return;
    }
    if (Npc* n = scene.npcs().find(b.root)) { n->velocity = v; n->grounded = false; }
}

State::Victim& victim(State& st, uint64_t root, uint32_t disaster, bool& isNew) {
    for (State::Victim& v : st.victims)
        if (v.root == root && v.disaster == disaster) { isNew = false; return v; }
    isNew = true;
    State::Victim v;
    v.root = root;
    v.disaster = disaster;
    st.victims.push_back(v);
    return st.victims.back();
}

// Damage a little every second (whole points, so health doesn't flicker).
void bleedOut(Scene& scene, State::Victim& v, const Body& b, float perSecond, float dt) {
    if (perSecond <= 0.0f) return;
    v.hurtLeft += perSecond * dt;
    if (v.hurtLeft >= 1.0f) {
        const float whole = std::floor(v.hurtLeft);
        v.hurtLeft -= whole;
        hurt(scene, b, whole, 0.3f, glm::vec3(0.0f));
    }
}

void smash(Scene& scene, State& st, const Disaster& d) {
    std::vector<std::pair<SceneNode*, glm::vec3>> hit;
    const float level = d.kind == Kind::Earthquake ? quakeLevel(d) : 0.0f;
    scene.forEach([&](SceneNode* n) {
        if (hit.size() >= 6 || !n->isPart() || !Shatter::breakable(*n)) return;
        const glm::vec3 p(n->worldMatrix()[3]);
        if (d.kind == Kind::Tornado) {
            const float r = glm::length(flat(p - d.pos));
            if (r < tornadoCore(d) * 2.5f && p.y - d.pos.y < tornadoCarry(d) + 10.0f) hit.push_back({n, p + unitFlat(d.pos - p) * 0.5f});
        } else if (d.kind == Kind::Tsunami) {
            if (p.y < waterTop(d, p)) hit.push_back({n, p - d.dir * 0.5f});
        } else if (d.kind == Kind::Earthquake) {
            const float here = level * (d.radius > 0.0f ? 1.0f - smooth(d.radius, d.radius * 1.5f, glm::length(flat(p - d.pos))) : 1.0f);
            if (here > 0.4f && rnd(st) < (here - 0.4f) * 0.25f) hit.push_back({n, p - glm::vec3(0.0f, 0.4f, 0.0f)});
        }
    });
    for (auto& [part, at] : hit) Shatter::breakPart(scene, part, at, d.kind == Kind::Earthquake ? 0.6f : 1.0f);
}

void event(State& st, Event::What what, uint32_t disaster, uint64_t character, const glm::vec3& pos) {
    if (st.events.size() < 500) st.events.push_back({what, disaster, character, pos});
}

void hostLightning(Scene& scene, State& st, Disaster& d, const std::vector<Body>& people) {
    if (d.struck) return;
    d.struck = true;
    if (d.follow) {
        for (const Body& b : people)
            if (b.root == d.follow) d.pos = b.pos - glm::vec3(0.0f, 2.5f, 0.0f);
    }
    const float reach = d.radius > 0.0f ? d.radius : 6.0f;
    for (const Body& b : people) {
        const glm::vec3 off = b.pos - d.pos;
        const bool target = b.root == d.follow;
        if (!target && (glm::length(flat(off)) > reach || off.y > reach + 6.0f || off.y < -4.0f)) continue;
        const glm::vec3 throwV = (unitFlat(off) * 0.6f + glm::vec3(0.0f, 1.0f, 0.0f)) * 18.0f * scene.world().launchScale();
        event(st, Event::Hit, d.id, b.root, b.pos);
        if (d.kill) {
            killBody(scene, b, d.ragdoll ? 0.5f : 0.0f, throwV);
            event(st, Event::Killed, d.id, b.root, b.pos);
            continue;
        }
        hurt(scene, b, d.damage, d.ragdoll ? 0.5f : 0.0f, glm::vec3(0.0f));
        if (d.ragdoll) { launch(scene, b, b.vel + throwV); trip(scene, b, 2.5f); }
    }
    // Loose parts near it jump, and Breakable ones crack.
    if (d.destroy) {
        std::vector<SceneNode*> crack;
        scene.forEach([&](SceneNode* n) {
            if (crack.size() >= 4 || !n->isPart() || !Shatter::breakable(*n)) return;
            if (glm::length(glm::vec3(n->worldMatrix()[3]) - d.pos) < reach) crack.push_back(n);
        });
        for (SceneNode* n : crack) Shatter::breakPart(scene, n, d.pos, 0.8f);
    }
}

void hostTornado(Scene& scene, State& st, Disaster& d, const std::vector<Body>& people, float dt) {
    // Drift along, or chase someone.
    if (d.follow) {
        for (const Body& b : people)
            if (b.root == d.follow) {
                const glm::vec3 to = flat(b.pos - d.pos);
                const float speed = d.followSpeed > 0.0f ? d.followSpeed : 12.0f;
                const glm::vec3 want = glm::length(to) > 1.0f ? glm::normalize(to) * speed : glm::vec3(0.0f);
                d.velocity += (want - d.velocity) * std::min(1.0f, dt * 1.5f);
            }
    }
    d.pos += d.velocity * dt;
    d.pos.y = groundAt(scene, d.pos);
    // Let go of the ones it threw, after a while.
    for (size_t i = 0; i < d.released.size();) {
        if (i < d.releasedFor.size() && (d.releasedFor[i] -= dt) > 0.0f) { ++i; continue; }
        d.released.erase(d.released.begin() + (long)i);
        if (i < d.releasedFor.size()) d.releasedFor.erase(d.releasedFor.begin() + (long)i);
    }
    if (d.fade < 0.3f) return;
    const float core = tornadoCore(d);
    for (const Body& b : people) {
        if (released(d, b.root)) continue;
        // NPCs have no computer of their own: the host blows them about.
        if (b.who == 2)
            if (Npc* n = scene.npcs().find(b.root)) {
                const glm::vec3 push = pushOf(scene, d, b.pos, n->velocity);
                if (glm::length(push) > 0.01f) {
                    n->velocity += push * dt;
                    if (push.y > scene.world().gravity * 1.05f) n->grounded = false;
                }
            }
        const float r = glm::length(flat(b.pos - d.pos));
        const float up = b.pos.y - d.pos.y;
        if (r > core * 1.6f || up < 2.0f) continue;
        bool fresh = false;
        State::Victim& v = victim(st, b.root, d.id, fresh);
        if (v.thrown) continue;
        v.seen = true;
        if (fresh) event(st, Event::Hit, d.id, b.root, b.pos);   // (no ragdoll yet: a limp body can't be whirled round; that's for the throw)
        v.inside += dt;
        bleedOut(scene, v, b, d.damage, dt);
        if (v.inside < d.throwAfter) continue;
        // Thrown out of the side of the funnel, spinning away.
        const glm::vec3 out = unitFlat(b.pos - d.pos), around(-out.z, 0.0f, out.x);
        const float speed = (28.0f + 6.0f * d.strength) * d.throwForce * scene.world().launchScale();
        const glm::vec3 vel = glm::normalize(out * 0.8f + around * 0.5f + glm::vec3(0.0f, 0.55f, 0.0f)) * speed;
        d.released.push_back(b.root);
        d.releasedFor.push_back(5.0f);
        v.thrown = true;
        v.sinceThrow = 0.0f;
        v.peakSpeed = 0.0f;
        event(st, Event::Thrown, d.id, b.root, b.pos);
        if (d.kill && !d.explode) {
            killBody(scene, b, 0.8f, vel);
            event(st, Event::Killed, d.id, b.root, b.pos);
            continue;
        }
        launch(scene, b, vel);
        if (d.ragdoll || d.explode) trip(scene, b, 5.0f);
        v.armed = d.explode;
    }
}

void hostTsunami(Scene& scene, State& st, Disaster& d, const std::vector<Body>& people, float dt) {
    d.pos += d.dir * d.speed * dt;
    if (d.distance > 0.0f && d.speed * d.age >= d.distance && !d.stopping) { d.stopping = true; d.lifeLeft = fadeTimeOf(d.kind); }
    if (d.fade < 0.2f) return;
    for (const Body& b : people) {
        if (b.pos.y - 1.0f > waterTop(d, b.pos)) continue;
        if (b.who == 2)
            if (Npc* n = scene.npcs().find(b.root)) {
                n->velocity += pushOf(scene, d, b.pos, n->velocity) * dt;
                n->grounded = false;
            }
        bool fresh = false;
        State::Victim& v = victim(st, b.root, d.id, fresh);
        v.seen = true;
        if (fresh) {
            event(st, Event::Hit, d.id, b.root, b.pos);
            if (d.kill) {
                killBody(scene, b, d.ragdoll ? 0.3f : 0.0f, d.dir * d.speed);
                event(st, Event::Killed, d.id, b.root, b.pos);
                continue;
            }
        }
        v.inside += dt;
        if (d.ragdoll && (v.tripAgain -= dt) <= 0.0f) {   // tumbled along by the water
            launch(scene, b, d.dir * d.speed * 0.9f + glm::vec3(0.0f, 6.0f, 0.0f));
            trip(scene, b, 2.2f);
            v.tripAgain = 2.6f;
        }
        bleedOut(scene, v, b, d.damage, dt);
    }
}

void hostQuake(Scene& scene, State& st, Disaster& d, const std::vector<Body>& people, float dt) {
    for (const Body& b : people) {
        const float level = quakeLevel(d) * (d.radius > 0.0f ? 1.0f - smooth(d.radius, d.radius * 1.5f, glm::length(flat(b.pos - d.pos))) : 1.0f);
        if (level < 0.05f) continue;
        bool fresh = false;
        State::Victim& v = victim(st, b.root, d.id, fresh);
        v.seen = true;
        if (fresh) event(st, Event::Hit, d.id, b.root, b.pos);
        v.inside += dt;
        bleedOut(scene, v, b, d.damage * level, dt);
        if ((v.tripAgain -= dt) > 0.0f) continue;
        // The harder it shakes, the more often people go over.
        if (d.ragdoll && level > 0.2f && rnd(st) < (level - 0.1f) * 1.6f * dt) {
            trip(scene, b, 2.0f + 3.0f * level);
            v.tripAgain = 3.0f + 3.0f * level;
        } else if (d.loseFooting && level > 0.12f && rnd(st) < (level - 0.05f) * 1.4f * dt) {
            trip(scene, b, 0.6f + level);
            v.tripAgain = 1.5f;
        }
    }
}

// People a tornado threw: did they hit something? (Their speed suddenly drops.)
void watchThrown(Scene& scene, State& st, const std::vector<Body>& people, float dt) {
    for (State::Victim& v : st.victims) {
        if (!v.thrown) continue;
        v.seen = true;
        v.sinceThrow += dt;
        if (!v.armed) { if (v.sinceThrow > 6.0f) v.seen = false; continue; }
        const Body* b = nullptr;
        for (const Body& p : people) if (p.root == v.root) b = &p;
        if (!b) { v.seen = false; continue; }
        const float speed = glm::length(b->vel);
        v.peakSpeed = std::max(v.peakSpeed, speed);
        const bool landed = v.sinceThrow > 0.35f && v.peakSpeed > 12.0f && speed < v.peakSpeed * 0.35f;
        if (!landed && v.sinceThrow < 8.0f) continue;
        BlastOptions o;
        o.radius = 7.0f;
        o.power = 1.1f;
        o.fire = 2.0f;
        Effects::blast(scene, b->pos, o);
        killBody(scene, *b, 1.0f, glm::vec3(0.0f, 20.0f, 0.0f));
        event(st, Event::Exploded, v.disaster, v.root, b->pos);
        event(st, Event::Killed, v.disaster, v.root, b->pos);
        v.armed = false;
        v.seen = false;
    }
}

float numAttr(const SceneNode* n, const char* name, float fallback) {
    const Attribute* a = n->findAttribute(name);
    return a && a->type == Attribute::Number ? (float)a->n : fallback;
}
bool boolAttr(const SceneNode* n, const char* name, bool fallback) {
    const Attribute* a = n->findAttribute(name);
    return a && a->type == Attribute::Bool ? a->b : fallback;
}

void startMarkers(Scene& scene, State& st) {
    scene.forEach([&](SceneNode* n) {
        if (!n->isPart()) return;
        const Attribute* a = n->findAttribute("Disaster");
        if (!a || a->type != Attribute::String || !boolAttr(n, "AutoStart", true)) return;
        st.timers.push_back({n->id, std::max(0.0f, numAttr(n, "Delay", 0.0f))});
    });
}

float r2(float v) { return std::round(v * 100.0f) / 100.0f; }
json v3(const glm::vec3& v) { return json::array({r2(v.x), r2(v.y), r2(v.z)}); }
glm::vec3 v3(const json& j, const glm::vec3& fallback = glm::vec3(0.0f)) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

} // namespace

bool kindFromName(const std::string& name, Kind& out) {
    std::string low;
    for (char c : name) low += (char)std::tolower((unsigned char)c);
    for (int i = 0; i < kKinds; ++i) {
        std::string k;
        for (const char* p = kKindNames[i]; *p; ++p) k += (char)std::tolower((unsigned char)*p);
        if (k == low) { out = (Kind)i; return true; }
    }
    if (low == "quake") { out = Kind::Earthquake; return true; }
    if (low == "flood" || low == "wave") { out = Kind::Tsunami; return true; }
    return false;
}

float tornadoCore(const Disaster& d) { return d.radius > 0.0f ? d.radius : Storms::coreRadius(d.strength); }
float tornadoReach(const Disaster& d) { return tornadoCore(d) * (5.0f + 2.0f * std::min(3.0f, d.pull)); }
float tornadoCarry(const Disaster& d) { return d.height > 0.0f ? d.height : 14.0f + 4.0f * d.strength; }
float tsunamiLength(const Disaster& d) { return std::max(30.0f, d.height * 5.0f); }

float quakeLevel(const Disaster& d) {
    if (d.kind != Kind::Earthquake) return 0.0f;
    const float peak = Storms::quakeStrength(std::clamp(d.strength, 1.0f, 9.5f));
    float shape;
    if (d.duration > 0.0f) {
        // A light trembling first (the P wave), then the heavy shaking (S wave), dying away.
        const float u = std::clamp(d.age / d.duration, 0.0f, 1.0f);
        const float first = 0.22f * smooth(0.0f, 0.04f, u) * (1.0f - smooth(0.8f, 1.0f, u));
        const float main = smooth(0.12f, 0.25f, u) * (1.0f - smooth(0.55f, 1.0f, u));
        shape = std::max(first, main);
    } else {
        shape = std::max(0.22f * smooth(0.0f, 0.5f, d.age), smooth(2.0f, 4.0f, d.age));
    }
    return peak * shape * d.fade;
}

float waterTop(const Disaster& d, const glm::vec3& p) {
    if (d.kind != Kind::Tsunami || d.fade <= 0.0f) return -1e9f;
    const glm::vec3 side(-d.dir.z, 0.0f, d.dir.x);
    const glm::vec3 off = p - d.pos;
    const float s = -glm::dot(flat(off), d.dir);   // how far behind the front
    const float lat = std::abs(glm::dot(flat(off), side));
    const float L = tsunamiLength(d);
    if (s < 0.0f || s > L || lat > d.radius) return -1e9f;
    const float x = s / L;
    float h = d.height * smooth(0.0f, 0.08f, x) * (1.0f - 0.85f * smooth(0.3f, 1.0f, x));
    h *= 1.0f - smooth(d.radius * 0.85f, d.radius, lat);
    return d.pos.y + h * d.fade;
}

Disaster defaults(Kind kind) {
    Disaster d;
    d.kind = kind;
    switch (kind) {
        case Kind::Lightning:
            d.duration = 1.2f; d.strength = 1.0f; d.radius = 6.0f; d.damage = 100.0f; d.kill = false; d.ragdoll = true;
            break;
        case Kind::Tornado:
            d.duration = 60.0f; d.strength = 2.0f; d.damage = 0.0f; d.ragdoll = true;
            break;
        case Kind::Tsunami:
            d.duration = 0.0f; d.height = 18.0f; d.radius = 80.0f; d.speed = 30.0f; d.distance = 300.0f; d.damage = 10.0f; d.ragdoll = true;
            break;
        case Kind::Earthquake:
            d.duration = 20.0f; d.strength = 6.0f; d.radius = 0.0f; d.loseFooting = true; d.ragdoll = false;
            break;
    }
    return d;
}

uint32_t spawn(Scene& scene, Disaster d) {
    State& st = scene.disasters();
    d.id = st.nextId++;
    d.age = 0.0f;
    d.stopping = false;
    d.lifeLeft = -1.0f;
    d.struck = false;
    d.released.clear();
    d.releasedFor.clear();
    d.strength = std::clamp(d.strength, 0.0f, d.kind == Kind::Earthquake ? 9.5f : 5.0f);
    d.radius = std::clamp(d.radius, 0.0f, 2000.0f);
    d.height = std::clamp(d.height, 0.0f, 400.0f);
    d.speed = std::clamp(d.speed, 0.0f, 400.0f);
    d.duration = std::clamp(d.duration, 0.0f, 3600.0f);
    d.dir = unitFlat(d.dir);
    if (d.kind == Kind::Lightning) d.duration = 1.2f;
    if (d.kind == Kind::Tornado) d.pos.y = groundAt(scene, d.pos);
    d.fade = fadeOf(d);
    st.list.push_back(d);
    if (d.kind == Kind::Lightning) {
        // (The strike happens on the host's next update, where the target is found.)
        st.seenBolts.push_back(d.id);
    }
    st.publish = 0.0f;
    return d.id;
}

Disaster* find(Scene& scene, uint32_t id) {
    for (Disaster& d : scene.disasters().list) if (d.id == id) return &d;
    return nullptr;
}
const Disaster* find(const Scene& scene, uint32_t id) {
    for (const Disaster& d : scene.disasters().list) if (d.id == id) return &d;
    return nullptr;
}

void stop(Scene& scene, uint32_t id, bool now) {
    if (Disaster* d = find(scene, id)) {
        if (now) { d->stopping = true; d->lifeLeft = 0.0f; }
        else if (!d->stopping) { d->stopping = true; d->lifeLeft = fadeTimeOf(d->kind); }
        scene.disasters().publish = 0.0f;
    }
}

void reset(Scene& scene) {
    State& st = scene.disasters();
    const uint32_t serial = st.boltSerial;
    st = State{};
    st.boltSerial = serial;
}

void stopAll(Scene& scene) {
    for (Disaster& d : scene.disasters().list) { d.stopping = true; d.lifeLeft = 0.0f; }
    scene.disasters().publish = 0.0f;
}

uint32_t spawnFromPart(Scene& scene, uint64_t partId) {
    SceneNode* n = scene.findById(partId);
    if (!n) return 0;
    const Attribute* a = n->findAttribute("Disaster");
    Kind kind;
    if (!a || a->type != Attribute::String || !kindFromName(a->s, kind)) return 0;
    Disaster d = defaults(kind);
    const glm::mat4 m = n->worldMatrix();
    d.pos = glm::vec3(m[3]);
    if (kind != Kind::Tornado) d.pos.y -= n->transform.scale.y * 0.5f;   // (its bottom: the ground)
    d.dir = -glm::vec3(m[2]);   // its front (LookVector)
    d.strength = numAttr(n, kind == Kind::Earthquake ? "Magnitude" : "Strength", numAttr(n, "Strength", d.strength));
    d.radius = numAttr(n, "Radius", d.radius);
    d.height = numAttr(n, "Height", d.height);
    d.speed = numAttr(n, "Speed", d.speed);
    d.distance = numAttr(n, "Distance", d.distance);
    d.duration = numAttr(n, "Duration", d.duration);
    d.damage = numAttr(n, "Damage", d.damage);
    d.spin = numAttr(n, "RotationSpeed", d.spin);
    d.pull = numAttr(n, "PullForce", d.pull);
    d.lift = numAttr(n, "LiftForce", d.lift);
    d.throwForce = numAttr(n, "ThrowForce", d.throwForce);
    d.throwAfter = numAttr(n, "ThrowAfter", d.throwAfter);
    d.shake = numAttr(n, "Shake", d.shake);
    d.kill = boolAttr(n, "Kill", d.kill);
    d.ragdoll = boolAttr(n, "Ragdoll", d.ragdoll);
    d.explode = boolAttr(n, "ExplodeOnImpact", d.explode);
    d.loseFooting = boolAttr(n, "LoseFooting", d.loseFooting);
    d.destroy = boolAttr(n, "Destroy", d.destroy);
    if (const Attribute* v = n->findAttribute("Velocity"); v && v->type == Attribute::Vector3) d.velocity = v->v;
    d.marker = n->id;
    n->transparency = 1.0f;   // (the spawner itself is only for building)
    return spawn(scene, d);
}

void update(Scene& scene, float dt, bool host) {
    State& st = scene.disasters();
    dt = std::clamp(dt, 0.0f, 0.1f);
    st.clock += dt;

    if (!host) {
        // Follow the host's list: carry on moving between its updates.
        for (Disaster& d : st.list) {
            d.age += dt;
            if (d.stopping && d.lifeLeft > 0.0f) d.lifeLeft = std::max(0.0f, d.lifeLeft - dt);
            if (d.kind == Kind::Tornado) d.pos += d.velocity * dt;
            if (d.kind == Kind::Tsunami) d.pos += d.dir * d.speed * dt;
            d.fade = fadeOf(d);
        }
        return;
    }

    // Studio spawner parts that start by themselves (and the ones that repeat).
    if (!st.markersStarted) { st.markersStarted = true; startMarkers(scene, st); }
    for (size_t i = 0; i < st.timers.size();) {
        if ((st.timers[i].second -= dt) > 0.0f) { ++i; continue; }
        const uint64_t part = st.timers[i].first;
        st.timers.erase(st.timers.begin() + (long)i);
        if (spawnFromPart(scene, part))
            if (SceneNode* n = scene.findById(part))
                if (float again = numAttr(n, "Repeat", 0.0f); again > 0.0f) st.timers.push_back({part, std::max(1.0f, again)});
    }

    if (st.list.empty() && st.victims.empty()) return;
    std::vector<Body> people = bodies(scene, st, dt);
    for (State::Victim& v : st.victims) v.seen = false;
    for (Disaster& d : st.list) {
        d.age += dt;
        if (!d.stopping && d.duration > 0.0f && d.age >= d.duration) { d.stopping = true; d.lifeLeft = fadeTimeOf(d.kind); }
        if (d.stopping) d.lifeLeft = std::max(0.0f, d.lifeLeft - dt);
        d.fade = fadeOf(d);
        switch (d.kind) {
            case Kind::Lightning:  hostLightning(scene, st, d, people); break;
            case Kind::Tornado:    hostTornado(scene, st, d, people, dt); break;
            case Kind::Tsunami:    hostTsunami(scene, st, d, people, dt); break;
            case Kind::Earthquake: hostQuake(scene, st, d, people, dt); break;
        }
    }
    watchThrown(scene, st, people, dt);
    // Smash what's in the way, a few times a second.
    static float smashTimer = 0.0f;
    if ((smashTimer -= dt) <= 0.0f) {
        smashTimer = 0.3f;
        for (const Disaster& d : st.list)
            if (d.destroy && d.fade > 0.5f && d.kind != Kind::Lightning) smash(scene, st, d);
    }
    // Forget people who got away (and the ones who were dealt with).
    st.victims.erase(std::remove_if(st.victims.begin(), st.victims.end(), [&](const State::Victim& v) {
        return !v.seen;
    }), st.victims.end());
    // The ones that are over.
    for (size_t i = 0; i < st.list.size();) {
        const Disaster& d = st.list[i];
        if (d.stopping && d.lifeLeft <= 0.0f) {
            event(st, Event::Ended, d.id, 0, d.pos);
            if (d.marker) if (SceneNode* m = scene.findById(d.marker)) if (!m->findAttribute("Repeat")) m->transparency = 1.0f;
            st.list.erase(st.list.begin() + (long)i);
            st.publish = 0.0f;
        } else ++i;
    }
    // A bolt the host itself started: show it here too.
    for (const Disaster& d : st.list)
        if (d.kind == Kind::Lightning && d.struck && d.age <= dt + 1e-4f) st.addBolt(d.pos, d.strength);
}

std::string encode(const State& st) {
    if (st.list.empty()) return "";
    json list = json::array();
    for (const Disaster& d : st.list) {
        json e = {{"i", d.id}, {"k", (int)d.kind}, {"p", v3(d.pos)}, {"a", r2(d.age)}, {"T", r2(d.duration)},
                  {"s", r2(d.strength)}, {"r", r2(d.radius)}, {"sh", r2(d.shake)}};
        if (d.kind == Kind::Tornado) {
            e["v"] = v3(d.velocity);
            e["h"] = r2(d.height);
            e["tw"] = json::array({r2(d.spin), r2(d.pull), r2(d.lift)});
            if (!d.released.empty()) e["rel"] = d.released;
        }
        if (d.kind == Kind::Tsunami) { e["d"] = v3(d.dir); e["h"] = r2(d.height); e["sp"] = r2(d.speed); }
        if (d.stopping) e["ll"] = r2(d.lifeLeft);
        list.push_back(e);
    }
    return list.dump();
}

void decode(Scene& scene, const std::string& text) {
    State& st = scene.disasters();
    std::vector<Disaster> next;
    if (!text.empty()) {
        json list = json::parse(text, nullptr, false);
        if (list.is_array())
            for (const json& e : list) {
                if (!e.is_object()) continue;
                Disaster d;
                d.id = e.value("i", 0u);
                const int k = e.value("k", 0);
                if (k < 0 || k >= kKinds) continue;
                d.kind = (Kind)k;
                d.pos = v3(e.value("p", json()));
                d.age = e.value("a", 0.0f);
                d.duration = e.value("T", 0.0f);
                d.strength = e.value("s", 0.0f);
                d.radius = e.value("r", 0.0f);
                d.shake = e.value("sh", 1.0f);
                d.velocity = v3(e.value("v", json()));
                d.height = e.value("h", 0.0f);
                d.dir = unitFlat(v3(e.value("d", json()), glm::vec3(1, 0, 0)));
                d.speed = e.value("sp", 0.0f);
                if (e.contains("tw") && e["tw"].is_array() && e["tw"].size() == 3) {
                    d.spin = e["tw"][0].get<float>(); d.pull = e["tw"][1].get<float>(); d.lift = e["tw"][2].get<float>();
                }
                if (e.contains("rel") && e["rel"].is_array())
                    for (const json& r : e["rel"]) if (r.is_number_unsigned()) d.released.push_back(r.get<uint64_t>());
                if (e.contains("ll")) { d.stopping = true; d.lifeLeft = e.value("ll", 0.0f); }
                d.fade = fadeOf(d);
                // A new bolt: draw it.
                if (d.kind == Kind::Lightning && std::find(st.seenBolts.begin(), st.seenBolts.end(), d.id) == st.seenBolts.end()) {
                    st.seenBolts.push_back(d.id);
                    if (st.seenBolts.size() > 64) st.seenBolts.erase(st.seenBolts.begin());
                    st.addBolt(d.pos, d.strength);
                }
                next.push_back(std::move(d));
            }
    }
    st.list = std::move(next);
}

bool any(const Scene& scene) { return !scene.disasters().list.empty(); }

glm::vec3 wind(const Scene& scene, const glm::vec3& p) {
    glm::vec3 w(0.0f);
    for (const Disaster& d : scene.disasters().list) {
        if (d.fade <= 0.0f) continue;
        if (d.kind == Kind::Tornado) {
            Storms::State s;
            s.tornado = std::max(0.1f, d.strength) * d.fade;
            s.tornadoPos = d.pos;
            w += Storms::tornadoWind(s, p) * (0.5f + 0.5f * d.spin);
        } else if (d.kind == Kind::Tsunami) {
            if (p.y < waterTop(d, p)) w += d.dir * d.speed * 1.2f + glm::vec3(0.0f, 4.0f, 0.0f);
        }
    }
    return w;
}

float airBoost(const Scene& scene, const glm::vec3& p) {
    float boost = 0.0f;
    for (const Disaster& d : scene.disasters().list) {
        if (d.fade <= 0.0f) continue;
        if (d.kind == Kind::Tornado) {
            const float r = glm::length(flat(p - d.pos)), core = tornadoCore(d);
            boost += (25.0f + 15.0f * d.strength) * d.fade * (1.0f - smooth(core * 2.0f, core * 9.0f, r));
        } else if (d.kind == Kind::Tsunami && p.y < waterTop(d, p)) {
            boost += 80.0f;   // water: far heavier than air
        }
    }
    return boost;
}

glm::vec3 characterPush(const Scene& scene, const glm::vec3& p, const glm::vec3& velocity) {
    const Scene& s = scene;
    Player* me = const_cast<Scene&>(s).player();
    const uint64_t mine = me ? me->rootId() : 0;
    glm::vec3 push(0.0f);
    for (const Disaster& d : scene.disasters().list) {
        if (released(d, mine)) continue;
        push += pushOf(scene, d, p, velocity);
    }
    return push;
}

float quakeAt(const Scene& scene, const glm::vec3& p) {
    float level = 0.0f;
    for (const Disaster& d : scene.disasters().list) {
        if (d.kind != Kind::Earthquake) continue;
        float here = quakeLevel(d);
        if (d.radius > 0.0f) here *= 1.0f - smooth(d.radius, d.radius * 1.5f, glm::length(flat(p - d.pos)));
        level = std::max(level, here);
    }
    return level;
}

float cameraShake(const Scene& scene, const glm::vec3& at) {
    float shake = 0.0f;
    for (const Disaster& d : scene.disasters().list) {
        const float dist = glm::length(flat(at - d.pos));
        switch (d.kind) {
            case Kind::Earthquake: {
                float here = quakeLevel(d);
                if (d.radius > 0.0f) here *= 1.0f - smooth(d.radius, d.radius * 1.5f, dist);
                shake += here * 3.2f * d.shake;
                break;
            }
            case Kind::Tornado: {
                const float core = tornadoCore(d);
                shake += 0.25f * std::max(0.5f, d.strength) * d.fade * (1.0f - smooth(core, core * 8.0f, dist)) * d.shake;
                break;
            }
            case Kind::Tsunami: {
                const float front = std::abs(glm::dot(flat(at - d.pos), d.dir));
                shake += 0.6f * d.fade * (1.0f - smooth(0.0f, d.height * 4.0f, front)) * d.shake;
                break;
            }
            case Kind::Lightning:
                if (d.age < 0.4f) shake += 1.4f * (1.0f - smooth(0.0f, 45.0f, glm::length(at - d.pos))) * d.shake;
                break;
        }
    }
    return shake;
}

bool near(const Scene& scene, const glm::vec3& p) {
    for (const Disaster& d : scene.disasters().list) {
        const float dist = glm::length(flat(p - d.pos));
        if (d.kind == Kind::Tornado && dist < tornadoReach(d) * 1.5f) return true;
        if (d.kind == Kind::Tsunami && dist < tsunamiLength(d) + d.radius + d.height) return true;
        if (d.kind == Kind::Earthquake && quakeAt(scene, p) > 0.2f) return true;
    }
    return false;
}

} // namespace Disasters
