#pragma once
#include <cstdint>
#include <vector>
#include "../core/SoundFx.h"

class Scene;
class SceneNode;

// SoundGroups and sound effects while a game plays: how loud each Sound really is (its
// Volume times its SoundGroup's, times the group's parent group's...) and which effects it
// goes through. Audio.h does the actual mixing.
namespace SoundMix {

// The SoundGroup a sound plays through (null if it has none, or it's gone).
SceneNode* groupOf(Scene& scene, const SceneNode* sound);
// A group's parent group (a SoundGroup inside another one), or null.
SceneNode* parentGroup(const SceneNode* group);
// Volume x every SoundGroup it goes through.
float volume(Scene& scene, const SceneNode* sound);
// The effects directly inside a Sound or SoundGroup that are on, highest Priority first.
std::vector<SoundFx::Settings> effectsIn(const SceneNode* container);
// Everything a sound goes through, in order: its own effects, then its group's, then that
// group's parent group's... (what a test tone sent through it hears).
std::vector<SoundFx::Settings> chainOf(Scene& scene, const SceneNode* sound);
// Point a playing sound (Audio handle) at its volume, effects and group.
void apply(Scene& scene, const SceneNode* sound, int handle);
// Every frame while playing: keep every playing sound and group up to date (scripts and
// Studio's Properties can change them any time).
void update(Scene& scene);

} // namespace SoundMix
