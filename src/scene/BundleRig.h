#pragma once
#include <nlohmann/json.hpp>

class SceneNode;

// Bones inside avatar bundles: a body part bought in the catalog can carry its own
// armature (jiggly hair on a head, a belly, a tail, floppy ears), and an arm or leg can
// "bend" softly in the middle (an elbow / knee) instead of being one stiff block.
//
// In the bundle file each body part has "bones" (see Online::bundleBody for the exact
// rules) and maybe "w" (each corner's weights). Studio writes them from the bones the
// creator put in the part (Armature window); wearing the bundle builds the same bones
// on the character, so the Jiggle bones wobble for everyone in the server (each screen
// works the wobble out itself, nothing extra is sent).
namespace BundleRig {

// The bones of a Studio part (and the mesh's weights, if it's bound), for a bundle file:
// adds "bones" and "w" to `out`. Nothing if the part has no bones.
void toJson(const SceneNode& part, nlohmann::json& out);

// Put a bundle part's bones on a character's body part (after its mesh is set): the old
// bundle bones go, the new ones are built, and the mesh is bound to them (with the
// bundle's weights, or worked out by distance when it has none). "bend": true adds an
// upper and a lower bone to an arm / leg, the lower one springy. False if it has none.
bool apply(SceneNode& part, const nlohmann::json& j);

// Take a body part's bundle bones off (going back to the normal body).
void clear(SceneNode& part);

// The two bones "bend" makes, by name (so tests and scripts can find them).
inline constexpr const char* kUpperBone = "BendUpper";
inline constexpr const char* kLowerBone = "BendLower";

} // namespace BundleRig
