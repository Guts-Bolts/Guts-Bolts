#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

class Physics;
class SceneNode;

// Small flying bits used for gore and effects: blood / oil drops that splat
// and stick where they land, meat chunks, bolts, sparks, fire and smoke.
// Runtime only — never saved, cleared when Play stops.
struct Particle {
    enum Kind { Drop, Chunk, Bolt, Spark, Fire, Smoke, Splat, Spray };   // Spray: water droplets
    Kind      kind = Drop;
    glm::vec3 pos{0.0f}, vel{0.0f};
    glm::vec3 color{1.0f};
    glm::vec3 size{0.1f};
    glm::vec3 rot{0.0f}, spin{0.0f};    // degrees, degrees / second
    float     life = 1.0f, maxLife = 1.0f;
    bool      glossy = false;           // oil / bolts shine like metal
    bool      wet = false;              // liquid (blood, oil): shiny and wet-looking
    // Liquid pools and splats (Splat):
    float     spread = 0.0f;            // how wide a pool grows to as it spreads out
    float     slide = 0.0f;             // seconds left running down a wall
    float     trail = 0.0f;             // (how far it's run since leaving the last drip mark)
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // the surface it's on
    // Blood that has landed:
    int       look = 0;                 // Splat shape: 0 pool / splash, 1 smear, 2 footprint
    float     age = 0.0f;               // seconds since it landed (it darkens and dries)
    float     seed = 0.0f;              // 0..1: makes every edge a different wobbly shape
    float     drips = 0.0f;             // pools: how much can still run off an edge
    float     dripTimer = 0.0f;
    // A stain on a body part (moves with it): which part, and where on it in its own space.
    uint64_t  onPart = 0;
    glm::vec3 localPos{0.0f}, localNormal{0.0f, 1.0f, 0.0f}, localTangent{1.0f, 0.0f, 0.0f};
    glm::vec3 tangent{0.0f};            // (stains: which way the part's side runs, so it turns with it)
    float     alpha = 1.0f;             // footprints and smears get fainter
    // Gibs (Chunk):
    int       gib = 0;                  // 0 meat, 1 bone
    glm::vec3 skin{-1.0f};              // a piece of a body part: its colour on one side (negative = plain meat)
    float     bleed = 0.0f;             // seconds it still drips blood
    float     gibSeed = 0.0f;           // 0..1: so no two look the same
};

enum class GoreKind { Blood, Oil };

// Bloody shoes: walk through a pool and the next steps leave red footprints,
// fainter each step. A Player and every NPC carry one.
struct BloodyFeet {
    float     wet = 0.0f;               // 1 = just stepped in a pool, 0 = clean
    float     walked = 0.0f;            // since the last footprint
    glm::vec3 last{0.0f};
    bool      left = false;
    bool      started = false;
    void reset() { wet = walked = 0.0f; started = false; }
};

// A particle from a ParticleEmitter, Fire, Smoke or Sparkles object: a flat picture
// that always faces the camera. runEmitters() moves them and works out how each looks
// right now (from its emitter's settings), so drawing them is just drawing.
struct Sprite {
    uint64_t  owner = 0;                // the emitter it came from
    glm::vec3 pos{0.0f}, vel{0.0f};     // world space (LockedToPart: `local` is where it really is)
    glm::vec3 local{0.0f};              // LockedToPart: its place in the emitter's own space
    float     age = 0.0f, life = 1.0f;
    float     rot = 0.0f, spin = 0.0f;  // degrees, degrees / second
    float     seed = 0.0f;              // 0..1, so they don't all look the same
    float     scale = 1.0f;             // Fire / Smoke / Sparkles: its own size
    // How it looks now:
    glm::vec3 color{1.0f};
    float     alpha = 1.0f;
    float     size = 1.0f;              // studs across
    float     glow = 0.0f;              // LightEmission
    int       look = 0;                 // built-in picture (kParticleLooks)
    int       tex = -1;                 // or a picture: an index into ParticleSystem::spriteTextures()
};

class Scene;

// A ParticleEmitter's Texture as given (a script, an imported Roblox file): Roblox's own
// pictures ("rbxasset://textures/particles/fire_main.dds", "rbxassetid://...") can't be
// loaded here, so they become the nearest built-in look; anything else stays as it is.
std::string particleTexture(const std::string& texture);

