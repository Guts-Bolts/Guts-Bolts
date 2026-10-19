#include "Physics.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Player.h"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <cmath>

namespace {

void localBounds(PrimitiveType type, glm::vec3& bmin, glm::vec3& bmax) {
    if (type == PrimitiveType::Plane) {
        bmin = {-0.5f, -0.02f, -0.5f};
        bmax = { 0.5f,  0.02f,  0.5f};
    } else {
        bmin = {-0.5f, -0.5f, -0.5f};
        bmax = { 0.5f,  0.5f,  0.5f};
    }
}

// Slab-method ray/AABB. Returns the nearest non-negative hit distance.
bool rayAABB(const glm::vec3& ro, const glm::vec3& rd,
             const glm::vec3& bmin, const glm::vec3& bmax, float& tHit) {
    float t0 = -1e30f, t1 = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(rd[i]) < 1e-8f) {
            if (ro[i] < bmin[i] || ro[i] > bmax[i]) return false;
        } else {
            float inv = 1.0f / rd[i];
            float ta = (bmin[i] - ro[i]) * inv;
            float tb = (bmax[i] - ro[i]) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
    }
    tHit = (t0 >= 0.0f) ? t0 : t1;
    return tHit >= 0.0f;
}

bool hasDynamicAncestor(const SceneNode* n) {
    for (const SceneNode* p = n->parent; p; p = p->parent)
        if (p->isPart() && !p->anchored) return true;
    return false;
}

} // namespace

AABB Physics::worldBounds(const SceneNode* node) {
    glm::vec3 bmin, bmax;
    localBounds(node->primitiveType, bmin, bmax);
    glm::mat4 m = node->worldMatrix();
    AABB box{glm::vec3(1e30f), glm::vec3(-1e30f)};
    for (int i = 0; i < 8; ++i) {
        glm::vec3 c{(i & 1) ? bmax.x : bmin.x, (i & 2) ? bmax.y : bmin.y, (i & 4) ? bmax.z : bmin.z};
        glm::vec3 w = glm::vec3(m * glm::vec4(c, 1.0f));
        box.min = glm::min(box.min, w);
        box.max = glm::max(box.max, w);
    }
    return box;
}

SceneNode* Physics::raycast(Scene& scene, const glm::vec3& ro, const glm::vec3& rd,
                            float* distance, const SceneNode* ignore) {
    return raycastIf(scene, ro, rd, distance, [ignore](const SceneNode* n) { return n != ignore; });
}

SceneNode* Physics::raycastIf(Scene& scene, const glm::vec3& ro, const glm::vec3& rd, float* distance,
                              const std::function<bool(const SceneNode*)>& counts) {
    SceneNode* best = nullptr;
    float bestDist = 1e30f;
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* node = stack.back();
        stack.pop_back();
        if (!node->visible || !counts(node)) continue;
        for (auto& c : node->children) stack.push_back(c.get());
        if (!node->isPart() || node->internal) continue;

        glm::mat4 world = node->worldMatrix();
        glm::mat4 inv   = glm::inverse(world);
        glm::vec3 lro   = glm::vec3(inv * glm::vec4(ro, 1.0f));
        glm::vec3 lrd   = glm::vec3(inv * glm::vec4(rd, 0.0f));
        glm::vec3 bmin, bmax;
        localBounds(node->primitiveType, bmin, bmax);

        float tLocal;
        if (rayAABB(lro, lrd, bmin, bmax, tLocal)) {
            // Compare in world space so objects of different scales are fair.
            glm::vec3 hit = glm::vec3(world * glm::vec4(lro + lrd * tLocal, 1.0f));
            float d = glm::dot(hit - ro, rd);
            if (d > 0.0f && d < bestDist) { bestDist = d; best = node; }
        }
    }
    // The terrain (hills sculpted in Studio).
    const Terrain& terrain = scene.terrain();
    if (!terrain.empty() && counts(scene.terrainNode())) {
        const float len = glm::length(rd);
        float t;
        if (len > 1e-9f && terrain.raycast(ro, rd / len, std::min(bestDist * len, 1e5f), t)) {
            float d = glm::dot(rd / len * t, rd);
            if (d > 0.0f && d < bestDist) { bestDist = d; best = scene.terrainNode(); }
        }
    }
    if (distance) *distance = bestDist;
    return best;
}

