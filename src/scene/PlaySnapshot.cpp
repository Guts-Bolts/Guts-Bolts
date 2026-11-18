#include "PlaySnapshot.h"
#include "Disasters.h"
#include "Scene.h"
#include "Serializer.h"
#include "Storms.h"

void PlaySnapshot::take(Scene& scene) {
    m_text = Serializer::saveScene(scene);
    m_settings = Serializer::settingsToString(scene);
    m_terrain.copyFrom(scene.terrain());
    m_taken = true;
}

bool PlaySnapshot::restore(Scene& scene) {
    if (!m_taken) return false;
    Serializer::loadScene(scene, m_text);
    // The saved text rounds terrain heights to a sixteenth of a stud: put back the exact ones.
    if (!scene.terrain().sameAs(m_terrain)) scene.terrain().copyFrom(m_terrain);
    // What only lives while a game runs.
    scene.storms() = Storms::State{};
    Disasters::reset(scene);
    return true;
}

std::string PlaySnapshot::differences(Scene& scene) const {
    if (!m_taken) return {};
    std::string out;
    if (!scene.terrain().sameAs(m_terrain)) out += "the terrain; ";
    const std::string now = Serializer::saveScene(scene);
    if (now != m_text) {
        // Which part: the Lighting / world settings, or the things in the place.
        if (Serializer::settingsToString(scene) != m_settings) out += "the Lighting or world settings; ";
        else out += "the objects in the place; ";
    }
    if (Storms::active(scene)) out += "a storm is still blowing; ";
    if (Disasters::any(scene)) out += "a disaster is still going; ";
    if (out.size() >= 2) out.resize(out.size() - 2);
    return out;
}
