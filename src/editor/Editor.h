#pragma once
#include "AiTools.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "EditorState.h"
#include "Premades.h"
#include "../scene/SceneNode.h"
#include <imgui.h>

struct GLFWwindow;
class Scene;
class GameSession;
class ViewportPanel;
class AssistantPanel;
class OutlinerPanel;
class PropertiesPanel;
class EnvironmentPanel;
class ToolboxPanel;
struct ToolboxTile;
class Thumbnailer;
class PlayerPanel;
class OutputPanel;
class ScriptEditorPanel;
class AnimationEditor;
class TeamCreate;
class Plugins;

class Physics;

class Editor {
public:
    Editor(GLFWwindow* window, Scene* scene);
    ~Editor();
    void render(float dt);
    void openFile(const std::string& path);
    // Bring files in: 3D models, Roblox files, games, pictures, sounds and scripts
    // (File > Import, the Import button, or dragged from the computer onto Studio).
    // `mouse`: where they were dropped (the part under it gets pictures / sounds / scripts).
    void importFiles(const std::vector<std::string>& paths, ImVec2 mouse, bool dropped);
    void importDialog();                          // the computer's Open window, then importFiles
    void togglePlay();
    void runCommand(const std::string& code);   // Command Bar
    // AI helpers (Assistant tab and MCP): run one of Studio's tools (see AiTools.h).
    AiToolResult runAiTool(const std::string& name, const nlohmann::json& args);
    static std::vector<std::string> insertKinds();   // what insert_object / Insert Object can make
    // Test / command-line helpers.
    void startTeamCreate(bool host, const std::string& address);
    void testAddPart(const std::string& name);
    void testInsert(const std::string& names);   // Insert Object, each one into the one before
    void testPremades(const std::string& list);
    void testToolStep(const std::string& step);
    // File > Upload Missing Item Pictures: your hats/gear that still point at picture files on
    // this computer (like imported Roblox hats) get those pictures uploaded, so everyone sees them.
    void fixItemPictures();
    void testAnalysis();   // --test-analysis   // --test-tools: "1".. hold a slot, "click", "print"
    void testSelect(const std::string& names);
    void testSnapshot(const std::string& file);   // --test-snapshot
    void testExportRoblox(const std::string& path);
    void testMesh(const std::string& steps);    // --test-mesh "enter,face,top,extrude"
    void testAnimation(int frame);               // --test-anim
    void testCollisions();                       // --test-collide
    void testDrop(const std::string& files);     // --test-drop "a.fbx;b.png": dropped in the middle of the 3D view
    void setMode(StudioMode mode);               // Build / Modeling / Simulate / Play

private:
    void buildDockspace();
    void renderMenuBar();
    void renderToolbar();
    void renderStatusBar();
    void renderDialogs();
    void renderTeamPanel();
    void handleShortcuts();