namespace {
bool isRotated(const glm::mat4& m) {
    for (int i = 0; i < 3; ++i) {
        glm::vec3 c = glm::vec3(m[i]);
        float len = glm::length(c);
        if (len < 1e-6f) continue;
        c /= len;
        float mx = std::max(std::abs(c.x), std::max(std::abs(c.y), std::abs(c.z)));
        if (mx < 0.9995f) return true;
    }
    return false;
}
} // namespace

OBB Physics::worldOBB(const SceneNode* node) {
    glm::vec3 bmin, bmax;
    localBounds(node->primitiveType, bmin, bmax);
    glm::mat4 m = node->worldMatrix();
    OBB o;
    o.center = glm::vec3(m * glm::vec4((bmin + bmax) * 0.5f, 1.0f));
    for (int i = 0; i < 3; ++i) {
        glm::vec3 col = glm::vec3(m[i]);
        float len = glm::length(col);
        o.axis[i] = len > 1e-8f ? col / len : glm::vec3(i == 0, i == 1, i == 2);
        o.half[i] = len * (bmax[i] - bmin[i]) * 0.5f;
    }
    return o;
}

bool Physics::obbOverlap(const OBB& a, const OBB& b, glm::vec3& normal, float& depth) {
    glm::vec3 axes[15];
    int n = 0;
    for (int i = 0; i < 3; ++i) axes[n++] = a.axis[i];
    for (int i = 0; i < 3; ++i) axes[n++] = b.axis[i];
    int faceAxes = n;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            glm::vec3 c = glm::cross(a.axis[i], b.axis[j]);
            float l = glm::length(c);
            if (l > 1e-3f) axes[n++] = c / l;
        }
    glm::vec3 d = b.center - a.center;
    depth = 1e30f;
    for (int k = 0; k < n; ++k) {
        const glm::vec3& L = axes[k];
        float ra = 0.0f, rb = 0.0f;
        for (int i = 0; i < 3; ++i) {
            ra += a.half[i] * std::abs(glm::dot(a.axis[i], L));
            rb += b.half[i] * std::abs(glm::dot(b.axis[i], L));
        }
        float proj = glm::dot(d, L);
        float overlap = ra + rb - std::abs(proj);
        if (overlap <= 0.0f) return false;
        // Prefer face directions a little: they give steadier pushes than edges.
        float score = k < faceAxes ? overlap : overlap * 1.05f;
        if (score < depth) {
            depth = overlap;
            normal = proj > 0.0f ? -L : L;
        }
    }
    return true;
}

OBB Physics::charOBB(const glm::vec3& f, float yaw) {
    OBB o;
    o.center = f + glm::vec3(0.0f, kCharHeight * 0.5f, 0.0f);
    o.half = {kCharHalfWidth, kCharHeight * 0.5f, kCharHalfDepth};
    const float a = glm::radians(yaw);
    o.axis[0] = {std::cos(a), 0.0f, -std::sin(a)};   // the shoulders' direction
    o.axis[2] = {std::sin(a), 0.0f, std::cos(a)};    // facing
    return o;
}

std::vector<Physics::Shove> Physics::shoves(const glm::vec3& feet, float yaw, float charMass, int group) const {
    std::vector<Shove> out;
    OBB me = charOBB(feet, yaw);
    me.half += glm::vec3(0.06f);
    AABB mine = bounds(me);
    for (const auto& c : m_colliders) {
        if (!c.dynamic || !c.solid || !m_groups.collides(group, c.group) || !mine.overlaps(c.box)) continue;
        glm::vec3 n; float d;
        if (!obbOverlap(me, c.rotated ? c.obb : OBB::fromAABB(c.box), n, d)) continue;
        float approach = glm::dot(c.node->velocity, n);   // how fast it's coming at us
        if (approach < 1.0f) continue;
        glm::vec3 size = c.box.max - c.box.min;
        float density = densityOf(c.node);
        float mass = density * size.x * size.y * size.z;
        out.push_back({n, approach * mass / (mass + charMass)});
    }
    return out;
}

