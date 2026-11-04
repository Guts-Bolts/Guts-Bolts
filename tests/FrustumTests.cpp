// Only drawing what the camera can see (src/renderer/Frustum.h): parts in front of
// the camera are kept, parts behind it or off to the side are skipped.
//
//   FrustumTests
#include "renderer/Camera.h"
#include "renderer/Frustum.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
} // namespace

int main() {
    Camera cam;
    cam.resize(1280, 720);
    cam.useCFrame = true;   // at (0, 5, 0), looking along -Z
    cam.cframe = glm::mat4(1.0f);
    cam.cframe[3] = glm::vec4(0, 5, 0, 1);
    const Frustum f(cam.projection() * cam.view());

    check(f.sees({0, 5, -20}, 1.0f), "a part straight ahead is drawn");
    check(!f.sees({0, 5, 20}, 1.0f), "a part behind the camera is skipped");
    check(!f.sees({200, 5, -10}, 1.0f), "a part far off to the right is skipped");
    check(!f.sees({0, 300, -10}, 1.0f), "a part high above the view is skipped");
    check(f.sees({0, 5, 3}, 5.0f), "a big part just behind the camera that reaches in front is drawn");
    check(f.sees({0, 5, -5000}, 1.0f), "far away is left to the render distance");

    cam.orthographic = true;
    const Frustum o(cam.projection() * cam.view());
    check(o.sees({0, 5, -20}, 1.0f), "orthographic: ahead is drawn");
    check(o.sees({0, 5, 20}, 1.0f), "orthographic: just behind is still drawn (its view reaches back)");
    check(!o.sees({500, 5, -20}, 1.0f), "orthographic: far off to the side is skipped");

    const Frustum broken(glm::mat4(0.0f));
    check(broken.sees({0, 0, 1000}, 0.0f), "a broken camera matrix draws everything");

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
