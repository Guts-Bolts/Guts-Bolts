#include "Npc.h"
#include "Scene.h"
#include "Physics.h"
#include "../core/Audio.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace {

const char* kLimbs[] = {"Left Arm", "Right Arm", "Left Leg", "Right Leg"};

float approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * std::min(1.0f, rate * dt);
}

std::mt19937& rng() { static std::mt19937 r{777u}; return r; }
float rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }

bool hasTag(const SceneNode* n, const char* tag) {
    return std::find(n->tags.begin(), n->tags.end(), tag) != n->tags.end();
}

// Is this point where the feet are? (The bottom of the body parts, under the root part.)
glm::vec3 feetOf(SceneNode* model) {
    SceneNode* hrp = model->findChild("HumanoidRootPart");
    glm::vec3 feet = glm::vec3(hrp->worldMatrix()[3]);
    float bottom = 1e9f;
    for (auto& c : model->children)
        if (c->isPart()) bottom = std::min(bottom, Physics::worldBounds(c.get()).min.y);
    if (bottom < 1e8f) feet.y = bottom;
    return feet;
}

} // namespace

bool NpcSystem::isRig(Scene& scene, const SceneNode* m) {
    if (!m || m->kind != NodeKind::Model || m->internal || !m->parent) return false;
    if (scene.isCharacterRoot(m->id) || scene.isCharacterPart(m)) return false;
    const SceneNode* hrp = m->findChild("HumanoidRootPart");
    const SceneNode* torso = m->findChild("Torso");
    const SceneNode* head = m->findChild("Head");
    if (!hrp || !torso || !head || !hrp->isPart() || !torso->isPart() || !head->isPart()) return false;
    // Hidden = kept in storage (ServerStorage, ReplicatedStorage) as a template.
    for (const SceneNode* n = m; n; n = n->parent) if (!n->visible) return false;
    // A character inside another character (or a tool) isn't walking around.
    // (nor is one shown in a ViewportFrame)
    for (const SceneNode* n = m->parent; n; n = n->parent)
        if (n->isTool() || n->isPart() || n->isViewportFrame()) return false;
    return true;
}

void NpcSystem::begin(Scene& scene) {
    end();
    m_active = true;
    std::vector<SceneNode*> rigs;
    scene.forEach([&](SceneNode* n) { if (isRig(scene, n)) rigs.push_back(n); });
    for (SceneNode* r : rigs) adopt(scene, r);
}

void NpcSystem::end() {
    m_npcs.clear();
    died.clear();
    finished.clear();
    m_active = false;
}

Npc* NpcSystem::find(uint64_t rootId) {
    for (auto& n : m_npcs) if (n->rootId == rootId) return n.get();
    return nullptr;
}

Npc* NpcSystem::find(Scene& scene, uint64_t rootId, bool adoptIt) {
    if (Npc* n = find(rootId)) return n;
    if (!adoptIt || !m_active) return nullptr;
    SceneNode* m = scene.findById(rootId);
    return isRig(scene, m) ? adopt(scene, m) : nullptr;
}

Npc* NpcSystem::adopt(Scene& scene, SceneNode* model) {
    // Put the model's origin at its feet (Roblox rigs have it anywhere), so it
    // turns around its middle and stands where the physics expects.
    glm::vec3 feet = feetOf(model);
    glm::vec3 local = glm::vec3(glm::inverse(model->worldMatrix()) * glm::vec4(feet, 1.0f));
    for (auto& c : model->children) c->transform.position -= local;
    glm::mat4 parentW = model->parent ? model->parent->worldMatrix() : glm::mat4(1.0f);
    model->transform.position = glm::vec3(glm::inverse(parentW) * glm::vec4(feet, 1.0f));

    auto npc = std::make_unique<Npc>();
    npc->rootId = model->id;
    npc->zombie = hasTag(model, "Zombie") || model->name.find("Zombie") != std::string::npos;
    // Its body parts are carried by the Humanoid: held in place, and not solid
    // (so the player and other NPCs don't get stuck on them, and it doesn't trip on itself).
    for (auto& c : model->children) {
        if (!c->isPart()) continue;
        c->anchored = true;
        c->canCollide = false;
        npc->rest[c->id] = c->transform;
    }
    // Settings a builder can give the model as attributes.
    if (const Attribute* a = model->findAttribute("WalkSpeed"); a && a->type == Attribute::Number) npc->humanoid.walkSpeed = (float)a->n;
    if (const Attribute* a = model->findAttribute("MaxHealth"); a && a->type == Attribute::Number)
        npc->humanoid.maxHealth = npc->humanoid.health = std::max(1.0f, (float)a->n);
    npc->lastHealth = npc->humanoid.health;
    scene.markDirty();
    m_npcs.push_back(std::move(npc));
    return m_npcs.back().get();
}

