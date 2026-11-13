#pragma once
// Publishing a picture a player drew in a game (AssetService:PromptPublishImageAsync).
//
// The game asks; a window asks the player ("Publish 'My cat' to your inventory?") with the
// picture shown; if they say yes, it's uploaded from their own computer with their own
// account as a decal: theirs, in their inventory, and usable in any game (decals are
// Open Use on Guts&Bolts). Pictures from players who aren't Verified get the usual staff
// check before anyone else sees them. Publishing a drawing is free (no upload fee).
#include <string>

namespace ImagePublish {

enum class Status { Waiting, Published, Cancelled, Failed };

// A new question for the player. Returns its ticket (0 if a question is already showing).
// `png` is the picture; `game` the game's id (kept with the decal, so its page can say where it came from).
int ask(const std::string& png, int width, int height, const std::string& name, const std::string& description,
        const std::string& game);
// How it went. `assetId` / `message` are filled in once it isn't Waiting.
Status result(int ticket, std::string& assetId, std::string& message);

// The ticket whose window is showing (0 = none).
int pending();
// The window (call once a frame while a game shows, inside an ImGui frame).
void draw();
// Say yes or no without the window (tests, and the window's buttons).
void answer(int ticket, bool publish);
// Forget everything (a game run ends).
void reset();

} // namespace ImagePublish
