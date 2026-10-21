#pragma once

// The "Settings" window: frame rate and graphics quality. Used by both the
// editor and the Player app. Changes apply instantly and are saved.
namespace SettingsWindow {
void draw(bool* open);
// Just the language list (also in the Player's in-game menu).
void languagePicker(float width = 0.0f);
}
