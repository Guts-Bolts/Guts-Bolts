#pragma once
// Roblox members scripts use a lot that Guts&Bolts objects didn't have yet, written in Lua
// on top of the ones they do have: Model:MoveTo / GetBoundingBox / ScaleTo, part:Resize /
// GetConnectedParts / CanQuery, Attachment.WorldCFrame, Lighting.TimeOfDay / GetSunDirection,
// RunService:BindToRenderStep, UserInputService:GetKeysPressed, TextLabel.TextBounds,
// Camera:GetPartsObscuringTarget and more. They're asked last (see extraGet in
// LuaInstance.cpp), so an object's own member always wins.
struct lua_State;
namespace RobloxMembers {
// Runs after the prelude and LuauLibs.
void install(lua_State* L);
}
