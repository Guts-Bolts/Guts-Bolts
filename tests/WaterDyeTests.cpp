// Colour hanging in the water (src/scene/WaterDye.h): blood that gets into a pool
// becomes a cloud that drifts, spreads and thins away.
//
//   WaterDyeTests
#include "scene/WaterDye.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
// All the colour in the pool (amount x area), added up from the points.
float sum(const WaterDye::Grid& g) {
    float t = 0.0f;
    for (const glm::vec4& c : g.c) t += c.a;
    return t * g.cell * g.cell;
}
// Where the middle of the cloud is.
glm::vec2 middle(const WaterDye::Grid& g) {
    glm::vec2 m(0.0f);
    float t = 0.0f;
    for (int k = 0; k < g.nz; ++k)
        for (int i = 0; i < g.nx; ++i) {
            const float a = g.c[g.idx(i, k)].a;
            m += glm::vec2(g.min.x + i * g.cell, g.min.y + k * g.cell) * a;
            t += a;
        }
    return t > 0.0f ? m / t : m;
}
} // namespace

int main() {
    using namespace WaterDye;
    const glm::vec3 blood(0.5f, 0.02f, 0.03f), oil(0.04f, 0.04f, 0.05f);

    // --- putting colour in ---
    {
        Grid g;
        resize(g, 40, 30, 0.5f, glm::vec2(-10.0f, 4.0f));
        check(!g.live && sum(g) == 0.0f, "a new pool is clean");
        check(add(g, 0.0f, 10.0f, blood, 1.0f, 2.0f), "colour goes in inside the pool");
        check(g.live && near(sum(g), 1.0f, 0.01f), "as much goes in as was asked for");
        check(!add(g, 40.0f, 10.0f, blood, 1.0f, 2.0f) && !add(g, 0.0f, -3.0f, blood, 1.0f, 2.0f), "nothing goes in outside the pool");
        check(!add(g, 0.0f, 10.0f, blood, 0.0f, 2.0f), "no amount is nothing");
        glm::vec3 col;
        const float here = at(g, 0.0f, 10.0f, &col);
        check(here > at(g, 0.8f, 10.0f) && at(g, 0.8f, 10.0f) > 0.0f && at(g, 3.0f, 10.0f) == 0.0f, "a round patch, thickest in the middle");
        check(near(col.r, blood.r, 0.01f) && near(col.g, blood.g, 0.01f), "it keeps its colour");
        // A finer grid holds the same amount.
        Grid fine;
        resize(fine, 80, 60, 0.25f, glm::vec2(-10.0f, 4.0f));
        add(fine, 0.0f, 10.0f, blood, 1.0f, 2.0f);
        check(near(sum(fine), sum(g), 0.02f), "the same amount on a finer grid");
        // Smaller than one point: still all goes in.
        Grid coarse;
        resize(coarse, 6, 6, 4.0f, glm::vec2(0.0f));
        add(coarse, 9.0f, 9.0f, blood, 2.0f, 0.5f);
        check(near(sum(coarse), 2.0f, 0.01f), "a tiny patch on a coarse grid isn't lost");
        // A point can only hold so much.
        for (int i = 0; i < 400; ++i) add(g, 0.0f, 10.0f, blood, 5.0f, 1.0f);
        check(at(g, 0.0f, 10.0f) <= kFull + 1e-3f, "a point never holds more than the most");
    }

    // --- two colours ---
    {
        Grid g;
        resize(g, 20, 20, 0.5f, glm::vec2(0.0f));
        add(g, 5.0f, 5.0f, blood, 1.0f, 2.0f);
        add(g, 5.0f, 5.0f, oil, 1.0f, 2.0f);
        glm::vec3 col;
        at(g, 5.0f, 5.0f, &col);
        check(near(col.r, (blood.r + oil.r) * 0.5f, 0.01f), "equal parts of two colours mix half and half");
    }

    // --- still water: it spreads and thins, and none is made ---
    {
        Grid g;
        resize(g, 48, 48, 0.5f, glm::vec2(0.0f));
        add(g, 12.0f, 12.0f, blood, 3.0f, 1.5f);
        const float before = sum(g), peak = at(g, 12.0f, 12.0f);
        const glm::vec2 m0 = middle(g);
        const int v0 = g.version;
        float last = before;
        bool neverMore = true;
        for (int i = 0; i < 300; ++i) {   // 5 seconds
            step(g, 1.0f / 60.0f, nullptr, glm::vec2(0.0f), nullptr);
            const float now = sum(g);
            if (now > before + 1e-3f) neverMore = false;   // (the slow swirls shuffle it about; they never add any)
            last = now;
        }
        check(neverMore, "colour is never made from nothing");
        check(at(g, 12.0f, 12.0f) < peak * 0.9f, "the middle of the cloud thins");
        check(at(g, 13.6f, 12.0f) > 0.0f, "it reaches further out than it started");
        check(glm::length(middle(g) - m0) < 1.0f, "in still water it stays about where it is (slow swirls only)");
        check(g.version > v0, "the picture is marked as changed");
        // By itself it halves in about kHalfLife.
        const float expect = before * std::exp(-5.0f * 0.6931f / kHalfLife);
        check(last < expect * 1.03f && last > expect * 0.8f, "it thins at the speed the half-life says");
        check(near(g.total, last, 0.01f), "the total is kept up to date");
        // Left alone long enough, the pool is clean again and costs nothing.
        for (int i = 0; i < 60 * 600 && g.live; ++i) step(g, 1.0f / 60.0f, nullptr, glm::vec2(0.0f), nullptr);
        check(!g.live && sum(g) == 0.0f, "in the end the pool is clean");
        const int v1 = g.version;
        step(g, 1.0f / 60.0f, nullptr, glm::vec2(0.0f), nullptr);
        check(g.version == v1, "a clean pool does no work");
    }

    // --- a current carries it ---
    {
        Grid g;
        resize(g, 80, 24, 0.5f, glm::vec2(0.0f));
        add(g, 6.0f, 6.0f, blood, 2.0f, 1.5f);
        const glm::vec2 m0 = middle(g);
        for (int i = 0; i < 120; ++i) step(g, 1.0f / 60.0f, nullptr, glm::vec2(3.0f, 0.0f), nullptr);   // 2 seconds at 3 studs / second
        const glm::vec2 m1 = middle(g);
        check(near(m1.x - m0.x, 6.0f, 0.5f) && std::fabs(m1.y - m0.y) < 0.1f, "the cloud drifts with the current");
        // A flow map does the same as one current everywhere.
        Grid h;
        resize(h, 80, 24, 0.5f, glm::vec2(0.0f));
        add(h, 6.0f, 6.0f, blood, 2.0f, 1.5f);
        std::vector<glm::vec2> flow((size_t)80 * 24, glm::vec2(3.0f, 0.0f));
        for (int i = 0; i < 120; ++i) step(h, 1.0f / 60.0f, flow.data(), glm::vec2(0.0f), nullptr);
        check(glm::length(middle(h) - m1) < 0.01f, "a flow map carries it the same way");
        // Out of the end of the pool, it's gone (the river took it).
        for (int i = 0; i < 60 * 30 && g.live; ++i) step(g, 1.0f / 60.0f, nullptr, glm::vec2(3.0f, 0.0f), nullptr);
        check(!g.live, "a river carries it away");
    }

    // --- splashing stirs it ---
    {
        Grid calm, rough;
        resize(calm, 48, 48, 0.5f, glm::vec2(0.0f));
        resize(rough, 48, 48, 0.5f, glm::vec2(0.0f));
        add(calm, 12.0f, 12.0f, blood, 3.0f, 1.5f);
        add(rough, 12.0f, 12.0f, blood, 3.0f, 1.5f);
        std::vector<float> churn((size_t)48 * 48, -1.5f);   // (moving down counts as much as up)
        for (int i = 0; i < 120; ++i) {
            step(calm, 1.0f / 60.0f, nullptr, glm::vec2(0.0f), nullptr);
            step(rough, 1.0f / 60.0f, nullptr, glm::vec2(0.0f), churn.data());
        }
        check(at(rough, 12.0f, 12.0f) < at(calm, 12.0f, 12.0f) * 0.8f, "churned-up water spreads the cloud faster");
        // A huge step or wild water never blows up.
        std::vector<float> wild((size_t)48 * 48, 500.0f);
        for (int i = 0; i < 50; ++i) step(rough, 5.0f, nullptr, glm::vec2(900.0f, -900.0f), wild.data());
        bool sane = true;
        for (const glm::vec4& c : rough.c)
            if (!(c.a >= 0.0f) || c.a > kFull + 1e-3f || !(c.r >= 0.0f)) sane = false;
        check(sane, "big steps and wild water stay sane");
    }

    // --- the picture for the graphics card ---
    {
        check(encode(0.0f) == 0 && encode(kFull) == 255 && encode(99.0f) == 255, "amounts fit a byte");
        check(near(decode(encode(1.0f)), 1.0f, 0.02f) && near(decode(encode(0.05f)), 0.05f, 0.01f), "thin clouds keep their detail");
        Grid g;
        resize(g, 16, 12, 1.0f, glm::vec2(0.0f));
        add(g, 8.0f, 6.0f, blood, 2.0f, 3.0f);
        const std::vector<uint8_t>& px = pixels(g);
        check(px.size() == (size_t)16 * 12 * 4, "four bytes a point");
        const size_t mid = g.idx(8, 6) * 4, corner = 0;
        check(px[mid + 3] > 0 && px[corner + 3] == 0, "amount where the cloud is, none in the corner");
        check(px[mid] == (uint8_t)std::lround(blood.r * 255.0f), "the colour is stored straight");
        check(px[corner] == px[mid] && px[corner + 1] == px[mid + 1], "empty points carry the cloud's colour (no dark edge)");
        const uint8_t* before = px.data();
        step(g, 1.0f / 60.0f, nullptr, glm::vec2(0.0f), nullptr);
        check(pixels(g).data() == before, "the picture reuses its list");
        clear(g);
        check(!g.live && pixels(g)[mid + 3] == 0, "clearing empties it");
    }

    std::cout << (failures ? "FAILED" : "All water dye tests passed") << "\n";
    return failures ? 1 : 0;
}
