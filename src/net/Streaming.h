#pragma once
// Loading big maps in pieces, like Roblox's Workspace.StreamingEnabled. When it's on, a
// joined player's computer only gets the parts of the map near their character; the
// rest arrives as they walk towards it and goes away again when they're far off. Big
// maps join faster and use less memory on phones.
//
// The map is cut into "pieces": each part or Model straight in the Workspace (Folders
// and groups bigger than the streaming circle are opened up and their insides used
// instead). A piece is sent when any of it comes within StreamingTargetRadius of the
// player, and taken away once all of it is a quarter further out than that (so walking
// along the edge doesn't make things flicker in and out).
//
// The host never streams (it has everything); NetServer does the sending (NetGame.cpp).
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>

class Scene;
class SceneNode;

namespace Streaming {

inline constexpr float kMinRadius = 32.0f, kMaxRadius = 5000.0f;
inline constexpr float kKeepFactor = 1.25f;   // pieces leave only past radius * this

struct Piece {
    SceneNode* node = nullptr;
    glm::vec3  center{0.0f};
    float      radius = 0.0f;   // a ball around all its parts
};

// The pieces of the map that can come and go (characters and services never do).
std::vector<Piece> pieces(Scene& scene, float targetRadius);

// Should a player whose character is at `focus` have this piece? `haveIt`: they do now.
bool wanted(const Piece& p, const glm::vec3& focus, float targetRadius, bool haveIt);

// The whole scene saved for a joining player, leaving out these pieces.
std::string sceneWithout(Scene& scene, const std::unordered_set<uint64_t>& leaveOut);

} // namespace Streaming
