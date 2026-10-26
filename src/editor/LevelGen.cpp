#include "LevelGen.h"
#include "Premades.h"
#include "../renderer/MeshLibrary.h"
#include "../scene/NavMesh.h"
#include "../scene/Physics.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace LevelGen {

namespace {

// Our own random numbers: std::uniform_*_distribution give different numbers on
// different compilers, and a seed must make the same level everywhere.
struct Rng {
    uint64_t state;
    explicit Rng(uint32_t seed) : state(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { next(); }
    uint32_t next() {   // PCG-style
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        uint32_t x = (uint32_t)(((state >> 18u) ^ state) >> 27u);
        uint32_t r = (uint32_t)(state >> 59u);
        return (x >> r) | (x << ((32 - r) & 31));
    }
    float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }        // 0..1
    float range(float a, float b) { return a + (b - a) * unit(); }
    int   pick(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }   // a..b
    bool  chance(float p) { return unit() < p; }
};

struct Rect {   // on the ground: centre and size
    float x, z, w, d;
    bool overlaps(const Rect& o, float margin = 0.0f) const {
        return std::fabs(x - o.x) * 2 < w + o.w + margin * 2 && std::fabs(z - o.z) * 2 < d + o.d + margin * 2;
    }
};

constexpr float kCorridor = 6.0f;   // corridor (and door) width
constexpr float kWall = 1.0f;       // wall thickness

Piece piece(Piece::Kind k, const char* name, glm::vec3 pos, glm::vec3 size, glm::vec3 color, Material m, int room = -1) {
    Piece p{k, name, pos, size, color, m, room};
    return p;
}

void dungeon(const Settings& s, Level& L) {
    Rng rng(s.seed);
    struct Door { int side; float at; };       // side 0 +x, 1 -x, 2 +z, 3 -z; `at` along the side (world x or z)
    struct Room { Rect r; std::vector<Door> doors; int parent = -1, depth = 0; };
    std::vector<Room> rooms;
    std::vector<Rect> corridors;
    auto size = [&] { return 2.0f * rng.pick(s.roomMin / 2, std::max(s.roomMin, s.roomMax) / 2); };
    rooms.push_back({{0, 0, size(), size()}, {}, -1, 0});

    for (int tries = 0; (int)rooms.size() < s.count && tries < s.count * 60; ++tries) {
        const int from = rng.pick(0, (int)rooms.size() - 1);
        const Rect a = rooms[from].r;
        const int side = rng.pick(0, 3);
        Rect b{0, 0, size(), size()};
        const float len = (float)rng.pick(3, 7) * 2.0f;      // corridor length, wall to wall
        const bool alongX = side < 2;
        const float sign = side % 2 == 0 ? 1.0f : -1.0f;
        // How far the new room may slide sideways and still share a corridor-wide stretch with `a`.
        const float across = alongX ? std::min(a.d, b.d) : std::min(a.w, b.w);
        const float slide = std::max(0.0f, across / 2 - kCorridor / 2 - 1.5f);
        const float off = std::round(rng.range(-slide, slide));
        if (alongX) { b.x = a.x + sign * (a.w / 2 + kWall + len + kWall + b.w / 2); b.z = a.z + off; }
        else        { b.z = a.z + sign * (a.d / 2 + kWall + len + kWall + b.d / 2); b.x = a.x + off; }
        // The corridor: where the two rooms face each other.
        float lo, hi;
        if (alongX) { lo = std::max(a.z - a.d / 2, b.z - b.d / 2); hi = std::min(a.z + a.d / 2, b.z + b.d / 2); }
        else        { lo = std::max(a.x - a.w / 2, b.x - b.w / 2); hi = std::min(a.x + a.w / 2, b.x + b.w / 2); }
        lo += kCorridor / 2 + 1; hi -= kCorridor / 2 + 1;
        if (hi < lo) continue;
        const float mid = std::round(rng.range(lo, hi));
        Rect c;
        if (alongX) {
            const float x0 = sign > 0 ? a.x + a.w / 2 : b.x + b.w / 2, x1 = sign > 0 ? b.x - b.w / 2 : a.x - a.w / 2;
            c = {(x0 + x1) / 2, mid, x1 - x0, kCorridor};
        } else {
            const float z0 = sign > 0 ? a.z + a.d / 2 : b.z + b.d / 2, z1 = sign > 0 ? b.z - b.d / 2 : a.z - a.d / 2;
            c = {mid, (z0 + z1) / 2, kCorridor, z1 - z0};
        }
        // Room for it? (walls included, plus a little air)
        Rect bw{b.x, b.z, b.w + 2 * kWall, b.d + 2 * kWall};
        bool clash = false;
        for (const Room& r : rooms) if (bw.overlaps({r.r.x, r.r.z, r.r.w + 2 * kWall, r.r.d + 2 * kWall}, 2.0f)) clash = true;
        Rect cw = alongX ? Rect{c.x, c.z, c.w - 2 * kWall, c.d + 2 * kWall} : Rect{c.x, c.z, c.w + 2 * kWall, c.d - 2 * kWall};
        for (size_t i = 0; i < rooms.size() && !clash; ++i)
            if ((int)i != from && cw.overlaps({rooms[i].r.x, rooms[i].r.z, rooms[i].r.w + 2 * kWall, rooms[i].r.d + 2 * kWall}, 1.0f)) clash = true;
        for (const Rect& o : corridors) if (!clash && (cw.overlaps(o, 1.0f) || bw.overlaps(o, 1.0f))) clash = true;
        if (clash) continue;
        const int back = side ^ 1;
        rooms[from].doors.push_back({side, mid});
        rooms.push_back({b, {{back, mid}}, from, rooms[from].depth + 1});
        corridors.push_back(c);
    }
    L.placed = (int)rooms.size();

    // Floors and walls
    for (size_t i = 0; i < rooms.size(); ++i) {
        const Rect& r = rooms[i].r;
        L.pieces.push_back(piece(Piece::Floor, "Floor", {r.x, -0.5f, r.z}, {r.w, 1, r.d}, s.floorColor, Material::Slate, (int)i));
        for (int side = 0; side < 4; ++side) {
            const bool alongZ = side < 2;   // an east/west wall runs along z
            const float sign = side % 2 == 0 ? 1.0f : -1.0f;
            // East/west walls also cover the corners.
            const float start = alongZ ? r.z - r.d / 2 - kWall : r.x - r.w / 2, end = alongZ ? r.z + r.d / 2 + kWall : r.x + r.w / 2;
            std::vector<std::pair<float, float>> gaps;
            for (const auto& d : rooms[i].doors) if (d.side == side) gaps.push_back({d.at - kCorridor / 2, d.at + kCorridor / 2});
            std::sort(gaps.begin(), gaps.end());
            float at = start;
            gaps.push_back({end, end});
            for (const auto& g : gaps) {
                if (g.first - at > 0.01f) {
                    const float mid = (at + g.first) / 2, len = g.first - at;
                    glm::vec3 pos = alongZ ? glm::vec3(r.x + sign * (r.w / 2 + kWall / 2), s.wallHeight / 2, mid)
                                           : glm::vec3(mid, s.wallHeight / 2, r.z + sign * (r.d / 2 + kWall / 2));
                    glm::vec3 size = alongZ ? glm::vec3(kWall, s.wallHeight, len) : glm::vec3(len, s.wallHeight, kWall);
                    L.pieces.push_back(piece(Piece::Wall, "Wall", pos, size, s.wallColor, Material::Brick, (int)i));
                }
                at = std::max(at, g.second);
            }
        }
    }
    for (const Rect& c : corridors) {
        L.pieces.push_back(piece(Piece::Floor, "CorridorFloor", {c.x, -0.5f, c.z}, {c.w, 1, c.d}, s.floorColor * 0.9f, Material::Slate));
        const bool alongX = c.d == kCorridor;
        for (float sign : {-1.0f, 1.0f}) {
            if (alongX) {
                if (c.w - 2 * kWall <= 0.01f) continue;
                L.pieces.push_back(piece(Piece::Wall, "CorridorWall", {c.x, s.wallHeight / 2, c.z + sign * (kCorridor / 2 + kWall / 2)},
                                         {c.w - 2 * kWall, s.wallHeight, kWall}, s.wallColor, Material::Brick));
            } else {
                if (c.d - 2 * kWall <= 0.01f) continue;
                L.pieces.push_back(piece(Piece::Wall, "CorridorWall", {c.x + sign * (kCorridor / 2 + kWall / 2), s.wallHeight / 2, c.z},
                                         {kWall, s.wallHeight, c.d - 2 * kWall}, s.wallColor, Material::Brick));
            }
        }
    }

    // Spawn in the first room; the goal in the room that's the most corridors away.
    int far = 0;
    for (size_t i = 0; i < rooms.size(); ++i) if (rooms[i].depth > rooms[far].depth) far = (int)i;
    L.spawn = {rooms[0].r.x, 0, rooms[0].r.z};
    L.goal = {rooms[far].r.x, 0, rooms[far].r.z};
    L.pieces.push_back(piece(Piece::Spawn, "SpawnLocation", L.spawn + glm::vec3(0, 0.1f, 0), {4, 0.2f, 4}, glm::vec3(0.55f), Material::Plastic, 0));
    if (far != 0)
        L.pieces.push_back(piece(Piece::Goal, "Goal", L.goal + glm::vec3(0, 0.1f, 0), {5, 0.2f, 5}, {0.2f, 0.9f, 0.35f}, Material::Neon, far));

    // Crates and coins: inside rooms, away from doors, the spawn, the goal and each other.
    std::vector<Rect> taken;
    for (size_t i = 0; i < rooms.size(); ++i) {
        const Rect& r = rooms[i].r;
        for (const auto& d : rooms[i].doors) {
            const float depth = 6.0f;
            if (d.side < 2) taken.push_back({r.x + (d.side == 0 ? 1 : -1) * (r.w / 2 - depth / 2), d.at, depth, kCorridor + 2});
            else            taken.push_back({d.at, r.z + (d.side == 2 ? 1 : -1) * (r.d / 2 - depth / 2), kCorridor + 2, depth});
        }
    }
    taken.push_back({L.spawn.x, L.spawn.z, 7, 7});
    taken.push_back({L.goal.x, L.goal.z, 8, 8});
    auto place = [&](const Rect& room, float w, Rect& out) {
        for (int t = 0; t < 25; ++t) {
            Rect c{std::round(rng.range(room.x - room.w / 2 + w / 2 + 1, room.x + room.w / 2 - w / 2 - 1)),
                   std::round(rng.range(room.z - room.d / 2 + w / 2 + 1, room.z + room.d / 2 - w / 2 - 1)), w, w};
            bool free = true;
            for (const Rect& o : taken) if (c.overlaps(o, 0.5f)) { free = false; break; }
            if (free) { taken.push_back(c); out = c; return true; }
        }
        return false;
    };
    for (size_t i = 0; i < rooms.size(); ++i) {
        const Rect& r = rooms[i].r;
        if (s.props) {
            const int crates = rng.pick(0, std::max(1, (int)(r.w * r.d / 140.0f)));
            for (int k = 0; k < crates; ++k) {
                const float w = (float)rng.pick(2, 4);
                Rect c;
                if (place(r, w, c))
                    L.pieces.push_back(piece(Piece::Crate, "Crate", {c.x, w / 2, c.z}, glm::vec3(w), {0.62f, 0.45f, 0.26f}, Material::Wood, (int)i));
            }
        }
        if (s.coins) {
            const int coins = rng.pick(1, 2);
            for (int k = 0; k < coins; ++k) {
                Rect c;
                if (place(r, 1.0f, c))
                    L.pieces.push_back(piece(Piece::Coin, "Coin", {c.x, 1.2f, c.z}, {0.9f, 0.12f, 0.9f}, {1.0f, 0.8f, 0.15f}, Material::Metal, (int)i));
            }
        }
    }
}

void obby(const Settings& s, Level& L) {
    Rng rng(s.seed);
    struct Plat { Rect r; float top; };
    std::vector<Plat> plats;
    plats.push_back({{0, 0, 12, 12}, 0.0f});
    glm::vec2 dir(0, 1);
    for (int i = 1; i < s.count + 1; ++i) {
        const bool last = i == s.count;
        bool placed = false;
        for (int t = 0; t < 30 && !placed; ++t) {
            glm::vec2 d = dir;
            if (rng.chance(t == 0 ? 0.25f : 0.5f)) {   // turn now and then (never back towards the start)
                if (d.y != 0) d = rng.chance(0.5f) ? glm::vec2(1, 0) : glm::vec2(-1, 0);
                else d = glm::vec2(0, 1);
            }
            const Plat& p = plats.back();
            const bool wide = last || i % 6 == 0;   // checkpoints and the goal get room to land
            // (Whole-stud sizes and half-stud steps keep every edge on the navmesh's half-stud grid.)
            const float w = wide ? 8.0f : (float)rng.pick(3, 6), dd = wide ? 8.0f : (float)rng.pick(3, 6);
            // Either a jump across a gap (level or down a bit), or a climb up onto a ledge right
            // next to this one: the moves the robot (and any character) can make.
            const bool climb = rng.chance(0.3f);
            const float rise = climb ? std::round(rng.range(1.5f, 2.75f) * 4) / 4 : std::round(rng.range(-1.5f, 0.5f) * 4) / 4;
            const float gap = climb ? 0.0f : std::round(rng.range(1.0f, 2.5f) * 2) / 2;
            const float pAlong = d.x != 0 ? p.r.w : p.r.d, nAlong = d.x != 0 ? w : dd;
            const float pAcross = d.x != 0 ? p.r.d : p.r.w, nAcross = d.x != 0 ? dd : w;
            const float slide = std::max(0.0f, std::min(pAcross, nAcross) / 2 - 1.5f);
            const float off = std::round(rng.range(-slide, slide) * 2) / 2;
            const float along = pAlong / 2 + gap + nAlong / 2;
            Rect r{p.r.x + d.x * along + (d.y != 0 ? off : 0), p.r.z + d.y * along + (d.x != 0 ? off : 0), w, dd};
            bool clash = false;
            for (size_t k = 0; k + 1 < plats.size() && !clash; ++k) if (r.overlaps(plats[k].r, 2.0f)) clash = true;
            if (clash) continue;
            plats.push_back({r, p.top + rise});
            dir = d;
            placed = true;
        }
        if (!placed) break;
    }
    L.placed = (int)plats.size() - 1;
    float low = 0, minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (size_t i = 0; i < plats.size(); ++i) {
        const Plat& p = plats[i];
        const float hue = (float)(i % 6) / 6.0f;
        glm::vec3 col = glm::vec3(0.5f) + 0.45f * glm::vec3(std::cos(6.283f * hue), std::cos(6.283f * (hue + 0.33f)), std::cos(6.283f * (hue + 0.66f)));
        if (i == 0) col = s.floorColor;
        L.pieces.push_back(piece(Piece::Platform, i == 0 ? "Start" : "Platform", {p.r.x, p.top - 0.5f, p.r.z}, {p.r.w, 1, p.r.d},
                                 col, Material::SmoothPlastic, (int)i));
        low = std::min(low, p.top);
        minX = std::min(minX, p.r.x - p.r.w / 2); maxX = std::max(maxX, p.r.x + p.r.w / 2);
        minZ = std::min(minZ, p.r.z - p.r.d / 2); maxZ = std::max(maxZ, p.r.z + p.r.d / 2);
        const bool last = i + 1 == plats.size() && i > 0;
        if (i > 0 && !last && i % 6 == 0)
            L.pieces.push_back(piece(Piece::Checkpoint, "Checkpoint", {p.r.x, p.top + 0.1f, p.r.z}, {3, 0.2f, 3}, {0.2f, 0.85f, 0.4f}, Material::Neon, (int)i));
        else if (s.coins && i > 0 && !last && i % 3 == 1)
            L.pieces.push_back(piece(Piece::Coin, "Coin", {p.r.x, p.top + 1.6f, p.r.z}, {0.9f, 0.12f, 0.9f}, {1.0f, 0.8f, 0.15f}, Material::Metal, (int)i));
    }
    L.spawn = {0, 0, 0};
    L.pieces.push_back(piece(Piece::Spawn, "SpawnLocation", {0, 0.1f, 0}, {4, 0.2f, 4}, glm::vec3(0.55f), Material::Plastic, 0));
    if (plats.size() > 1) {
        const Plat& g = plats.back();
        L.goal = {g.r.x, g.top, g.r.z};
        L.pieces.push_back(piece(Piece::Goal, "Goal", L.goal + glm::vec3(0, 0.1f, 0), {5, 0.2f, 5}, {0.2f, 0.9f, 0.35f}, Material::Neon,
                                 (int)plats.size() - 1));
    }
    // Lava far below: too far to plan a jump down to (and you don't come back up).
    const float m = 10.0f;
    L.pieces.push_back(piece(Piece::Lava, "Lava", {(minX + maxX) / 2, low - 14.0f, (minZ + maxZ) / 2},
                             {maxX - minX + 2 * m, 1, maxZ - minZ + 2 * m}, {1.0f, 0.3f, 0.05f}, Material::Neon));
}

const char* kGoalScript = R"(-- Goal: says who made it to the end.
local goal = script.Parent
local done = {}

goal.Touched:Connect(function(hit)
    local player = game.Players:GetPlayerFromCharacter(hit.Parent)
    if player and not done[player] then
        done[player] = true
        print(player.Name .. " reached the goal!")
    end
end)
)";

} // namespace