void NpcSystem::kill(uint64_t rootId, float force, const glm::vec3& impulse) {
    if (Npc* n = find(rootId)) {
        n->humanoid.health = 0.0f;
        n->pendingForce = std::max(n->pendingForce, force);
        n->pendingImpulse += impulse;
    }
}

void NpcSystem::update(float dt, Scene& scene, Physics& physics) {
    if (!m_active) return;
    // Now and then, look for characters scripts have put in the world.
    if ((m_scanTime -= dt) <= 0.0f) {
        m_scanTime = 0.5f;
        std::vector<SceneNode*> rigs;
        scene.forEach([&](SceneNode* n) { if (isRig(scene, n) && !find(n->id)) rigs.push_back(n); });
        for (SceneNode* r : rigs) adopt(scene, r);
    }
    // Forget ones that were destroyed.
    m_npcs.erase(std::remove_if(m_npcs.begin(), m_npcs.end(),
                                [&](const std::unique_ptr<Npc>& n) { return !scene.findById(n->rootId); }),
                 m_npcs.end());

    for (auto& n : m_npcs) {
        SceneNode* root = scene.findById(n->rootId);
        if (n->dead) { updateDeath(*n, root, dt, scene, physics); continue; }
        step(*n, root, dt, scene, physics);
    }

    // Don't let NPCs stand inside each other: nudge apart ones that are too close.
    for (size_t i = 0; i < m_npcs.size(); ++i)
        for (size_t j = i + 1; j < m_npcs.size(); ++j) {
            Npc& a = *m_npcs[i]; Npc& b = *m_npcs[j];
            if (a.dead || b.dead) continue;
            SceneNode* ra = scene.findById(a.rootId); SceneNode* rb = scene.findById(b.rootId);
            glm::vec3 d = ra->transform.position - rb->transform.position;
            if (std::abs(d.y) > 2.0f) continue;
            d.y = 0.0f;
            float len = glm::length(d);
            if (len >= 1.0f) continue;
            glm::vec3 push = (len > 1e-3f ? d / len : glm::vec3(1, 0, 0)) * (1.0f - len) * 0.5f;
            if (!physics.solidAt(ra->transform.position + push + glm::vec3(0, 1.3f, 0))) ra->transform.position += push;
            if (!physics.solidAt(rb->transform.position - push + glm::vec3(0, 1.3f, 0))) rb->transform.position -= push;
        }
}