class ParticleSystem {
public:
    void update(float dt, float gravity, const Physics& physics);
    // The game's blood (Game Settings > Damage & Blood).
    void setBlood(const glm::vec3& color, float amount, float stay) { m_bloodColor = color; m_bloodAmount = amount; m_bloodStay = stay; }
    void clear() { m_items.clear(); m_sprites.clear(); m_spawnDebt.clear(); }
    const std::vector<Particle>& items() const { return m_items; }

    // --- ParticleEmitter, Fire, Smoke, Sparkles --------------------------------
    // Makes new particles from every switched-on emitter in the scene (Rate, Emit(n)),
    // moves them, and drops ones whose emitter is gone (or was cleared). Each computer
    // runs this itself: particles are only for looks and never sent.
    void runEmitters(Scene& scene, float dt);
    const std::vector<Sprite>& sprites() const { return m_sprites; }
    const std::vector<std::string>& spriteTextures() const { return m_spriteTex; }
    int spriteCount(uint64_t owner) const;   // (for tests)

    // --- Effects --------------------------------------------------------------
    // A burst of drops flying out from `pos` (direction biased by `dir`).
    void spray(GoreKind kind, const glm::vec3& pos, const glm::vec3& dir, int count, float speed);
    // Chunks of meat / bolts and metal scraps.
    void gibs(GoreKind kind, const glm::vec3& pos, const glm::vec3& vel, int count);
    // A body part bursting into pieces: chunks from all through it, with its colour on
    // the outside and meat (or metal) inside, a few bits of bone, and a lot of blood.
    void burst(GoreKind kind, const SceneNode* part, const glm::vec3& vel, float violence);
    void sparks(const glm::vec3& pos, int count);
    // An arterial spurt: a tight jet of drops (one heartbeat). `inherit` = how the wound is moving.
    void spurt(GoreKind kind, const glm::vec3& pos, const glm::vec3& dir, const glm::vec3& inherit, float strength);
    // A slow drip from a wound (between heartbeats, or after the heart stops).
    void ooze(GoreKind kind, const glm::vec3& pos, const glm::vec3& inherit);
    // Something bloody sliding along a surface: a streak in the way it's moving.
    void smear(GoreKind kind, const glm::vec3& pos, const glm::vec3& vel, const glm::vec3& normal, float width, float wet);
    // How much wet (not yet dried) blood is on the floor at `pos` (0 none, 1 a puddle).
    // Taking some (`take` > 0) shrinks the pool a little, like a foot soaking it up.
    float wetBloodAt(const glm::vec3& pos, float radius, float take = 0.0f);
    // A footprint on the floor, its toe pointing along `dir`.
    void footprint(GoreKind kind, const glm::vec3& pos, const glm::vec3& dir, float wet);
    // Walking with `feet`: soak up blood from pools and leave footprints. `at` = the feet.
    void walk(BloodyFeet& feet, GoreKind kind, const glm::vec3& at, bool onGround);
    // Blood soaking into a body part where it was hurt (`at` is a world point near it).
    void stain(GoreKind kind, const SceneNode* part, const glm::vec3& at, float size);
    // Stains ride along with their parts (and go when the part does). Call once a frame.
    void follow(Scene& scene);
    // Wash a character clean (respawning): drop every stain on parts under `root`.
    void wipe(Scene& scene, const SceneNode* root);
    float dryTime() const { return std::max(8.0f, m_bloodStay * 0.6f); }
    void explosion(const glm::vec3& pos, float radius);
    // Water thrown up by a splash (droplets that vanish when they land).
    void waterSpray(const glm::vec3& pos, int count, float speed, const glm::vec3& color, float radius = 0.3f);   // radius: a ring around what fell in

    static constexpr int kMaxParticles = 2500;
    static constexpr int kMaxSplats    = 700;

private:
    void add(const Particle& p);
    // A chunk flying, tumbling, bouncing off things and sliding to a stop (bleeding as it goes).
    void stepGib(Particle& p, float dt, float gravity, const Physics& physics, std::vector<Particle>& born);
    std::vector<Particle> m_items;
    std::vector<Sprite>   m_sprites;
    std::vector<std::string> m_spriteTex;
    std::unordered_map<uint64_t, float> m_spawnDebt;   // emitter -> part of a particle owed (Rate * dt adds up)
    glm::vec3 m_bloodColor{0.50f, 0.02f, 0.03f};
    float     m_bloodAmount = 1.0f, m_bloodStay = 30.0f;
    int m_splats = 0;
    float m_splatSound = 0.0f;
};
