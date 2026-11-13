#pragma once
// Sound that knows the room (Realism): every Sound in the world is worked out against
// where you are listening from:
//   * Walls between you and it make it quieter and muffled (the highs go first).
//   * The space around you echoes: a small room a little, a big hall or a cave a lot,
//     out in the open hardly at all. (A few rays from your ears find the walls.)
//   * Things racing past bend in pitch (Doppler): the parts the sounds are in, and you.
// Off with SoundService.Acoustics = false (on to start with).
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Scene;

namespace Acoustics {

// How muffled a sound is (0 clear .. 1 behind thick walls) with `walls` solid things in
// the way, `thickness` studs of them all together.
float occlusion(int walls, float thickness);

// What the rays from your ears found: `hits` of `rays` hit something within reach,
// `averageDistance` studs away on average.
struct Room {
    float decay = 0.0f;   // seconds the echo rings on
    float wet = 0.0f;     // how much echo (0 = none: out in the open)
};
Room roomFrom(int hits, int rays, float averageDistance);

// The ray directions used to feel out the room (spread evenly over a ball).
const std::vector<glm::vec3>& roomRays();

// Once a frame from the game: works out the walls and the room every so often and
// sets them on the playing sounds (Audio::setOcclusion / setRoom / setVelocity).
class Listener {
public:
    void update(Scene& scene, float dt, const glm::vec3& ears);
    void reset();
    const Room& room() const { return m_room; }
private:
    float     m_occTimer = 0.0f, m_roomTimer = 0.0f;
    glm::vec3 m_lastEars{0.0f};
    bool      m_hasLast = false;
    Room      m_room;
};

} // namespace Acoustics