float Physics::densityOf(const SceneNode* n) {
    if (n->density >= 0.0f) return std::max(0.01f, n->density);
    return materialPhysics(n->material).density;
}
float Physics::frictionOf(const SceneNode* n) { return n->friction >= 0.0f ? n->friction : materialPhysics(n->material).friction; }
float Physics::elasticityOf(const SceneNode* n) { return n->elasticity >= 0.0f ? n->elasticity : materialPhysics(n->material).elasticity; }

AABB Physics::bounds(const OBB& o) {
    glm::vec3 e(0.0f);
    for (int i = 0; i < 3; ++i) e += glm::abs(o.axis[i]) * o.half[i];
    return {o.center - e, o.center + e};
}

AABB Physics::characterBox(const glm::vec3& f) {
    return {{f.x - kCharHalfWidth, f.y, f.z - kCharHalfWidth},
            {f.x + kCharHalfWidth, f.y + kCharHeight, f.z + kCharHalfWidth}};
}

void Physics::reset() {
    m_colliders.clear();
    m_terrain = nullptr;
    m_bodies.clear();
    m_touching.clear();
}

const std::string& Physics::characterGroup(const SceneNode* root) {
    static const std::string kDefault = CollisionGroups::kDefault;
    if (!root) return kDefault;
    for (const char* name : {"HumanoidRootPart", "Torso", "UpperTorso"})
        if (const SceneNode* p = root->findChild(name); p && p->isPart()) return p->collisionGroup;
    return root->isPart() ? root->collisionGroup : kDefault;
}

void Physics::gather(Scene& scene) {
    m_colliders.clear();
    m_terrain = scene.terrain().empty() ? nullptr : &scene.terrain();
    if (m_groups != scene.world().collisionGroups) m_groups = scene.world().collisionGroups;
    const bool groups = !m_groups.onlyDefault();
    uint64_t hash = 1469598103934665603ull;
    // Manual walk so whole subtrees (hidden models, the character) can be skipped.
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (!n->visible) continue;
        if (scene.isCharacterPart(n)) continue;
        if (n->isPart() && !n->internal)
        {
            glm::mat4 m = n->worldMatrix();
            bool rot = isRotated(m);
            m_colliders.push_back({n, worldBounds(n), n->canCollide,
                                   !n->anchored && !hasDynamicAncestor(n), rot,
                                   rot ? worldOBB(n) : OBB{}, groups ? m_groups.slotOf(n->collisionGroup) : 0});
            const Collider& c = m_colliders.back();
            if (c.solid && !c.dynamic) {   // the navmesh only cares about these
                auto mix = [&](float f) { uint32_t u; std::memcpy(&u, &f, 4); hash = (hash ^ u) * 1099511628211ull; };
                mix(c.box.min.x); mix(c.box.min.y); mix(c.box.min.z); mix(c.box.max.x); mix(c.box.max.y); mix(c.box.max.z);
                hash = (hash ^ n->id) * 1099511628211ull;
            }
        }
        for (auto& c : n->children) stack.push_back(c.get());
    }
    if (m_terrain) hash = (hash ^ m_terrain->version()) * 1099511628211ull;   // (reshaping the terrain rebakes it too)
    m_staticHash = hash;

    // Characters as solid bodies, so players (and NPCs) bump into each other
    // instead of walking through. Only the character movement uses these.
    m_bodies.clear();
    if (!scene.world().playerCollisions) return;
    auto addBody = [&](SceneNode* root) {
        if (!root || !root->visible) return;
        OBB o = charOBB(root->transform.position, root->transform.rotation.y);
        m_bodies.push_back({root, bounds(o), true, false, true, o, groups ? groupOf(root) : 0});
    };
    if (Player* p = scene.player(); p && !p->isDead()) addBody(p->root());
    for (const RemoteCharacter& rc : scene.remotes())
        if (rc.alive) addBody(scene.findById(rc.rootId));
}

