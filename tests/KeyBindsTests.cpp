// Your own keys (core/KeyBinds.h): moving an action to another key, swapping when two
// would share one, keys the game keeps for itself, AZERTY, and saving in settings.json.
//
//   KeyBindsTests
#include "core/KeyBinds.h"
#include "core/Settings.h"

#include <nlohmann/json.hpp>
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
    using namespace KeyBinds;
    reset();
    check(key(Forward) == ImGuiKey_W && key(Jump) == ImGuiKey_Space && key(Emotes) == ImGuiKey_B, "the usual keys to start with");
    check(!changed(), "...so nothing is changed");

    set(Jump, ImGuiKey_F);
    check(key(Jump) == ImGuiKey_F && changed(), "jump moves to F");
    set(Emotes, ImGuiKey_F);
    check(key(Emotes) == ImGuiKey_F && key(Jump) == ImGuiKey_B, "giving F to the emote menu swaps jump onto B");
    set(Jump, ImGuiKey_Space);
    check(key(Jump) == ImGuiKey_Space && GraphicsSettings::get().keys[Jump] == 0, "back on the usual key it isn't stored");

    set(Forward, ImGuiKey_Escape);
    set(Forward, ImGuiKey_Slash);
    set(Forward, ImGuiKey_3);
    set(Forward, ImGuiKey_MouseLeft);
    check(key(Forward) == ImGuiKey_W, "Esc, /, hotbar numbers and the mouse can't be used");
    check(reserved(ImGuiKey_Enter) && reserved(ImGuiKey_0) && !reserved(ImGuiKey_F), "reserved() says which keys are kept");

    useAzerty();
    check(key(Forward) == ImGuiKey_Z && key(Left) == ImGuiKey_Q && key(Back) == ImGuiKey_S && key(Right) == ImGuiKey_D, "AZERTY walks with Z Q S D");
    check(key(Emotes) == ImGuiKey_B, "...and the rest are the usual keys");
    check(used(ImGuiKey_Q) && !used(ImGuiKey_W), "used() knows Q walks now");

    // settings.json: only the changed ones, by name, and read back the same.
    const nlohmann::json j = toJson(GraphicsSettings::get().keys);
    check(j.size() == 2 && j.value("forward", "") == "Z" && j.value("left", "") == "Q", "saved as {forward: Z, left: Q}");
    int back[GraphicsSettings::kKeyActions] = {};
    fromJson(j, back);
    check(back[Forward] == ImGuiKey_Z && back[Left] == ImGuiKey_Q && back[Jump] == 0, "...and read back the same");
    fromJson(nlohmann::json{{"jump", "Escape"}, {"forward", "NotAKey"}, {"talk", "LeftShift"}}, back);
    check(back[Jump] == 0 && back[Forward] == 0 && back[Talk] == ImGuiKey_LeftShift, "a bad or kept key in the file is ignored");
    check(keyByName("Left Shift") == ImGuiKey_LeftShift && keyByName("Space") == ImGuiKey_Space, "keys are found by name");
    check(std::string(keyName(ImGuiKey_LeftShift)) == "Left Shift", "names read nicely");

    reset();
    check(!changed(), "reset puts everything back");
    std::cout << (failures ? "key binding tests FAILED\n" : "key binding tests passed\n");
    return failures ? 1 : 0;
}
