#pragma once
#include "AiTools.h"
#include <functional>
#include <memory>
#include <set>
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
class MultiTest;

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
    // The same, for a call from an AI (source: "Assistant" or "AI app"): it goes in the AI
    // Activity log, its changes in the change report, and risky ones wait for the
    // person's OK first (EditorAiActivity.cpp).
    AiToolResult runAiToolFrom(const std::string& source, const std::string& name, const nlohmann::json& args);
    void showAiActivity() { m_showAiActivity = true; }
    bool aiHasChanges() const { return !m_aiBaseline.empty(); }
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
    // TEST > Players: "Test with N players" (see MultiTest.h). Starts a local test server
    // with the game as it is now, and N Player windows that join it. False if it couldn't.
    bool startMultiTest(int players, std::string& error);
    void stopMultiTest();
    MultiTest& multiTest() { return *m_multiTest; }

private:
    void buildDockspace();
    void renderMenuBar();
    void renderToolbar();
    void renderStatusBar();
    void renderDialogs();
    void renderTeamPanel();
    void renderPerformanceWindow();
    void renderPerformanceDoctor();
    void renderLuaDebugger();
    void renderSceneValidator();
    void renderProjectTimeMachine();
    void renderMaterialEditor();    // EditorMaterials.cpp
    void renderScriptHistory();     // EditorScriptHistory.cpp
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
public:   // (the menu tables in EditorMeshMenus.cpp name these)
    enum class MeshOp {
        Extrude, Inset, Subdivide, Delete, Merge, Fill, Flip, Smooth, SelectAll, SelectNone, Invert,
        // Blender-style ones (EditorMeshMenus.cpp has their menus)
        Grab, Rotate, Scale, ExtrudeMove, DuplicateMove, InsetMove, LoopCut,
        DeleteVerts, DeleteEdges, DeleteFaces, DissolveVerts, DissolveEdges, DissolveFaces,
        Bridge, Triangulate, TrisToQuads, NormalsOut, NormalsIn, SmoothVerts, MergeByDistance, Collapse,
        SubdivideSmooth, MirrorX, MirrorY, MirrorZ, Spin, ShadeSmooth, ShadeFlat, Separate,
        SelectLinked, SelectGrow, SelectShrink, SelectBoundary, SelectNonManifold, SelectLoop, SelectRing,
        Hide, HideOthers, Reveal, Proportional,
    };
private:
    void       enterModeling();
    void       exitModeling();
    void       checkModeling();
    void       meshOp(MeshOp op);
    void       handleModelingKeys();
    // Modeling mode's menus (EditorMeshMenus.cpp): right-click, X delete, M merge,
    // Shift+A add, Ctrl+V / E / F vertex / edge / face, F3 search, Alt+N normals.
    void       renderModelingMenus();
    void       modelingContextMenu();             // the right-click menu (inside the viewport's popup)
    bool       meshMenuItems(int menu);           // one menu's items; true if one was clicked
    void       addMeshShape(int shape);           // Shift+A: a new shape into the mesh being modelled
    void       joinSelected();                    // Ctrl+J (Build mode): picked mesh parts -> one
    bool       meshOpNamed(const std::string& name);   // --test-mesh "op:loop-cut": a menu action by its name
    std::string m_meshMenu;                       // a menu to open at the mouse next frame
    std::string m_meshSearch;                     // F3: what's typed
    void       addMeshPart();
    // Armatures: bones inside MeshParts (EditorArmature.cpp)
    void       renderArmature();
    SceneNode* armaturePart();                          // the part the Armature window shows
    SceneNode* addBone(SceneNode* where, int chain);    // in a part or bone; `chain` bones in a row
    // UV mapping: the UV Editor window and the U menu (EditorUV.cpp)
    void       renderUVEditor();
    void       unwrapMesh(int how);          // a MeshEdit::Unwrap
    void       markSeams(bool on);
    SceneNode* uvNode();                     // the MeshPart the UV Editor shows
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
    std::vector<ToolboxTile> libraryTiles(bool mine, int kind, const std::string& query, bool bestRated, bool reload, std::string& status);
    unsigned   libraryPicture(const nlohmann::json& asset);
    void       useLibraryAsset(const nlohmann::json& asset);
    void       renderAccessoryWindow();      // AVATAR > Accessories (Verified creators)
    void       renderBundleWindow();         // AVATAR > Bundles (staff): bundles with a new body
    void       renderPluginsTab();
    void       insertObject(const std::string& what, SceneNode* parent);
    // "#42", "Workspace.Castle.Door" or a service path like "StarterPack" / "game.ServerScriptService.Main".
    // `create`: make a service's folder (StarterPack, StarterPlayer.StarterPlayerScripts...) if it isn't there yet.
    SceneNode* aiFind(const std::string& ref, bool create = false);
    // File > Publish (the dialog and the AI tools' publish_game): sends the game to the
    // Guts&Bolts server as the signed-in account. `update` replaces the published copy.
    // `done` gets the server's reply ({ok, error?, asset?}).
    void       publishGame(const std::string& name, const std::string& description, bool update,
                           std::function<void(const nlohmann::json&)> done);
    void       renderInsertObject();
    void       renderCommandBar();
    void       ungroupSelected();
    void       selectAll();
    void       selectParent();
    void       selectChildren();
    void       toggleHidden();
    void       renderShortcuts();
    void       renderCollisionGroups();   // the Collision Groups editor (PhysicsService's table)
