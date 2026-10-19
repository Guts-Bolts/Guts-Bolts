#pragma once
#include <imgui.h>

class Scene;

// The emote menu: a wheel of up to 8 emotes in the middle of the screen. B opens and
// closes it (phones have a button, see TouchControls); click or tap an emote to play
// it. It shows the emotes you equipped on your avatar (Emotes::menu()), or the
// built-in ones if you haven't equipped any. Games turn it off with
// StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu, false), and scripts can
// open it with GuiService:SetEmotesMenuOpen(true). Its open / closed state lives in
// scene.coreGui().emotesOpen.
namespace EmoteMenu {

// Once a frame while playing (draws it when it's open). `keys`: B may open / close it
// (false while typing). `tap`: a finger's tap this frame, or null; `mouse`: mouse
// clicks count. Returns true when this frame's click / tap was the menu's (so the game
// doesn't also use it).
bool update(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, bool keys, const ImVec2* tap, bool mouse = true);

// Can this player use it right now? (a character, not turned off by the game)
bool available(Scene& scene);
// Open or close it (if it's available).
void toggle(Scene& scene);

} // namespace EmoteMenu