bool Physics::blocked(const AABB& box) const {
    float ground;
    if (m_terrain && m_terrain->highestUnder(box.min.x, box.min.z, box.max.x, box.max.z, ground) && ground > box.min.y + 0.01f)
        return true;
    OBB ob = OBB::fromAABB(box);
    for (const auto& c : m_colliders) {
        if (!c.solid || !box.overlaps(c.box)) continue;
        if (!c.rotated) return true;
        glm::vec3 n; float d;
        if (obbOverlap(ob, c.obb, n, d)) return true;
    }
    return false;
}

float Physics::pushUp(const AABB& box) const {
    float push = 0.0f;
    float h = box.max.y - box.min.y;
    float ground;
    if (m_terrain && m_terrain->highestUnder(box.min.x, box.min.z, box.max.x, box.max.z, ground)) {
        float d = ground - box.min.y;
        if (d > 0.0f && d <= h + 0.05f) push = std::max(push, d);
    }
    OBB ob = OBB::fromAABB(box);
    for (const auto& c : m_colliders) {
        if (!c.solid || !box.overlaps(c.box)) continue;
        if (c.rotated) {
            glm::vec3 n; float d;
            if (obbOverlap(ob, c.obb, n, d) && n.y > 0.4f) push = std::max(push, std::min(d / n.y, h + 0.05f));
            continue;
        }
        float d = c.box.max.y - box.min.y;
        if (d > 0.0f && d <= h + 0.05f) push = std::max(push, d);
    }
    return push;
}

bool Physics::solidAt(const glm::vec3& p) const {
    if (m_terrain && m_terrain->solidAt(p)) return true;
    for (const auto& c : m_colliders) {
        if (!(c.solid && p.x > c.box.min.x && p.x < c.box.max.x && p.y > c.box.min.y &&
              p.y < c.box.max.y && p.z > c.box.min.z && p.z < c.box.max.z))
            continue;
        if (!c.rotated) return true;
        glm::vec3 d = p - c.obb.center;
        bool inside = true;
        for (int i = 0; i < 3; ++i)
            if (std::abs(glm::dot(d, c.obb.axis[i])) > c.obb.half[i]) inside = false;
        if (inside) return true;
    }
    return false;
}

bool Physics::resolveSphere(glm::vec3& c, float r, glm::vec3* normal) const {
    bool hit = false;
    float ground;
    if (m_terrain && m_terrain->heightAt(c.x, c.z, ground)) {
        // Close enough to a flat-ish plane under the ball.
        const glm::vec3 n = m_terrain->normalAt(c.x, c.z);
        const float dist = (c.y - ground) * n.y;
        if (dist < r) {
            c += n * (r - dist);
            if (normal) *normal = n;
            hit = true;
        }
    }
    for (const auto& col : m_colliders) {
        if (!col.solid) continue;
        if (col.rotated) {
            // Work in the box's own coordinates.
            const OBB& o = col.obb;
            glm::vec3 rel = c - o.center;
            glm::vec3 local(glm::dot(rel, o.axis[0]), glm::dot(rel, o.axis[1]), glm::dot(rel, o.axis[2]));
            glm::vec3 cl = glm::clamp(local, -o.half, o.half);
            glm::vec3 dl = local - cl;
            float dist2 = glm::dot(dl, dl);
            if (dist2 > r * r) continue;
            glm::vec3 nl;
            if (dist2 > 1e-10f) {
                float dist = std::sqrt(dist2);
                nl = dl / dist;
                local = cl + nl * r;
            } else {
                glm::vec3 room = o.half - glm::abs(local);
                int ax = room.x < room.y ? (room.x < room.z ? 0 : 2) : (room.y < room.z ? 1 : 2);
                nl = glm::vec3(0.0f);
                nl[ax] = local[ax] >= 0.0f ? 1.0f : -1.0f;
                local[ax] = nl[ax] * (o.half[ax] + r);
            }
            glm::vec3 n = o.axis[0] * nl.x + o.axis[1] * nl.y + o.axis[2] * nl.z;
            c = o.center + o.axis[0] * local.x + o.axis[1] * local.y + o.axis[2] * local.z;
            if (normal) *normal = n;
            hit = true;
            continue;
        }
        glm::vec3 closest = glm::clamp(c, col.box.min, col.box.max);
        glm::vec3 d = c - closest;
        float dist2 = glm::dot(d, d);
        if (dist2 > r * r) continue;
        glm::vec3 n;
        if (dist2 > 1e-10f) {
            float dist = std::sqrt(dist2);
            n = d / dist;
            c += n * (r - dist);
        } else {
            // Centre is inside the box: leave through the nearest face.
            glm::vec3 toMin = c - col.box.min, toMax = col.box.max - c;
            float best = toMax.y; n = {0, 1, 0};
            if (toMin.y < best) { best = toMin.y; n = {0, -1, 0}; }
            if (toMax.x < best) { best = toMax.x; n = {1, 0, 0}; }
            if (toMin.x < best) { best = toMin.x; n = {-1, 0, 0}; }
            if (toMax.z < best) { best = toMax.z; n = {0, 0, 1}; }
            if (toMin.z < best) { best = toMin.z; n = {0, 0, -1}; }
            c += n * (best + r);
        }
        if (normal) *normal = n;
        hit = true;
    }
    return hit;
}

