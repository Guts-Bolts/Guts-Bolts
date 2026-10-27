#pragma once
// The iPhone / iPad bits of the phone window that SDL2 doesn't do for us
// (IosPlatform.mm, built only for iOS).
struct SDL_Window;

namespace IosPlatform {

// How much of each edge the notch, rounded corners and home bar cover, in the
// same points as SDL's window size. All 0 when there's nothing in the way.
struct SafeArea { float left = 0, top = 0, right = 0, bottom = 0; };
SafeArea safeArea(SDL_Window* window);

// true = games: landscape only (the phone turns the screen now if it can).
// false = the site: portrait or landscape, whichever way the phone is held.
void setLandscape(SDL_Window* window, bool on);

} // namespace IosPlatform