public:
    // Find and Change (EditorBulkEdit.cpp): find objects by kind / name / tag / value and change them all at once.
    struct BulkQuery {
        std::string name, cls, nameHas, tag, notTag, whereProp, whereOp = "=", whereVal, setProp, op = "Set to", value;
        bool        inSelection = false;
    };
private:
    struct BulkRow { uint64_t id; std::string old, now, error; };
    void       renderBulkEdit();
    void       bulkRun(bool apply);
    bool                   m_showBulkEdit = false, m_bulkPreviewed = false, m_bulkLoaded = false;
    BulkQuery              m_bulk;
    std::vector<BulkRow>   m_bulkRows;
    std::vector<BulkQuery> m_bulkSaved;
    std::string            m_bulkStatus, m_bulkSaveName;
    int                    m_bulkChanged = -1, m_bulkBad = 0;   // after Apply
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
    std::unique_ptr<MultiTest>         m_multiTest;     // TEST > Players: Test with N players
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
    bool        m_showArmature = false;
    uint64_t    m_armBone = 0;   // the bone picked in the Armature window
    struct UVEditorState {
        bool      show = false;
        bool      hovered = false;      // the mouse was over it last frame: keys go to it
        bool      openUnwrap = false;   // U: open the Unwrap menu
        bool      checker = true;       // a checker picture behind the layout when the part has no texture
        float     zoom = 0.9f;
        glm::vec2 pan{0.5f, 0.5f};      // the UV point in the middle of the view
        uint64_t  node = 0;
        std::vector<std::vector<char>> sel;   // picked UV corners (per face, per corner)
        int       modal = 0;            // 0 none, 1 move (G), 2 rotate (R), 3 scale (S)
        int       axis = -1;            // -1 free, 0 only U, 1 only V
        glm::vec2 startMouse{0.0f}, pivot{0.0f};
        std::vector<std::vector<glm::vec2>> before;
        bool      boxing = false;
        glm::vec2 boxStart{0.0f};
    } m_uv;
    int         m_accessoryKind = 0, m_accessoryPrice = 0;
    std::string m_accessoryName, m_accessoryDesc, m_accessoryMsg, m_accessoryEditId;
    bool        m_showBundles = false, m_bundleHeadless = false;
    int         m_bundlePrice = 0;
    std::string m_bundleName, m_bundleDesc, m_bundleItems, m_bundleMsg;
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
    bool m_showPerformance = false;
    bool m_showPerformanceDoctor = false;
    bool m_showLuaDebugger = false;
    int m_debugLineInput = 1;
    bool m_showSceneValidator = false;
    bool m_showProjectTimeMachine = false;
    bool m_showMaterialEditor = false;
    uint64_t    m_materialEditing = 0;    // the MaterialVariant shown in the Material Editor
    std::string m_materialOldName;        // its name before a rename (parts follow it)
    bool m_showScriptHistory = false;
    std::string m_historyFilter;
    int  m_historyKind = 0;           // 0 every entry, 1 property changes, 2 events
    bool m_historySelected = false;   // only the selected object's
    bool m_timeMachineLoaded = false;
    std::string m_timeMachineProjectKey, m_timeMachineCheckpointName;
    int m_timeMachineSelected = -1;
    struct TimeMachineCheckpoint { std::string project, name, snapshot; long long time = 0; };
    std::vector<TimeMachineCheckpoint> m_timeMachineCheckpoints;
    int m_sceneValidatorSeverityFilter = 0; // 0 = all, 1 = errors, 2 = warnings, 3 = info
    std::string m_sceneValidatorSearch;
    double m_sceneValidatorLastScan = -100.0;
    struct SceneValidationIssue {
        uint64_t nodeId = 0;
        int severity = 1; // 2 = error, 1 = warning, 0 = info
        std::string path, category, message, recommendation;
    };
    std::vector<SceneValidationIssue> m_sceneValidationIssues;
    int m_sceneValidatorObjectCount = 0;
    struct PerformanceDoctorScript { uint64_t id = 0; std::string path; std::size_t sourceBytes = 0; };
    double m_perfDoctorLastScan = -100.0;
    int m_perfDoctorObjects = 0, m_perfDoctorParts = 0, m_perfDoctorDynamicParts = 0;
    int m_perfDoctorLights = 0, m_perfDoctorEmitters = 0, m_perfDoctorScriptsCount = 0;
    std::size_t m_perfDoctorTotalScriptBytes = 0;
    std::vector<PerformanceDoctorScript> m_perfDoctorLargestScripts;
    float m_perfCpuFrameMs = 0.0f, m_perfViewportMs = 0.0f, m_perfSimulationMs = 0.0f;
    bool m_perfSampleMode = false; // last mode represented by the rolling frame samples
    std::vector<float> m_perfFrameSamples; // rolling frame-delta samples in milliseconds
    int m_perfHitchCount = 0; // frames over 33 ms in the current rolling sample window
    bool m_playing     = false;
    int  m_objCounter  = 0;
    int  m_frame       = 0;

    std::string              m_playSnapshot;   // world before Play
    // AI tools: named save points (checkpoint tool) and a navmesh baked from the
    // game as it is (bake_navmesh / find_path work outside playtests too).
    std::map<std::string, std::string> m_aiCheckpoints;
    std::unique_ptr<Physics>           m_aiNav;
    nlohmann::json                     m_aiPublish;   // the publish_game tool's last publish: {state, name, ...}
    // AI Activity (EditorAiActivity.cpp): every AI tool call, and what the AI changed since
    // the person last pressed Keep / Undo all (the game before its first change).
    struct AiLogEntry {
        long long   time = 0;
        std::string source, tool, args, result;   // result: "" = it worked, else the error
        int added = 0, removed = 0, changed = 0;  // what this call did to the game
        bool        settings = false;             // ...and to Lighting / game settings
    };
    std::vector<AiLogEntry> m_aiLog;
    bool        m_aiLogLoaded = false;
    std::string m_aiBaseline, m_aiBaselinePath;   // the game before the AI's changes ("" = none)
    bool        m_aiAskFirst = true;              // ask before deleting / restoring
    struct AiAsk { std::string source, tool, summary; nlohmann::json args; };
    std::vector<AiAsk>    m_aiAsks;               // waiting for Allow / Don't allow
    std::set<std::string> m_aiAllowed, m_aiRefused;   // tool + args the person answered (used once)
    bool        m_showAiActivity = false;
    void        loadAiLog();
    void        saveAiLog() const;
    void        aiKeepChanges();
    bool        aiUndoChanges();                  // false = nothing to undo / playtesting
    nlohmann::json aiChangeReport();
    void        renderAiActivity();
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
