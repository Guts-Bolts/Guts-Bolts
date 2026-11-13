#pragma once
#include <cstdint>
#include <functional>
#include <string>

class Scene;
class SceneNode;

// Lip sync: a character's mouth opens and closes with what it says.
//
// How open (0 shut .. 1 wide) comes from, in this order:
//   - the attribute MouthOpen (a number 0..1) on the character's Model or Humanoid,
//     for scripts that want to work it themselves (remove it to hand back);
//   - sounds playing inside the character (an NPC's lines: Sound.PlaybackLoudness);
//   - the player's voice chat (how loud they are right now).
// The face gets a mouth drawn on it that opens that far, and a bone called "Jaw" in
// the head (a bundle's, or an accessory's) turns down with it. Smoothed, so it moves
// like talking and doesn't flicker.
namespace LipSync {

// Every frame while playing. `voiceLevel(name)`: how loud a player is on voice chat
// (0..1), or null for none.
void update(Scene& scene, float dt, const std::function<float(const std::string&)>& voiceLevel);
float mouth(uint64_t headId);    // how open this head's mouth is (0 if not talking)
float target(Scene& scene, SceneNode& model, const std::function<float(const std::string&)>& voiceLevel);
void clear();

constexpr float kJawDegrees = 18.0f;   // a Jaw bone turns this far when wide open

} // namespace LipSync
