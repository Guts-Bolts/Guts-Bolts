#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Scene;
class SceneNode;

// The Explorer: every object in the scene as a tree. Click to select
// (Ctrl / Shift + click to pick several), double-click a Script to edit it,
// drag an object onto another to put it inside, right-click for more actions.
class OutlinerPanel {
public:
    using NodeFn = std::function<void(SceneNode*)>;
    OutlinerPanel(Scene* scene, NodeFn openScript, NodeFn addScriptTo);
    void render();

    // Open / close these objects and everything inside them.
    void expand(const std::vector<SceneNode*>& nodes);
    void collapse(const std::vector<SceneNode*>& nodes);
    void collapseAll();
    // Open the folders above a node and scroll to it.
    void reveal(SceneNode* node);
    void beginRename(SceneNode* node);

    // Extra right-click menu items, filled in by the Editor.
    std::function<void()> contextMenuExtras;
    // The "+" on a row: open Insert Object for that object.
    std::function<void(SceneNode*)> onInsert;
    // Insert Object (right-click menu): put a new `what` inside `parent` (null = Workspace).
    std::function<void(const std::string& what, SceneNode* parent)> onInsertNamed;
    // The Lighting row was clicked (show its settings).
    std::function<void()> onLighting;
    // Is a game running? (Then the character shows in the Workspace, like Roblox.)
    std::function<bool()> playing;

private:
    void drawNode(SceneNode* node);
    void insertMenu(SceneNode* parent);                // the Insert Object submenu
    bool serviceRow(const char* name, int icon, bool hasKids, bool selected);   // Lighting / StarterPlayer
    void folderService(const char* name, int icon, const char* tip, const std::function<void()>& onClick = {});           // ReplicatedStorage, StarterPack...
    bool m_showAllServices = false;   // Explorer: also list the services that only scripts use
    bool matches(const SceneNode* node) const;         // the search filter
    bool anyMatch(const SceneNode* node) const;        // it or something inside it
    bool isOpen(const SceneNode* node) const;

    Scene*     m_scene;
    NodeFn     m_openScript;
    NodeFn     m_addScriptTo;
    SceneNode* m_pendingDelete = nullptr;
    std::vector<SceneNode*> m_dragNodes;       // reparent after drawing
    SceneNode* m_dropTarget    = nullptr;

    std::unordered_map<uint64_t, bool> m_open; // folder open / closed, by node id
    uint64_t    m_scrollTo = 0;
    uint64_t    m_renaming = 0;
    bool        m_renameFocus = false;
    std::string m_renameText;
    SceneNode*  m_lastSelected = nullptr;
    std::string m_filter;                          // Roblox-style search: name, c:Class, tag:x, Prop=value
};