void NpcSystem::step(Npc& n, SceneNode* r, float dt, Scene& scene, Physics& physics) {
    Humanoid& h = n.humanoid;
    // Hurt since last frame: a grunt, and gore if the game has it on.
    if (h.health < n.lastHealth - 0.5f) {
        glm::vec3 at = r->transform.position + glm::vec3(0, 1.5f, 0);
        if (h.health > 0.0f) Audio::play("hit", 0.5f, 0.9f, false, &at);
        if (scene.goreEnabled())
            scene.particles().spray(scene.goreKind(), at, glm::vec3(0, 1, 0),
                                    (int)std::clamp((n.lastHealth - h.health) * 0.3f, 3.0f, 20.0f), 3.0f);
    }
    n.lastHealth = h.health;
    if (h.health <= 0.0f) { startDeath(n, r, scene); return; }

    glm::vec3 pos = r->transform.position;

    // Where to go: a PathfindTo route, a MoveTo point, or a Move direction.
    glm::vec3 dir = n.moveDir;
    if (n.route.state == Npc::Route::Walking) {
        dir = followRoute(n, pos, dt, scene, physics);
    } else if (n.hasTarget) {
        glm::vec3 d = n.target - pos;
        d.y = 0.0f;
        float len = glm::length(d);
        n.targetTime += dt;
        if (len < 0.5f) {
            n.hasTarget = false;
            finished.push_back({n.rootId, true});
            dir = glm::vec3(0.0f);
        } else if (n.targetTime > 8.0f) {
            n.hasTarget = false;
            finished.push_back({n.rootId, false});
            dir = glm::vec3(0.0f);
        } else {
            // Slow down right at the end so it doesn't overshoot.
            dir = d / len * std::min(1.0f, len / std::max(0.1f, h.walkSpeed * dt) );
        }
    }
    dir.y = 0.0f;
    float len = glm::length(dir);
    bool moving = len > 1e-4f;
    glm::vec3 horiz(0.0f);
    if (moving) {
        horiz = dir / len;
        if (h.autoRotate) {
            float target = glm::degrees(std::atan2(horiz.x, horiz.z));
            float cur = r->transform.rotation.y;
            float diff = std::fmod(target - cur + 540.0f, 360.0f) - 180.0f;
            r->transform.rotation.y = cur + diff * std::min(1.0f, dt * 10.0f);
        }
        horiz *= std::min(1.0f, len);
    }

    // Jumping: asked to (Humanoid.Jump), or stuck against something while walking.
    if (n.grounded && (n.jump || n.stuckTime > 0.25f)) {
        n.velocity.y = h.launchSpeed(scene.world().gravity);
        n.grounded = false;
        n.stuckTime = 0.0f;
    }
    n.jump = false;
    n.velocity.y -= scene.world().gravity * dt;

    glm::vec3 delta = horiz * h.walkSpeed * dt;
    delta.x += n.velocity.x * dt;
    delta.z += n.velocity.z * dt;
    delta.y = n.velocity.y * dt;
    float drag = std::max(0.0f, 1.0f - (n.grounded ? 8.0f : 0.8f) * dt);
    n.velocity.x *= drag;
    n.velocity.z *= drag;

    Physics::MoveResult res = physics.moveCharacter(pos, delta, n.grounded, r->transform.rotation.y);
    glm::vec2 wanted(delta.x, delta.z), got(res.position.x - pos.x, res.position.z - pos.z);
    if (moving && n.grounded && glm::length(wanted) > 1e-4f && glm::length(got) < glm::length(wanted) * 0.3f)
        n.stuckTime += dt;
    else
        n.stuckTime = 0.0f;
    n.grounded = res.grounded;
    if (res.grounded && n.velocity.y < 0.0f) n.velocity.y = 0.0f;
    if (res.hitCeiling && n.velocity.y > 0.0f) n.velocity.y = 0.0f;
    r->transform.position = res.position;
    if (res.position.y < scene.world().fallenPartsHeight) h.health = 0.0f;

    const float moved = dt > 0.0f ? glm::length(got) / dt : 0.0f;
    n.groundSpeed = approach(n.groundSpeed, moving ? std::min(moved, h.walkSpeed * 2.0f + 2.0f) : 0.0f, 12.0f, dt);
    animate(n, r, dt, n.groundSpeed);
}

void NpcSystem::animate(Npc& n, SceneNode* r, float dt, float groundSpeed) {
    n.swing = approach(n.swing, n.grounded ? Player::strideSwing(groundSpeed) : 0.0f, 10.0f, dt);
    n.air = approach(n.air, n.grounded ? 0.0f : 1.0f, 10.0f, dt);
    n.walkPhase += dt * Player::strideRate(groundSpeed);
    const float s = std::sin(n.walkPhase) * n.swing;
    float angles[4] = {
        s * (1 - n.air) + (-165.0f) * n.air,
        -s * (1 - n.air) + (-165.0f) * n.air,
        -s * (1 - n.air) + 12.0f * n.air,
        s * (1 - n.air) - 12.0f * n.air,
    };
    if (n.zombie) {   // arms straight out in front, swaying a little
        angles[0] = -90.0f + s * 0.15f;
        angles[1] = -90.0f - s * 0.15f;
    }
    for (int i = 0; i < 4; ++i) {
        SceneNode* limb = r->findChild(kLimbs[i]);
        if (!limb) continue;
        auto it = n.rest.find(limb->id);
        if (it == n.rest.end()) continue;
        const Transform& rest = it->second;
        // Swing around the shoulder / hip (the top of the limb).
        float a = glm::radians(angles[i]);
        float hh = rest.scale.y * 0.5f;
        glm::vec3 pivot = rest.position + glm::vec3(0.0f, hh, 0.0f);
        limb->transform.position = pivot + glm::vec3(0.0f, -hh * std::cos(a), -hh * std::sin(a));
        limb->transform.rotation = rest.rotation + glm::vec3(angles[i], 0.0f, 0.0f);
    }
}

