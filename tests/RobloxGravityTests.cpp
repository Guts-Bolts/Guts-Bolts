// Gravity: new games fall a bit lighter than Roblox (60), and games saved
// with an older default (22 before 0.7.10, Roblox's 98.1 until Softer Landing) are scaled to
// it when they load.
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

// An older save: what saveScene writes, with an older gravity version (1 = none written).
std::string oldSave(float gravity, float fallDamageSpeed, bool writeGravity = true, int version = 1) {
    Scene s;
    s.world().gravity = gravity;
    s.world().fallDamageSpeed = fallDamageSpeed;
    std::string text = Serializer::saveScene(s);
    auto cut = [&](const std::string& what) {
        const size_t at = text.find(what);
        if (at != std::string::npos) text.erase(at, what.size());
    };
    cut("\"gravityVersion\":3,");
    if (version > 1) text.insert(text.find("\"world\":{") + 9, "\"gravityVersion\":" + std::to_string(version) + ",");
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
        check(near(s.world().gravity, 60.0f), "new games use a bit lighter than Roblox gravity (60)");
        // A default jump (3.6 units high) lasts about 0.7 seconds.
        Humanoid h;
        const float v = h.launchSpeed(s.world().gravity);
        const float airTime = 2.0f * v / s.world().gravity;
        check(airTime > 0.6f && airTime < 0.8f, "a jump lasts about 0.7 seconds (" + std::to_string(airTime) + "s)");
        // The same height of fall still hurts as before (about 9 units).
        const float safeDrop = s.world().fallDamageSpeed * s.world().fallDamageSpeed / (2.0f * s.world().gravity);
        check(safeDrop > 8.5f && safeDrop < 9.6f, "fall damage starts at about a 9 unit drop");
        check(v < s.world().fallDamageSpeed, "landing from a normal jump doesn't hurt");
        check(near(s.world().launchScale(), std::sqrt(60.0f / 22.0f), 0.01f), "launchScale is how much harder to throw things");
        check(near(s.world().fluidGravity(), 22.0f), "water and blood keep real-world gravity");
        Humanoid p; p.useJumpPower = true;
        check(std::abs(p.launchSpeed(s.world().gravity) - v) < 0.3f, "the default JumpPower jumps as high as the default JumpHeight");
    }
    // A game saved with the old gravity: scaled up when it loads.
    {
        Scene s;
        check(Serializer::loadScene(s, oldSave(22.0f, 20.0f)), "an old save loads");
        check(near(s.world().gravity, 60.0f), "old default gravity 22 becomes 60");
        check(near(s.world().fallDamageSpeed, 20.0f * std::sqrt(60.0f / 22.0f)), "its fall damage speed scales with it");
    }
    {
        Scene s;
        Serializer::loadScene(s, oldSave(5.5f, 20.0f));
        check(near(s.world().gravity, 5.5f * 60.0f / 22.0f), "a moon game stays just as light compared");
    }
    // A game saved with Roblox gravity (0.7.10 until Softer Landing): brought down to the new one.
    {
        Scene s;
        check(Serializer::loadScene(s, oldSave(98.1f, 42.0f, true, 2)), "a Roblox-gravity save loads");
        check(near(s.world().gravity, 60.0f), "Roblox gravity 98.1 becomes 60");
        check(near(s.world().fallDamageSpeed, 42.0f * std::sqrt(60.0f / 98.1f)), "its fall damage speed comes down with it");
        const float safeDrop = s.world().fallDamageSpeed * s.world().fallDamageSpeed / (2.0f * s.world().gravity);
        check(safeDrop > 8.5f && safeDrop < 9.6f, "the same 9 unit drop still starts to hurt");
    }
    {
        Scene s;
        Serializer::loadScene(s, oldSave(24.5f, 42.0f, true, 2));
        check(near(s.world().gravity, 24.5f * 60.0f / 98.1f), "a low-gravity Roblox-era game stays just as light compared");
    }
    {
        Scene s;   // (the sample games: no gravity written down, just a fall damage speed)
        check(oldSave(22.0f, 20.0f, false).find("\"gravity\"") == std::string::npos, "(test: the gravity was left out)");
        Serializer::loadScene(s, oldSave(22.0f, 20.0f, false));
        check(near(s.world().gravity, 60.0f) && near(s.world().fallDamageSpeed, 33.0f, 0.2f), "old saves without a gravity get the new one");
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
