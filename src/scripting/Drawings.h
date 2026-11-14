#pragma once
// AssetService for games: EditableImages to draw on, and PromptPublishImageAsync so a player can
// publish what they drew into their own inventory (Drawings.cpp).
struct lua_State;

namespace Drawings {
// Adds AssetService (CreateEditableImage, PromptPublishImageAsync). Runs after the prelude.
void install(lua_State* L);
}