void NpcSystem::startDeath(Npc& n, SceneNode* r, Scene& scene) {
    n.dead = true;
    n.deadTime = 0.0f;
    n.hasTarget = false;
    died.push_back(n.rootId);
    glm::vec3 at = r->transform.position + glm::vec3(0, 2, 0);
    Audio::play("oof", 0.6f, n.zombie ? 0.8f : 1.0f, false, &at);

    if (scene.world().deathStyle == DeathStyle::Ragdoll) {
        n.ragdoll.start(scene, r, n.velocity, n.pendingImpulse, n.pendingForce);
        return;
    }
    // Classic: the pieces fall apart (moved into world space so each can tumble).
    glm::mat4 rootM = r->transform.matrix();
    float yaw = r->transform.rotation.y;
    glm::vec3 center = r->transform.position + glm::vec3(0, 1.3f, 0);
    for (auto& c : r->children) {
        if (!c->isPart()) continue;
        c->transform.position = glm::vec3(rootM * glm::vec4(c->transform.position, 1.0f));
        c->transform.rotation.y += yaw;
        glm::vec3 out = c->transform.position - center;
        out.y = 0.0f;
        if (glm::length(out) > 1e-3f) out = glm::normalize(out);
        glm::vec3 vel = out * (1.5f + rand01() * 2.0f) + glm::vec3(rand01() - 0.5f, 3.0f + rand01() * 2.5f, rand01() - 0.5f);
        n.debris.push_back({c->id, vel + n.pendingImpulse, (glm::vec3(rand01(), rand01(), rand01()) - 0.5f) * 500.0f});
    }
    r->transform.position = glm::vec3(0.0f);
    r->transform.rotation = glm::vec3(0.0f);
    r->transform.scale = glm::vec3(1.0f);
}

void NpcSystem::updateDeath(Npc& n, SceneNode* r, float dt, Scene& scene, Physics& physics) {
    (void)r;
    n.deadTime += dt;
    if (n.ragdoll.active()) {
        // After a while the body stops moving (and stops costing anything).
        if (n.deadTime < 8.0f) n.ragdoll.update(dt, scene, physics);
        return;
    }
    if (n.deadTime > 6.0f) return;
    const float g = scene.world().gravity;
    for (auto& d : n.debris) {
        SceneNode* p = scene.findById(d.id);
        if (!p) continue;
        d.vel.y -= g * dt;
        p->transform.position += d.vel * dt;
        p->transform.rotation += d.spin * dt;
        float push = physics.pushUp(Physics::worldBounds(p));
        if (push > 0.0f) {
            p->transform.position.y += push;
            if (d.vel.y < 0.0f) d.vel.y = -d.vel.y * 0.25f;
            d.vel.x *= 0.8f; d.vel.z *= 0.8f;
            d.spin *= 0.8f;
        }
    }
}

// ===========================================================================
// PathfindTo
// ===========================================================================

void NpcSystem::startRoute(Npc& n, const glm::vec3& goal, uint64_t goalNode, const NavMesh::Agent& agent) {
    Npc::Route& r = n.route;
    r = Npc::Route{};
    r.state = Npc::Route::Walking;
    r.goal = goal;
    r.goalNode = goalNode;
    r.agent = agent;
    n.hasTarget = false;
    n.moveDir = glm::vec3(0.0f);
}

