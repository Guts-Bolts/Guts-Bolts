// AssetService for games: pictures a game draws on (EditableImage), and letting a player publish
// what they drew into their own inventory (renderer/EditableImages.h, game/ImagePublish.h).
//
//   local AssetService = game:GetService("AssetService")
//   local canvas = AssetService:CreateEditableImage({ Size = Vector2.new(256, 256) })
//   canvas:DrawRectangle(Vector2.new(0, 0), canvas.Size, Color3.new(1, 1, 1), 0)     -- white paper
//   canvas:DrawCircle(Vector2.new(128, 128), 20, Color3.new(1, 0, 0), 0)            -- a red dot
//   canvas:DrawLine(Vector2.new(10, 10), Vector2.new(200, 90), Color3.new(0, 0, 0), 0, nil, 4)
//   imageLabel.Image = canvas.ContentId                                            -- show it (or a Decal's Texture)
//   -- in a LocalScript, when the player presses "Publish":
//   local result, assetId, message = AssetService:PromptPublishImageAsync(player, canvas, { Name = "My cat" })
//   -- result: "Published" (assetId is the new decal's id), "Cancelled" or "Failed" (message says why)
//
// More: canvas:SetPixel(pos, color [, transparency]), canvas:GetPixel(pos) -> Color3, transparency,
// canvas:Clear([color, transparency]), canvas:ReadPixels(pos, size) -> {r, g, b, a, ...},
// canvas:WritePixels(pos, size, {r, g, b, a, ...}), canvas:Destroy(). Colours and alpha are 0..1.
// The last value of the Draw ones is Enum.ImageCombineType.Overwrite (replace) or BlendSourceOver (mix, the default).
// Pictures stay on the computer that made them, like Roblox's EditableImages; at most 1024 x 1024 each.
#include "Drawings.h"
#include "LuaApi.h"
#include "ScriptEngine.h"
#include "../core/Log.h"
#include "../game/ImagePublish.h"
#include "../renderer/EditableImages.h"

