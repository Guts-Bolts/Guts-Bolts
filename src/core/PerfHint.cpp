#include "PerfHint.h"
#include "Settings.h"
#include "I18n.h"
#include <algorithm>
#include <cstdio>

void PerfHint::update(float dt, const GraphicsSettings& gs) {
    m_show = std::max(0.0f, m_show - std::max(0.0f, dt));
    m_cooldown = std::max(0.0f, m_cooldown - std::max(0.0f, dt));
    if (dt <= 0.0f || dt > 1.0f) return;   // (a long hitch, like loading, doesn't count)
    m_clock += dt;
    ++m_frames;
    if (m_clock < kWindow) return;
    const float fps = m_frames / m_clock;
    m_clock = 0.0f;
    m_frames = 0;
    const int cap = gs.frameCap();
    const float target = cap > 0 ? std::min(60.0f, (float)cap) : 60.0f;
    if (fps >= target * 0.7f) { m_slowRun = 0; return; }   // smooth enough
    if (++m_slowRun < kSlowWindows || m_cooldown > 0.0f || m_count >= kMaxHints || showing()) return;

    char buf[400];
    const bool autoQ = gs.quality == GraphicsSettings::Auto;
    const bool lowest = gs.quality == GraphicsSettings::VeryLow ||
                        (autoQ && gs.autoLevel() >= GraphicsSettings::kAutoLevels - 1);
    if (autoQ && !lowest) return;   // Auto is still stepping down by itself
    if (!lowest) {
        std::snprintf(buf, sizeof buf, I18n::tr("This game is running slowly (%d FPS). Try Graphics Quality: Auto in Settings. It picks the best look your device can keep smooth."), (int)(fps + 0.5f));
    } else if (gs.renderDistance > 2) {
        std::snprintf(buf, sizeof buf, I18n::tr("This game is heavy for your device (%d FPS), even on the lowest graphics. A shorter Render Distance in Settings helps, and so does closing other apps."), (int)(fps + 0.5f));
    } else {
        std::snprintf(buf, sizeof buf, I18n::tr("This game is heavy for your device (%d FPS), even on the lowest graphics. Closing other apps can help. If your device feels hot, Battery saver in Settings lets it cool down."),
                      (int)(fps + 0.5f));
    }
    m_text = buf;
    m_slow = true;
    m_show = kShowFor;
    m_cooldown = kCooldown;
    m_slowRun = 0;
    ++m_count;
}

void PerfHint::saverChanged(bool on, int batteryPercent) {
    char buf[300];
    if (on && batteryPercent >= 0) {
        std::snprintf(buf, sizeof buf, I18n::tr("Battery low (%d%%): battery saver is on. 30 FPS and simpler effects until you charge."), batteryPercent);
        m_text = buf;
    } else {
        m_text = on ? I18n::tr("Battery saver is on: 30 FPS and simpler effects.") : I18n::tr("Battery saver is off: the graphics are back to normal.");
    }
    m_slow = false;
    m_show = 6.0f;
}
