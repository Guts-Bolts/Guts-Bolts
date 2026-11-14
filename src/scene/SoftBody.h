#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

#include "Physics.h"

class Scene;
class SceneNode;
struct EditMesh;

// Soft bodies: parts that squash, wobble and tear, like jelly or a lump of meat.
//
// A part with the attribute Soft = true is still an ordinary part to the rest of the
// game (it falls, stacks and gets pushed like a solid one), but its surface is alive:
// every corner of its shape is a little weight on a spring, joined to its neighbours,
// and the whole thing tries to keep its volume. Drop it and it flattens against the
// floor and bulges at the sides, then wobbles back. Hit it and it dents and ripples.
//
//   Softness (0..1, default 0.5): 0 is firm rubber, 1 is jelly.
//   TearAt   (number, default none): how far it can be stretched before it tears, as a
//            share of its own size (1.6 = 60% longer than it was). A tear is a real cut
//            (Slice.h): two soft halves, with meat and bone showing on a Flesh part.
//
// This is "XPBD" (extended position-based dynamics): each frame is cut into a few small
// steps, and in each one the corners are nudged until the springs, the volume and the
// things it touches all agree. It stays steady however soft the body is.
//
// A script hits or tugs one with part:Poke(point, velocity [, radius]). Every screen
// works the wobble out itself (nothing extra is sent online); only the host tears.
namespace SoftBody {

// Something a soft body bumps into: a box (`axes` = its turned and sized axes, half of it).
struct Collider { glm::vec3 center; glm::mat3 axes; };

// One soft body, as plain lists (a corner is the same number in each).
struct Body {
    // Corners. Corners of the shape that sit in the same place share one.
    std::vector<glm::vec3> rest;        // in the part's own space
    std::vector<glm::vec3> pos, prev;   // in the world
    std::vector<glm::vec3> vel;
    std::vector<uint32_t>  cornerOf;    // shape corner -> body corner
    // Springs along the edges.
    struct Edge { uint32_t a, b; float rest; };
    std::vector<Edge> edges;
    // Triangles (for the volume), wound outside out.
    std::vector<uint32_t> tris;
    float restVolume = 0.0f;            // in the world (0 = the shape isn't closed: no volume kept)
    // How it feels.
    float softness = 0.5f;
    // Where it's at.
    glm::mat4 frame{1.0f};              // where the part was last step
    float calm = 0.0f;                  // seconds it has been still
    bool  asleep = false;               // still, and its part hasn't moved: costs nothing
    float time = 0.0f;
    std::vector<glm::vec3> grad;        // scratch (kept, so a step never asks for memory)
};

// Make a body from a shape. `frame` = the part's place in the world.
void build(Body& body, const EditMesh& shape, const glm::mat4& frame, float softness);
// Move it on by dt seconds: `frame` is where the part is now, `gravity` in studs/s²
// (downwards). Returns false if it had nothing to do (asleep).
bool solve(Body& body, const glm::mat4& frame, float dt, float gravity, const Collider* colliders, size_t colliderCount);
// Give the corners within `radius` of `point` a shove (studs/s), fading with distance.
void poke(Body& body, const glm::vec3& point, const glm::vec3& velocity, float radius);

// Measures (for tests, and to decide when it tears).
float volume(const Body& body);                                  // now, in the world
float stretch(const Body& body, uint32_t* edge = nullptr);       // the most stretched edge: length / its own length
float offShape(const Body& body);
// How stretched it is for tearing: like stretch, but short edges count as a fair length.
float tearStretch(const Body& body, uint32_t* edge = nullptr);                                // how far (studs) the furthest corner is from where the part alone would put it

// A block with enough corners to bend (a plain block has only eight).
EditMesh softBox(int cuts = 4);

// --- parts in the world ---

// Does this part ask to be soft?
bool wants(const SceneNode& part);
// Every frame while playing. `knocks` = what hit what this step (hits make dents and
// ripples). Only the host tears things.
void update(Scene& scene, float dt, const std::vector<Physics::Knock>& knocks, bool host);
// Shove a soft part (part:Poke). False if it isn't soft.
bool poke(Scene& scene, SceneNode& part, const glm::vec3& point, const glm::vec3& velocity, float radius);
// The body of a part, if it has one yet (for tests).
const Body* bodyOf(const SceneNode& part);
// Forget one (it goes back to its own shape), or all of them (Play stopped).
void reset(SceneNode& part);
void clearAll();

constexpr int   kMaxCorners = 4000;   // bigger shapes stay solid
constexpr float kStill = 0.05f;       // slower than this (studs/s) counts as still

} // namespace SoftBody
