#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

// Colour in the water: blood (or oil, or any coloured liquid) that gets into a pool
// doesn't vanish, it hangs there as a cloud. The cloud drifts with the current,
// spreads out, gets stirred by splashes and slowly thins away to nothing.
//
// One grid per pool, seen from above (the same points as the pool's ripples). Each
// point keeps how much colour is there and what colour it is. No OpenGL in here: the
// renderer uploads pixels() as a picture and the water shader tints what's behind it.
//
// Nothing is allocated after resize(): add() and step() only touch the lists made then.
namespace WaterDye {

constexpr float kFull    = 4.0f;     // the most colour one point holds (the picture's 255)
constexpr float kHalfLife = 26.0f;   // seconds for a cloud to thin to half by itself
constexpr float kSpread  = 0.16f;    // how fast it spreads in still water (studs² / second)
constexpr float kStir    = 0.9f;     // ... and how much faster where the surface is moving
constexpr float kSwirl   = 0.3f;     // how fast the slow swirls in still water turn (studs / second)
constexpr float kGone    = 0.004f;   // when the thickest part is thinner than this, the pool is clean

struct Grid {
    int   nx = 0, nz = 0;
    float cell = 1.0f;
    glm::vec2 min{0.0f};                 // x, z of point (0, 0)
    // (r, g, b) x amount, amount. Keeping the colour multiplied means two clouds of
    // different colours mix the right way when they meet.
    std::vector<glm::vec4> c, scratch;
    std::vector<uint8_t> px;             // the picture: straight colour, amount (see pixels())
    float clock = 0.0f;                  // (the slow swirls move with it)
    float total = 0.0f;                  // all the colour in the pool (amount x area)
    int   version = 0;                   // goes up when the picture changes
    bool  live = false;                  // is there any colour in it?
    size_t idx(int i, int k) const { return (size_t)k * nx + i; }
};

// A pool's grid (nx x nz points, `cell` apart, point (0, 0) at min). Clears it.
void resize(Grid& g, int nx, int nz, float cell, const glm::vec2& min);
void clear(Grid& g);

// Put colour in at (x, z): `amount` is how much (1 = enough to hide one square stud),
// spread over a round patch `radius` across. False if it's outside the pool.
bool add(Grid& g, float x, float z, const glm::vec3& color, float amount, float radius);

// One step. `flow` is the current at each point (x, z speeds, studs / second) or null
// for `base` everywhere; `churn` is how fast the surface is moving up or down at each point
// (or null): splashes and wakes stir the cloud. Does nothing while the pool is clean.
void step(Grid& g, float dt, const glm::vec2* flow, const glm::vec2& base, const float* churn);

// How much colour is at (x, z), and its colour (bilinear). 0 outside.
float at(const Grid& g, float x, float z, glm::vec3* color = nullptr);

// How an amount is kept in a byte: a square root, so thin clouds keep their detail.
uint8_t encode(float amount);
float   decode(uint8_t byte);
// The picture for the graphics card, 4 bytes a point: the colour and encode(amount).
// Points with no colour get the pool's average colour, so the edges of a cloud don't
// come out dark when the graphics card blends between points.
const std::vector<uint8_t>& pixels(Grid& g);

} // namespace WaterDye
