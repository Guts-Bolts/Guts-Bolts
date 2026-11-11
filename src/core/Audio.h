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
void seek(int handle, double seconds);           // jump to this far into a sound file
void setPosition(int handle, const glm::vec3& position);
void setListener(const glm::vec3& position, const glm::vec3& forward);
glm::vec3 listenerPosition();
void setMasterVolume(float v);                   // 0..1
void suspend();                                    // temporarily mute while the app is backgrounded
void resume();                                     // restore the user's previous master volume

// Sound effects and SoundGroups (see SoundMix.h, which keeps these up to date while playing).
// A sound's own effects run first, then it goes through its group's "bus" (the group's
// effects, on everything in the group mixed together), then the parent group's, and so on.
void setEffects(int handle, const std::vector<SoundFx::Settings>& fx);   // in this order
void setRoute(int handle, uint64_t bus);
// How loud a playing sound is right now (RMS of what comes out of it, 0..1; 0 if it
// isn't playing). The first ask starts listening, so it reads 0 that once.
// (Sound.PlaybackLoudness is this * 1000; lip sync uses it.)
float loudness(int handle);
// Sound that knows the room (Acoustics.h works these out):
void  setOcclusion(int handle, float amount);   // 0 = clear, 1 = behind thick walls (quieter, muffled)
float occlusion(int handle);
void  setVelocity(int handle, const glm::vec3& v);    // how fast it moves: the pitch bends as it goes past (Doppler)
void  setListenerVelocity(const glm::vec3& v);
void  setRoom(float decaySeconds, float wet);   // the room's echo on every sound in the world (wet 0 = none)
float roomWet();
// Real recorded sounds (assets/sounds/real): "hit_metal" picks one of its takes.
bool  hasTakes(const std::string& name);
std::string takeBase(const std::string& name);   // "hit_metal_2" -> "hit_metal"
std::vector<std::string> realSoundNames();
float loudnessOf(const float* samples, size_t count);   // the same sum, for tests                                  // 0 = straight out
// A SoundGroup's bus: `parent` = the group it's inside (0 = none), its effects in order.
void setBus(uint64_t bus, uint64_t parent, const std::vector<SoundFx::Settings>& fx);
void keepBuses(const std::set<uint64_t>& used);                           // drop the rest

// The sound engine itself (a ma_engine*), for voice chat. Null with no sound device.
void* engineHandle();

// Captions (Settings > Accessibility): the sounds playing, as words. Built-in sounds caption
// themselves ("Explosion", "Splash"); a game's Sound objects are captioned with their name.
struct Caption {
    std::string text;
    double      at = 0.0;      // seconds (only differences matter: see captionClock)
    int         side = 0;      // -1 on your left, 1 on your right, 0 ahead / everywhere
    int         count = 1;     // the same sound again within a second: "Hit x3"
};
void caption(const std::string& text, const glm::vec3* position = nullptr);
std::vector<Caption> captions(double keepSeconds = 3.0);   // newest last
double captionClock();
// "explosion" -> "Explosion"; "" for sounds not worth a caption (footsteps, clicks) or not built in.
std::string builtinCaption(const std::string& soundId);

} // namespace Audio
