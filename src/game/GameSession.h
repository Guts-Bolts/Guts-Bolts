#pragma once
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "../scene/Physics.h"
#include "../scripting/ScriptEngine.h"
#include "GameGui.h"

class Scene;

// Everything that happens while a game is running: scripts, physics, the
// character and Touched / Died events. The editor's Play button and the
// Guts&BoltsPlayer app both drive the game through this class.
class GameSession {
public:
    // Solo = normal. Host = like Solo, but others can join. Client = joined
    // someone else's game: no scripts or world physics here (the host runs
    // them); we only move our own character and report touches / clicks.
    enum class Role { Solo, Host, Client };

    explicit GameSession(Scene* scene);

    void setRole(Role r) { m_role = r; m_scripts.setNetworked(r != Role::Solo); }
    // Studio's "Run": the world simulates and scripts run, but there's no player.
    void setRunOnly(bool on) { m_runOnly = on; }
    bool runOnly() const { return m_runOnly; }
    Role role() const { return m_role; }
    // Client mode: where touches and clicks get sent (to the host).
    std::function<void(uint64_t part, const std::string& limb)> onTouch;
    std::function<void(uint64_t part)>                          onClick;
    std::function<void(uint64_t button)>                        onGuiClick;   // a game UI button
    std::function<void(uint64_t box, const std::string& text, bool enter)> onGuiText;   // a TextBox typed in (when it loses focus)
    // A ProximityPrompt pressed: "t" Triggered, "te" TriggerEnded, "hb" / "he" hold began / ended.
    std::function<void(uint64_t prompt, const char* what)> onPrompt;
    // A ClickDetector: "c" MouseClick, "r" RightMouseClick, "he" / "hl" MouseHoverEnter / Leave.
    std::function<void(uint64_t detector, const char* what)> onClickDetector;
    // Client mode: tool requests for the host ("equip" with a tool id or 0, "drop",
    // "use" with down = mouse pressed / let go). The host keeps everyone's tools.
    std::function<void(const std::string& what, uint64_t tool, bool down)> onToolRequest;

    void start();
    void stop();
    bool running() const { return m_running; }
    // Studio multiplayer tests can collect a low-rate trace of effective character input.
    void setInputTracePath(const std::string& path);

    // One frame. `cameraYaw` makes WASD move relative to the camera;
    // `acceptInput` = false ignores the keyboard (e.g. while typing).
    // swimLook: the camera's up/down for swimming (see Player::setSwimInput).
    void update(float dt, float cameraYaw, bool acceptInput, float swimLook = 0.0f);
    void click(uint64_t partId);                 // left-click in the 3D view (a ClickDetector's MouseClick too)
    // ClickDetectors. The part under the pointer each frame (0 = none): fires MouseHoverEnter /
    // MouseHoverLeave as that changes. True when it can be clicked (show a hand); `cursor`
    // gets its CursorIcon ("" = the hand).
    bool hoverPart(uint64_t partId, std::string* cursor = nullptr);
    void rightClick(uint64_t partId);            // RightMouseClick
    void mouseDown(int button);                  // the right (2) or middle (3) button went down on the game view
    bool clickDetector(uint64_t partId);         // just MouseClick (a controller's RT); false if there's none in reach
    void useTool();                              // the held tool's Activated (a click, a controller's RT)
    // Game UI: pointer events from GameGui (clicks go to the host in multiplayer).
    void guiEvents(const std::vector<GameGui::Event>& events);
    // On-screen joystick (x = right, y = forward) and jump button, for touch screens.
    void setTouchInput(glm::vec2 move, bool jump) { m_touchMove = move; m_touchJump = jump; }

    // Tools: the held tool's number keys, Backspace to drop, a hotbar click.
    void selectToolSlot(int slot);
    // Your gear from the catalog: given now and again each time you respawn, like a
    // StarterPack tool. (Only where tools run: playing alone or hosting.)
    void addGear(std::unique_ptr<SceneNode> tool);
    void dropTool();

    // Hosting: the tools of people who joined (their characters live here, and the
    // tools' scripts run here). rig = their character's root.
    void joinerArrived(uint64_t rig);                    // their StarterPack tools
    void joinerRespawned(uint64_t rig);                  // back to just the StarterPack, like Roblox
    void joinerEquip(uint64_t rig, uint64_t toolId);     // 0 = put it away
    void joinerDrop(uint64_t rig);
    void joinerUse(uint64_t rig, bool down);             // Activated / Deactivated
    void joinerTouched(uint64_t rig, uint64_t part);     // walking into a tool picks it up
    std::vector<SceneNode*> joinerTools(uint64_t rig);   // hotbar order, held one included

    ScriptEngine& scripts() { return m_scripts; }
    GuiState&     gui()     { return m_scripts.gui(); }

private:
    Scene*       m_scene;
    Physics      m_physics;
    ScriptEngine m_scripts;
    bool         m_running = false;
    Role         m_role = Role::Solo;
    glm::vec2    m_touchMove{0.0f};
    bool         m_touchJump = false;
    bool         m_runOnly = false;
    std::ofstream m_inputTrace;
    double       m_traceStartTime = 0.0;
    double       m_traceNextAt = 0.0;
    uint64_t     m_traceSamples = 0;

    // Tools
    void setupTools();                         // take StarterPack tools out, hand them out
    void giveStarterTools();
    bool giveTo(uint64_t rig, SceneNode* tool);          // a joiner's backpack (false if full)
    void holdTools();                                    // everyone else's held tool, in their hand
    void pickUpTools(const std::vector<TouchEvent>& touches);
    void reachCheckpoints(const std::vector<TouchEvent>& touches);   // parts called "Checkpoint" (and SpawnLocations' team change)
    std::vector<std::unique_ptr<SceneNode>> m_starterPack;   // templates (like Roblox's StarterPack)
    std::vector<std::unique_ptr<SceneNode>> m_gear;          // your own gear (only yours, not joiners')
    std::unordered_map<uint64_t, double> m_noPickupUntil;    // just dropped: don't grab it straight back
    double       m_time = 0.0;
    bool         m_toolDown = false;          // mouse held after activating the tool
    bool         m_padTrigger = false;        // a controller's RT held
    uint64_t     m_hoverDetector = 0;         // the ClickDetector the pointer is on
    SceneNode*   reachableDetector(uint64_t partId);
    void         fireDetector(SignalKind kind, const char* what, uint64_t detector);
};
