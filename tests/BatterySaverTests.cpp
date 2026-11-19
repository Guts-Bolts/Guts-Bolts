// Phones and battery: battery saver (30 FPS and the cheaper versions of the costliest
// effects), Auto turning it on when the battery is low, and the in-game "running
// slowly" hint.
//
//   BatterySaverTests
#include "core/PerfHint.h"
#include "core/Settings.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
// Play `seconds` at `fps`, feeding the hint (and Auto quality when asked).
void play(PerfHint& h, GraphicsSettings& s, float fps, float seconds, bool autoQ = false) {
    for (int i = 0; i < (int)(fps * seconds); ++i) {
        if (autoQ) s.autoAdjust(1.0f / fps);
        h.update(1.0f / fps, s);
    }
}
} // namespace

int main() {
    using GS = GraphicsSettings;
    using namespace Scalability;

    // --- Battery saver's caps and frame limit -------------------------------------
    GS s;
    s.setDevice("NVIDIA GeForce RTX 3060", Desktop);
    s.applyPreset(GS::High);
    check(s.batterySaver == GS::SaverAuto, "battery saver starts on Auto");
    check(!s.saverActive() && s.frameCap() == 0, "a computer with no battery: saver off, no frame limit (VSync on)");
    const int highShadow = s.shadowRes;
    check(s.volumetricFog() > 0 && s.shinyReflections() > 0, "High has light-catching fog and mirror reflections");

    check(!s.setBattery(-1, false) && !s.saverActive(), "no battery: Auto never turns it on");
    check(!s.setBattery(80, false) && !s.saverActive(), "80% on battery: still off");
    check(s.setBattery(19, false) && s.saverActive(), "19% and not charging: Auto turns it on");
    check(s.frameCap() == 30, "battery saver limits the game to 30 FPS");
    check(s.volumetricFog() == 0 && s.volumetricClouds() == 0, "battery saver: no light-catching fog or 3D clouds");
    check(s.shinyReflections() == 0, "battery saver: no mirror reflections (sky and water stay)");
    check(s.groupLevel(GlobalIllumination) == 0, "battery saver: dark corners instead of bounce light");
    check(s.shadowRes < highShadow && s.shadows, "battery saver: smaller shadow maps, but shadows stay");
    check(s.renderScale <= 0.75f, "battery saver: 75% of the pixels");
    check(s.postFx, "battery saver keeps the game's colours and look");
    check(s.quality == GS::High, "the menu still says High");
    check(!s.setBattery(22, false) && s.saverActive(), "22%: stays on (no flickering on and off around 20%)");
    check(s.setBattery(22, true) && !s.saverActive(), "plugged in: off again");
    check(s.frameCap() == 0 && s.shadowRes == highShadow && s.volumetricFog() > 0, "off again: High is back exactly");
    check(!s.setBattery(10, true) && !s.saverActive(), "10% but charging: stays off");

    s.vsync = false; s.fpsCap = 144;
    check(s.frameCap() == 144, "your own FPS limit still works (VSync off)");
    check(s.setBatterySaver(GS::SaverOn) && s.frameCap() == 30, "On: 30 FPS even with a higher limit");
    s.fpsCap = 24;
    check(s.frameCap() == 24, "a limit under 30 stays as it is");
    s.fpsCap = 0; s.vsync = true;
    check(s.setBatterySaver(GS::SaverOff) && !s.saverActive(), "Off: never on, even...");
    check(!s.setBattery(5, false) && !s.saverActive(), "...at 5%");
    s.setBatterySaver(GS::SaverAuto);
    check(s.saverActive(), "back to Auto at 5%: on");
    s.setBattery(90, true);

    // Cheaper presets don't go *up* under battery saver.
    s.applyPreset(GS::VeryLow);
    const float vlScale = s.renderScale;
    s.setBatterySaver(GS::SaverOn);
    check(!s.shadows && s.renderScale == vlScale, "Very Low stays Very Low with battery saver");
    s.setBatterySaver(GS::SaverAuto);

    // Auto quality aims for 30 FPS on battery saver: 30 FPS isn't "laggy" there.
    s.applyPreset(GS::Auto);
    s.setBatterySaver(GS::SaverOn);
    int changes = 0;
    for (int i = 0; i < 30 * 20; ++i) changes += s.autoAdjust(1.0f / 30.0f) ? 1 : 0;
    check(changes == 0 && s.autoLevel() == 0, "Auto on battery saver: a steady 30 FPS is smooth, nothing steps down");
    s.setBatterySaver(GS::SaverOff);
    for (int i = 0; i < 30 * 6; ++i) s.autoAdjust(1.0f / 30.0f);
    check(s.autoLevel() > 0, "without battery saver, 30 FPS is laggy and Auto steps down");
    s.setBatterySaver(GS::SaverAuto);

    // Desktop quality isn't touched: no battery means the same High as before.
    GS d;
    d.setDevice("NVIDIA GeForce RTX 3060", Desktop);
    d.applyPreset(GS::Ultra);
    check(d.groupLevel(Shadow) == 3 && d.groupLevel(PostProcess) == 3 && d.frameCap() == 0, "a desktop on Epic stays fully Epic");

    // Phones: caps from the device profile, then battery saver on top.
    GS p;
    p.setDevice("Adreno (TM) 740", Phone);
    p.applyPreset(GS::High);
    check(p.groupLevel(GlobalIllumination) == 0, "phones keep the cheap lighting (as before)");
    p.setBatterySaver(GS::SaverOn);
    check(p.renderScale <= 0.75f && p.frameCap() == 30, "phone on battery saver: 75% pixels and 30 FPS");
    for (int g = 0; g < kGroups; ++g)
        check(p.groupLevel(g) <= GS::saverCap(g), std::string("battery saver caps ") + groupName(g));

    // --- The "running slowly" hint ------------------------------------------------
    {
        GS g;
        g.applyPreset(GS::High);
        PerfHint h;
        play(h, g, 60.0f, 60.0f);
        check(!h.showing(), "smooth at 60 FPS: no hint");
        play(h, g, 30.0f, 10.0f);
        check(!h.showing(), "laggy for 10 seconds: not yet (it could be a moment)");
        play(h, g, 30.0f, 6.0f);
        check(h.showing() && h.slowHint(), "laggy for 15 seconds on High: a hint");
        check(h.text().find("Auto") != std::string::npos && h.text().find("30 FPS") != std::string::npos,
              "it suggests Auto and says the frame rate");
        play(h, g, 60.0f, 13.0f);
        check(!h.showing(), "it goes away by itself after a few seconds");
        play(h, g, 30.0f, 60.0f);
        check(!h.showing() && h.hintsShown() == 1, "not again for 10 minutes");
        play(h, g, 30.0f, 600.0f);
        check(h.hintsShown() == 2, "after 10 more laggy minutes, one more");
        play(h, g, 30.0f, 3600.0f);
        check(h.hintsShown() == PerfHint::kMaxHints, "never more than 3 in one go");
    }
    {
        GS g;   // Auto still has steps left: it's handling it, so no hint
        g.applyPreset(GS::Auto);
        PerfHint h;
        play(h, g, 30.0f, 20.0f);
        check(!h.showing(), "Auto with steps left: no hint (it steps down by itself)");
        while (g.autoLevel() < GS::kAutoLevels - 1 && !h.showing()) play(h, g, 20.0f, 1.0f, true);
        check(g.autoLevel() == GS::kAutoLevels - 1 && !h.showing(), "no hint all the way down Auto's steps");
        play(h, g, 20.0f, 10.0f, true);
        check(h.showing() && h.text().find("heavy") != std::string::npos, "Auto at its lowest and still laggy: says what else helps");
    }
    {
        GS g;   // battery saver: 30 FPS is the aim, so 30 isn't slow
        g.applyPreset(GS::High);
        g.setBatterySaver(GS::SaverOn);
        PerfHint h;
        play(h, g, 30.0f, 60.0f);
        check(!h.showing(), "30 FPS on battery saver is the aim: no hint");
        play(h, g, 15.0f, 20.0f);
        check(h.showing(), "15 FPS on battery saver: a hint");
    }
    {
        GS g;
        PerfHint h;
        play(h, g, 30.0f, 1.0f);
        h.update(5.0f, g);   // loading a map
        check(!h.showing(), "a long hitch (loading) doesn't count");
        h.saverChanged(true, 15);
        check(h.showing() && !h.slowHint() && h.text().find("15%") != std::string::npos, "battery saver turning on says so, with the battery level");
        h.dismiss();
        check(!h.showing(), "X closes it");
    }

    std::cout << (failures ? "\nFAILED: " : "\nAll passed. ") << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
