#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

class SceneNode;
struct EditMesh;

// Armatures, like Blender's (and Roblox's Bones): Bones inside a MeshPart, each
// one a joint. Binding the mesh gives every vertex a weight for the bones near
// it; turning a bone (with the Rotate tool, or an animation) then bends the
// mesh around it, an elbow or a tail or a tree in the wind.
//
// A bone's place is in the part's "studs space": studs from the part's middle,
// turned with the part but not stretched by its size. Bones inside bones are
// placed relative to the bone they're in.
namespace Skin {

// The bones of a part, parents before their children.
std::vector<SceneNode*> bones(SceneNode* part);
// The part a bone belongs to (null if it isn't in one).
SceneNode* partOf(SceneNode* bone);
// Where a bone is now, in its part's studs space (with the extra turn its type
// gives it this frame: a jiggle, an IK reach, a look).
glm::mat4 frame(const SceneNode* bone, const SceneNode* part);
// The part's place in the world without its size: studs space -> world.
glm::mat4 partSpace(const SceneNode* part);
// Where a bone's tip is in the bone's own space (its length and direction).
glm::vec3 localTip(const SceneNode* bone);
// Where a bone ends: at its first child bone, or a bit along its own up (Y).
glm::vec3 tip(SceneNode* bone, SceneNode* part);

// Bind the mesh to the bones as they are now, with weights worked out from how
// close each vertex is to each bone (Blender's "With Automatic Weights").
bool bindAutomatic(SceneNode& part);
// Set the picked vertices' weight for one bone (0 takes them off it); the other
// bones' weights share what's left.
bool assign(SceneNode& part, const std::vector<char>& picked, const std::string& bone, float weight);
// The weight brush: change each vertex's weight for one bone by amount[v] (0..1 =
// how hard the brush hit it). mode 0 adds (towards 1), 1 takes away (towards 0),
// 2 smooths (towards its neighbours' weights). False if nothing changed.
bool paint(SceneNode& part, const std::vector<float>& amount, const std::string& bone, int mode);
// Forget the binding (the mesh stops bending).
void unbind(SceneNode& part);
// Put every bone back where it was when the mesh was bound.
void resetPose(SceneNode& part);
// A vertex's weight for a bone (0 if none).
float weightOf(const EditMesh& m, size_t vertex, const std::string& bone);

// The mesh's corners as the bones bend them (in the mesh's own -0.5..0.5 space).
// False if it isn't bound.
bool deform(const SceneNode& part, std::vector<glm::vec3>& out);
// Every frame, before drawing: move the Jiggle / LookAt / IK bones, then bend each
// bound mesh to its bones (only redone when they move).
void update(SceneNode* root);
// Move one part's Jiggle / LookAt / IK bones on by dt seconds (update does this).
void solve(SceneNode& part, float dt);
// Take the extra turns off a part's bones (they stand where they're posed).
void still(SceneNode& part);
// Whether Jiggle / LookAt / IK bones move by themselves. On in the Player; Studio
// turns it on while playing, or when the Armature window's Preview is ticked.
void setLive(bool on);
bool live();
// The names BoneType goes by ("Normal", "Jiggle", "LookAt", "IK").
const char* typeName(int type);
int typeFrom(const std::string& name);   // -1 if it isn't one
// After a mesh tool changed the vertices: give the new ones weights (from the nearest old one).
void carry(const EditMesh& before, EditMesh& after);

} // namespace Skin
