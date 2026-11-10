#pragma once
// The sound of things hitting things (Realism): a crate knocking onto concrete clunks,
// a metal barrel clangs, a plank knocks, glass clinks, and breaking glass, wood or stone
// shatters. Louder the harder and heavier the hit; a part can't spam the same sound
// (a box rattling on the floor plays now and then, not every frame).
// Off with workspace.ImpactSounds = false.
#include "Physics.h"
#include <vector>

class Scene;

namespace Impacts {
void play(Scene& scene, const std::vector<Physics::Knock>& knocks, const std::vector<Physics::Smash>& smashes);
// (For tests) how many hit sounds the last call played.
int  lastCount();
}