// The character is a box shaped like a Roblox R6 body (as wide as the torso,
// half as deep, feet to the top of the head) that turns with the character.
// Every solid part is tested against it with the separating-axis test, so it
// fits through gaps sideways, brushes past corners and stands on ramps.
Physics::MoveResult Physics::moveCharacter(const glm::vec3& feet, const glm::vec3& delta,
                                           bool wasGrounded, float yaw, uint64_t self, int group) const {
    MoveResult r;
    glm::vec3 pos = feet;
    // Parts, then the other characters' bodies (never our own); only the ones our
    // collision group bumps into. The terrain is in the Default group.
    std::vector<const Collider*> solids;
    solids.reserve(m_colliders.size() + m_bodies.size());
    for (const auto& c : m_colliders) if (c.solid && m_groups.collides(group, c.group)) solids.push_back(&c);
    for (const auto& c : m_bodies) if (c.node->id != self && m_groups.collides(group, c.group)) solids.push_back(&c);
    const bool useTerrain = m_terrain && m_groups.collides(group, 0);
    auto boxOf = [](const Collider& c) { return c.rotated ? c.obb : OBB::fromAABB(c.box); };
    auto fits = [&](const glm::vec3& p) {
        OBB me = charOBB(p, yaw);
        AABB mine = bounds(me);
        for (const Collider* cp : solids) {
            const Collider& c = *cp;
            if (!mine.overlaps(c.box)) continue;
            glm::vec3 n; float d;
            if (obbOverlap(me, boxOf(c), n, d)) return false;
        }
        return true;
    };

    // Push the body out of everything solid it's inside. `stepUp`: walking into
    // something low (a stair, a curb) climbs on top of it instead of stopping.
    auto resolve = [&](bool stepUp, bool falling) {
        for (int iter = 0; iter < 4; ++iter) {
            bool any = false;
            for (const Collider* cp : solids) {
                const Collider& c = *cp;
                OBB me = charOBB(pos, yaw);
                if (!bounds(me).overlaps(c.box)) continue;
                glm::vec3 n; float d;
                if (!obbOverlap(me, boxOf(c), n, d)) continue;
                any = true;
                if (stepUp && wasGrounded && !c.rotated) {
                    float rise = c.box.max.y - pos.y;
                    if (rise > 0.0f && rise <= kStepHeight && fits({pos.x, c.box.max.y + 0.001f, pos.z})) {
                        r.stepped += c.box.max.y + 0.001f - pos.y;
                        pos.y = c.box.max.y + 0.001f;
                        continue;
                    }
                }
                if (n.y > 0.55f) {                       // something to stand on
                    pos.y += std::min(d / n.y, 1.0f);
                    r.grounded = true;
                    r.groundId = c.node->id;
                } else if (n.y < -0.55f) {               // bumped our head
                    pos += n * d;
                    r.hitCeiling = true;
                } else {                                  // a wall: slide along it
                    glm::vec3 h(n.x, 0.0f, n.z);
                    float l = glm::length(h);
                    if (l > 1e-4f) pos += h / l * std::min(d / l, 1.0f);
                    if (c.dynamic && !falling) r.pushed.push_back({c.node, -h / std::max(l, 1e-4f)});
                }
            }
            if (!any) break;
        }
    };

    // Something moved into us (a door, a pusher, turning around next to a wall).
    resolve(false, false);

    // The terrain under us: climb gentle slopes, stop at cliffs.
    auto onTerrain = [&](const glm::vec3& before) {
        float g;
        if (!useTerrain || !terrainGround(pos, yaw, g) || g <= pos.y) return;
        const glm::vec3 n = m_terrain->normalAt(pos.x, pos.z);
        if (n.y < 0.6f && g - before.y > 0.05f) {   // too steep to walk up (over about 53 degrees)
            pos.x = before.x;
            pos.z = before.z;
            if (terrainGround(pos, yaw, g) && g > pos.y) pos.y = g;
            return;
        }
        pos.y = g;
        r.grounded = true;
        r.groundId = 0;
    };
    onTerrain(pos);

    // Walk, in small steps so fast movement can't pass through thin walls.
    glm::vec2 h(delta.x, delta.z);
    int steps = std::max(1, (int)std::ceil(glm::length(h) / 0.2f));
    for (int s = 0; s < steps; ++s) {
        const glm::vec3 before = pos;
        pos.x += h.x / steps;
        pos.z += h.y / steps;
        resolve(true, false);
        if (useTerrain) onTerrain(before);
    }

    // Fall / jump.
    pos.y += delta.y;
    bool wasGround = r.grounded;
    r.grounded = false;
    resolve(false, true);
    if (useTerrain) {
        float g;
        if (terrainGround(pos, yaw, g) && g >= pos.y) { pos.y = g; r.grounded = true; r.groundId = 0; }
    }
    if (delta.y > 0.0f && r.grounded && !wasGround) r.grounded = false;   // brushing a ledge on the way up isn't landing
    if (wasGround && delta.y <= 0.0f) r.grounded = true;

    // Stick to the ground when walking down slopes and steps (instead of
    // flying off every little edge).
    if (wasGrounded && delta.y <= 0.0f && !r.grounded) {
        glm::vec3 probe = pos - glm::vec3(0.0f, 0.4f, 0.0f);
        OBB me = charOBB(probe, yaw);
        float bestY = -1e30f;
        uint64_t bestId = 0;
        for (const Collider* cp : solids) {
            const Collider& c = *cp;
            glm::vec3 n; float d;
            if (!bounds(me).overlaps(c.box) || !obbOverlap(me, boxOf(c), n, d) || n.y <= 0.55f) continue;
            float y = probe.y + d / n.y;
            if (y <= pos.y + 0.01f && y > bestY) { bestY = y; bestId = c.node->id; }
        }
        float g;
        if (useTerrain && terrainGround(pos, yaw, g) && g <= pos.y + 0.01f && g >= pos.y - 0.6f && g > bestY) { bestY = g; bestId = 0; }
        if (bestY > -1e29f) {
            r.dropped += pos.y - bestY;
            pos.y = bestY;
            r.grounded = true;
            r.groundId = bestId;
        }
    }

    r.position = pos;
    return r;
}