    // Creating / removing things
    SceneNode* addPrimitive(const char* label, PrimitiveType type);
    void       spawnPrimitive(PrimitiveType type);
    void       spawnPremade(Premade kind);
    void       addScript(SceneNode* parent, bool local = false);
    void       addModel();
    void       addLight(LightType type);
    void       addSound();
    void       connectParts(int type, SceneNode* a, glm::vec3 pa, SceneNode* b, glm::vec3 pb);
    void       openScript(SceneNode* script);
    void       duplicateSelected();
    void       deleteSelected();
    void       copySelected();
    void       paste();
    void       groupSelected();
    // Solid modeling (EditorSolid.cpp): Union / Intersect, Negate, Separate
    void       unionSelected(int mode);   // 0 union, 1 intersect
    void       negateSelected();
    void       separateSelected();
    void       cutSelected();
    void       pasteInto();
    void       toggleLocked();
    void       toggleAnchored();
    void       centerModelPivot();
    void       alignSelected(int axis, int where);
    void       startPlay(int mode);       // 0 Play, 1 Play Here, 2 Simulate
    // Modeling mode (EditorModes.cpp)
    enum class MeshOp { Extrude, Inset, Subdivide, Delete, Merge, Fill, Flip, Smooth, SelectAll, SelectNone, Invert };
    void       enterModeling();
    void       exitModeling();
    void       checkModeling();
    void       meshOp(MeshOp op);
    void       handleModelingKeys();
    void       addMeshPart();
    // Online: the Guts&Bolts server, publishing and the Library (EditorOnline.cpp)
    void       renderServerDialog();
    void       renderPublishDialog();
    void       renderPluginLibrary();
    void       renderPublishModelDialog();   // File > Publish Selection to Library
    void       publishMenuItem();            // right-click: "Publish to Guts&Bolts..." (opens the dialog above)
    void       selectionMenuItems(bool inViewport);   // the rest of the right-click menu (Explorer and 3D view)
    void       renderMakeGearDialog();
    std::string m_fixPicturesMsg;
    // Pictures on parts that only live on this computer (an imported Roblox hat's texture) go up
    // as decals first, so the website and everyone else's game see them too. Then `done` runs
    // with the node's "texture"s pointing at them ("gb:<number>").
    void       uploadLocalTextures(nlohmann::json node, const std::string& name,
                                   std::function<void(nlohmann::json node, std::string error)> done);       // File > Make Gear (staff): start from a ready-made Tool, sell it in the catalog
    // The Toolbox's Library tiles (everyone's public models, decals and audio, or your own).
    std::vector<ToolboxTile> libraryTiles(bool mine, int kind, const std::string& query, bool reload, std::string& status);
    unsigned   libraryPicture(const nlohmann::json& asset);
    void       useLibraryAsset(const nlohmann::json& asset);
    void       renderAccessoryWindow();      // AVATAR > Accessories (Verified creators)
    void       renderPluginsTab();
    void       insertObject(const std::string& what, SceneNode* parent);
    SceneNode* aiFind(const std::string& ref);   // "#42" or "Workspace.Castle.Door"
    void       renderInsertObject();
    void       renderCommandBar();
    void       ungroupSelected();
    void       selectAll();
    void       selectParent();
    void       selectChildren();
    void       toggleHidden();
    void       renderShortcuts();
    void       renderCollisionGroups();   // the Collision Groups editor (PhysicsService's table)
    bool       canEdit(const SceneNode* n) const;
    glm::vec3  spawnPoint() const;

    // Files
    void newScene();
    // Save to a file. A game that's published is sent to the server too (`sync`), so the
    // website always has the newest version.
    void saveFile(const std::string& path, bool sync = true);
    void save();
    void updateTitle();
    // AutoSave / Auto-Recovery (EditorAutosave.cpp)
    void        autoSaveTick();
    void        dropAutoSave();
    void        renderRecoverDialog();
    double      m_lastAutoSave = 0.0;
    long long   m_autoSavedAt = 0, m_recoverTime = 0;
    bool        m_openRecover = false, m_recoverPending = false;   // (pending: keep the old backup until they choose)
    std::string m_recoverFrom;
    void exportRoblox(bool selectionOnly);

    // Undo / redo (whole-scene snapshots)
    void trackChanges();
    void undo();
    void redo();
    void restore(const std::string& snapshot);
    void resetHistory();


