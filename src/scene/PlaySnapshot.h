#pragma once
// Studio's playtest: the world exactly as it was before Play, put back on Stop.
//
// Like Roblox Studio, a playtest is a copy of the game that runs and is thrown away:
// whatever happens while it plays (scripts moving and deleting parts, the weather
// changing, terrain dug out, disasters, a tornado) is gone when you stop, and you're
// back at what you built. Everything that is saved with the place is kept as text, and
// the terrain as an exact copy (a saved file rounds its heights a tiny bit). Things that
// only live while a game runs (storms, disasters...) are cleared.
#include <string>
#include "Terrain.h"

class Scene;

class PlaySnapshot {
public:
    void take(Scene& scene);          // just before Play
    bool restore(Scene& scene);       // on Stop: false if there was nothing to restore
    bool taken() const { return m_taken; }
    const std::string& text() const { return m_text; }   // the place as saved text (for undo)
    void clear() { m_taken = false; m_text.clear(); m_settings.clear(); m_terrain.clear(); }

    // Anything about the world that differs from the snapshot, in plain words
    // (empty = exactly as it was). For checks and the Output after Stop.
    std::string differences(Scene& scene) const;

private:
    bool        m_taken = false;
    std::string m_text, m_settings;
    Terrain     m_terrain;
};
