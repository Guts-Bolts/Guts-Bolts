#include "AnimFromText.h"
#include "Emotes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

// How the classic body turns (the built-in emotes use the same):
//   arms and legs hang down from their joint; rot X -90 points one forward, -170 straight up.
//   With an arm forward, rot Y + swings it toward the body's left; with it hanging or up, rot Z +
//   swings the right arm out (left arm: the other way). Head X + looks down, Y + looks left.
//   Torso X + leans forward, Y + turns left; Torso pos Y + lifts the whole body.
namespace AnimText {

namespace {
using Anim::Easing;
using Anim::EaseDir;

struct P { glm::vec3 rot{0.0f}, pos{0.0f}; };
using Frame = std::map<std::string, P>;
enum class Side { Right, Left, Both };

P rot(float x, float y, float z) { P p; p.rot = {x, y, z}; return p; }
P lift(float y) { P p; p.pos = {0.0f, y, 0.0f}; return p; }
P rotLift(float x, float y, float z, float up) { P p; p.rot = {x, y, z}; p.pos = {0.0f, up, 0.0f}; return p; }

std::string other(const std::string& n) {
    if (n.rfind("Right", 0) == 0) return "Left" + n.substr(5);
    if (n.rfind("Left", 0) == 0) return "Right" + n.substr(4);
    return n;
}
// The same pose, done by the other side of the body.
Frame mirror(const Frame& f) {
    Frame out;
    for (const auto& [n, p] : f) {
        P m = p;
        m.rot.y = -p.rot.y; m.rot.z = -p.rot.z; m.pos.x = -p.pos.x;
        out[other(n)] = m;
    }
    return out;
}
// Frames are written for the right side: flip or double them for the side asked for.
Frame sided(const Frame& f, Side s) {
    if (s == Side::Right) return f;
    Frame m = mirror(f);
    if (s == Side::Left) return m;
    Frame both = f;
    for (const auto& [n, p] : m) if (n.find("Left") == 0) both[n] = p;
    return both;
}

class Builder {
public:
    float speed = 1.0f, size = 1.0f;
    Anim::Clip clip;
    std::set<std::string> used;
    float t = 0.0f;

