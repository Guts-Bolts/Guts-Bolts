// Roblox-strength gravity: new games fall like Roblox (196.2 studs/s² at our half
// size), and games saved with the old gravity are scaled up when they load.
#include "scene/Scene.h"
#include "scene/Serializer.h"
#include "renderer/Mesh.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool near(float a, float b, float eps = 0.05f) { return std::abs(a - b) < eps; }

// A save from before 0.7.10: what saveScene writes, without the gravity version.
std::string oldSave(float gravity, float fallDamageSpeed, bool writeGravity = true) {
    Scene s;
    s.world().gravity = gravity;
    s.world().fallDamageSpeed = fallDamageSpeed;
    std::string text = Serializer::saveScene(s);
    auto cut = [&](const std::string& what) {
        const size_t at = text.find(what);
        if (at != std::string::npos) text.erase(at, what.size());
    };
    cut("\"gravityVersion\":2,");
    if (!writeGravity) {   // (hand-made sample games don't write a gravity down)
        const size_t at = text.find("\"gravity\":");
        if (at != std::string::npos) text.erase(at, text.find(',', at) + 1 - at);
    }
    return text;
}
} // namespace

int main() {
    Mesh::headless = true;   // (no window: parts don't need their meshes on a graphics card)
    // A brand new game.
    {
        Scene s;
        check(near(s.world().gravity, 98.1f), "new games use Roblox-strength gravity (98.1)");
        // A default jump (3.6 units high) is over in about half a second, like Roblox.
        Humanoid h;
        const float v = h.launchSpeed(s.world().gravity);
        const float airTime = 2.0f * v / s.world().gravity;
        check(airTime > 0.45f && airTime < 0.62f, "a jump lasts about half a second (" + std::to_string(airTime) + "s)");
        // The same height of fall still hurts as before (about 9 units).
        const float safeDrop = s.world().fallDamageSpeed * s.world().fallDamageSpeed / (2.0f * s.world().gravity);
        check(safeDrop > 8.5f && safeDrop < 9.6f, "fall damage starts at about a 9 unit drop");
        check(v < s.world().fallDamageSpeed, "landing from a normal jump doesn't hurt");
        check(near(s.world().launchScale(), std::sqrt(98.1f / 22.0f), 0.01f), "launchScale is how much harder to throw things");
        check(near(s.world().fluidGravity(), 22.0f), "water and blood keep real-world gravity");
    }
    // A game saved with the old gravity: scaled up when it loads.
    {
        Scene s;
        check(Serializer::loadScene(s, oldSave(22.0f, 20.0f)), "an old save loads");
        check(near(s.world().gravity, 98.1f), "old default gravity 22 becomes 98.1");
        check(near(s.world().fallDamageSpeed, 20.0f * std::sqrt(98.1f / 22.0f)), "its fall damage speed scales with it");
    }
    {
        Scene s;
        Serializer::loadScene(s, oldSave(5.5f, 20.0f));
        check(near(s.world().gravity, 5.5f * 98.1f / 22.0f), "a moon game stays just as light compared");
    }
    {
        Scene s;   // (the sample games: no gravity written down, just a fall damage speed)
        check(oldSave(22.0f, 20.0f, false).find("\"gravity\"") == std::string::npos, "(test: the gravity was left out)");
        Serializer::loadScene(s, oldSave(22.0f, 20.0f, false));
        check(near(s.world().gravity, 98.1f) && near(s.world().fallDamageSpeed, 42.2f, 0.2f), "old saves without a gravity get the new one");
    }
    // A new save loads back exactly (no scaling twice).
    {
        Scene s;
        s.world().gravity = 150.0f;
        s.world().fallDamageSpeed = 50.0f;
        Scene again;
        check(Serializer::loadScene(again, Serializer::saveScene(s)), "a new save loads");
        check(near(again.world().gravity, 150.0f) && near(again.world().fallDamageSpeed, 50.0f), "a new save isn't scaled again");
    }
    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
