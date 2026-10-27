#pragma once
// Linked models (like Roblox packages): copies of one model, spread around a game, that
// stay the same inside. Change what's in any one of them in Studio and every other copy
// changes to match; each copy keeps its own place, turn and name.
//
// A linked model is a Model with a "LinkId" attribute; copies made by Duplicate or
// Copy / Paste keep it, so they're linked too. Removing the attribute (Unlink) makes a
// copy stand on its own again. While playing they're just ordinary models.
#include <cstdint>
#include <string>
#include <unordered_map>

class Scene;
class SceneNode;

namespace LinkedModels {

inline constexpr const char* kAttribute = "LinkId";

std::string linkOf(const SceneNode* n);   // its link ("" = not a linked model)
std::string newLink();                    // a fresh link id
void        link(SceneNode* model, const std::string& id);
void        unlink(SceneNode* model);

// Watches the linked copies in a scene between edits.
class Tracker {
public:
    // Note what every linked copy holds now (after loading, undo...: nothing to copy).
    void remember(Scene& scene);
    // After an edit: in each set of linked copies where exactly one copy's insides
    // changed, copy them into the others. Returns how many copies changed, and the
    // name of one of the models they were copies of.
    int  sync(Scene& scene, std::string* name = nullptr);
private:
    std::unordered_map<uint64_t, std::string> m_seen;   // model id -> its insides when last seen
};

} // namespace LinkedModels