glm::vec3 NpcSystem::followRoute(Npc& n, const glm::vec3& feet, float dt, Scene& scene, Physics& physics) {
    Npc::Route& r = n.route;
    // A goal that moves: where it is now (a character's feet, or on top of a part).
    bool besidePart = false;
    if (r.goalNode) {
        SceneNode* g = scene.findById(r.goalNode);
        if (!g) { r.state = Npc::Route::Failed; return glm::vec3(0.0f); }
        if (g->isPart()) {
            AABB b = Physics::worldBounds(g);
            r.goal = glm::vec3((b.min.x + b.max.x) * 0.5f, b.max.y, (b.min.z + b.max.z) * 0.5f);
            // Touching distance of it counts as there (a tall part you can't stand on, say).
            glm::vec2 nearest(std::clamp(feet.x, b.min.x, b.max.x), std::clamp(feet.z, b.min.z, b.max.z));
            besidePart = glm::length(nearest - glm::vec2(feet.x, feet.z)) < std::max(0.5f, r.agent.radius) + 0.35f &&
                         feet.y > b.min.y - 1.0f && feet.y < b.max.y + 1.0f;
        } else {
            r.goal = glm::vec3(g->worldMatrix()[3]);
        }
    }
    const float reach = std::max(0.5f, r.agent.radius);
    glm::vec2 toGoal(r.goal.x - feet.x, r.goal.z - feet.z);
    // (Beside it only counts when there's no way up onto it.)
    if ((besidePart && (r.closest || r.tries > 0)) || (glm::length(toGoal) < reach && std::abs(r.goal.y - feet.y) < 3.0f && n.grounded)) {
        r.state = Npc::Route::Arrived;   // (call PathfindTo again to keep following something)
        return glm::vec3(0.0f);
    }

    const NavMesh& nav = physics.navMesh();
    r.replan -= dt;
    const bool goalMoved = glm::length(r.goal - r.plannedFor) > std::max(1.0f, 0.15f * glm::length(toGoal));
    if (r.stuck > 1.2f) n.jump = true;   // stuck: hop, and think again
    // Work out a (new) route: none yet (a moment after the last try), the world
    // changed, stuck, or the goal moved away from where the route ends.
    const bool plan = (r.waypoints.empty() && r.replan <= 0.0f) || r.navVersion != nav.version() ||
                      r.stuck > 1.2f || (r.replan <= 0.0f && goalMoved);
    if (plan) {
        NavMesh::Status st = nav.findPath(feet, r.goal, r.agent, r.waypoints);
        r.navVersion = nav.version();
        r.plannedFor = r.goal;
        r.replan = 0.35f;
        r.stuck = 0.0f;
        r.next = 1;
        r.closest = st == NavMesh::Status::ClosestNoPath;
        if (st == NavMesh::Status::Success || r.closest) {
            r.tries = 0;
        } else {
            r.waypoints.clear();
            // No way there (yet). A moving goal may come back in reach; a fixed one gets a few goes.
            r.replan = 1.0f;
            if (!r.goalNode && ++r.tries >= 4) { r.state = Npc::Route::Failed; return glm::vec3(0.0f); }
        }
    }
    if (r.waypoints.empty()) {
        // Meanwhile head straight for it if it's close (like plain MoveTo).
        float gl = glm::length(toGoal);
        return gl < 12.0f ? glm::vec3(toGoal.x, 0.0f, toGoal.y) / gl : glm::vec3(0.0f);
    }

    // Next waypoint reached? (Close enough sideways, and about the same height.)
    while (r.next < r.waypoints.size()) {
        const NavMesh::Waypoint& w = r.waypoints[r.next];
        glm::vec2 d(w.pos.x - feet.x, w.pos.z - feet.z);
        const bool last = r.next + 1 == r.waypoints.size();
        if (glm::length(d) > (last ? 0.35f : 0.55f) || std::abs(w.pos.y - feet.y) > 2.0f) break;
        ++r.next;
        if (r.next < r.waypoints.size() && r.waypoints[r.next].action == NavMesh::Action::Jump) {
            n.jump = true;            // take off here
            r.toJump = 0.0f;
        }
    }
    if (r.next >= r.waypoints.size()) {
        if (r.closest || r.goalNode) { r.waypoints.clear(); r.replan = 0.5f; if (!r.goalNode && r.closest) r.state = Npc::Route::Failed; return glm::vec3(0.0f); }
        r.state = Npc::Route::Arrived;
        return glm::vec3(0.0f);
    }
    const NavMesh::Waypoint& w = r.waypoints[r.next];
    glm::vec2 d(w.pos.x - feet.x, w.pos.z - feet.z);
    float len = glm::length(d);
    // Heading for a jump and still on the ground a moment later: jump again.
    if (w.action == NavMesh::Action::Jump && n.grounded) {
        r.toJump += dt;
        if (r.toJump > 0.35f && len < 2.5f) { n.jump = true; r.toJump = 0.0f; }
    }
    // Not getting anywhere?
    if (n.stuckTime > 0.0f || (n.grounded && n.groundSpeed < 0.15f * n.humanoid.walkSpeed)) r.stuck += dt;
    else r.stuck = std::max(0.0f, r.stuck - dt);
    if (len < 1e-4f) return glm::vec3(0.0f);
    float slow = r.next + 1 == r.waypoints.size() ? std::min(1.0f, len / std::max(0.1f, n.humanoid.walkSpeed * dt)) : 1.0f;
    return glm::vec3(d.x, 0.0f, d.y) / len * slow;
}