#include <cmath>
#include <cstring>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace Drawings {
namespace {

EditableImages::Colour colourAt(lua_State* L, int idx) {
    EditableImages::Colour c;
    c.r = (float)luaL_checknumber(L, idx);
    c.g = (float)luaL_checknumber(L, idx + 1);
    c.b = (float)luaL_checknumber(L, idx + 2);
    c.a = (float)luaL_checknumber(L, idx + 3);
    return c;
}

// __gb_ei(op, ...): the drawing (the Lua side checks the values first).
//   "new" w h -> id | nil      "free" id          "content" id -> "editable://n"
//   "rect" id x y w h r g b a blend               "circle" id cx cy radius r g b a blend
//   "line" id x0 y0 x1 y1 thickness r g b a blend "set" id x y r g b a blend
//   "get" id x y -> r g b a                       "read" id x y w h -> {r, g, b, a, ...}
//   "write" id x y w h {r, g, b, a, ...}          "size" id -> w h
int l_ei(lua_State* L) {
    const std::string op = luaL_checkstring(L, 1);
    if (op == "new") {
        const int id = EditableImages::create((int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3));
        if (!id) return 0;
        lua_pushinteger(L, id);
        return 1;
    }
    const int id = (int)luaL_checkinteger(L, 2);
    if (op == "free") { EditableImages::destroy(id); return 0; }
    if (!EditableImages::exists(id)) return luaL_error(L, "This EditableImage was destroyed");
    if (op == "content") { lua_pushstring(L, EditableImages::contentId(id).c_str()); return 1; }
    if (op == "size") {
        int w = 0, h = 0;
        EditableImages::pixels(id, &w, &h);
        lua_pushinteger(L, w);
        lua_pushinteger(L, h);
        return 2;
    }
    if (op == "rect") {
        EditableImages::fillRect(id, (int)std::floor(luaL_checknumber(L, 3)), (int)std::floor(luaL_checknumber(L, 4)),
                                 (int)std::lround(luaL_checknumber(L, 5)), (int)std::lround(luaL_checknumber(L, 6)), colourAt(L, 7),
                                 lua_toboolean(L, 11));
        return 0;
    }
    if (op == "circle") {
        EditableImages::fillCircle(id, (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                                   colourAt(L, 6), lua_toboolean(L, 10));
        return 0;
    }
    if (op == "line") {
        EditableImages::line(id, (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                             (float)luaL_checknumber(L, 6), (float)luaL_checknumber(L, 7), colourAt(L, 8), lua_toboolean(L, 12));
        return 0;
    }
    if (op == "set") {
        EditableImages::setPixel(id, (int)std::floor(luaL_checknumber(L, 3)), (int)std::floor(luaL_checknumber(L, 4)), colourAt(L, 5),
                                 lua_toboolean(L, 9));
        return 0;
    }
    if (op == "get") {
        EditableImages::Colour c;
        if (!EditableImages::getPixel(id, (int)std::floor(luaL_checknumber(L, 3)), (int)std::floor(luaL_checknumber(L, 4)), c))
            return luaL_error(L, "GetPixel: that spot is outside the picture");
        lua_pushnumber(L, c.r); lua_pushnumber(L, c.g); lua_pushnumber(L, c.b); lua_pushnumber(L, c.a);
        return 4;
    }
    if (op == "read" || op == "write") {
        int iw = 0, ih = 0;
        std::vector<uint8_t>* px = EditableImages::pixels(id, &iw, &ih);
        const int x = (int)luaL_checkinteger(L, 3), y = (int)luaL_checkinteger(L, 4);
        const int w = (int)luaL_checkinteger(L, 5), h = (int)luaL_checkinteger(L, 6);
        if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > iw || y + h > ih)
            return luaL_error(L, "%s: that area goes outside the picture (it's %d x %d)", op == "read" ? "ReadPixels" : "WritePixels", iw, ih);
        if (op == "read") {
            lua_createtable(L, w * h * 4, 0);
            int n = 0;
            for (int yy = y; yy < y + h; ++yy)
                for (int xx = x; xx < x + w; ++xx)
                    for (int k = 0; k < 4; ++k) {
                        lua_pushnumber(L, (*px)[((size_t)yy * iw + xx) * 4 + k] / 255.0);
                        lua_rawseti(L, -2, ++n);
                    }
            return 1;
        }
        luaL_checktype(L, 7, LUA_TTABLE);
        if ((int)lua_rawlen(L, 7) < w * h * 4)
            return luaL_error(L, "WritePixels: the list needs %d numbers (r, g, b, a for each pixel)", w * h * 4);
        int n = 0;
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx)
                for (int k = 0; k < 4; ++k) {
                    lua_rawgeti(L, 7, ++n);
                    const double v = lua_tonumber(L, -1);
                    lua_pop(L, 1);
                    (*px)[((size_t)yy * iw + xx) * 4 + k] = (uint8_t)std::lround(std::fmin(1.0, std::fmax(0.0, v)) * 255.0);
                }
        EditableImages::touch(id);
        return 0;
    }
    return luaL_error(L, "unknown EditableImage request '%s'", op.c_str());
}

// __gb_publishStart(id, name, description) -> ticket (a question is up already: nil, why)
int l_publishStart(lua_State* L) {
    ScriptEngine* engine = LuaApi::engine(L);
    if (engine->runMode() == ScriptEngine::RunMode::Server || engine->noLocalPlayer())   // (like RunService:IsClient())
        return luaL_error(L, "PromptPublishImageAsync works in LocalScripts: the player publishes from their own computer");
    const int id = (int)luaL_checkinteger(L, 1);
    if (!EditableImages::exists(id)) return luaL_error(L, "This EditableImage was destroyed");
    int w = 0, h = 0;
    EditableImages::pixels(id, &w, &h);
    const std::string png = EditableImages::png(id);
    if (png.size() > 4u * 1024u * 1024u) return luaL_error(L, "PromptPublishImageAsync: the picture is too big to publish (4 MB at most)");
    const int ticket = ImagePublish::ask(png, w, h, luaL_checkstring(L, 2), luaL_optstring(L, 3, ""), engine->onlineGameId());
    if (!ticket) {
        lua_pushnil(L);
        lua_pushstring(L, "Another picture is waiting for the player's answer.");
        return 2;
    }
    lua_pushinteger(L, ticket);
    return 1;
}

// __gb_publishDone(ticket) -> nil (still waiting) | result, assetId, message
int l_publishDone(lua_State* L) {
    std::string assetId, message;
    const ImagePublish::Status s = ImagePublish::result((int)luaL_checkinteger(L, 1), assetId, message);
    if (s == ImagePublish::Status::Waiting) return 0;
    lua_pushstring(L, s == ImagePublish::Status::Published ? "Published" : s == ImagePublish::Status::Cancelled ? "Cancelled" : "Failed");
    if (!assetId.empty() && assetId.size() < 16 && assetId.find_first_not_of("0123456789") == std::string::npos)
        lua_pushinteger(L, (lua_Integer)std::stoll(assetId));
    else if (!assetId.empty()) lua_pushstring(L, assetId.c_str());
    else lua_pushnil(L);
    lua_pushstring(L, message.c_str());
    return 3;
}

const char* kLua = R"LUA(
local ei, publishStart, publishDone = ...

local function rgb(c, what)
    if typeof(c) ~= "Color3" then error(what .. ": the colour should be a Color3", 3) end
    return c.R, c.G, c.B
end
local function alpha(t, what)
    if t == nil then return 1 end
    if type(t) ~= "number" then error(what .. ": the transparency should be a number (0 = solid, 1 = see-through)", 3) end
    return 1 - math.clamp(t, 0, 1)
end
local function blendOf(kind)
    if kind == nil then return true end
    return not tostring(kind):find("Overwrite")
end
local function v2(v, what)
    if typeof(v) ~= "Vector2" then error(what .. " should be a Vector2", 3) end
    return v.X, v.Y
end

local EditableImage = {}
EditableImage.__index = EditableImage
EditableImage.__tostring = function() return "EditableImage" end
function EditableImage:IsA(c) return c == "EditableImage" or c == "Object" end
function EditableImage:GetContentId() return self.ContentId end
function EditableImage:DrawRectangle(position, size, color, transparency, combineType)
    local x, y = v2(position, "DrawRectangle: the position")
    local w, h = v2(size, "DrawRectangle: the size")
    local r, g, b = rgb(color, "DrawRectangle")
    ei("rect", self.__id, x, y, w, h, r, g, b, alpha(transparency, "DrawRectangle"), blendOf(combineType))
end
function EditableImage:DrawCircle(center, radius, color, transparency, combineType)
    local x, y = v2(center, "DrawCircle: the middle")
    if type(radius) ~= "number" then error("DrawCircle: the radius should be a number", 2) end
    local r, g, b = rgb(color, "DrawCircle")
    ei("circle", self.__id, x, y, radius, r, g, b, alpha(transparency, "DrawCircle"), blendOf(combineType))
end
function EditableImage:DrawLine(p0, p1, color, transparency, combineType, thickness)
    local x0, y0 = v2(p0, "DrawLine: the start")
    local x1, y1 = v2(p1, "DrawLine: the end")
    local r, g, b = rgb(color, "DrawLine")
    if thickness ~= nil and type(thickness) ~= "number" then error("DrawLine: the thickness should be a number", 2) end
    ei("line", self.__id, x0, y0, x1, y1, thickness or 1, r, g, b, alpha(transparency, "DrawLine"), blendOf(combineType))
end
function EditableImage:SetPixel(position, color, transparency)
    local x, y = v2(position, "SetPixel: the position")
    local r, g, b = rgb(color, "SetPixel")
    ei("set", self.__id, x, y, r, g, b, alpha(transparency, "SetPixel"), false)
end
function EditableImage:GetPixel(position)
    local x, y = v2(position, "GetPixel: the position")
    local r, g, b, a = ei("get", self.__id, x, y)
    return Color3.new(r, g, b), 1 - a
end
function EditableImage:Clear(color, transparency)
    local r, g, b = 0, 0, 0
    if color ~= nil then r, g, b = rgb(color, "Clear") end
    local a = color == nil and transparency == nil and 0 or alpha(transparency, "Clear")
    ei("rect", self.__id, 0, 0, self.Size.X, self.Size.Y, r, g, b, a, false)
end
function EditableImage:ReadPixels(position, size)
    local x, y = v2(position, "ReadPixels: the position")
    local w, h = v2(size, "ReadPixels: the size")
    return ei("read", self.__id, math.floor(x), math.floor(y), math.floor(w), math.floor(h))
end
function EditableImage:WritePixels(position, size, list)
    local x, y = v2(position, "WritePixels: the position")
    local w, h = v2(size, "WritePixels: the size")
    if type(list) ~= "table" then error("WritePixels needs a list of numbers (r, g, b, a for each pixel)", 2) end
    ei("write", self.__id, math.floor(x), math.floor(y), math.floor(w), math.floor(h), list)
end
function EditableImage:Destroy() ei("free", self.__id) end

local AssetService = { Name = "AssetService", ClassName = "AssetService" }
function AssetService:CreateEditableImage(options)
    local size = options and options.Size or Vector2.new(512, 512)
    if typeof(size) ~= "Vector2" then error("CreateEditableImage: Size should be a Vector2", 2) end
    local w, h = math.floor(size.X), math.floor(size.Y)
    if w < 1 or h < 1 or w > 1024 or h > 1024 then error("CreateEditableImage: each side is 1 to 1024 pixels", 2) end
    local id = ei("new", w, h)
    if not id then error("CreateEditableImage: this game has too many pictures already (destroy some first)", 2) end
    return setmetatable({ ClassName = "EditableImage", Name = "EditableImage", Size = Vector2.new(w, h),
                          ContentId = ei("content", id), __id = id }, EditableImage)
end
-- Ask the player to publish a picture into their inventory (a LocalScript, for the player playing).
function AssetService:PromptPublishImageAsync(player, image, options)
    if type(image) ~= "table" or getmetatable(image) ~= EditableImage then
        error("PromptPublishImageAsync(player, editableImage, { Name = ... }): the second value should be an EditableImage", 2)
    end
    if Players and Players.LocalPlayer and player ~= Players.LocalPlayer then
        error("PromptPublishImageAsync: only the player on this computer can be asked (Players.LocalPlayer)", 2)
    end
    options = options or {}
    local name = options.Name or "My drawing"
    if type(name) ~= "string" or name:gsub("%s", "") == "" then error("PromptPublishImageAsync: Name should be some text", 2) end
    local ticket, why = publishStart(image.__id, name:sub(1, 50), type(options.Description) == "string" and options.Description:sub(1, 1000) or "")
    if not ticket then return "Failed", nil, why end
    while true do
        local result, assetId, message = publishDone(ticket)
        if result then return result, assetId, message end
        task.wait(0.1)
    end
end
function AssetService:IsA(c) return c == "AssetService" or c == "Instance" end
setmetatable(AssetService, {
    __tostring = function() return "AssetService" end,
    __index = function(_, key)
        if type(key) == "string" and key:sub(1, 1):match("%u") then
            return function() error("AssetService:" .. key .. " isn't in Guts and Bolts yet", 2) end
        end
    end })

local more = __gb_moreServices
if more then more.AssetService = AssetService end
-- Content.fromObject(editableImage) (Roblox's way to show one): here it's the picture's ContentId.
if Content == nil then
    Content = { fromObject = function(o) return type(o) == "table" and o.ContentId or nil end,
                fromUri = function(u) return u end }
end
)LUA";

} // namespace

void install(lua_State* L) {
    EditableImages::clearAll();   // (a fresh start for this game run)
    ImagePublish::reset();
    if (luaL_loadbuffer(L, kLua, std::strlen(kLua), "=drawings") != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    lua_pushcfunction(L, l_ei);
    lua_pushcfunction(L, l_publishStart);
    lua_pushcfunction(L, l_publishDone);
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

} // namespace Drawings
