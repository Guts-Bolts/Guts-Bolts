#pragma once
// Internal glue shared by the Lua binding files. Not needed outside scripting/.
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cstdint>
#include <glm/glm.hpp>
#include "../scene/NavMesh.h"

class ScriptEngine;
class SceneNode;
struct UDim2;

// Events a script can :Connect() to.
enum class SignalKind : int {
    Heartbeat,     // RunService.Heartbeat            (dt)
    InputBegan,    // UserInputService.InputBegan     (input, gameProcessed)
    InputEnded,    // UserInputService.InputEnded     (input, gameProcessed)
    Touched,       // part.Touched                    (otherPart)
    Clicked,       // part.Clicked                    ()
    Died,          // Humanoid.Died                   ()   (id = character)
    PlayerAdded,   // Players.PlayerAdded             (player)
    PlayerRemoving,// Players.PlayerRemoving          (player)
    AttributeChanged, // obj.AttributeChanged         (name)
    TagAdded,      // CollectionService (id 0)        (object, tag)
    TagRemoved,    // CollectionService (id 0)        (object, tag)
    Activated,     // tool.Activated                  ()   (the player clicked while holding it)
    Deactivated,   // tool.Deactivated                ()
    Equipped,      // tool.Equipped                   ()
    Unequipped,    // tool.Unequipped                 ()
    Changed,       // value.Changed                   (newValue)
    AnimStopped,   // track.Stopped                   ()   (id = track)
    AnimEnded,     // track.Ended                     ()
    AnimDidLoop,   // track.DidLoop                   ()
    KeyframeReached, // track.KeyframeReached         (keyframeName)
    GuiClick,      // button.MouseButton1Click / .Activated  ()
    GuiEnter,      // guiObject.MouseEnter            ()
    GuiLeave,      // guiObject.MouseLeave            ()
    GuiFocused,    // textBox.Focused                 ()
    GuiFocusLost,  // textBox.FocusLost               (enterPressed)
    MoveToFinished, // humanoid.MoveToFinished        (reached)   (id = character)
    RemoteServer,  // remoteEvent.OnServerEvent       (player, ...)
    RemoteClient,  // remoteEvent.OnClientEvent       (...)
    // ProximityPrompts: on the prompt (player), and on ProximityPromptService (id 0: prompt, player)
    PromptTriggered,     // prompt.Triggered
    PromptTriggerEnded,  // prompt.TriggerEnded
    PromptHoldBegan,     // prompt.PromptButtonHoldBegan
    PromptHoldEnded,     // prompt.PromptButtonHoldEnded
    PromptShown,         // prompt.PromptShown    (inputType)  (on the computer it shows on)
    PromptHidden,        // prompt.PromptHidden   ()
    // ClickDetectors (player): the computer it happened on, and the game's server
    ClickMouseClick,     // detector.MouseClick       (player)
    ClickRightClick,     // detector.RightMouseClick  (player)
    ClickHoverEnter,     // detector.MouseHoverEnter  (player)
    ClickHoverLeave,     // detector.MouseHoverLeave  (player)
};

namespace LuaApi {

ScriptEngine* engine(lua_State* L);

// Value types (LuaTypes.cpp)
void       registerTypes(lua_State* L);
void       pushVector3(lua_State* L, const glm::vec3& v);
glm::vec3* toVector3  (lua_State* L, int idx);            // null if not a Vector3
glm::vec3  checkVector3(lua_State* L, int idx);
void       pushColor3 (lua_State* L, const glm::vec3& c);
glm::vec3  checkColor3(lua_State* L, int idx);
glm::vec3* toColor3   (lua_State* L, int idx);            // null if not a Color3
void       pushCFrame (lua_State* L, const glm::mat4& m);
glm::mat4* toCFrame   (lua_State* L, int idx);
glm::mat4  checkCFrame(lua_State* L, int idx);
void       pushUDim2  (lua_State* L, const UDim2& u);      // game UI sizes / positions
UDim2*     toUDim2    (lua_State* L, int idx);
UDim2      checkUDim2 (lua_State* L, int idx);
void       pushUDim   (lua_State* L, float scale, float offset);
glm::vec2  checkUDim  (lua_State* L, int idx);             // (scale, offset)
void       pushVector2(lua_State* L, const glm::vec2& v);
glm::vec2* toVector2  (lua_State* L, int idx);
glm::vec2  checkVector2(lua_State* L, int idx);

// PathfindingService settings: a CreatePath-style table at idx (or nil) -> what the
// navmesh needs (ScriptEngine.cpp).
NavMesh::Agent checkAgent(lua_State* L, int idx);

// Objects (LuaInstance.cpp)
void       registerInstance(lua_State* L);
void       pushInstance(lua_State* L, uint64_t id);        // pushes nil for 0
SceneNode* checkNode   (lua_State* L, int idx);            // errors if destroyed
uint64_t   toInstanceId(lua_State* L, int idx);            // 0 if it isn't an object
void       pushSignal  (lua_State* L, SignalKind kind, uint64_t id);
void       pushHumanoid(lua_State* L, uint64_t characterRootId);
void       pushLighting(lua_State* L);
void       pushCamera  (lua_State* L);                     // workspace.CurrentCamera (always the same one)
void       pushValue   (lua_State* L, const SceneNode& valueObject);   // a Value object's .Value

} // namespace LuaApi
