#include "KeyBinds.h"
#include "Settings.h"

#include <nlohmann/json.hpp>
#include <cstring>
#include <string>

namespace KeyBinds {

static_assert(kCount == GraphicsSettings::kKeyActions, "GraphicsSettings::keys needs one slot per action");

namespace {
struct Info { const char* id; const char* label; ImGuiKey key; };
const Info kInfo[kCount] = {
    {"forward",   "Walk forward",              ImGuiKey_W},
    {"back",      "Walk back",                 ImGuiKey_S},
    {"left",      "Walk left",                 ImGuiKey_A},
    {"right",     "Walk right",                ImGuiKey_D},
    {"jump",      "Jump",                      ImGuiKey_Space},
    {"dive",      "Dive (swimming)",           ImGuiKey_C},
    {"emotes",    "Emote menu",                ImGuiKey_B},
    {"talk",      "Hold to talk (voice chat)", ImGuiKey_V},
    {"shiftLock", "Shift Lock",                ImGuiKey_LeftShift},
};
// Keyboard keys only (not the mouse or a controller).
bool isKeyboard(ImGuiKey k) { return k >= ImGuiKey_NamedKey_BEGIN && k < ImGuiKey_GamepadStart; }
int s_waiting = -1;             // the action waiting for a new key (-1 = none)
int s_drawnAt = -10;            // the frame the list was last drawn (closed meanwhile = stop waiting)
std::string s_note;             // "That key is for chat", shown under the list
} // namespace

const char* id(Action a)        { return kInfo[a].id; }
const char* label(Action a)     { return kInfo[a].label; }
ImGuiKey    defaultKey(Action a) { return kInfo[a].key; }

ImGuiKey key(Action a) {
    const int k = GraphicsSettings::get().keys[a];
    return k && isKeyboard((ImGuiKey)k) ? (ImGuiKey)k : kInfo[a].key;
}

bool down(Action a)    { return ImGui::IsKeyDown(key(a)); }
bool pressed(Action a) { return ImGui::IsKeyPressed(key(a), false); }
bool shiftLockPressed() {
    if (pressed(ShiftLock)) return true;
    return key(ShiftLock) == ImGuiKey_LeftShift && ImGui::IsKeyPressed(ImGuiKey_RightShift, false);
}

const char* keyName(ImGuiKey k) {
    switch (k) {   // (a few read better with a space)
        case ImGuiKey_LeftShift:  return "Left Shift";
        case ImGuiKey_RightShift: return "Right Shift";
        case ImGuiKey_LeftCtrl:   return "Left Ctrl";
        case ImGuiKey_RightCtrl:  return "Right Ctrl";
        case ImGuiKey_LeftAlt:    return "Left Alt";
        case ImGuiKey_RightAlt:   return "Right Alt";
        case ImGuiKey_CapsLock:   return "Caps Lock";
        case ImGuiKey_UpArrow:    return "Up Arrow";
        case ImGuiKey_DownArrow:  return "Down Arrow";
        case ImGuiKey_LeftArrow:  return "Left Arrow";
        case ImGuiKey_RightArrow: return "Right Arrow";
        default: break;
    }
    return isKeyboard(k) ? ImGui::GetKeyName(k) : "?";
}

ImGuiKey keyByName(const char* name) {
    if (!name || !*name) return ImGuiKey_None;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k)
        if (std::strcmp(ImGui::GetKeyName((ImGuiKey)k), name) == 0 || std::strcmp(keyName((ImGuiKey)k), name) == 0)
            return (ImGuiKey)k;
    return ImGuiKey_None;
}

const char* reserved(ImGuiKey k) {
    if (k == ImGuiKey_Escape) return "the menu";
    if (k == ImGuiKey_Enter || k == ImGuiKey_KeypadEnter || k == ImGuiKey_Slash) return "chat";
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return "picking a tool from your hotbar";
    if (k == ImGuiKey_Backspace) return "dropping the tool you're holding";
    if (k == ImGuiKey_Tab) return "the player list";
    if (k == ImGuiKey_F2) return "photo mode";
    if (k == ImGuiKey_F3) return "saving a clip";
    if (k == ImGuiKey_UpArrow || k == ImGuiKey_DownArrow || k == ImGuiKey_LeftArrow || k == ImGuiKey_RightArrow)
        return "walking (the arrow keys always walk)";
    return nullptr;
}

