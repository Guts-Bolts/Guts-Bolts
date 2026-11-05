#pragma once
#include <imgui.h>
#include <nlohmann/json_fwd.hpp>

// Your own keys (Settings > Controls > Change keys). Everyone can move the game's
// keys somewhere else: French (AZERTY) keyboards walk with Z Q S D, left-handed
// players can use the arrow side, and so on. Saved on this computer in
// settings.json ("keys"), only the ones you changed.
//
// The arrow keys always walk too, and Left Ctrl always dives, whatever you pick.
// Scripts (UserInputService / ContextActionService) still see the real keys.
namespace KeyBinds {

enum Action { Forward, Back, Left, Right, Jump, Dive, Emotes, Talk, ShiftLock, kCount };

const char* id(Action a);           // its name in settings.json ("forward")
const char* label(Action a);        // what it does, for the settings ("Walk forward")
ImGuiKey    defaultKey(Action a);   // the usual key (W, S, A, D, Space, C, B, V, Shift)
ImGuiKey    key(Action a);          // the key it's on now (yours, or the usual one)

bool down(Action a);                // held right now
bool pressed(Action a);             // went down this frame
bool shiftLockPressed();            // the Shift Lock key (on Shift, either Shift works)

const char* keyName(ImGuiKey k);    // "W", "Space", "Left Shift"
ImGuiKey    keyByName(const char* name);   // ImGuiKey_None if there's no such key

// A key the game already uses for something you can't move (Esc, Enter, /, 0-9...):
// what it's for, or nullptr if it's free to use.
const char* reserved(ImGuiKey k);

// Put an action on a key. Another action that had that key swaps over to the old one,
// so two things never share a key. Saves the settings.
void set(Action a, ImGuiKey k);
void reset();        // everything back to the usual keys
void useAzerty();    // walk with Z Q S D (French keyboards)
bool changed();      // any key isn't the usual one
bool used(ImGuiKey k);   // one of the actions is on this key

// settings.json's "keys" (for GraphicsSettings::keys): only the changed ones, {"forward": "Z", ...}.
nlohmann::json toJson(const int* keys);
void           fromJson(const nlohmann::json& j, int* keys);

// The "Change keys" list: each action with a button showing its key. Click the
// button, press the new key (Esc cancels). Returns true while waiting for a key,
// so the caller can keep the game from reacting to it.
bool drawEditor(float labelWidth);
bool waiting();   // the list (drawn last frame) is waiting for a key: other key shortcuts should wait

} // namespace KeyBinds
