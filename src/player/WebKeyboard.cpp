#include "WebKeyboard.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <imgui.h>

// The page's hidden text box (web/index.html) sends what's typed on the phone's keyboard.
extern "C" {
EMSCRIPTEN_KEEPALIVE void gb_webChar(unsigned int c) { ImGui::GetIO().AddInputCharacter(c); }
// 1 = Backspace, 2 = Enter (pressed and let go).
EMSCRIPTEN_KEEPALIVE void gb_webKey(int k) {
    const ImGuiKey key = k == 1 ? ImGuiKey_Backspace : ImGuiKey_Enter;
    ImGui::GetIO().AddKeyEvent(key, true);
    ImGui::GetIO().AddKeyEvent(key, false);
}
}

void WebKeyboard::update() {
    // A text box (ours, or a game's TextBox) wants typing: the page opens the keyboard on
    // the next tap (Safari only allows that during a tap). Done typing: it closes.
    static bool was = false;
    const bool want = ImGui::GetIO().WantTextInput;
    if (want == was) return;
    was = want;
    EM_ASM({
        Module.gbWantsText = !!$0;
        const kb = document.getElementById('kb');
        if (kb && !$0 && document.activeElement === kb) kb.blur();
    }, want ? 1 : 0);
}
#else
void WebKeyboard::update() {}
#endif