Level make(const Settings& in) {
    Level L;
    L.settings = in;
    Settings& s = L.settings;
    s.count = std::clamp(s.count, 2, 60);
    s.roomMin = std::clamp(s.roomMin, 12, 60);
    s.roomMax = std::clamp(s.roomMax, s.roomMin, 80);
    s.wallHeight = std::clamp(s.wallHeight, 4.0f, 30.0f);
    if (s.style == Style::Obby) obby(s, L);
    else dungeon(s, L);
    return L;
}

bool check(Level& L) {
    L.problems.clear();
    const bool obby = L.settings.style == Style::Obby;
    const int want = L.settings.count;
    if (L.placed < want)
        L.problems.push_back("Only " + std::to_string(L.placed) + " of " + std::to_string(want) + (obby ? " platforms" : " rooms") + " fitted.");
    bool goal = false;
    for (const Piece& p : L.pieces) goal |= p.kind == Piece::Goal;
    if (!goal) L.problems.push_back("There's no goal.");
    // Nothing inside anything else (touching is fine).
    int overlaps = 0;
    std::string first;
    for (size_t i = 0; i < L.pieces.size(); ++i)
        for (size_t j = i + 1; j < L.pieces.size(); ++j) {
            const Piece& a = L.pieces[i];
            const Piece& b = L.pieces[j];
            const glm::vec3 d = glm::abs(a.pos - b.pos) * 2.0f, s = a.size + b.size - glm::vec3(0.04f);
            if (d.x < s.x && d.y < s.y && d.z < s.z) {
                if (!overlaps++) first = a.name + " and " + b.name;
            }
        }
    if (overlaps)
        L.problems.push_back(std::to_string(overlaps) + " thing(s) are inside each other (like " + first + ").");
    return L.problems.empty();
}