    void key(float dt, const Frame& f, Easing e = Easing::Cubic) {
        t += dt / std::max(0.1f, speed);
        Anim::Keyframe k;
        k.time = std::round(t * 1000.0f) / 1000.0f;
        for (const auto& [n, p] : f) {
            Anim::Pose pose;
            pose.rot = p.rot * size;
            pose.pos = p.pos * size;
            pose.easing = e;
            pose.dir = EaseDir::InOut;
            k.poses[n] = pose;
            used.insert(n);
        }
        clip.keys.push_back(std::move(k));
    }
    void rest(float dt) { key(dt, {}); }
    // Fill in every part on every keyframe (a part a keyframe doesn't mention is standing normally).
    void finish() {
        for (Anim::Keyframe& k : clip.keys)
            for (const std::string& n : used)
                if (!k.poses.count(n)) { Anim::Pose p; p.easing = Easing::Cubic; p.dir = EaseDir::InOut; k.poses[n] = p; }
        clip.sort();
    }
};

struct Ask { Side side = Side::Right; bool sideSaid = false; int times = 1; bool timesSaid = false; std::string dir; };

// A motion that repeats a cycle (walk, swim...): neutral, the cycle `times` times, neutral.
void cycle(Builder& b, const std::vector<Frame>& frames, float step, int times, bool loopOnly) {
    if (loopOnly) {   // a looping clip: start and end on the first frame, no standing still
        b.t = 0.0f;
        b.clip.keys.clear();
        b.key(0.0f, frames[0]);
        for (int i = 0; i < times; ++i) for (size_t f = 1; f <= frames.size(); ++f) b.key(step, frames[f % frames.size()]);
        return;
    }
    b.key(step, frames[0]);
    for (int i = 0; i < times; ++i) for (size_t f = 1; f <= frames.size(); ++f) b.key(step, frames[f % frames.size()]);
    b.rest(step);
}

// An emote everyone has (Emotes.cpp), added `times` times at the end.
void emote(Builder& b, const char* name, int times, bool loopOnly) {
    const Emotes::Emote* e = Emotes::findBuiltIn(name);
    if (!e || !e->clip || e->clip->keys.empty()) return;
    for (int i = 0; i < times; ++i) {
        float last = 0.0f;
        for (const Anim::Keyframe& k : e->clip->keys) {
            if (k.time <= 0.0f && !b.clip.keys.empty()) continue;   // (we're already standing there)
            Frame f;
            for (const auto& [n, p] : k.poses) { P q; q.rot = p.rot; q.pos = p.pos; f[n] = q; }
            b.key((k.time - last) * std::max(0.1f, b.speed), f);   // (key() divides by the speed again)
            last = k.time;
        }
    }
    if (e->clip->loop && !loopOnly) b.rest(0.3f);   // a looping emote doesn't end standing
}

using MotionFn = void (*)(Builder&, const Ask&, bool loopOnly);

void wave(Builder& b, const Ask& a, bool) {
    b.key(0.3f, sided({{"Right Arm", rot(-160, 0, 30)}, {"Head", rot(-4, 0, 6)}}, a.side));
    for (int i = 0; i < 2 * a.times; ++i) {
        b.key(0.22f, sided({{"Right Arm", rot(-160, 0, 60)}}, a.side));
        b.key(0.22f, sided({{"Right Arm", rot(-160, 0, 15)}}, a.side));
    }
    b.rest(0.3f);
}
void point(Builder& b, const Ask& a, bool) {
    const Frame f = sided({{"Right Arm", rot(-90, 0, 0)}, {"Head", rot(0, 6, 0)}, {"Torso", rot(0, -10, 0)}}, a.side);
    b.key(0.2f, f);
    b.key(1.2f * a.times, f);
    b.rest(0.3f);
}
void cheer(Builder& b, const Ask& a, bool) {
    for (int i = 0; i < 2 * a.times; ++i) {
        b.key(0.22f, {{"Right Arm", rot(-170, 0, 18)}, {"Left Arm", rot(-170, 0, -18)}, {"Head", rot(-12, 0, 0)}, {"Torso", lift(0.18f)}});
        b.key(0.22f, {{"Right Arm", rot(-145, 0, 18)}, {"Left Arm", rot(-145, 0, -18)}, {"Head", rot(-12, 0, 0)}});
    }
    b.rest(0.3f);
}
void clap(Builder& b, const Ask& a, bool) {
    b.key(0.25f, {{"Right Arm", rot(-75, -15, 0)}, {"Left Arm", rot(-75, 15, 0)}});
    for (int i = 0; i < std::max(3, 3 * a.times); ++i) {
        b.key(0.14f, {{"Right Arm", rot(-75, 22, 0)}, {"Left Arm", rot(-75, -22, 0)}});
        b.key(0.14f, {{"Right Arm", rot(-75, -15, 0)}, {"Left Arm", rot(-75, 15, 0)}});
    }
    b.rest(0.3f);
}
void bow(Builder& b, const Ask& a, bool) {
    for (int i = 0; i < a.times; ++i) {
        const Frame f = {{"Torso", rot(40, 0, 0)}, {"Head", rot(10, 0, 0)}, {"Right Arm", rot(-12, 0, 0)}, {"Left Arm", rot(-12, 0, 0)}};
        b.key(0.5f, f);
        b.key(0.6f, f);
        b.rest(0.5f);
    }
}
void nod(Builder& b, const Ask& a, bool) {
    for (int i = 0; i < std::max(2, a.timesSaid ? a.times : 2); ++i) {
        b.key(0.2f, {{"Head", rot(22, 0, 0)}});
        b.key(0.2f, {{"Head", rot(-5, 0, 0)}});
    }
    b.rest(0.2f);
}
void shake(Builder& b, const Ask& a, bool) {
    for (int i = 0; i < std::max(2, a.timesSaid ? a.times : 3); ++i) {
        b.key(0.18f, {{"Head", rot(0, 28, 0)}});
        b.key(0.18f, {{"Head", rot(0, -28, 0)}});
    }
    b.rest(0.2f);
}
void look(Builder& b, const Ask& a, bool) {
    auto hold = [&](const Frame& f) { b.key(0.35f, f); b.key(0.6f, f); };
    if (a.dir == "up") hold({{"Head", rot(-35, 0, 0)}});
    else if (a.dir == "down") hold({{"Head", rot(35, 0, 0)}});
    else if (a.dir == "left" || (a.sideSaid && a.side == Side::Left)) hold({{"Head", rot(0, 60, 0)}, {"Torso", rot(0, 15, 0)}});
    else if (a.dir == "right" || (a.sideSaid && a.side == Side::Right)) hold({{"Head", rot(0, -60, 0)}, {"Torso", rot(0, -15, 0)}});
    else {   // look around
        hold({{"Head", rot(0, 60, 0)}, {"Torso", rot(0, 15, 0)}});
        hold({{"Head", rot(0, -60, 0)}, {"Torso", rot(0, -15, 0)}});
    }
    b.rest(0.35f);
}
void jump(Builder& b, const Ask& a, bool) {
    for (int i = 0; i < a.times; ++i) {
        b.key(0.2f, {{"Torso", rotLift(12, 0, 0, -0.35f)}, {"Right Arm", rot(25, 0, 0)}, {"Left Arm", rot(25, 0, 0)}});
        b.key(0.25f, {{"Torso", lift(1.6f)}, {"Right Arm", rot(-160, 0, 15)}, {"Left Arm", rot(-160, 0, -15)},
                      {"Right Leg", rot(-15, 0, 0)}, {"Left Leg", rot(10, 0, 0)}}, Easing::Cubic);
        b.key(0.25f, {{"Torso", lift(0.3f)}, {"Right Arm", rot(-60, 0, 25)}, {"Left Arm", rot(-60, 0, -25)}});
        b.key(0.12f, {{"Torso", rotLift(10, 0, 0, -0.3f)}, {"Right Arm", rot(-20, 0, 20)}, {"Left Arm", rot(-20, 0, -20)}});
        b.rest(0.25f);
    }
}
void jacks(Builder& b, const Ask& a, bool) {
    for (int i = 0; i < std::max(2, a.timesSaid ? a.times : 3); ++i) {
        b.key(0.22f, {{"Torso", lift(0.4f)}, {"Right Arm", rot(0, 0, -160)}, {"Left Arm", rot(0, 0, 160)},
                      {"Right Leg", rot(0, 0, -18)}, {"Left Leg", rot(0, 0, 18)}});
        b.rest(0.22f);
    }
}
void crouch(Builder& b, const Ask& a, bool) {
    const Frame f = {{"Torso", rotLift(20, 0, 0, -0.6f)}, {"Head", rot(-15, 0, 0)}, {"Right Leg", rot(-35, 0, 0)}, {"Left Leg", rot(-35, 0, 0)},
                     {"Right Arm", rot(-40, 0, 0)}, {"Left Arm", rot(-40, 0, 0)}};
    b.key(0.3f, f);
    b.key(1.0f * a.times, f);
    b.rest(0.35f);
}
void sit(Builder& b, const Ask& a, bool loopOnly) {
    const Frame f = {{"Torso", lift(-1.0f)}, {"Right Leg", rot(-90, 0, 0)}, {"Left Leg", rot(-90, 0, 0)},
                     {"Right Arm", rot(-25, 0, 0)}, {"Left Arm", rot(-25, 0, 0)}};
    if (loopOnly) { b.clip.keys.clear(); b.t = 0.0f; b.key(0.0f, f); b.key(2.0f, f); return; }
    b.key(0.4f, f);
    b.key(1.5f * a.times, f);
    b.rest(0.4f);
}
void walk(Builder& b, const Ask& a, bool loopOnly) {
    const Frame f1 = {{"Right Leg", rot(-30, 0, 0)}, {"Left Leg", rot(30, 0, 0)}, {"Right Arm", rot(30, 0, 0)}, {"Left Arm", rot(-30, 0, 0)}};
    cycle(b, {f1, mirror(f1)}, 0.4f, std::max(2, a.timesSaid ? a.times : 3), loopOnly);
}
void run(Builder& b, const Ask& a, bool loopOnly) {
    const Frame f1 = {{"Right Leg", rot(-60, 0, 0)}, {"Left Leg", rot(50, 0, 0)}, {"Right Arm", rot(55, 0, 0)}, {"Left Arm", rot(-65, 0, 0)},
                      {"Torso", rot(12, 0, 0)}};
    cycle(b, {f1, mirror(f1)}, 0.25f, std::max(2, a.timesSaid ? a.times : 4), loopOnly);
}
void march(Builder& b, const Ask& a, bool loopOnly) {
    const Frame f1 = {{"Right Leg", rot(-60, 0, 0)}, {"Right Arm", rot(35, 0, 0)}, {"Left Arm", rot(-35, 0, 0)}, {"Torso", lift(0.1f)}};
    const Frame mid = {};
    cycle(b, {f1, mid, mirror(f1), mid}, 0.25f, std::max(2, a.timesSaid ? a.times : 3), loopOnly);
}
void punch(Builder& b, const Ask& a, bool) {
    Side s = a.side;
    for (int i = 0; i < a.times; ++i) {
        b.key(0.18f, sided({{"Right Arm", rot(-35, 0, 10)}, {"Torso", rot(0, -18, 0)}}, s));
        b.key(0.1f, sided({{"Right Arm", rot(-90, 10, 0)}, {"Torso", rot(0, 20, 0)}}, s), Easing::Linear);
        b.key(0.15f, sided({{"Right Arm", rot(-90, 10, 0)}, {"Torso", rot(0, 20, 0)}}, s));
        b.rest(0.2f);
        if (a.side == Side::Both) s = (i % 2 == 0) ? Side::Left : Side::Right;
    }
}
void kick(Builder& b, const Ask& a, bool) {
    for (int i = 0; i < a.times; ++i) {
        b.key(0.2f, sided({{"Right Leg", rot(20, 0, 0)}, {"Torso", rot(-8, 0, 0)}, {"Right Arm", rot(0, 0, -30)}, {"Left Arm", rot(0, 0, 30)}}, a.side));
        b.key(0.12f, sided({{"Right Leg", rot(-85, 0, 0)}, {"Torso", rot(-15, 0, 0)}, {"Right Arm", rot(0, 0, -40)}, {"Left Arm", rot(0, 0, 40)}}, a.side), Easing::Linear);
        b.key(0.2f, sided({{"Right Leg", rot(-80, 0, 0)}, {"Torso", rot(-15, 0, 0)}, {"Right Arm", rot(0, 0, -40)}, {"Left Arm", rot(0, 0, 40)}}, a.side));
        b.rest(0.3f);
    }
}
void spin(Builder& b, const Ask& a, bool) {
    const float way = a.sideSaid && a.side == Side::Right ? -1.0f : 1.0f;
    for (int i = 0; i < a.times; ++i) {
        b.key(0.25f, {{"Torso", rot(0, 120 * way, 0)}, {"Right Arm", rot(0, 0, -40)}, {"Left Arm", rot(0, 0, 40)}}, Easing::Linear);
        b.key(0.25f, {{"Torso", rot(0, 240 * way, 0)}, {"Right Arm", rot(0, 0, -40)}, {"Left Arm", rot(0, 0, 40)}}, Easing::Linear);
        b.key(0.25f, {{"Torso", rot(0, 0, 0)}, {"Right Arm", rot(0, 0, -40)}, {"Left Arm", rot(0, 0, 40)}}, Easing::Linear);
    }
    b.rest(0.25f);
}
void salute(Builder& b, const Ask& a, bool) {
    const Frame f = sided({{"Right Arm", rot(-125, 45, 0)}, {"Head", rot(-5, 0, 0)}}, a.side);
    b.key(0.3f, f);
    b.key(1.0f * a.times, f);
    b.rest(0.3f);
}
void shrug(Builder& b, const Ask& a, bool) {
    for (int i = 0; i < a.times; ++i) {
        const Frame f = {{"Right Arm", rot(-25, 0, -25)}, {"Left Arm", rot(-25, 0, 25)}, {"Torso", lift(0.15f)}, {"Head", rot(0, 0, 12)}};
        b.key(0.3f, f);
        b.key(0.5f, f);
        b.rest(0.3f);
    }
}
void idle(Builder& b, const Ask& a, bool loopOnly) {
    const Frame up = {{"Torso", lift(0.06f)}, {"Right Arm", rot(0, 0, -4)}, {"Left Arm", rot(0, 0, 4)}, {"Head", rot(-2, 0, 0)}};
    cycle(b, {Frame{}, up}, 1.0f, std::max(1, a.times), loopOnly);
}
void stretch(Builder& b, const Ask& a, bool) {
    for (int i = 0; i < a.times; ++i) {
        const Frame f = {{"Right Arm", rot(-175, 0, 10)}, {"Left Arm", rot(-175, 0, -10)}, {"Torso", lift(0.12f)}, {"Head", rot(-15, 0, 0)}};
        b.key(0.5f, f);
        b.key(0.9f, f);
        b.rest(0.5f);
    }
}
void flap(Builder& b, const Ask& a, bool loopOnly) {
    const Frame upF = {{"Right Arm", rot(0, 0, -95)}, {"Left Arm", rot(0, 0, 95)}, {"Torso", lift(0.1f)}};
    const Frame downF = {{"Right Arm", rot(0, 0, -30)}, {"Left Arm", rot(0, 0, 30)}};
    cycle(b, {downF, upF}, 0.18f, std::max(3, a.timesSaid ? a.times : 4), loopOnly);
}
void swim(Builder& b, const Ask& a, bool loopOnly) {
    const Frame f1 = {{"Right Arm", rot(-170, 0, 0)}, {"Left Arm", rot(10, 0, 0)}, {"Right Leg", rot(-15, 0, 0)}, {"Left Leg", rot(15, 0, 0)}, {"Torso", rot(25, 0, 0)}};
    cycle(b, {f1, mirror(f1)}, 0.45f, std::max(2, a.timesSaid ? a.times : 3), loopOnly);
}
void climb(Builder& b, const Ask& a, bool loopOnly) {
    const Frame f1 = {{"Right Arm", rot(-165, 0, 0)}, {"Left Arm", rot(-100, 0, 0)}, {"Right Leg", rot(-10, 0, 0)}, {"Left Leg", rot(-45, 0, 0)}};
    cycle(b, {f1, mirror(f1)}, 0.4f, std::max(2, a.timesSaid ? a.times : 3), loopOnly);
}
void think(Builder& b, const Ask& a, bool) {
    const Frame f = sided({{"Right Arm", rot(-125, 40, 0)}, {"Head", rot(8, 0, 6)}, {"Left Arm", rot(-30, -25, 0)}}, a.side);
    b.key(0.4f, f);
    b.key(1.4f * a.times, f);
    b.rest(0.4f);
}
void facepalm(Builder& b, const Ask& a, bool) {
    const Frame f = sided({{"Right Arm", rot(-140, 30, 0)}, {"Head", rot(18, 0, 0)}}, a.side);
    b.key(0.25f, f);
    b.key(1.0f * a.times, f);
    b.rest(0.4f);
}
void hug(Builder& b, const Ask& a, bool) {
    const Frame open = {{"Right Arm", rot(-90, -35, 0)}, {"Left Arm", rot(-90, 35, 0)}};
    const Frame closed = {{"Right Arm", rot(-90, 30, 0)}, {"Left Arm", rot(-90, -30, 0)}, {"Torso", rot(8, 0, 0)}};
    b.key(0.3f, open);
    b.key(0.3f, closed);
    b.key(1.0f * a.times, closed);
    b.rest(0.4f);
}
void dab(Builder& b, const Ask& a, bool) {
    const Frame f = sided({{"Right Arm", rot(-110, 60, 0)}, {"Left Arm", rot(0, 0, 130)}, {"Head", rot(30, 30, 0)}, {"Torso", rot(10, 10, 0)}}, a.side);
    for (int i = 0; i < a.times; ++i) {
        b.key(0.18f, f, Easing::Linear);
        b.key(0.6f, f);
        b.rest(0.3f);
    }
}
void dance(Builder& b, const Ask& a, bool loopOnly) {
    if (loopOnly) { b.clip.keys.clear(); b.t = 0.0f; }
    emote(b, a.dir == "2" ? "dance2" : a.dir == "3" ? "dance3" : "dance", std::max(1, a.times), loopOnly);
}
void laugh(Builder& b, const Ask& a, bool) { emote(b, "laugh", std::max(1, a.times), false); }

struct Entry { const char* name; const char* about; MotionFn fn; bool cyclic; std::vector<const char*> words; };
const std::vector<Entry>& table() {
    static const std::vector<Entry> t = {
        {"wave", "say hi with one arm (left, right or both)", wave, false, {"wave", "waves", "waving", "waved", "hi", "hello", "bye", "goodbye", "greet"}},
        {"point", "point forward", point, false, {"point", "points", "pointing"}},
        {"cheer", "both arms up, bouncing", cheer, false, {"cheer", "cheers", "cheering", "celebrate", "celebrating", "hooray", "yay", "win", "victory"}},
        {"clap", "clap your hands", clap, false, {"clap", "claps", "clapping", "applaud", "applause"}},
        {"bow", "bow from the waist", bow, false, {"bow", "bows", "bowing", "curtsy"}},
        {"nod", "nod yes", nod, false, {"nod", "nods", "nodding", "yes", "agree"}},
        {"shake head", "shake your head no", shake, false, {"no", "disagree", "headshake"}},
        {"look", "look left, right, up, down or around", look, false, {"look", "looks", "looking", "glance", "peek", "search"}},
        {"jump", "jump up and land", jump, false, {"jump", "jumps", "jumping", "hop", "hops", "hopping", "leap"}},
        {"jumping jacks", "jumping jacks", jacks, false, {"jumpingjacks"}},
        {"crouch", "crouch down low", crouch, false, {"crouch", "crouches", "crouching", "squat", "squats", "duck", "ducks", "sneak", "kneel"}},
        {"sit", "sit down on the floor", sit, true, {"sit", "sits", "sitting", "seated"}},
        {"walk", "walk on the spot", walk, true, {"walk", "walks", "walking", "stroll"}},
        {"run", "run on the spot", run, true, {"run", "runs", "running", "sprint", "sprinting", "jog", "jogging"}},
        {"march", "march with high knees", march, true, {"march", "marches", "marching", "stomp", "stomping"}},
        {"punch", "punch forward", punch, false, {"punch", "punches", "punching", "hit", "box", "boxing", "jab"}},
        {"kick", "kick forward", kick, false, {"kick", "kicks", "kicking"}},
        {"spin", "spin all the way round", spin, false, {"spin", "spins", "spinning", "twirl", "twirls"}},
        {"salute", "salute", salute, false, {"salute", "salutes", "saluting"}},
        {"shrug", "shrug: \"I don't know\"", shrug, false, {"shrug", "shrugs", "shrugging", "dunno"}},
        {"idle", "stand and breathe", idle, true, {"idle", "breathe", "breathing", "stand", "standing", "wait", "waiting"}},
        {"stretch", "stretch both arms up", stretch, false, {"stretch", "stretches", "stretching", "yawn", "reach"}},
        {"flap", "flap your arms like a bird", flap, true, {"flap", "flaps", "flapping", "fly", "flying", "bird", "chicken"}},
        {"swim", "swim (front crawl)", swim, true, {"swim", "swims", "swimming", "paddle"}},
        {"climb", "climb a ladder", climb, true, {"climb", "climbs", "climbing", "ladder"}},
        {"think", "hand on chin, thinking", think, false, {"think", "thinks", "thinking", "hmm", "wonder", "ponder"}},
        {"facepalm", "hand on face", facepalm, false, {"facepalm", "facepalms", "embarrassed"}},
        {"hug", "a big hug", hug, false, {"hug", "hugs", "hugging", "embrace"}},
        {"dab", "dab", dab, false, {"dab", "dabs", "dabbing"}},
        {"dance", "dance (also \"dance 2\", \"dance 3\")", dance, true, {"dance", "dances", "dancing", "groove", "party", "boogie"}},
        {"laugh", "laugh", laugh, false, {"laugh", "laughs", "laughing", "lol", "haha", "giggle"}},
    };
    return t;
}

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string w;
    for (char c : s) {
        if (std::isalnum((unsigned char)c)) w += c;
        else if (!w.empty()) { out.push_back(w); w.clear(); }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}
int numberWord(const std::string& w) {
    static const std::map<std::string, int> n = {{"once", 1}, {"one", 1}, {"twice", 2}, {"two", 2}, {"three", 3}, {"thrice", 3},
        {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10}, {"few", 3}, {"couple", 2}};
    if (auto it = n.find(w); it != n.end()) return it->second;
    if (!w.empty() && w.size() <= 2 && std::all_of(w.begin(), w.end(), ::isdigit)) return std::atoi(w.c_str());
    if (w.size() >= 2 && w[0] == 'x' && std::all_of(w.begin() + 1, w.end(), ::isdigit)) return std::atoi(w.c_str() + 1);
    return 0;
}
const Entry* findMotion(const std::vector<std::string>& ws, size_t i, size_t& used) {
    used = 1;
    const std::string& w = ws[i];
    auto next = [&](const char* s) { return i + 1 < ws.size() && ws[i + 1] == s; };
    // Two-word ones first.
    if ((w == "shake" || w == "shakes" || w == "shaking") && (next("head") || next("his") || next("her") || next("their") || next("your"))) {
        used = next("head") ? 2 : 3;
        for (const Entry& e : table()) if (std::string(e.name) == "shake head") return &e;
    }
    if (w == "jumping" && next("jacks")) { used = 2; for (const Entry& e : table()) if (std::string(e.name) == "jumping jacks") return &e; }
    if ((w == "turn" || w == "turns" || w == "spin") && next("around")) { used = 2; for (const Entry& e : table()) if (std::string(e.name) == "spin") return &e; }
    if ((w == "hands" || w == "arms") && next("up")) { used = 2; for (const Entry& e : table()) if (std::string(e.name) == "cheer") return &e; }
    if (w == "face" && next("palm")) { used = 2; for (const Entry& e : table()) if (std::string(e.name) == "facepalm") return &e; }
    if (w == "high" && next("five")) { used = 2; for (const Entry& e : table()) if (std::string(e.name) == "wave") return &e; }
    for (const Entry& e : table())
        for (const char* x : e.words) if (w == x) return &e;
    return nullptr;
}
const std::set<std::string>& fillers() {
    static const std::set<std::string> f = {"a", "an", "the", "and", "then", "with", "his", "her", "their", "my", "your", "its", "arm", "arms",
        "hand", "hands", "leg", "legs", "foot", "head", "to", "of", "on", "in", "at", "up", "down", "around", "left", "right", "both",
        "times", "time", "slowly", "slow", "quickly", "quick", "fast", "very", "really", "big", "huge", "small", "little", "tiny", "gentle",
        "gently", "loop", "looping", "forever", "over", "repeat", "keep", "again", "while", "it", "they", "he", "she", "character", "player",
        "guy", "person", "npc", "make", "makes", "do", "does", "doing", "animation", "like", "is", "are", "be", "for", "seconds", "second",
        "after", "that", "some", "can", "you", "me", "please", "him", "them", "all", "way", "round", "front", "forward", "side", "sideways",
        "air", "into", "hard", "happy", "angry", "sad", "excited", "proud", "short", "long", "first", "last", "next", "finally", "end"};
    return f;
}
} // namespace

const std::vector<Motion>& motions() {
    static const std::vector<Motion> m = [] {
        std::vector<Motion> v;
        for (const Entry& e : table()) v.push_back({e.name, e.about});
        return v;
    }();
    return m;
}

void fitToRig(Anim::Clip& clip, const std::vector<std::string>& rigParts, std::vector<std::string>* missing) {
    if (rigParts.empty()) return;
    const std::set<std::string> has(rigParts.begin(), rigParts.end());
    const bool r15 = !has.count("Torso") && (has.count("UpperTorso") || has.count("LowerTorso"));
    static const std::map<std::string, std::string> kR15 = {{"Torso", "LowerTorso"}, {"Right Arm", "RightUpperArm"}, {"Left Arm", "LeftUpperArm"},
        {"Right Leg", "RightUpperLeg"}, {"Left Leg", "LeftUpperLeg"}, {"Head", "Head"}};
    std::set<std::string> gone;
    for (Anim::Keyframe& k : clip.keys) {
        std::map<std::string, Anim::Pose> poses;
        for (auto& [n, p] : k.poses) {
            std::string to = n;
            if (r15) if (auto it = kR15.find(n); it != kR15.end()) to = it->second;
            if (has.count(to)) poses[to] = p;
            else gone.insert(n);
        }
        k.poses = std::move(poses);
    }
    if (missing) missing->assign(gone.begin(), gone.end());
}

Result make(const std::string& text, const std::vector<std::string>& rigParts) {
    Result r;
    // Steps: split on "then", commas, semicolons and full stops.
    std::string s = " " + lower(text) + " ";
    for (const char* sep : {" and then ", " then ", " after that ", " afterwards ", " next ", " finally "}) {
        for (size_t at; (at = s.find(sep)) != std::string::npos;) s.replace(at, std::strlen(sep), " | ");
    }
    for (char& c : s) if (c == ',' || c == ';' || c == '.' || c == '!' || c == '\n') c = '|';
    std::vector<std::string> parts;
    { std::stringstream ss(s); std::string p; while (std::getline(ss, p, '|')) parts.push_back(p); }

    struct Planned { const Entry* e; Ask ask; float speed, size; std::string text; };
    std::vector<Planned> plan;
    bool loop = false;
    for (const std::string& clause : parts) {
        const std::vector<std::string> ws = words(clause);
        if (ws.empty()) continue;
        Ask ask;
        float speed = 1.0f, size = 1.0f;
        bool very = false;
        std::vector<const Entry*> found;
        std::vector<std::string> leftover;
        for (size_t i = 0; i < ws.size();) {
            const std::string& w = ws[i];
            size_t used = 1;
            if (const Entry* e = findMotion(ws, i, used)) { found.push_back(e); i += used; continue; }
            if (w == "left") { ask.side = ask.sideSaid && ask.side == Side::Right ? Side::Both : Side::Left; ask.sideSaid = true; ask.dir = ask.dir.empty() ? "left" : ask.dir; }
            else if (w == "right") { ask.side = ask.sideSaid && ask.side == Side::Left ? Side::Both : Side::Right; ask.sideSaid = true; ask.dir = ask.dir.empty() ? "right" : ask.dir; }
            else if (w == "both") { ask.side = Side::Both; ask.sideSaid = true; }
            else if (w == "up" || w == "down" || w == "around") ask.dir = w;
            else if (w == "very" || w == "really" || w == "super") very = true;
            else if (w == "slowly" || w == "slow" || w == "gently") speed *= very ? 0.45f : 0.65f;
            else if (w == "quickly" || w == "quick" || w == "fast" || w == "hard") speed *= very ? 2.0f : 1.5f;
            else if (w == "big" || w == "huge" || w == "wild" || w == "wildly") size = very ? 1.35f : 1.2f;
            else if (w == "small" || w == "little" || w == "tiny" || w == "gentle") size = very ? 0.5f : 0.7f;
            else if (w == "loop" || w == "looping" || w == "forever" || w == "repeat" || w == "repeating" || (w == "over" && i + 2 < ws.size() && ws[i + 2] == "over"))
                loop = true;
            else if (int n = numberWord(w)) {
                if (!found.empty() && std::string(found.back()->name) == "dance" && (n == 2 || n == 3) && (i + 1 >= ws.size() || ws[i + 1] != "times"))
                    ask.dir = std::to_string(n);   // "dance 2"
                else { ask.times = std::clamp(n, 1, 10); ask.timesSaid = true; }
            }
            else if (!fillers().count(w)) leftover.push_back(w);
            ++i;
        }
        if (found.empty()) {
            std::string bit;
            for (const std::string& w : ws) bit += (bit.empty() ? "" : " ") + w;
            if (!leftover.empty()) r.unknown.push_back(bit);
            continue;
        }
        for (const Entry* e : found) plan.push_back({e, ask, speed, size, clause});
    }
    if (plan.empty()) {
        r.error = "I didn't find a motion I know in that. Try words like: wave, bow, jump, clap, walk, run, dance, nod, "
                  "point, spin, kick, punch, sit, cheer (for example \"wave twice, then bow\").";
        return r;
    }
    Builder b;
    b.clip.loop = loop;
    b.clip.priority = Anim::Priority::Action;
    const bool loopOnly = loop && plan.size() == 1 && plan[0].e->cyclic;
    if (loopOnly && std::string(plan[0].e->name) == "idle") b.clip.priority = Anim::Priority::Idle;
    else if (loopOnly && (std::string(plan[0].e->name) == "walk" || std::string(plan[0].e->name) == "run")) b.clip.priority = Anim::Priority::Movement;
    b.key(0.0f, {});
    for (const Planned& p : plan) {
        Step st;
        st.text = p.text;
        while (!st.text.empty() && st.text.front() == ' ') st.text.erase(st.text.begin());
        while (!st.text.empty() && st.text.back() == ' ') st.text.pop_back();
        st.motion = p.e->name;
        st.times = p.ask.times;
        st.start = b.t;
        b.speed = p.speed;
        b.size = p.size;
        p.e->fn(b, p.ask, loopOnly);
        st.end = b.t;
        r.steps.push_back(st);
    }
    b.finish();
    r.clip = std::move(b.clip);
    fitToRig(r.clip, rigParts, &r.missing);
    for (const Step& st : r.steps) {
        std::string n = st.motion;
        n[0] = (char)std::toupper((unsigned char)n[0]);
        if (r.name.find(n) == std::string::npos) r.name += (r.name.empty() ? "" : ", ") + n;
        if (r.name.size() > 40) break;
    }
    if (r.clip.keys.empty() || r.clip.keys.front().poses.empty())
        r.error = "This rig has none of the parts these motions move (Head, Torso, arms, legs).";
    return r;
}

} // namespace AnimText
