#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <string>

// Voice chat: your microphone goes out to the other players in a game, and their
// voices come out of their characters (louder the closer they are).
//
// The microphone is squeezed with Opus (about 24 kbps), 20 ms at a time, and sent
// in packets of three (60 ms). Packets travel through the game's normal
// connection, so they pass the host (who checks the game allows voice) and the
// Guts&Bolts relay like everything else. Each speaker gets their own little
// buffer and a 3D sound placed at their head.
//
// If the computer has no microphone (or no sound at all), sending quietly does
// nothing; hearing others still works with only speakers.
namespace Voice {

// Open the microphone (true) or close it (false). Opening asks the computer for
// the mic, so only do it while in a multiplayer game with voice turned on.
void setActive(bool on);
bool active();
bool micWorks();                 // the microphone opened

// Push-to-talk held (or the Talk button on), or Open Mic. Open Mic only sends
// while you're actually making sound.
void setTalking(bool held, bool openMic);
bool sending();                  // sound is going out right now (for the "talking" light)
float micLevel();                // 0..1, for the little meter

// Encoded packets waiting to be sent (one at a time). False if none.
bool takePacket(std::string& bytes);

// A packet from `speaker` arrived; their head is at `pos`. Plays it (unless muted).
void receive(const std::string& speaker, const std::string& bytes, const glm::vec3& pos);
// Heard in the last moment (for the speaker icon by their name and over their head).
bool speaking(const std::string& speaker);
float level(const std::string& speaker);   // how loud they are right now, 0..1 (lip sync)
void eachSpeaking(const std::function<void(const std::string& name, const glm::vec3& pos)>& fn);
void forget(const std::string& speaker);   // they left
void forgetAll();                          // we left the game

// Mute someone (only for you; they aren't told).
void setMuted(const std::string& speaker, bool muted);
bool muted(const std::string& speaker);
void setVolume(float v);                   // everyone's voices, 0..2

void update(float dt);   // every frame: squeeze the mic, tidy up quiet speakers
void shutdown();

// Pack / unpack: [count] then [length lo][length hi][bytes] for each frame.
std::string pack(const std::string* frames, int count);
bool unpack(const std::string& bytes, std::string* frames, int& count, int maxCount);

} // namespace Voice
