#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Physics;

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
};

enum class GoreKind { Blood, Oil };

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
    void sparks(const glm::vec3& pos, int count);
    void explosion(const glm::vec3& pos, float radius);
    // Water thrown up by a splash (droplets that vanish when they land).
    void waterSpray(const glm::vec3& pos, int count, float speed, const glm::vec3& color, float radius = 0.3f);   // radius: a ring around what fell in

    static constexpr int kMaxParticles = 2500;
    static constexpr int kMaxSplats    = 700;

private:
    void add(const Particle& p);
    std::vector<Particle> m_items;
    std::vector<Sprite>   m_sprites;
    std::vector<std::string> m_spriteTex;
    std::unordered_map<uint64_t, float> m_spawnDebt;   // emitter -> part of a particle owed (Rate * dt adds up)
    glm::vec3 m_bloodColor{0.50f, 0.02f, 0.03f};
    float     m_bloodAmount = 1.0f, m_bloodStay = 30.0f;
    int m_splats = 0;
    float m_splatSound = 0.0f;
};
