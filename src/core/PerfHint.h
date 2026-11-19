#pragma once
#include <string>

struct GraphicsSettings;

// The in-game "running slowly" hint: a small note at the top of the screen when a game
// has been laggy for a while and something in Settings would help. It only speaks when
// it has something useful to say:
//   - a fixed quality (High, Epic...) that's too much for this device -> try Auto
//   - Auto already at its lowest and still laggy -> what else helps (render distance,
//     battery saver, closing other apps)
//   - nothing while Auto is still stepping down by itself (it's handling it)
// It also says so once when battery saver turns itself on or off.
// At most one slow hint every 10 minutes, and never more than 3 in one session.
class PerfHint {
public:
    // Once a frame while playing, with how long the frame took.
    void update(float dt, const GraphicsSettings& gs);
    // Battery saver just turned on (true) or off: say so for a few seconds.
    void saverChanged(bool on, int batteryPercent);

    bool showing() const { return m_show > 0.0f; }
    const std::string& text() const { return m_text; }
    bool slowHint() const { return m_slow; }   // (vs the battery note)
    void dismiss() { m_show = 0.0f; }
    int  hintsShown() const { return m_count; }

    static constexpr float kWindow = 5.0f;     // seconds measured at a time
    static constexpr int   kSlowWindows = 3;   // laggy this many windows in a row (15 s) before a hint
    static constexpr float kShowFor = 12.0f;   // seconds the hint stays up
    static constexpr float kCooldown = 600.0f; // seconds before another slow hint
    static constexpr int   kMaxHints = 3;

private:
    float m_clock = 0.0f, m_show = 0.0f, m_cooldown = 0.0f;
    int   m_frames = 0, m_slowRun = 0, m_count = 0;
    bool  m_slow = false;
    std::string m_text;
};
