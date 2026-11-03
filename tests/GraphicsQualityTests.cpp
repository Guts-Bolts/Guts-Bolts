// Graphics quality: the menu's presets, and Auto stepping the graphics down when a
// game is laggy (and back up when it's smooth again).
//
//   GraphicsQualityTests
#include "core/Settings.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Play `seconds` at `fps` frames a second.
int play(GraphicsSettings& s, float fps, float seconds) {
    int changes = 0;
    for (int i = 0; i < (int)(fps * seconds); ++i) changes += s.autoAdjust(1.0f / fps) ? 1 : 0;
    return changes;
}
} // namespace

int main() {
    // The menu: every choice maps to a preset and back.
    for (int i = 0; i < GraphicsSettings::kQualityChoices; ++i)
        check(GraphicsSettings::menuIndexOf(GraphicsSettings::qualityAt(i)) == i,
              std::string("menu choice ") + GraphicsSettings::qualityNames()[i] + " maps back to itself");
    check(std::string(GraphicsSettings::qualityNames()[0]) == "Auto", "Auto is first in the menu");

    GraphicsSettings s;
    check(s.quality == GraphicsSettings::Auto, "new computers start on Auto");

    s.applyPreset(GraphicsSettings::VeryLow);
    check(!s.shadows && s.renderScale <= 0.5f && s.waterQuality == 0, "Very Low: no shadows, half sharpness, light water");
    check(s.waterMaxDrops() < 10000, "Very Low keeps the liquid small");
    s.applyPreset(GraphicsSettings::High);
    check(s.shadows && s.renderScale == 1.0f, "High: shadows back on, full sharpness");

    // Auto: smooth at 60 FPS, it stays on its best level.
    s.applyPreset(GraphicsSettings::Auto);
    check(s.quality == GraphicsSettings::Auto && s.autoLevel() == 0, "Auto starts at the top");
    check(play(s, 60.0f, 20.0f) == 0 && s.autoLevel() == 0, "smooth: Auto changes nothing");

    // 14 FPS (Mega Water Slide on a slow computer): a step down every two seconds.
    play(s, 14.0f, 4.5f);
    check(s.autoLevel() == 2, "laggy for 4 seconds: two steps down (level " + std::to_string(s.autoLevel()) + ")");
    check(s.quality == GraphicsSettings::Auto, "still Auto after stepping down");
    play(s, 14.0f, 60.0f);
    check(s.autoLevel() == GraphicsSettings::kAutoLevels - 1, "still laggy: it goes all the way down");
    check(!s.shadows && s.renderScale <= 0.5f, "the bottom is Very Low");
    check(s.lowEnd(), "Auto at the bottom counts as low-end");

    // Smooth again: it waits a bit, then steps up one at a time.
    play(s, 60.0f, 5.0f);
    check(s.autoLevel() == GraphicsSettings::kAutoLevels - 1, "doesn't jump straight back up");
    play(s, 60.0f, 90.0f);
    check(s.autoLevel() < GraphicsSettings::kAutoLevels - 1, "smooth for a while: steps back up");

    // One long hitch (loading a game) isn't lag.
    GraphicsSettings t;
    t.applyPreset(GraphicsSettings::Auto);
    t.autoAdjust(3.0f);
    play(t, 60.0f, 4.0f);
    check(t.autoLevel() == 0, "a single long frame doesn't count");

    // Not on Auto: never touched.
    GraphicsSettings u;
    u.applyPreset(GraphicsSettings::Ultra);
    check(play(u, 10.0f, 30.0f) == 0 && u.quality == GraphicsSettings::Ultra, "Ultra stays Ultra even when slow");

    std::cout << (failures ? "FAILED: " : "all passed") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}