bool Physics::terrainGround(const glm::vec3& feet, float yaw, float& y) const {
    if (!m_terrain) return false;
    AABB b = bounds(charOBB(feet, yaw));
    // (a little smaller than the body, so brushing past a hill doesn't climb it)
    return m_terrain->highestUnder(b.min.x + 0.15f, b.min.z + 0.15f, b.max.x - 0.15f, b.max.z - 0.15f, y);
}

// stepParts (rigid bodies + constraints) lives in RigidBodies.cpp.

void Physics::collectTouches(Scene& scene, std::vector<TouchEvent>& out) {
    std::set<std::pair<uint64_t, uint64_t>> now;

    auto report = [&](uint64_t part, uint64_t pairKey, uint64_t other) {
        auto key = std::make_pair(part, pairKey);
        if (!now.insert(key).second) return;
        if (!m_touching.count(key)) out.push_back({part, other});
    };

    // Character vs parts (every part, even non-collidable ones like coins). Like
    // Roblox, each body part that really touches something fires Touched on its
    // own: the arm that brushes the wall, the leg on the button, the head that
    // bumps the ceiling. Both sides hear about it (part.Touched and limb.Touched).
    Player* player = scene.player();
    SceneNode* charRoot = player ? player->root() : nullptr;
    if (charRoot && !player->isDead()) {
        AABB body = characterBox(player->position()).inflated(1.2f);   // everything the limbs could reach
        std::vector<std::pair<SceneNode*, OBB>> limbs;
        for (auto& ch : charRoot->children) {
            if (!ch->isPart() || ch->internal) continue;
            OBB o = worldOBB(ch.get());
            o.half += glm::vec3(0.05f);   // resting on something counts as touching it
            limbs.push_back({ch.get(), o});
        }
        for (const auto& c : m_colliders) {
            if (!body.overlaps(c.box)) continue;
            OBB other = c.rotated ? c.obb : OBB::fromAABB(c.box);
            for (auto& [limb, o] : limbs) {
                if (!bounds(o).overlaps(c.box)) continue;
                glm::vec3 nrm; float depth;
                if (!obbOverlap(o, other, nrm, depth)) continue;
                report(c.node->id, limb->id, limb->id);
                report(limb->id, c.node->id, c.node->id);
            }
        }
    }

    // The tool in each character's hand vs everything it swings through (a sword's
    // Handle.Touched fires on what it hits; the thing hit gets Touched too). That's
    // yours, and (hosting) the ones held by people who joined.
    std::vector<SceneNode*> held;
    if (charRoot && !player->isDead())
        if (SceneNode* tool = player->equippedTool()) held.push_back(tool);
    for (const RemoteCharacter& rc : scene.remotes())
        if (rc.alive)
            if (SceneNode* tool = Player::heldTool(scene.findById(rc.rootId))) held.push_back(tool);
    for (SceneNode* tool : held) {
        std::vector<SceneNode*> stack{tool};
        while (!stack.empty()) {
            SceneNode* p = stack.back(); stack.pop_back();
            for (auto& ch : p->children) stack.push_back(ch.get());
            if (!p->isPart()) continue;
            AABB box = worldBounds(p).inflated(0.02f);
            for (const auto& c : m_colliders) {
                if (!box.overlaps(c.box)) continue;
                report(p->id, c.node->id, c.node->id);
                report(c.node->id, p->id, p->id);
            }
        }
    }

    // Unanchored parts vs everything they bump into.
    for (size_t i = 0; i < m_colliders.size(); ++i) {
        if (!m_colliders[i].dynamic) continue;
        AABB a = m_colliders[i].box.inflated(0.02f);
        for (size_t j = 0; j < m_colliders.size(); ++j) {
            if (i == j || !a.overlaps(m_colliders[j].box)) continue;
            uint64_t idA = m_colliders[i].node->id, idB = m_colliders[j].node->id;
            report(idA, idB, idB);
            report(idB, idA, idA);
        }
    }

    m_touching.swap(now);
}

const NavMesh& Physics::navMesh() const {
    using namespace std::chrono;
    const double now = duration<double>(steady_clock::now().time_since_epoch()).count();
    // Rebuilt when the anchored parts changed - but not more than about once a second,
    // so moving platforms don't make every path search rebake the whole world.
    if (!m_nav.baked() || (m_navHash != m_staticHash && now - m_navTime > 1.0)) rebakeNavMesh();
    return m_nav;
}

void Physics::rebakeNavMesh() const {
    using namespace std::chrono;
    m_nav.bake(*this, m_navSettings);
    m_navHash = m_staticHash;
    m_navTime = duration<double>(steady_clock::now().time_since_epoch()).count();
}