SceneNode* build(Scene& scene, const Level& L, const glm::vec3& at, const std::string& name) {
    SceneNode* model = scene.insert(std::make_unique<SceneNode>(name, NodeKind::Model));
    Attribute seed{"Seed", Attribute::Number};
    seed.n = (double)L.settings.seed;
    Attribute style{"Style", Attribute::String};
    style.s = kStyleNames[(int)L.settings.style];
    model->attributes = {seed, style};
    for (const Piece& p : L.pieces) {
        SceneNode* n = nullptr;
        switch (p.kind) {
            case Piece::Spawn: n = buildPremade(scene, Premade::SpawnLocation, at + p.pos - glm::vec3(0, 0.1f, 0)); break;
            case Piece::Checkpoint: n = buildPremade(scene, Premade::Checkpoint, at + p.pos - glm::vec3(0, 0.1f, 0)); break;
            case Piece::Coin: n = buildPremade(scene, Premade::Coin, at + p.pos - glm::vec3(0, 1.2f, 0)); break;
            case Piece::Lava: n = buildPremade(scene, Premade::KillBrick, at); break;
            default: {
                const PrimitiveType cube = PrimitiveType::Cube;
                n = scene.addNode(p.name, cube, MeshLibrary::get(cube));
                break;
            }
        }
        if (!n) continue;
        if (p.kind != Piece::Coin) {
            n->transform.position = at + p.pos;
            n->transform.scale = p.size;
        }
        n->name = p.name;
        n->color = p.color;
        n->material = p.material;
        n->anchored = true;
        if (p.kind == Piece::Goal) {
            auto sc = std::make_unique<SceneNode>("Script", NodeKind::Script);
            sc->source = kGoalScript;
            scene.insert(std::move(sc), n);
        }
        scene.reparent(n, model);
    }
    return model;
}

Route walk(const Level& L) {
    Route out;
    Scene world;
    std::vector<SceneNode*> old;   // (a new Scene has a baseplate and a spawn: only the level, please)
    for (auto& c : world.root()->children) old.push_back(c.get());
    for (SceneNode* n : old) world.removeNode(n);
    build(world, L, glm::vec3(0.0f), "Level");
    Physics physics;
    physics.gather(world);
    physics.rebakeNavMesh();
    const NavMesh& nav = physics.navMesh();
    std::vector<NavMesh::Waypoint> wps;
    NavMesh::Agent agent;
    const NavMesh::Status st = nav.findPath(L.spawn + glm::vec3(0, 0.5f, 0), L.goal + glm::vec3(0, 0.5f, 0), agent, wps);
    out.status = NavMesh::statusName(st);
    out.ok = st == NavMesh::Status::Success;
    for (size_t i = 0; i < wps.size(); ++i) {
        if (i) out.length += glm::length(wps[i].pos - wps[i - 1].pos);
        if (wps[i].action == NavMesh::Action::Jump) ++out.jumps;
    }
    return out;
}

} // namespace LevelGen
