#include "ScriptHistory.h"
#include "LuaApi.h"

namespace ScriptHistory {
namespace {
bool              s_on = false;
std::deque<Entry> s_entries;
uint64_t          s_added = 0;
} // namespace

bool recording() { return s_on; }
void setRecording(bool on) { s_on = on; }

void add(Entry e) {
    s_entries.push_back(std::move(e));
    while (s_entries.size() > kMax) s_entries.pop_front();
    ++s_added;
}

void clear() { s_entries.clear(); ++s_added; }
const std::deque<Entry>& entries() { return s_entries; }
uint64_t added() { return s_added; }

const char* signalName(SignalKind kind) {
    // The same order as SignalKind (LuaApi.h).
    static const char* names[] = {
        "Heartbeat", "InputBegan", "InputEnded", "Touched", "Clicked", "Died", "PlayerAdded", "PlayerRemoving",
        "AttributeChanged", "TagAdded", "TagRemoved", "Activated", "Deactivated", "Equipped", "Unequipped",
        "Changed", "Stopped", "Ended", "DidLoop", "KeyframeReached", "MouseButton1Click", "MouseEnter",
        "MouseLeave", "Focused", "FocusLost", "MoveToFinished", "OnServerEvent", "OnClientEvent",
        "Triggered", "TriggerEnded", "PromptButtonHoldBegan", "PromptButtonHoldEnded", "PromptShown",
        "PromptHidden", "MouseClick", "RightMouseClick", "MouseHoverEnter", "MouseHoverLeave",
        "CharacterAdded", "CharacterRemoving"};
    static_assert(sizeof(names) / sizeof(names[0]) == (int)SignalKind::CharacterRemoving + 1,
                  "add the new SignalKind's name here");
    int i = (int)kind;
    return i >= 0 && i < (int)(sizeof(names) / sizeof(names[0])) ? names[i] : "?";
}

} // namespace ScriptHistory
