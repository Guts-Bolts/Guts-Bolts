#pragma once
// What the camera can see: the four sides and the near end of its view, as
// planes. Parts entirely outside one of them aren't drawn (they'd only cost time).
//
//   Frustum f(projection * view);
//   if (!f.sees(center, radius)) skip it;
//
// The far end isn't checked: render distance already handles things too far away.
#include <glm/glm.hpp>
#include <cmath>

struct Frustum {
    glm::vec4 planes[5];   // left, right, bottom, top, near; inside = dot >= 0
    bool      on = true;   // false: sees everything (a broken matrix)

    Frustum() { on = false; }
    explicit Frustum(const glm::mat4& viewProj) {
        // Gribb & Hartmann: each plane is a row of the matrix plus or minus the 4th row.
        auto row = [&](int r) { return glm::vec4(viewProj[0][r], viewProj[1][r], viewProj[2][r], viewProj[3][r]); };
        const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        planes[0] = r3 + r0;
        planes[1] = r3 - r0;
        planes[2] = r3 + r1;
        planes[3] = r3 - r1;
        planes[4] = r3 + r2;
        for (glm::vec4& p : planes) {
            const float len = glm::length(glm::vec3(p));
            if (!(len > 1e-8f) || !std::isfinite(len)) { on = false; return; }
            p /= len;
        }
    }

    // Could any of a ball (center, radius) be on screen?
    bool sees(const glm::vec3& c, float radius) const {
        if (!on) return true;
        for (const glm::vec4& p : planes)
            if (glm::dot(glm::vec3(p), c) + p.w < -radius) return false;
        return true;
    }
};
