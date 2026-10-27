#include "PhysicsReplay.h"
#include "../scene/Physics.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <glm/gtc/quaternion.hpp>

using nlohmann::json;

namespace PhysicsReplay {

namespace {

// The parts the physics moves (the same ones stepParts treats as loose).
bool loose(Scene& scene, const SceneNode* n) {
    if (!n->isPart() || n->internal || n->anchored || n->inViewportFrame() || scene.isCharacterPart(n)) return false;
    for (const SceneNode* p = n; p; p = p->parent) {
        if (!p->visible) return false;                            // (hidden, or in a storage folder)
        if (p != n && p->isPart() && !p->anchored) return false;  // (welded to a loose part: moves with it)
    }
    return true;
}

PartState stateOf(const SceneNode* n) {
    PartState s;
    s.id = n->id;
    s.position = n->transform.position;
    s.rotation = n->transform.rotation;
    s.velocity = n->velocity;
    s.angularVelocity = n->angularVelocity;
    s.sleepTime = n->sleepTime;
    return s;
}

glm::quat turnOf(const glm::vec3& degrees) {
    return glm::angleAxis(glm::radians(degrees.z), glm::vec3(0, 0, 1)) * glm::angleAxis(glm::radians(degrees.y), glm::vec3(0, 1, 0)) *
           glm::angleAxis(glm::radians(degrees.x), glm::vec3(1, 0, 0));
}

json vec(const glm::vec3& v) {
    auto r = [](float f) { return std::round(f * 10000.0f) / 10000.0f; };
    return json::array({r(v.x), r(v.y), r(v.z)});
}
glm::vec3 vecFrom(const json& j) {
    if (!j.is_array() || j.size() != 3) return glm::vec3(0.0f);
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

} // namespace

const PartState* Frame::find(uint64_t id) const {
    for (const auto& p : parts) if (p.id == id) return &p;
    return nullptr;
}

std::string Recording::nameOf(uint64_t id) const {
    for (size_t i = 0; i < ids.size() && i < names.size(); ++i) if (ids[i] == id) return names[i];
    return "Part " + std::to_string(id);
}

Recording record(const std::string& sceneText, float seconds, const std::vector<PartState>* start) {
    const auto t0 = std::chrono::steady_clock::now();
    Recording rec;
    Scene world;
    if (!Serializer::loadScene(world, sceneText)) return rec;
    if (start)
        for (const PartState& s : *start)
            if (SceneNode* n = world.findById(s.id)) {
                n->transform.position = s.position;
                n->transform.rotation = s.rotation;
                n->velocity = s.velocity;
                n->angularVelocity = s.angularVelocity;
                n->sleepTime = s.sleepTime;
            }
    std::vector<SceneNode*> parts;
    world.forEach([&](SceneNode* n) { if (loose(world, n)) parts.push_back(n); });
    for (SceneNode* n : parts) { rec.ids.push_back(n->id); rec.names.push_back(n->name); }
    auto snap = [&](float t) {
        Frame f;
        f.time = t;
        for (uint64_t id : rec.ids)
            if (SceneNode* n = world.findById(id)) f.parts.push_back(stateOf(n));
        rec.frames.push_back(std::move(f));
    };
    snap(0.0f);
    Physics physics;
    const int steps = (int)std::lround(std::clamp(seconds, 0.0f, 60.0f) / rec.step);
    for (int i = 1; i <= steps; ++i) {
        physics.gather(world);
        std::vector<uint64_t> fallen;
        physics.stepParts(world, rec.step, fallen);
        for (uint64_t id : fallen)
            if (SceneNode* n = world.findById(id)) world.removeNode(n);
        snap(i * rec.step);
    }
    rec.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return rec;
}

void apply(Scene& scene, const Frame& frame) {
    for (const PartState& s : frame.parts)
        if (SceneNode* n = scene.findById(s.id)) {
            n->transform.position = s.position;
            n->transform.rotation = s.rotation;
            n->velocity = s.velocity;
            n->angularVelocity = s.angularVelocity;
            n->sleepTime = s.sleepTime;
        }
    scene.markDirty();
}

json testToJson(const Test& t) {
    json frames = json::array();
    const Recording& r = t.expected;
    for (size_t i = 0; i < r.frames.size(); ++i) {
        if (i % kTestEvery && i + 1 != r.frames.size()) continue;   // (a full recording: keep a few moments of it)
        json parts = json::array();
        for (const PartState& s : r.frames[i].parts) {
            json p = {{"id", s.id}, {"p", vec(s.position)}, {"r", vec(s.rotation)}};
            if (i == 0) { p["v"] = vec(s.velocity); p["w"] = vec(s.angularVelocity); p["sleep"] = s.sleepTime; }
            parts.push_back(p);
        }
        frames.push_back({{"t", std::round(r.frames[i].time * 1000.0f) / 1000.0f}, {"parts", parts}});
    }
    json names = json::object();
    for (size_t i = 0; i < r.ids.size() && i < r.names.size(); ++i) names[std::to_string(r.ids[i])] = r.names[i];
    return {{"physicsTest", 1}, {"seconds", t.seconds}, {"tolerance", t.tolerance}, {"step", r.step}, {"names", names}, {"frames", frames}};
}

bool testFromJson(const json& j, Test& t) {
    if (!j.is_object() || !j.contains("physicsTest") || !j.contains("frames")) return false;
    try {
        t.seconds = j.value("seconds", 3.0f);
        t.tolerance = j.value("tolerance", 0.1f);
        Recording& r = t.expected;
        r = Recording{};
        r.step = j.value("step", 1.0f / 60.0f);
        const json names = j.value("names", json::object());
        for (const auto& [id, name] : names.items()) {
            r.ids.push_back(std::stoull(id));
            r.names.push_back(name.get<std::string>());
        }
        for (const auto& fj : j["frames"]) {
            Frame f;
            f.time = fj.value("t", 0.0f);
            for (const auto& pj : fj.value("parts", json::array())) {
                PartState s;
                s.id = pj.value("id", (uint64_t)0);
                s.position = vecFrom(pj.value("p", json()));
                s.rotation = vecFrom(pj.value("r", json()));
                s.velocity = vecFrom(pj.value("v", json()));
                s.angularVelocity = vecFrom(pj.value("w", json()));
                s.sleepTime = pj.value("sleep", 0.0f);
                f.parts.push_back(s);
            }
            r.frames.push_back(std::move(f));
        }
    } catch (const std::exception&) {
        return false;
    }
    return !t.expected.frames.empty();
}

Result run(const std::string& sceneText, const Test& test) {
    Result res;
    const Recording& want = test.expected;
    if (want.frames.empty()) { res.message = "This test has nothing in it."; return res; }
    const Frame& first = want.frames.front();
    Recording got = record(sceneText, test.seconds, &first.parts);
    if (got.frames.empty()) { res.message = "The scene couldn't be loaded."; return res; }
    // A part the test knows about that isn't loose in the scene any more.
    for (const PartState& s : first.parts)
        if (!got.frames.front().find(s.id)) {
            res.worstPart = want.nameOf(s.id);
            res.message = res.worstPart + " isn't in the game any more (or isn't loose): save the test again if that's on purpose.";
            return res;
        }
    constexpr float kMaxTurn = 10.0f;   // degrees a part may be turned differently
    float worstTurn = 0.0f;
    std::string turnPart;
    float turnTime = 0.0f;
    for (const Frame& wf : want.frames) {
        const int index = (int)std::lround(wf.time / got.step);
        if (index < 0 || index >= (int)got.frames.size()) continue;
        const Frame& gf = got.frames[index];
        auto gone = [&](uint64_t id, bool nowGone) {
            res.worstPart = want.nameOf(id);
            res.worstTime = wf.time;
            char text[256];
            std::snprintf(text, sizeof text, nowGone ? "%s fell out of the world at %.2f s, but it didn't when the test was saved."
                                                     : "%s is still in the world at %.2f s, but it had fallen out when the test was saved.",
                          res.worstPart.c_str(), wf.time);
            res.message = text;
            return res;
        };
        for (const PartState& w : wf.parts) {
            const PartState* g = gf.find(w.id);
            if (!g) return gone(w.id, true);
            const float off = glm::length(g->position - w.position);
            const float turn = glm::degrees(glm::angle(turnOf(g->rotation) * glm::inverse(turnOf(w.rotation))));
            if (off > res.worst) { res.worst = off; res.worstPart = want.nameOf(w.id); res.worstTime = wf.time; }
            if (turn > worstTurn) { worstTurn = turn; turnPart = want.nameOf(w.id); turnTime = wf.time; }
        }
        for (const PartState& g : gf.parts)
            if (!wf.find(g.id)) return gone(g.id, false);
    }
    char text[256];
    if (res.worst > test.tolerance) {
        std::snprintf(text, sizeof text, "%s is %.2f studs off at %.2f s.", res.worstPart.c_str(), res.worst, res.worstTime);
    } else if (worstTurn > kMaxTurn) {
        std::snprintf(text, sizeof text, "%s is turned %.0f degrees differently at %.2f s.", turnPart.c_str(), worstTurn, turnTime);
        res.worstPart = turnPart;
        res.worstTime = turnTime;
    } else {
        res.passed = true;
        std::snprintf(text, sizeof text, "Same as when it was saved (every part within %.2f studs).", test.tolerance);
    }
    res.message = text;
    return res;
}

} // namespace PhysicsReplay
