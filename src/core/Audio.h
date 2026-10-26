#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "SoundFx.h"

// Sound for the whole engine. Built-in effects are generated in code (so no
// sound files are needed); games can also play .wav / .mp3 / .flac files from
// the games folder. If the computer has no sound device, everything quietly
// does nothing.
namespace Audio {

// Names of the built-in sounds: "jump", "coin", "oof", "explosion", ...
const std::vector<std::string>& builtinNames();

void init();
void shutdown();
void update();                                   // clean up finished sounds

// Play a built-in name or a file path. `position` null = not 3D (UI / music).
// Returns a handle (0 = couldn't play).
int  play(const std::string& soundId, float volume = 0.6f, float pitch = 1.0f,
          bool loop = false, const glm::vec3* position = nullptr);
void stop(int handle);
void stopAll();
bool isPlaying(int handle);
void setVolume(int handle, float volume);
void setPitch(int handle, float pitch);
void setPosition(int handle, const glm::vec3& position);
void setListener(const glm::vec3& position, const glm::vec3& forward);
void setMasterVolume(float v);                   // 0..1
void suspend();                                    // temporarily mute while the app is backgrounded
void resume();                                     // restore the user's previous master volume

// Sound effects and SoundGroups (see SoundMix.h, which keeps these up to date while playing).
// A sound's own effects run first, then it goes through its group's "bus" (the group's
// effects, on everything in the group mixed together), then the parent group's, and so on.
void setEffects(int handle, const std::vector<SoundFx::Settings>& fx);   // in this order
void setRoute(int handle, uint64_t bus);                                  // 0 = straight out
// A SoundGroup's bus: `parent` = the group it's inside (0 = none), its effects in order.
void setBus(uint64_t bus, uint64_t parent, const std::vector<SoundFx::Settings>& fx);
void keepBuses(const std::set<uint64_t>& used);                           // drop the rest

// The sound engine itself (a ma_engine*), for voice chat. Null with no sound device.
void* engineHandle();

} // namespace Audio
