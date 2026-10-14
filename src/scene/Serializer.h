#pragma once
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

class Scene;
class SceneNode;
struct Environment;
struct GuiProps;
struct PromptProps;
struct ClickProps;
struct HighlightProps;
struct EffectProps;
struct JointProps;

// Turns scenes and objects into JSON text and back. Used for:
//  * File > Save / Open (".gbscene" files)
//  * Undo / Redo (a snapshot of the scene before each change)
//  * Play mode (the world is snapshotted on Play and restored on Stop)
//  * Copy / Paste / Duplicate and script Clone()
namespace Serializer {

std::string saveScene(Scene& scene, bool pretty = false);
bool        loadScene(Scene& scene, const std::string& text, std::string* error = nullptr);

std::string                nodeToString(const SceneNode& node);
// Game UI properties as JSON (saving, and multiplayer updates).
nlohmann::json             guiToJson(const GuiProps& g);
void                       guiFromJson(GuiProps& g, const nlohmann::json& j);
// A ProximityPrompt's settings (and whether it's on), the same way.
nlohmann::json             promptToJson(const PromptProps& p, bool enabled);
void                       promptFromJson(const nlohmann::json& j, PromptProps& p, bool& enabled);
// A ClickDetector's settings, the same way.
nlohmann::json             clickToJson(const ClickProps& c);
void                       clickFromJson(const nlohmann::json& j, ClickProps& c);
// A Highlight's settings (and whether it's on), the same way.
nlohmann::json             highlightToJson(const HighlightProps& h, bool enabled);
void                       highlightFromJson(const nlohmann::json& j, HighlightProps& h, bool& enabled);
// A Trail's or Beam's settings (and whether it's on), the same way.
nlohmann::json             effectToJson(const EffectProps& e, bool enabled, bool beam);
void                       effectFromJson(const nlohmann::json& j, EffectProps& e, bool& enabled);
// A ball-socket / prismatic / cylindrical joint's settings, the same way.
nlohmann::json             jointToJson(const JointProps& p);
void                       jointFromJson(const nlohmann::json& j, JointProps& p);
// freshIds = true gives every object a brand-new id (needed for copies).
std::unique_ptr<SceneNode> nodeFromString(const std::string& text, bool freshIds);
std::unique_ptr<SceneNode> clone(const SceneNode& node);

// Everything except the object tree (lighting, rules, game info, character setup).
std::string settingsToString(Scene& scene);
void        settingsFromString(Scene& scene, const std::string& text);

// One object without its children (Team Create syncs objects one by one).
std::string                nodeShallowToString(const SceneNode& node);
std::unique_ptr<SceneNode> nodeShallowFromString(const std::string& text);
void                       applyNodeShallow(SceneNode& dst, const std::string& text);

std::string environmentToString(const Environment& env);
void        environmentFromString(Environment& env, const std::string& text);

bool writeFile(const std::string& path, const std::string& text);
bool readFile (const std::string& path, std::string& out);
// Open any game file: a .gbscene, or a Roblox place (.rbxl / .rbxlx).
bool loadGameFile(Scene& scene, const std::string& path, std::string* error = nullptr);

} // namespace Serializer
