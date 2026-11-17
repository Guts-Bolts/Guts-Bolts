#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <glm/glm.hpp>

class Physics;

// The navigation mesh: every floor a character can stand on, worked out ahead of
// time ("baked") so finding a route is quick. PathfindingService uses it.
//
// How it's baked: the world is cut into thin square columns (Settings::cell wide).
// In each column, the solid parts make stacks of "solid" and "empty" pieces; the
// top of a solid piece with enough empty room above it is a floor ("span"). A span
// is joined to the span next door when you can step between them. Ledges you can
// jump up to, drops you can walk off and gaps you can jump across get "links".
// Each span also remembers how far it is from the nearest wall and the nearest
// edge, so one bake works for skinny and chunky characters alike.
class NavMesh {
public:
    struct Settings {
        float cell        = 0.5f;    // column width (made bigger for huge worlds)
        float maxClimb    = 0.75f;   // a step you just walk up (Physics::kStepHeight)
        float maxSlope    = 55.0f;   // steeper tilted parts are walls (degrees)
        float jumpHeight  = 3.2f;    // ledges up to this high get jump links
        float jumpGap     = 3.0f;    // gaps up to this wide get jump links
        float maxDrop     = 7.0f;    // won't plan a walk off anything higher
        float bodyHeight  = 2.65f;   // the usual character height (walls vs ledges)
    };

    // What a path asks for (PathfindingService:CreatePath's table).
    struct Agent {
        float radius   = 0.6f;       // AgentRadius: stays this far from walls
        float height   = 2.7f;       // AgentHeight: needs this much room above the floor
        bool  canJump  = true;       // AgentCanJump
        float spacing  = 2.0f;       // WaypointSpacing: a waypoint at least this often
        std::map<std::string, float> costs;   // Costs: material names and labels -> how much worse (huge = never)
    };

    enum class Action { Walk, Jump };
    struct Waypoint {
        glm::vec3   pos;
        Action      action = Action::Walk;   // Jump: jump on the way to this point
        std::string label;                   // the PathfindingLabel / material it's on
    };
    enum class Status { Success, ClosestNoPath, NoPath, FailStartNotEmpty, FailFinishNotEmpty };
    static const char* statusName(Status s);

    // Bake from the parts the physics world has right now.
    void bake(const Physics& physics, const Settings& settings);
    bool baked() const { return m_baked; }
    uint32_t version() const { return m_version; }   // goes up on every bake
    double bakeMs() const { return m_bakeMs; }
    size_t spanCount() const { return m_spans.size(); }
    const Settings& settings() const { return m_set; }

    // A route from `start` to `goal` (feet positions). The first waypoint is the start.
    Status findPath(const glm::vec3& start, const glm::vec3& goal, const Agent& agent,
                    std::vector<Waypoint>& out) const;
    // The nearest point on the mesh within `range` (false if none).
    bool closestPoint(const glm::vec3& p, const Agent& agent, float range, glm::vec3& out) const;
    // Can someone stand here?
    bool walkable(const glm::vec3& p, const Agent& agent) const;
    // Can you walk in a straight line from a to b (no jumps)? `hit` = where it stops.
    bool straightWalk(const glm::vec3& a, const glm::vec3& b, const Agent& agent, glm::vec3* hit = nullptr) const;
    // A random standing spot (within `radius` of `around` if radius > 0), reachable from it.
    bool randomPoint(const glm::vec3& around, float radius, const Agent& agent, uint32_t seed, glm::vec3& out) const;

    // For drawing it (Studio's "Navmesh" view): walkable floor as triangles, and the
    // jump links as lines. 4 floats per colour: r, g, b, a.
    struct DrawVertex { glm::vec3 pos; glm::vec4 color; };
    void buildDrawing(std::vector<DrawVertex>& tris, std::vector<DrawVertex>& lines,
                      const Agent& agent) const;

private:
    struct Span {
        float    floor, ceil;            // standing height, and where the next solid thing starts above
        int32_t  col;                     // which column
        int32_t  next[4];                 // the joined span next door (+x, +z, -x, -z), -1 none
        uint16_t wallDist = 0;            // columns to the nearest wall
        uint16_t edgeDist = 0;            // columns to the nearest edge (drop-off)
        uint16_t area = 0;                // index into m_areas (label or material name)
        uint32_t firstLink = 0, linkCount = 0;
    };
    struct Link { int32_t to; bool jump; float length; };
    struct Solid { float lo, hi; };

    int  spanAt(const glm::vec3& p, const Agent& agent, float range) const;
    bool fits(const Span& s, const Agent& agent) const;
    float areaCost(const Span& s, const Agent& agent) const;
    glm::vec3 center(const Span& s) const;
    int  stepToward(int from, int dx, int dz, const Agent& agent) const;
    bool lineClear(int a, int b, const Agent& agent) const;
    bool blockedAt(int col, float lo, float hi) const;   // anything solid in this column between lo and hi?

    Settings m_set;
    bool     m_baked = false;
    uint32_t m_version = 0;
    double   m_bakeMs = 0.0;
    glm::vec3 m_origin{0.0f};           // corner of column (0, 0)
    int      m_w = 0, m_h = 0;           // columns across x and z
    std::vector<uint32_t> m_colStart;   // spans of column c: m_colStart[c] .. m_colStart[c + 1]
    std::vector<Span>     m_spans;
    std::vector<Link>     m_links;
    std::vector<uint32_t> m_solidStart; // solid pieces of each column (for wall / room checks)
    std::vector<Solid>    m_solids;
    std::vector<std::string> m_areas;   // [0] = "" (plain floor)
};