void set(Action a, ImGuiKey k) {
    if (!isKeyboard(k) || reserved(k)) return;
    GraphicsSettings& gs = GraphicsSettings::get();
    const ImGuiKey old = key(a);
    for (int i = 0; i < kCount; ++i)   // whoever had it gets this action's old key
        if (i != a && key((Action)i) == k) gs.keys[i] = old == kInfo[i].key ? 0 : (int)old;
    gs.keys[a] = k == kInfo[a].key ? 0 : (int)k;
    gs.save();
}

void reset() {
    GraphicsSettings& gs = GraphicsSettings::get();
    for (int i = 0; i < kCount; ++i) gs.keys[i] = 0;
    gs.save();
}

void useAzerty() {
    GraphicsSettings& gs = GraphicsSettings::get();
    reset();
    gs.keys[Forward] = ImGuiKey_Z;
    gs.keys[Left]    = ImGuiKey_Q;
    gs.save();
}

bool changed() {
    for (int i = 0; i < kCount; ++i) if (key((Action)i) != kInfo[i].key) return true;
    return false;
}

bool used(ImGuiKey k) {
    for (int i = 0; i < kCount; ++i) if (key((Action)i) == k) return true;
    return false;
}

nlohmann::json toJson(const int* keys) {
    nlohmann::json j = nlohmann::json::object();
    for (int i = 0; i < kCount; ++i)
        if (keys[i] && isKeyboard((ImGuiKey)keys[i]) && keys[i] != kInfo[i].key)
            j[kInfo[i].id] = ImGui::GetKeyName((ImGuiKey)keys[i]);
    return j;
}

void fromJson(const nlohmann::json& j, int* keys) {
    for (int i = 0; i < kCount; ++i) keys[i] = 0;
    if (!j.is_object()) return;
    for (int i = 0; i < kCount; ++i) {
        auto it = j.find(kInfo[i].id);
        if (it == j.end() || !it->is_string()) continue;
        const ImGuiKey k = keyByName(it->get<std::string>().c_str());
        if (k != ImGuiKey_None && !reserved(k)) keys[i] = k;
    }
}

bool waiting() { return s_waiting >= 0 && s_drawnAt >= ImGui::GetFrameCount() - 1; }

bool drawEditor(float labelWidth) {
    const int frame = ImGui::GetFrameCount();
    if (s_drawnAt < frame - 1) { s_waiting = -1; s_note.clear(); }   // (the settings were closed)
    s_drawnAt = frame;
    ImGui::PushID("keybinds");
    for (int i = 0; i < kCount; ++i) {
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(kInfo[i].label);
        ImGui::SameLine(labelWidth);
        const bool waiting = s_waiting == i;
        const bool moved = key((Action)i) != kInfo[i].key;
        if (waiting) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.15f, 1));
        else if (moved) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.8f, 1));
        const std::string text = waiting ? std::string("Press a key... (Esc cancels)") : keyName(key((Action)i));
        if (ImGui::Button(text.c_str(), ImVec2(-1, 0))) { s_waiting = waiting ? -1 : i; s_note.clear(); }
        if (waiting || moved) ImGui::PopStyleColor();
        if (moved && ImGui::IsItemHovered()) ImGui::SetTooltip("Usually %s", keyName(kInfo[i].key));
        ImGui::PopID();
    }
    // Waiting: the first keyboard key pressed is the new one.
    if (s_waiting >= 0) {
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k) {
            if (!ImGui::IsKeyPressed((ImGuiKey)k, false)) continue;
            if (k == ImGuiKey_Escape) { s_waiting = -1; break; }
            if (const char* why = reserved((ImGuiKey)k)) {
                s_note = std::string(keyName((ImGuiKey)k)) + " is kept for " + why + ". Pick another key.";
                break;
            }
            set((Action)s_waiting, (ImGuiKey)k);
            s_waiting = -1;
            s_note.clear();
            break;
        }
    }
    if (!s_note.empty()) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", s_note.c_str());
    if (ImGui::Button("Use Z Q S D (AZERTY)")) { useAzerty(); s_waiting = -1; s_note.clear(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("For French and Belgian keyboards: Z walks forward and Q walks left.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!changed());
    if (ImGui::Button("Back to the usual keys")) { reset(); s_waiting = -1; s_note.clear(); }
    ImGui::EndDisabled();
    ImGui::PopID();
    return s_waiting >= 0;
}

} // namespace KeyBinds