    GLFWwindow*                        m_window;
    Scene*                             m_scene;
    EditorState                        m_state;
    std::unique_ptr<GameSession>       m_session;
    std::unique_ptr<ViewportPanel>     m_viewport;
    std::unique_ptr<OutlinerPanel>     m_outliner;
    std::unique_ptr<PropertiesPanel>   m_properties;
    std::unique_ptr<EnvironmentPanel>  m_environment;
    std::unique_ptr<ToolboxPanel>      m_toolbox;
    std::unique_ptr<Thumbnailer>       m_thumbnailer;   // pictures of objects on their own
    std::unique_ptr<PlayerPanel>       m_player;
    std::unique_ptr<AssistantPanel>    m_assistant;   // AI chat + MCP for outside AI apps
    std::unique_ptr<OutputPanel>       m_output;
    std::unique_ptr<ScriptEditorPanel> m_scriptEditor;
    std::unique_ptr<AnimationEditor>   m_animEditor;
    std::unique_ptr<TeamCreate>        m_team;
    std::unique_ptr<Plugins>           m_plugins;
    // Online
    bool        m_openServer = false, m_openPublish = false, m_showPluginLibrary = false;
    std::string m_serverInput, m_publishName, m_publishDesc, m_publishMsg, m_marketMsg, m_marketQuery;
    int         m_marketTab = 0, m_publishPluginIndex = 0, m_publishPluginPrice = 0;
    bool        m_marketLoaded = false, m_onlineBusy = false;
    // Library: publishing a model (public or private) and browsing everyone's public ones.
    bool        m_openPublishModel = false, m_modelPublic = true, m_libraryLoaded = false;
    bool        m_modelAsGear = false;   // staff: sell a Tool in the catalog as Gear
    bool        m_openMakeGear = false, m_openPublishGear = false;
    int         m_gearPrice = 0;
    std::string m_modelName, m_modelDesc, m_modelMsg, m_libraryQuery, m_libraryMsg;
    int         m_libraryKind = 0;   // 0 models, 1 decals, 2 audio
    bool        m_showAccessory = false;
    int         m_accessoryKind = 0, m_accessoryPrice = 0;
    std::string m_accessoryName, m_accessoryDesc, m_accessoryMsg;
    nlohmann::json m_library = nlohmann::json::array();
    std::map<std::string, std::string> m_libraryThumbs;   // asset id -> picture file ("" = still coming)
    std::string m_libraryKey;                              // what m_library holds (mine/all, kind, search)
    std::vector<nlohmann::json> m_thumbJobs;               // downloaded models waiting for a picture
    nlohmann::json m_marketPlugins = nlohmann::json::array(), m_marketAudio = nlohmann::json::array();
    bool        m_openTeam = false;
    std::string m_teamAddress;
    std::string m_teamChat;
    std::string m_teamError;

    bool m_firstLayout = true;
    bool m_showSettings = false;
    bool m_playing     = false;
    int  m_objCounter  = 0;
    int  m_frame       = 0;

    std::string              m_playSnapshot;   // world before Play
    // AI tools: named save points (checkpoint tool) and a navmesh baked from the
    // game as it is (bake_navmesh / find_path work outside playtests too).
    std::map<std::string, std::string> m_aiCheckpoints;
    std::unique_ptr<Physics>           m_aiNav;
    std::vector<std::string> m_undo, m_redo;
    std::string              m_committed;      // last known scene state
    std::vector<std::string> m_clipboard;
    bool                     m_showShortcuts = false;
    bool                     m_showCollisionGroups = false;
    std::string              m_newGroupName, m_groupError;
    int                      m_renamingGroup = -1;      // the slot whose name is being edited
    std::string              m_renameText;
    int                      m_ribbonTab = 0;           // HOME / MODEL / TEST / VIEW (/ MESH in Modeling mode)
    int                      m_playMode = 0;
    bool                     m_openInsert = false;
    SceneNode*               m_insertParent = nullptr;
    std::string              m_insertFilter;
    std::string              m_cmdInput;
    std::vector<std::string> m_cmdHistory;
    int                      m_cmdHistoryPos = -1;
    enum Panel { kPanelExplorer, kPanelProperties, kPanelToolbox, kPanelOutput, kPanelCommandBar, kPanelScript,
                 kPanelLighting, kPanelPlayer, kPanelTeam, kPanelAnimation, kPanelAssistant, kPanelCount };
    bool                     m_showPanel[kPanelCount] = {true, true, true, true, true, true, true, true, true, false, true};
    void                     openAnimationEditor();
    bool                     m_focusAnim = false;
    std::function<void()>    m_deferred;       // tree changes asked for while the Explorer was drawing

    std::string m_path;                         // current file ("" = never saved)
    bool        m_dirty = false;
    std::string m_shownTitle;

    // Dialogs
    enum class Pending { None, New, Open, OpenDropped };
    Pending     m_pending = Pending::None;      // waiting on "discard changes?"
    bool        m_openSaveAs = false, m_openOpen = false, m_openDiscard = false,
                m_openInfo = false;
    std::string m_nameInput;
    std::string m_openPathInput;
    std::string m_droppedGame;                  // a game file dropped on Studio, opened after "discard changes?"
    SceneNode*  importModel(const std::string& path, glm::vec3 at, glm::vec3* size = nullptr);   // EditorImport.cpp
};
