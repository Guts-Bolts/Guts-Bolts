#include "Animation.h"
#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Anim {

// ---------------------------------------------------------------------------
// Keyframes and their text
// ---------------------------------------------------------------------------

Keyframe* Clip::keyAt(float t, float tolerance) {
    for (Keyframe& k : keys) if (std::fabs(k.time - t) <= tolerance) return &k;
    return nullptr;
}

Keyframe& Clip::addKey(float t) {
    if (Keyframe* k = keyAt(t)) return *k;
    Keyframe k;
    k.time = t;
    keys.push_back(k);
    sort();
    return *keyAt(t);
}

void Clip::sort() {
    std::stable_sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

namespace {
json vec(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
glm::vec3 vec(const json& j, glm::vec3 fallback = glm::vec3(0.0f)) {
    if (!j.is_array() || j.size() != 3) return fallback;
    try { return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; } catch (...) { return fallback; }
}
template <typename E, size_t N>
E pick(const json& j, const char* key, const char* const (&names)[N], E fallback) {
    if (!j.contains(key) || !j[key].is_string()) return fallback;
    std::string s = j[key].get<std::string>();
    for (size_t i = 0; i < N; ++i) if (s == names[i]) return (E)i;
    return fallback;
}
} // namespace

Clip parse(const std::string& text) {
    Clip c;
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return c;
    c.loop = j.value("loop", false);
    c.priority = pick(j, "priority", kPriorityNames, Priority::Action);
    if (j.contains("keys") && j["keys"].is_array())
        for (const json& kj : j["keys"]) {
            if (!kj.is_object()) continue;
            Keyframe k;
            k.time = std::max(0.0f, kj.value("t", 0.0f));
            k.name = kj.value("name", std::string());
            if (kj.contains("poses") && kj["poses"].is_object())
                for (auto it = kj["poses"].begin(); it != kj["poses"].end(); ++it) {
                    if (!it->is_object()) continue;
                    Pose p;
                    p.pos = vec(it->value("pos", json()));
                    p.rot = vec(it->value("rot", json()));
                    p.easing = pick(*it, "ease", kEasingNames, Easing::Linear);
                    p.dir = pick(*it, "dir", kEaseDirNames, EaseDir::In);
                    k.poses[it.key()] = p;
                }
            c.keys.push_back(std::move(k));
        }
    c.sort();
    return c;
}

std::string dump(const Clip& c) {
    json keys = json::array();
    for (const Keyframe& k : c.keys) {
        json poses = json::object();
        for (const auto& [name, p] : k.poses) {
            json pj = {{"pos", vec(p.pos)}, {"rot", vec(p.rot)}};
            if (p.easing != Easing::Linear) pj["ease"] = kEasingNames[(int)p.easing];
            if (p.dir != EaseDir::In) pj["dir"] = kEaseDirNames[(int)p.dir];
            poses[name] = pj;
        }
        json kj = {{"t", k.time}, {"poses", poses}};
        if (!k.name.empty()) kj["name"] = k.name;
        keys.push_back(kj);
    }
    return json{{"loop", c.loop}, {"priority", kPriorityNames[(int)c.priority]}, {"keys", keys}}.dump();
}

std::string emptyClipText() { return dump(Clip()); }

// ---------------------------------------------------------------------------
// In-betweens
// ---------------------------------------------------------------------------

namespace {
float easeIn(Easing e, float t) {
    constexpr float kPi = 3.14159265f;
    switch (e) {
        case Easing::Linear:   return t;
        case Easing::Constant: return t < 1.0f ? 0.0f : 1.0f;
        case Easing::Cubic:    return t * t * t;
        case Easing::Elastic: {
            if (t <= 0.0f || t >= 1.0f) return t;
            return -std::pow(2.0f, 10.0f * (t - 1.0f)) * std::sin((t - 1.075f) * (2.0f * kPi) / 0.3f);
        }
        case Easing::Bounce: {
            // In = the mirror of the usual bounce-out.
            auto out = [](float x) {
                if (x < 1 / 2.75f) return 7.5625f * x * x;
                if (x < 2 / 2.75f) { x -= 1.5f / 2.75f; return 7.5625f * x * x + 0.75f; }
                if (x < 2.5f / 2.75f) { x -= 2.25f / 2.75f; return 7.5625f * x * x + 0.9375f; }
                x -= 2.625f / 2.75f; return 7.5625f * x * x + 0.984375f;
            };
            return 1.0f - out(1.0f - t);
        }
    }
    return t;
}
} // namespace

float ease(Easing e, EaseDir d, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (e == Easing::Constant) return t < 1.0f ? 0.0f : 1.0f;
    switch (d) {
        case EaseDir::In:  return easeIn(e, t);
        case EaseDir::Out: return 1.0f - easeIn(e, 1.0f - t);
        default:           return t < 0.5f ? easeIn(e, t * 2.0f) * 0.5f : 1.0f - easeIn(e, (1.0f - t) * 2.0f) * 0.5f;
    }
}

glm::quat eulerToQuat(const glm::vec3& d) {
    glm::vec3 r = glm::radians(d);
    return glm::angleAxis(r.z, glm::vec3(0, 0, 1)) * glm::angleAxis(r.y, glm::vec3(0, 1, 0)) *
           glm::angleAxis(r.x, glm::vec3(1, 0, 0));
}

glm::vec3 quatToEuler(const glm::quat& q) {
    glm::mat3 m = glm::mat3_cast(glm::normalize(q));   // m[col][row]
    float sy = std::clamp(-m[0][2], -1.0f, 1.0f);
    float y = std::asin(sy), x, z;
    if (std::fabs(sy) < 0.9999f) {
        x = std::atan2(m[1][2], m[2][2]);
        z = std::atan2(m[0][1], m[0][0]);
    } else {   // straight up / down: X and Z turn the same way, put it all in X
        x = std::atan2(-m[2][1], m[1][1]);
        z = 0.0f;
    }
    return glm::degrees(glm::vec3(x, y, z));
}

void sample(const Clip& c, float t, std::map<std::string, Sample>& out) {
    out.clear();
    if (c.keys.empty()) return;
    // Every part posed anywhere in the clip.
    std::map<std::string, bool> names;
    for (const Keyframe& k : c.keys) for (const auto& [n, p] : k.poses) names[n] = true;
    for (const auto& [name, unused] : names) {
        const Keyframe* before = nullptr;
        const Keyframe* after = nullptr;
        for (const Keyframe& k : c.keys) {
            if (!k.poses.count(name)) continue;
            if (k.time <= t) before = &k;
            else if (!after) after = &k;
        }
        Sample s;
        if (!before && !after) continue;
        if (!before) before = after;   // before its first key: hold the first pose
        const Pose& a = before->poses.at(name);
        if (!after || after == before) {
            s.pos = a.pos;
            s.rot = eulerToQuat(a.rot);
        } else {
            const Pose& b = after->poses.at(name);
            float span = std::max(1e-4f, after->time - before->time);
            float f = ease(a.easing, a.dir, (t - before->time) / span);
            s.pos = glm::mix(a.pos, b.pos, f);
            glm::quat qa = eulerToQuat(a.rot), qb = eulerToQuat(b.rot);
            if (a.easing == Easing::Elastic || a.easing == Easing::Bounce) {
                // These overshoot (f outside 0..1): go by angle so they swing past.
                glm::quat d = glm::inverse(qa) * qb;
                if (d.w < 0.0f) d = -d;
                float ang = glm::angle(d);
                glm::vec3 axis = ang > 1e-5f ? glm::axis(d) : glm::vec3(1, 0, 0);
                s.rot = qa * glm::angleAxis(ang * f, axis);
            } else {
                s.rot = glm::slerp(qa, qb, std::clamp(f, 0.0f, 1.0f));
            }
        }
        out[name] = s;
    }
}

// ---------------------------------------------------------------------------
// Rigs: which part hangs off which, and where each one bends
// ---------------------------------------------------------------------------

namespace {

// A turn and a move (no size): how parts are placed in the rig.
struct Rigid {
    glm::vec3 t{0.0f};
    glm::quat q{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 apply(const glm::vec3& p) const { return t + q * p; }
};
Rigid operator*(const Rigid& a, const Rigid& b) { return {a.t + a.q * b.t, a.q * b.q}; }
Rigid inverse(const Rigid& a) { glm::quat iq = glm::inverse(a.q); return {-(iq * a.t), iq}; }
Rigid rigidOf(const Transform& tr) { return {tr.position, eulerToQuat(tr.rotation)}; }
Rigid blend(const Rigid& a, const Rigid& b, float w) {
    if (w >= 0.999f) return b;
    if (w <= 0.001f) return a;
    return {glm::mix(a.t, b.t, w), glm::slerp(a.q, b.q, w)};
}

// Classic characters are flat: the limbs sit next to the torso in the model.
// These say what each one hangs off, like Roblox's Motor6D joints (R6 and R15).
const char* jointParent(const std::string& n) {
    static const std::map<std::string, const char*> parents = {
        {"Torso", "HumanoidRootPart"}, {"Head", "Torso"},
        {"Left Arm", "Torso"}, {"Right Arm", "Torso"}, {"Left Leg", "Torso"}, {"Right Leg", "Torso"},
        {"LowerTorso", "HumanoidRootPart"}, {"UpperTorso", "LowerTorso"},
        {"LeftUpperArm", "UpperTorso"}, {"LeftLowerArm", "LeftUpperArm"}, {"LeftHand", "LeftLowerArm"},
        {"RightUpperArm", "UpperTorso"}, {"RightLowerArm", "RightUpperArm"}, {"RightHand", "RightLowerArm"},
        {"LeftUpperLeg", "LowerTorso"}, {"LeftLowerLeg", "LeftUpperLeg"}, {"LeftFoot", "LeftLowerLeg"},
        {"RightUpperLeg", "LowerTorso"}, {"RightLowerLeg", "RightUpperLeg"}, {"RightFoot", "RightLowerLeg"},
    };
    auto it = parents.find(n);
    return it == parents.end() ? nullptr : it->second;
}

// Hats and other accessories are worn: they hang off the head (or the body, for
// neck, shoulder and waist ones) so they turn with it, like Roblox's accessory welds.
// The built-in hats are parts named "Hat..." in the character; the ones made in
// Studio's Accessory window are an "Accessory (<kind>)" model of parts.
// Returns "head", "body", "waist" or "" (not worn).
std::string wornOn(const SceneNode* n, const SceneNode* rig) {
    if (n->parent == rig) return n->name.rfind("Hat", 0) == 0 ? "head" : "";
    for (const SceneNode* p = n->parent; p && p != rig; p = p->parent)
        if (p->kind == NodeKind::Model && p->name.rfind("Accessory (", 0) == 0) {
            const std::string kind = p->name.substr(11, p->name.size() > 12 ? p->name.size() - 12 : 0);
            if (kind == "neck" || kind == "shoulder") return "body";
            if (kind == "waist") return "waist";
            return "head";   // hat, hair, faceacc (and anything new)
        }
    return "";
}

// Where a part bends, in its own space (-0.5..0.5 across): an Attachment named
// "Pivot" inside it wins; otherwise arms and legs bend at the top (shoulder /
// hip), heads at the bottom (neck), everything else in the middle.
glm::vec3 pivotOf(const SceneNode* part) {
    for (const auto& c : part->children)
        if (c->isAttachment() && c->name == "Pivot") return c->transform.position;
    const std::string& n = part->name;
    auto ends = [&](const char* s) {
        size_t l = std::strlen(s);
        return n.size() >= l && n.compare(n.size() - l, l, s) == 0;
    };
    if (ends(" Arm") || ends(" Leg") || ends("UpperArm") || ends("LowerArm") || ends("Hand") ||
        ends("UpperLeg") || ends("LowerLeg") || ends("Foot"))
        return {0.0f, 0.5f, 0.0f};
    if (n == "Head" || n == "UpperTorso") return {0.0f, -0.5f, 0.0f};
    return glm::vec3(0.0f);
}

bool isPosable(const SceneNode* n) { return n->kind == NodeKind::Part && n->mesh && !n->internal; }

void collectParts(SceneNode* n, std::vector<SceneNode*>& out) {
    for (auto& c : n->children) {
        if (c->kind == NodeKind::Part && c->mesh) out.push_back(c.get());
        if (c->kind == NodeKind::Part || c->kind == NodeKind::Model) collectParts(c.get(), out);
    }
}

const Transform& restOf(const RestPose& rest, const SceneNode* n) {
    auto it = rest.find(n->id);
    return it == rest.end() ? n->transform : it->second;
}

// Bones (armatures inside MeshParts, see Skin.h), anywhere in the rig.
void collectBones(SceneNode* n, std::vector<SceneNode*>& out) {
    for (auto& c : n->children) {
        if (c->isBone()) out.push_back(c.get());
        if (c->kind == NodeKind::Part || c->kind == NodeKind::Model || c->isBone()) collectBones(c.get(), out);
    }
}

// A bone's pose turns and moves it in its own axes, from where it rests.
// A bone with no rest in `rest` uses (and the first time, fills in) `more`; with
// neither it's left alone (posing it from where it is would add up every frame).
void poseBones(SceneNode* rig, const RestPose& rest, const std::map<std::string, Sample>& poses,
               const std::map<std::string, float>* weights, RestPose* more = nullptr) {
    std::vector<SceneNode*> list;
    collectBones(rig, list);
    for (SceneNode* b : list) {
        auto it = poses.find(b->name);
        if (it == poses.end()) continue;
        const Transform* r = nullptr;
        if (auto ri = rest.find(b->id); ri != rest.end()) r = &ri->second;
        else if (more) r = &more->try_emplace(b->id, b->transform).first->second;
        if (!r) continue;
        float w = 1.0f;
        if (weights) { auto wi = weights->find(b->name); if (wi != weights->end()) w = wi->second; }
        Rigid restLocal = rigidOf(*r);
        Rigid own = blend(inverse(restLocal) * rigidOf(b->transform), Rigid{it->second.pos, it->second.rot}, w);
        Rigid placed = restLocal * own;
        b->transform.position = placed.t;
        b->transform.rotation = quatToEuler(placed.q);
    }
}

void restoreBones(SceneNode* rig, const RestPose& rest) {
    std::vector<SceneNode*> list;
    collectBones(rig, list);
    for (SceneNode* b : list) {
        auto it = rest.find(b->id);
        if (it == rest.end()) continue;
        b->transform.position = it->second.position;
        b->transform.rotation = it->second.rotation;
    }
}

// Everything worked out for one rig, one frame.
struct Solver {
    SceneNode* rig;
    const RestPose& rest;
    const std::map<std::string, Sample>& poses;
    const std::map<std::string, float>* weights;   // null = all 1
    std::vector<SceneNode*> parts;
    std::map<std::string, SceneNode*> byName;
    std::map<const SceneNode*, Rigid> posedRig;    // result, in rig space (flat parts)
    std::map<const SceneNode*, bool> done;

    Solver(SceneNode* r, const RestPose& re, const std::map<std::string, Sample>& p,
           const std::map<std::string, float>* w)
        : rig(r), rest(re), poses(p), weights(w) {
        collectParts(rig, parts);
        for (SceneNode* n : parts) if (!byName.count(n->name)) byName[n->name] = n;
    }

    bool nested(const SceneNode* n) const { return n->parent && n->parent != rig && n->parent->kind == NodeKind::Part; }

    // The part n hangs off: its joint, or for something worn, the head or body.
    const SceneNode* parentOf(const SceneNode* n) const {
        auto find = [&](const char* name) -> const SceneNode* {
            auto it = byName.find(name);
            return it == byName.end() || it->second == n || nested(it->second) ? nullptr : it->second;
        };
        if (const char* pn = jointParent(n->name)) return find(pn);
        const std::string on = wornOn(n, rig);
        if (on.empty()) return nullptr;
        if (on == "head") return find("Head");
        const SceneNode* p = find(on == "waist" ? "LowerTorso" : "UpperTorso");   // (R15)
        return p ? p : find("Torso");
    }

    // The (non-part) containers between the rig and a flat part, as they are now.
    Rigid containerRig(const SceneNode* n) const {
        Rigid r;
        std::vector<const SceneNode*> chain;
        for (const SceneNode* p = n->parent; p && p != rig; p = p->parent) chain.push_back(p);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) r = r * rigidOf((*it)->transform);
        return r;
    }

    // The pose on part n as a move in `space` (the rest placement of n in that space).
    Rigid poseMove(const SceneNode* n, const Rigid& restPlace, const Sample& s) const {
        glm::vec3 pivot = restPlace.apply(pivotOf(n) * restOf(rest, n).scale);
        Rigid toPivot{pivot, glm::quat(1, 0, 0, 0)}, fromPivot{-pivot, glm::quat(1, 0, 0, 0)};
        Rigid turn{glm::vec3(0.0f), restPlace.q * s.rot * glm::inverse(restPlace.q)};
        Rigid move{restPlace.q * s.pos, glm::quat(1, 0, 0, 0)};
        return move * toPivot * turn * fromPivot;
    }

    float weightOf(const std::string& n) const {
        if (!weights) return 1.0f;
        auto it = weights->find(n);
        return it == weights->end() ? 1.0f : it->second;
    }

    // Flat parts: where it goes in rig space.
    Rigid solve(const SceneNode* n) {
        if (done[n]) return posedRig[n];
        done[n] = true;   // (stops a loop if names are odd)
        Rigid restRig = containerRig(n) * rigidOf(restOf(rest, n));
        Rigid nowRig = containerRig(n) * rigidOf(n->transform);
        // What the part does on its own: the pose if it has one, else whatever moves it now.
        Rigid own = nowRig * inverse(restRig);
        if (auto it = poses.find(n->name); it != poses.end())
            own = blend(own, poseMove(n, restRig, it->second), weightOf(n->name));
        // Plus whatever its joint parent does.
        Rigid parentMove;
        if (const SceneNode* p = parentOf(n))
            parentMove = solve(p) * inverse(containerRig(p) * rigidOf(restOf(rest, p)));
        posedRig[n] = parentMove * own * restRig;
        return posedRig[n];
    }

    // Does part n move because of the poses (itself or something it hangs off)?
    bool affected(const SceneNode* n, int depth = 0) const {
        if (depth > 16) return false;
        if (poses.count(n->name)) return true;
        if (nested(n)) return false;   // follows its parent part by itself
        if (const SceneNode* p = parentOf(n)) return affected(p, depth + 1);
        return false;
    }

    void apply() {
        for (SceneNode* n : parts) {
            if (!affected(n)) continue;
            // Never seen at rest (say, a hat put on mid-animation): it has nothing to
            // go back to, so moving it would add up frame after frame. Leave it.
            if (!rest.count(n->id) && !poses.count(n->name)) continue;
            if (nested(n)) {
                // Inside another part: pose it in that part's space.
                auto it = poses.find(n->name);
                const Transform& r = restOf(rest, n);
                Rigid restLocal = rigidOf(r);
                Rigid nowLocal = rigidOf(n->transform);
                Rigid own = blend(nowLocal * inverse(restLocal), poseMove(n, restLocal, it->second), weightOf(n->name));
                Rigid placed = own * restLocal;
                n->transform.position = placed.t;
                n->transform.rotation = quatToEuler(placed.q);
                continue;
            }
            Rigid local = inverse(containerRig(n)) * solve(n);
            n->transform.position = local.t;
            n->transform.rotation = quatToEuler(local.q);
        }
    }
};

} // namespace

void captureRest(const SceneNode* rig, RestPose& rest) {
    rest.clear();
    std::vector<SceneNode*> parts;
    collectParts(const_cast<SceneNode*>(rig), parts);
    collectBones(const_cast<SceneNode*>(rig), parts);
    for (const SceneNode* n : parts) rest[n->id] = n->transform;
}

void applyPoses(SceneNode* rig, const RestPose& rest, const std::map<std::string, Sample>& poses, float weight) {
    if (!rig || poses.empty()) return;
    std::map<std::string, float> w;
    if (weight < 0.999f) for (const auto& [n, s] : poses) w[n] = weight;
    Solver s(rig, rest, poses, weight < 0.999f ? &w : nullptr);
    s.apply();
    poseBones(rig, rest, poses, weight < 0.999f ? &w : nullptr);
}

void restore(SceneNode* rig, const RestPose& rest) {
    if (!rig) return;
    std::vector<SceneNode*> parts;
    collectParts(rig, parts);
    for (SceneNode* n : parts) {
        auto it = rest.find(n->id);
        if (it == rest.end()) continue;
        n->transform.position = it->second.position;
        n->transform.rotation = it->second.rotation;
    }
    restoreBones(rig, rest);
}

bool poseFromTransform(SceneNode* rig, const RestPose& rest, const std::map<std::string, Sample>& poses,
                       const SceneNode* part, const Transform& posedLocal, Pose& out) {
    if (!rig || !part) return false;
    // The pose is the move that takes the part from rest to where it is, around its pivot:
    //   move = T(R p) * T(c) * R q R^-1 * T(-c)  ->  q = R^-1 M R,  p = R^-1 (t - c + M c)
    auto solveMove = [&](const Rigid& restPlace, const Rigid& moved) {
        Rigid m = moved * inverse(restPlace);
        glm::vec3 c = restPlace.apply(pivotOf(part) * restOf(rest, part).scale);
        glm::quat rq = glm::inverse(restPlace.q) * m.q * restPlace.q;
        glm::vec3 p = glm::inverse(restPlace.q) * (m.t - c + m.q * c);
        out.pos = p;
        out.rot = quatToEuler(rq);
    };
    if (part->isBone()) {   // in its own axes, from its rest
        Rigid own = inverse(rigidOf(restOf(rest, part))) * rigidOf(posedLocal);
        out.pos = own.t;
        out.rot = quatToEuler(own.q);
        return true;
    }
    Solver s(rig, rest, poses, nullptr);
    if (s.nested(part)) {
        solveMove(rigidOf(restOf(rest, part)), rigidOf(posedLocal));
        return true;
    }
    Rigid restRig = s.containerRig(part) * rigidOf(restOf(rest, part));
    Rigid want = s.containerRig(part) * rigidOf(posedLocal);
    Rigid parentMove;
    if (const SceneNode* p = s.parentOf(part))
        parentMove = s.solve(p) * inverse(s.containerRig(p) * rigidOf(restOf(rest, p)));
    // want = parentMove * own * restRig
    solveMove(restRig, inverse(parentMove) * want);
    return true;
}

std::vector<SceneNode*> rigParts(SceneNode* rig) {
    std::vector<SceneNode*> all, out;
    if (!rig) return out;
    collectParts(rig, all);
    std::map<std::string, bool> seen;
    for (SceneNode* n : all)
        if (isPosable(n) && !seen[n->name]) { seen[n->name] = true; out.push_back(n); }
    std::vector<SceneNode*> bones;
    collectBones(rig, bones);
    for (SceneNode* n : bones)
        if (!seen[n->name]) { seen[n->name] = true; out.push_back(n); }
    return out;
}

std::vector<SceneNode*> wornParts(SceneNode* rig) {
    std::vector<SceneNode*> all, out;
    if (!rig) return out;
    collectParts(rig, all);
    for (SceneNode* n : all)
        if (!wornOn(n, rig).empty()) out.push_back(n);
    return out;
}

SceneNode* findPart(SceneNode* rig, const std::string& name) {
    std::vector<SceneNode*> all;
    if (!rig) return nullptr;
    collectParts(rig, all);
    for (SceneNode* n : all) if (n->name == name) return n;
    all.clear();
    collectBones(rig, all);
    for (SceneNode* n : all) if (n->name == name) return n;
    return nullptr;
}

glm::vec3 jointPivot(const SceneNode* part) { return pivotOf(part); }

SceneNode* rigOf(SceneNode* node) {
    for (SceneNode* n = node; n; n = n->parent)
        if (n->kind == NodeKind::Model && n->parent) return n;   // (not the Workspace)
    return nullptr;
}

// ---------------------------------------------------------------------------
// Animator: tracks playing in a game
// ---------------------------------------------------------------------------

int Animator::load(uint64_t rigId, const SceneNode& animation) {
    Track t;
    t.id = m_next++;
    t.rig = rigId;
    t.animation = animation.id;
    t.name = animation.name;
    t.clip = parse(animation.source);
    t.looped = t.clip.loop;
    t.priority = t.clip.priority;
    m_tracks[t.id] = std::move(t);
    return m_next - 1;
}

int Animator::loadClip(uint64_t rigId, const std::string& name, const Clip& clip) {
    Track t;
    t.id = m_next++;
    t.rig = rigId;
    t.name = name;
    t.clip = clip;
    t.looped = clip.loop;
    t.priority = clip.priority;
    m_tracks[t.id] = std::move(t);
    return m_next - 1;
}

std::vector<int> Animator::playingOn(uint64_t rigId) const {
    std::vector<int> out;
    for (const auto& [id, t] : m_tracks) if (t.rig == rigId && t.playing && t.target > 0.0f) out.push_back(id);
    return out;
}

Animator::Track* Animator::track(int id) {
    auto it = m_tracks.find(id);
    return it == m_tracks.end() ? nullptr : &it->second;
}

void Animator::play(int id, float fade, float weight, float speed) {
    Track* t = track(id);
    if (!t) return;
    if (!t->playing) { t->time = 0.0f; t->weight = fade > 0.0f ? 0.0f : weight; }
    t->playing = true;
    t->speed = speed;
    t->target = std::clamp(weight, 0.0f, 1.0f);
    t->fadeRate = fade > 0.0f ? 1.0f / fade : 0.0f;
    if (fade <= 0.0f) t->weight = t->target;
}

void Animator::stop(int id, float fade) {
    Track* t = track(id);
    if (!t || !t->playing || t->target == 0.0f) return;
    t->target = 0.0f;
    t->fadeRate = fade > 0.0f ? 1.0f / fade : 0.0f;
    if (fade <= 0.0f) t->weight = 0.0f;
    events.push_back({Event::Stopped, id, {}});
}

void Animator::adjustSpeed(int id, float speed) { if (Track* t = track(id)) t->speed = speed; }

void Animator::adjustWeight(int id, float weight, float fade) {
    Track* t = track(id);
    if (!t) return;
    t->target = std::clamp(weight, 0.0f, 1.0f);
    t->fadeRate = fade > 0.0f ? 1.0f / fade : 0.0f;
    if (fade <= 0.0f) t->weight = t->target;
}

void Animator::stopRig(uint64_t rigId, bool restoreParts, Scene& scene) {
    for (auto& [id, t] : m_tracks)
        if (t.rig == rigId && t.playing) {
            t.playing = false;
            t.weight = t.target = 0.0f;
            events.push_back({Event::Stopped, id, {}});
            events.push_back({Event::Ended, id, {}});
        }
    auto it = m_rests.find(rigId);
    if (it != m_rests.end()) {
        if (restoreParts) restore(scene.findById(rigId), it->second);
        m_rests.erase(it);
    }
    if (auto b = m_boneRests.find(rigId); b != m_boneRests.end()) {
        if (restoreParts) if (SceneNode* rig = scene.findById(rigId)) restoreBones(rig, b->second);
        m_boneRests.erase(b);
    }
}

void Animator::clear() {
    m_tracks.clear();
    m_rests.clear();
    m_boneRests.clear();
    events.clear();
}

void Animator::update(float dt, Scene& scene) {
    // 1. Move each playing track along (and fade it in / out).
    std::map<uint64_t, std::vector<Track*>> byRig;
    for (auto& [id, t] : m_tracks) {
        if (!t.playing) continue;
        if (!scene.findById(t.rig)) { t.playing = false; continue; }   // the rig is gone
        float len = t.clip.length();
        float before = t.time;
        t.time += dt * t.speed;
        // Named keyframes passed this frame.
        auto passed = [&, id = id, &t = t](float from, float to) {
            for (const Keyframe& k : t.clip.keys)
                if (!k.name.empty() && k.time > from && k.time <= to) events.push_back({Event::Keyframe, id, k.name});
        };
        if (len <= 0.0f) {
            t.time = 0.0f;
        } else if (t.time >= len) {
            if (t.looped) {
                passed(before, len);
                t.time = std::fmod(t.time, len);
                passed(-1.0f, t.time);
                events.push_back({Event::DidLoop, id, {}});
            } else {
                passed(before, len);
                t.time = len;
                if (t.target > 0.0f) {   // finished: let go smoothly
                    t.target = 0.0f;
                    t.fadeRate = 1.0f / 0.1f;
                    events.push_back({Event::Stopped, id, {}});
                }
            }
        } else if (t.time < 0.0f) {
            t.time = t.looped ? len + std::fmod(t.time, len) : 0.0f;
        } else {
            passed(before, t.time);
        }
        if (t.fadeRate <= 0.0f) t.weight = t.target;
        else if (t.weight < t.target) t.weight = std::min(t.target, t.weight + t.fadeRate * dt);
        else if (t.weight > t.target) t.weight = std::max(t.target, t.weight - t.fadeRate * dt);
        if (t.target == 0.0f && t.weight <= 0.0f) {
            t.playing = false;
            events.push_back({Event::Ended, id, {}});
            continue;
        }
        byRig[t.rig].push_back(&t);
    }

    // 2. Rigs nobody animates any more go back how they were.
    for (auto it = m_rests.begin(); it != m_rests.end();) {
        if (byRig.count(it->first)) { ++it; continue; }
        if (SceneNode* rig = scene.findById(it->first)) {
            RestPose& rest = it->second;
            std::vector<SceneNode*> parts;
            collectParts(rig, parts);
            for (SceneNode* n : parts)
                if (!(drivenElsewhere && drivenElsewhere(it->first, n))) {
                    auto r = rest.find(n->id);
                    if (r != rest.end()) { n->transform.position = r->second.position; n->transform.rotation = r->second.rotation; }
                }
            restoreBones(rig, rest);
            restoreBones(rig, m_boneRests[it->first]);
        }
        m_boneRests.erase(it->first);
        it = m_rests.erase(it);
    }

    // 3. Pose each rig: mix its tracks, lowest priority first.
    for (auto& [rigId, tracks] : byRig) {
        SceneNode* rig = scene.findById(rigId);
        if (!rig) continue;
        const RestPose* rest = restFor ? restFor(rigId) : nullptr;
        if (!rest) {
            auto it = m_rests.find(rigId);
            if (it == m_rests.end()) { captureRest(rig, m_rests[rigId]); it = m_rests.find(rigId); }
            rest = &it->second;
        } else {
            m_rests[rigId] = *rest;   // remembered so we can put it back later
        }
        // Start from rest (except parts something else moves every frame).
        std::vector<SceneNode*> parts;
        collectParts(rig, parts);
        for (SceneNode* n : parts) {
            if (drivenElsewhere && drivenElsewhere(rigId, n)) continue;
            auto r = rest->find(n->id);
            if (r != rest->end()) { n->transform.position = r->second.position; n->transform.rotation = r->second.rotation; }
        }
        restoreBones(rig, *rest);
        restoreBones(rig, m_boneRests[rigId]);
        std::stable_sort(tracks.begin(), tracks.end(), [](const Track* a, const Track* b) { return a->priority < b->priority; });
        std::map<std::string, Sample> mixed;
        std::map<std::string, float> weights;
        std::map<std::string, Sample> one;
        for (const Track* t : tracks) {
            sample(t->clip, t->time, one);
            for (const auto& [name, s] : one) {
                auto it = mixed.find(name);
                if (it == mixed.end()) { mixed[name] = s; weights[name] = t->weight; continue; }
                // A later (same or higher priority) track mixes over what's there.
                it->second.pos = glm::mix(it->second.pos, s.pos, t->weight);
                it->second.rot = glm::slerp(it->second.rot, s.rot, t->weight);
                weights[name] = weights[name] + (1.0f - weights[name]) * t->weight;
            }
        }
        Solver solver(rig, *rest, mixed, &weights);
        solver.apply();
        poseBones(rig, *rest, mixed, &weights, &m_boneRests[rigId]);
    }
}

} // namespace Anim
