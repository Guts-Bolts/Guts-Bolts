#pragma once
#include <string>
#include <vector>

class Scene;
class SceneNode;
namespace RobloxAnim { struct Clip; }

// Roblox place and model files: .rbxl / .rbxm (binary) and .rbxlx / .rbxmx (XML).
//
// Importing turns Roblox objects into Guts and Bolts ones (Parts, Models,
// Scripts, lights, sounds, attachments and constraints, attributes and tags).
// Roblox is measured in studs and its characters are twice the size of ours,
// so everything is scaled by 1/2 on the way in and by 2 on the way out.
// Exporting writes XML (.rbxlx / .rbxmx), which Roblox Studio opens directly.
namespace RobloxFile {

struct Report {
    int parts = 0, models = 0, scripts = 0, lights = 0, sounds = 0, constraints = 0, other = 0;
    std::vector<std::string> notes;   // things that couldn't come across exactly
    std::string summary() const;
};

bool isRobloxFile(const std::string& path);   // by extension
bool isPlace(const std::string& path);         // .rbxl / .rbxlx

// Replace the scene with a Roblox place (.rbxl / .rbxlx).
bool importPlace(Scene& scene, const std::string& path, Report& report, std::string& error);
// Add a Roblox model (.rbxm / .rbxmx, or a place's Workspace) under `parent`.
// Returns the new top-level objects.
std::vector<SceneNode*> importModel(Scene& scene, SceneNode* parent, const std::string& path,
                                    Report& report, std::string& error);

// Save the whole game as a Roblox place (.rbxlx).
bool exportPlace(Scene& scene, const std::string& path, std::string& error);
// Save some objects as a Roblox model (.rbxmx).
bool exportModel(Scene& scene, const std::vector<SceneNode*>& nodes, const std::string& path, std::string& error);

// The animations in a Roblox file: every KeyframeSequence in a .rbxm / .rbxmx (as
// Roblox wrote it; RobloxAnim::convert makes them fit our characters). False with
// a reason if there are none (or it's only a link to one on Roblox's site).
bool readAnimations(const std::string& path, std::vector<RobloxAnim::Clip>& out, std::string& error);

} // namespace RobloxFile
