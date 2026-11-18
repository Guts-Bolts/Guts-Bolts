#pragma once
// Terrain: hills, valleys, beaches and mountains you sculpt with brushes in Studio
// (the TERRAIN tab), like Roblox's Terrain editor. It's a height map: a square grid
// of points around the middle of the world, each with a height and a material
// (grass, sand, rock...). Everything below the surface is solid ground.
//
// Characters walk on it, loose parts land and roll on it, rays hit it, it casts
// shadows, it's saved with the place, sent to players when they join, and scripts
// can change it while the game runs (workspace.Terrain in Lua).
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

enum class TerrainMaterial : uint8_t { Grass, Dirt, Sand, Rock, Snow, Mud };
inline constexpr int kTerrainMaterialCount = 6;
inline const char* const kTerrainMaterialNames[kTerrainMaterialCount] =
    {"Grass", "Dirt", "Sand", "Rock", "Snow", "Mud"};

// The kinds of land Generate can make (TerrainShape.cpp).
//  Hills      rolling green hills (the classic one)
//  Mountains  sharp ridges and peaks with snow on top, carved by rain into valleys
//  Islands    islands in a sea (the ground goes under y = 0: add water at 0), with beaches
//  Canyons    flat-topped mesas and deep, winding canyons in layered rock and sand
//  Dunes      a desert of long sand dunes
//  Plains     wide, nearly flat grassland with gentle swells and muddy hollows
enum class TerrainStyle { Hills, Mountains, Islands, Canyons, Dunes, Plains };
inline constexpr int kTerrainStyleCount = 6;
inline const char* const kTerrainStyleNames[kTerrainStyleCount] =
    {"Hills", "Mountains", "Islands", "Canyons", "Dunes", "Plains"};

class Terrain {
public:
    // What a brush does where you drag it.
    // Erode: rain running over the ground where you drag, washing soil down into gullies.
    enum class Brush { Raise, Lower, Smooth, Flatten, Paint, Erode };
    static constexpr int kBrushCount = 6;

    static constexpr int   kMinCells    = 16;
    static constexpr int   kMaxCells    = 256;    // 256 x 4 studs = 1,024 studs across
    static constexpr float kDefaultCell = 4.0f;   // studs between grid points
    static constexpr float kMinHeight   = -500.0f, kMaxHeight = 1000.0f;

    bool  empty() const { return m_cells == 0; }
    int   cells() const { return m_cells; }           // grid squares along each side
    float cellSize() const { return m_cell; }
    float halfWidth() const { return m_cells * m_cell * 0.5f; }   // it spans -halfWidth..+halfWidth on x and z
    // Goes up every time the terrain changes (the renderer, the network and Studio's
    // undo use it to notice).
    uint64_t version() const { return m_version; }

    // Start over: a flat square of `cells` x `cells` squares at `height`.
    void create(int cells, float cellSize, float height, TerrainMaterial m);
    void clear();   // no terrain at all
    // Become an exact copy of `other` (every height to the last bit, not rounded like a
    // saved file). Studio's Stop uses it to put the terrain back as it was before Play.
    // The version still goes up, so the renderer rebuilds it.
    void copyFrom(const Terrain& other);
    // True when both have the very same grid, heights and grounds.
    bool sameAs(const Terrain& other) const;
    // Rolling hills (the Generate button). `hills` 0..1 = how bumpy, `seed` = which
    // hills. Steep slopes come out rock, high tops snow and low ground sand.
    void generate(int cells, float cellSize, uint32_t seed, float hills);
    // Any kind of land (Hills is the same as generate()). `hills` 0..1 = how big.
    void generateStyle(TerrainStyle style, int cells, float cellSize, uint32_t seed, float hills);

    // Weathering, like years of real rain (TerrainShape.cpp). Thousands of raindrops run
    // downhill, picking up soil where they speed up and dropping it where they slow down:
    // that carves gullies and valleys down slopes and leaves flat, fanned-out ground at
    // the bottom. Then loose ground on slopes steeper than it can stand slides down.
    // `amount` 0..1 = how long it rains. Only inside the circle when `radius` > 0.
    void erode(float amount, uint32_t seed = 1, glm::vec2 center = glm::vec2(0.0f), float radius = 0.0f);
    // Paint the ground by its shape: rock on cliffs, snow on high peaks, sand by the sea
    // (y = 0) and in the lowest dry places, mud in wet hollows, dirt on slopes, grass
    // elsewhere. Keeps nothing that was painted before.
    void autoPaint(uint32_t seed = 7);

    // --- Reading it ---
    // The ground height at (x, z). False when that's outside the terrain.
    bool  heightAt(float x, float z, float& h) const;
    glm::vec3 normalAt(float x, float z) const;        // which way the ground faces there
    TerrainMaterial materialAt(float x, float z) const;
    bool  solidAt(const glm::vec3& p) const;           // under the ground?
    // First place a ray hits the ground (distance along `dir`, which is normalised).
    bool  raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist, float& t,
                  glm::vec3* normal = nullptr) const;
    // The tallest ground under a footprint (a box from min to max on x and z).
    bool  highestUnder(float x0, float z0, float x1, float z1, float& h) const;
    float friction(TerrainMaterial m) const;

    // --- Changing it ---
    // One dab of a brush at `center` (only x and z matter, except Flatten, which
    // levels to center.y). Raise / Lower: `strength` = studs up or down in the middle.
    // Smooth / Flatten: `strength` 0..1 = how much of the way to go. They fade out
    // towards the edge of the circle; Paint covers the whole circle.
    void brush(Brush b, const glm::vec3& center, float radius, float strength, TerrainMaterial m);
    // Fill a box with ground (raises the surface to its top), or dig it out
    // (lowers the surface to its bottom). Like Roblox's FillBlock for a height map.
    void fillBox(const glm::vec3& min, const glm::vec3& max, TerrainMaterial m);
    void digBox(const glm::vec3& min, const glm::vec3& max);
    // The same with a ball: fill raises the ground into a dome, dig scoops a bowl.
    void fillBall(const glm::vec3& center, float radius, TerrainMaterial m);
    void digBall(const glm::vec3& center, float radius);
    // A terrain material from its name ("Grass", or Roblox ones like "Ground" or
    // "Basalt"). False if it isn't one.
    static bool parseMaterial(const std::string& name, TerrainMaterial& out);
    void setHeight(int i, int k, float h);
    void setMaterial(int i, int k, TerrainMaterial m);

    // --- Grid access (for the renderer) ---
    int   points() const { return m_cells + 1; }       // grid points along each side
    float height(int i, int k) const { return m_h[(size_t)k * points() + i]; }
    TerrainMaterial material(int i, int k) const { return (TerrainMaterial)m_mat[(size_t)k * points() + i]; }
    glm::vec3 pointPos(int i, int k) const;
    glm::vec3 pointNormal(int i, int k) const;

    // --- Saving ---
    // A compact JSON object (heights and materials packed as base64). Empty
    // terrain saves as null.
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    void touched();
    bool cellAt(float x, float z, int& i, int& k, float& fx, float& fz) const;

    int   m_cells = 0;
    float m_cell  = kDefaultCell;
    std::vector<float>   m_h;     // points() x points(), row by row (z), then x
    std::vector<uint8_t> m_mat;
    float m_lo = 0.0f, m_hi = 0.0f;   // the lowest and highest point (for quick ray tests)
    uint64_t m_version = 1;
};
