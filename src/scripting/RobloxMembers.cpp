#include "RobloxMembers.h"

#include "LuaApi.h"

#include <string>

namespace RobloxMembers {
namespace {

// The Lua half. GET[name](self) gives back (true, value) when it knows name for this object,
// SET[name](self, value) gives back true; methods go in the table every object reads first.
const char* kMembers = R"LUA(
local GET, SET, methods = ...

-- Things a script set that we only keep (an object's own spot, per object).
local function classOf(o)
    local ok, c = pcall(function() return o.ClassName end)
    return ok and c or nil
end
local kept = setmetatable({}, { __mode = "k" })
-- (Lighting, the Camera and a Humanoid can come as a new Lua value each time: keep theirs by name.)
local function keyOf(o)
    if type(o) == "string" then return o end
    local c = classOf(o)
    if c == "Lighting" or c == "Camera" then return c end
    if c == "Humanoid" then local ok, p = pcall(function() return o.Parent end) if ok and p then return p end end
    return o
end
local function keep(o)
    local k = keyOf(o)
    local t = kept[k]
    if not t then t = {} kept[k] = t end
    return t
end
local function isA(o, c)
    local ok, r = pcall(function() return o:IsA(c) end)
    return ok and r
end
local function enumName(v)
    if v == nil then return nil end
    if type(v) == "string" then return v end
    local ok, n = pcall(function() return v.Name end)
    return ok and n or tostring(v)
end

-- A property only kept (read back what was set): `defaults` per class.
local function keptProperty(name, test, default, check)
    GET[name] = function(self)
        if not test(self) then return false end
        local v = keep(self)[name]
        if v == nil then
            if type(default) == "function" then v = default(self) else v = default end
        end
        return true, v
    end
    SET[name] = function(self, v)
        if not test(self) then return false end
        if check then v = check(v, name) end
        keep(self)[name] = v
        return true
    end
end
local function number(v, name)
    if type(v) ~= "number" then error(name .. " must be a number (got " .. typeof(v) .. ")", 3) end
    return v
end
local function boolean(v) return v and true or false end
local function enum(v) return enumName(v) end
local function isPart(o) return isA(o, "BasePart") end
local function isModel(o) return isA(o, "Model") and not isA(o, "BasePart") end
local function isLighting(o) return classOf(o) == "Lighting" end
local function isCamera(o) return classOf(o) == "Camera" end
local function isAttachment(o) return isA(o, "Attachment") end
local function isText(o) local c = classOf(o) return c == "TextLabel" or c == "TextButton" or c == "TextBox" end
local function isImage(o) local c = classOf(o) return c == "ImageLabel" or c == "ImageButton" end
local function isGuiObject(o) return isA(o, "GuiObject") end
local function method(name, test, what, fn)
    local old = methods[name]
    methods[name] = function(self, ...)
        if test(self) then return fn(self, ...) end
        if old then return old(self, ...) end
        error(name .. " is a " .. what .. " method", 2)
    end
    -- (Lighting, the Camera and a Humanoid don't read the methods every object has.)
    local oldGet = GET[name]
    GET[name] = function(self)
        if test(self) then return true, methods[name] end
        if oldGet then return oldGet(self) end
        return false
    end
end

-- ===== Parts =========================================================================

keptProperty("CanQuery", isPart, true, boolean)
keptProperty("CanTouch", isPart, true, boolean)
keptProperty("Massless", isPart, false, boolean)
keptProperty("RootPriority", isPart, 0, number)
keptProperty("EnableFluidForces", isPart, true, boolean)
keptProperty("PivotOffset", isPart, CFrame.new(), function(v) return v end)
GET.AssemblyRootPart = function(self) if not isPart(self) then return false end return true, self end
GET.CenterOfMass = function(self) if not isPart(self) then return false end return true, self.Position end
GET.ExtentsSize = function(self) if not isPart(self) then return false end return true, self.Size end

method("GetVelocityAtPosition", isPart, "BasePart", function(self, pos)
    return self.AssemblyLinearVelocity + self.AssemblyAngularVelocity:Cross(pos - self.Position)
end)
method("ApplyImpulseAtPosition", isPart, "BasePart", function(self, impulse, pos)
    self:ApplyImpulse(impulse)
    self:ApplyAngularImpulse((pos - self.Position):Cross(impulse))
end)
method("CanSetNetworkOwnership", isPart, "BasePart", function(self)
    if self.Anchored then return false, "Cannot change the network ownership of an anchored part" end
    return true
end)
-- (Guts&Bolts already gives a player's own character to their computer; others stay with the server.)
method("SetNetworkOwner", isPart, "BasePart", function(self, player)
    if self.Anchored then error("Network ownership can't be set for anchored parts", 2) end
    keep(self).owner = player
end)
method("SetNetworkOwnershipAuto", isPart, "BasePart", function(self) keep(self).owner = nil end)
method("GetNetworkOwner", isPart, "BasePart", function(self) return keep(self).owner end)
method("GetNetworkOwnershipAuto", isPart, "BasePart", function(self) return keep(self).owner == nil end)

-- The parts joined to this one by welds, joints and constraints.
local function joinedTo(part)
    local found = {}
    for _, d in ipairs(workspace:GetDescendants()) do
        if isA(d, "JointInstance") or isA(d, "WeldConstraint") or isA(d, "Constraint") then
            local a, b
            pcall(function() a, b = d.Part0, d.Part1 end)
            if a == nil and b == nil then
                pcall(function()
                    a = d.Attachment0 and d.Attachment0.Parent
                    b = d.Attachment1 and d.Attachment1.Parent
                end)
            end
            if a == part and b then found[#found + 1] = b end
            if b == part and a then found[#found + 1] = a end
        end
    end
    return found
end
method("GetConnectedParts", isPart, "BasePart", function(self, recursive)
    local seen, out, todo = { [self] = true }, {}, { self }
    while #todo > 0 do
        local p = table.remove(todo)
        for _, q in ipairs(joinedTo(p)) do
            if not seen[q] then
                seen[q] = true
                out[#out + 1] = q
                if recursive then todo[#todo + 1] = q end
            end
        end
    end
    return out
end)
method("GetJoints", isPart, "BasePart", function(self)
    local out = {}
    for _, d in ipairs(workspace:GetDescendants()) do
        if isA(d, "JointInstance") or isA(d, "WeldConstraint") or isA(d, "Constraint") then
            local a, b
            pcall(function() a, b = d.Part0, d.Part1 end)
            pcall(function()
                a = a or (d.Attachment0 and d.Attachment0.Parent)
                b = b or (d.Attachment1 and d.Attachment1.Parent)
            end)
            if a == self or b == self then out[#out + 1] = d end
        end
    end
    return out
end)
-- part:Resize(Enum.NormalId.Top, 2): grow (or shrink) one face, the other side stays put.
local faces = { Right = Vector3.new(1, 0, 0), Left = Vector3.new(-1, 0, 0), Top = Vector3.new(0, 1, 0),
                Bottom = Vector3.new(0, -1, 0), Back = Vector3.new(0, 0, 1), Front = Vector3.new(0, 0, -1) }
method("Resize", isPart, "BasePart", function(self, normalId, delta)
    local dir = faces[enumName(normalId)]
    if not dir then error("Resize needs a face (Enum.NormalId.Top...)", 2) end
    delta = tonumber(delta) or 0
    local axis = Vector3.new(math.abs(dir.X), math.abs(dir.Y), math.abs(dir.Z))
    local size = self.Size + axis * delta
    if size.X < 0.001 or size.Y < 0.001 or size.Z < 0.001 then return false end
    local cf = self.CFrame
    self.Size = size
    self.CFrame = cf * CFrame.new(dir * (delta / 2))
    return true
end)

-- ===== Models ========================================================================

local function partsOf(model)
    local out = {}
    for _, d in ipairs(model:GetDescendants()) do
        if isA(d, "BasePart") then out[#out + 1] = d end
    end
    return out
end
-- The box around a model's parts, turned like its pivot (or its PrimaryPart).
local function boundingBox(model)
    local pivot = model:GetPivot()
    local rot = pivot - pivot.Position
    local lo, hi
    for _, p in ipairs(partsOf(model)) do
        local cf, s = p.CFrame, p.Size / 2
        for _, c in ipairs({ Vector3.new(1, 1, 1), Vector3.new(1, 1, -1), Vector3.new(1, -1, 1), Vector3.new(1, -1, -1),
                             Vector3.new(-1, 1, 1), Vector3.new(-1, 1, -1), Vector3.new(-1, -1, 1), Vector3.new(-1, -1, -1) }) do
            local w = rot:PointToObjectSpace(cf:PointToWorldSpace(s * c))
            lo = lo and lo:Min(w) or w
            hi = hi and hi:Max(w) or w
        end
    end
    if not lo then return pivot, Vector3.zero end
    return rot + rot:PointToWorldSpace((lo + hi) / 2), hi - lo
end
method("GetBoundingBox", isModel, "Model", function(self) return boundingBox(self) end)
method("GetExtentsSize", isModel, "Model", function(self) local _, size = boundingBox(self) return size end)
method("GetModelSize", isModel, "Model", function(self) local _, size = boundingBox(self) return size end)
method("GetModelCFrame", isModel, "Model", function(self) return (boundingBox(self)) end)
method("TranslateBy", isModel, "Model", function(self, by) self:PivotTo(self:GetPivot() + by) end)
method("SetPrimaryPartCFrame", isModel, "Model", function(self, cf)
    if not self.PrimaryPart then error("SetPrimaryPartCFrame needs a PrimaryPart", 2) end
    self:PivotTo(cf)
end)
method("GetPrimaryPartCFrame", isModel, "Model", function(self)
    return self.PrimaryPart and self.PrimaryPart.CFrame or self:GetPivot()
end)
-- model:MoveTo(position): the model's middle (its PrimaryPart if it has one) goes there; if
-- that spot is taken it's lifted until it sits on top, like Roblox.
method("MoveTo", isModel, "Model", function(self, pos)
    local cf, size = boundingBox(self)
    local from = self.PrimaryPart and self.PrimaryPart.Position or cf.Position
    local offset = pos - from
    local params = OverlapParams.new()
    params.FilterType = Enum.RaycastFilterType.Exclude
    params.FilterDescendantsInstances = { self }
    local box = cf + offset
    local hits = workspace:GetPartBoundsInBox(box, size, params)
    if #hits > 0 then
        local top = -math.huge
        for _, h in ipairs(hits) do
            if h.CanCollide then top = math.max(top, h.Position.Y + h.Size.Y / 2) end
        end
        if top > -math.huge then
            local bottom = box.Position.Y - size.Y / 2
            if top > bottom then offset = offset + Vector3.new(0, top - bottom, 0) end
        end
    end
    self:PivotTo(self:GetPivot() + offset)
end)
-- model:ScaleTo(2) makes it twice its first size (parts grow, and move apart from the pivot).
method("GetScale", isModel, "Model", function(self) return keep(self).scale or 1 end)
method("ScaleTo", isModel, "Model", function(self, scale)
    scale = tonumber(scale)
    if not scale or scale <= 0 then error("ScaleTo needs a number above 0", 2) end
    local by = scale / (keep(self).scale or 1)
    local pivot = self:GetPivot()
    for _, p in ipairs(partsOf(self)) do
        local rel = pivot:ToObjectSpace(p.CFrame)
        p.Size = p.Size * by
        p.CFrame = pivot * (CFrame.new(rel.Position * by) * (rel - rel.Position))
    end
    keep(self).scale = scale
end)
GET.WorldPivot = function(self)
    if not isModel(self) then return false end
    return true, keep(self).pivot or self:GetPivot()
end
SET.WorldPivot = function(self, cf)
    if not isModel(self) then return false end
    if self.PrimaryPart then self.PrimaryPart.PivotOffset = self.PrimaryPart.CFrame:ToObjectSpace(cf)
    else keep(self).pivot = cf end
    return true
end
keptProperty("LevelOfDetail", isModel, "Automatic", enum)
keptProperty("ModelStreamingMode", isModel, "Default", enum)

-- ===== Attachments ===================================================================

local function parentCFrame(a)
    local p = a.Parent
    if p and isA(p, "BasePart") then return p.CFrame end
    return CFrame.new()
end
-- (The attachment's place in its part: its Position, turned so X is its Axis and Y its SecondaryAxis.)
local function localCFrame(a)
    local x, y = a.Axis, a.SecondaryAxis
    return CFrame.fromMatrix(a.Position, x, y, x:Cross(y))
end
-- (Its place is WorldPosition, which the engine works out with the part's own size and turn.)
local function worldCFrame(a)
    local turn = parentCFrame(a) * localCFrame(a)
    return turn - turn.Position + a.WorldPosition
end
GET.WorldCFrame = function(self)
    if not isAttachment(self) then return false end
    return true, worldCFrame(self)
end
SET.WorldCFrame = function(self, cf)
    if not isAttachment(self) then return false end
    local l = parentCFrame(self):ToObjectSpace(cf)
    self.Axis = l.XVector
    self.SecondaryAxis = l.YVector
    self.WorldPosition = cf.Position
    return true
end
GET.WorldAxis = function(self) if not isAttachment(self) then return false end return true, worldCFrame(self).XVector end
GET.WorldSecondaryAxis = function(self) if not isAttachment(self) then return false end return true, worldCFrame(self).YVector end
GET.WorldOrientation = function(self)
    if not isAttachment(self) then return false end
    local x, y, z = worldCFrame(self):ToOrientation()
    return true, Vector3.new(math.deg(x), math.deg(y), math.deg(z))
end

-- ===== Workspace =====================================================================

local function isWorkspace(o) return o == workspace end
local started = os.clock()
method("GetServerTimeNow", isWorkspace, "Workspace", function() return os.time() + (os.clock() % 1) end)
method("GetRealPhysicsFPS", isWorkspace, "Workspace", function() return 60 end)
method("GetNumAwakeParts", isWorkspace, "Workspace", function()
    local n = 0
    for _, d in ipairs(workspace:GetDescendants()) do if isA(d, "BasePart") and not d.Anchored then n = n + 1 end end
    return n
end)
method("BulkMoveTo", isWorkspace, "Workspace", function(_, parts, cframes)
    for i, p in ipairs(parts) do if cframes[i] then p.CFrame = cframes[i] end end
end)
method("ArePartsTouchingOthers", isWorkspace, "Workspace", function(_, parts, overlap)
    local mine = {}
    for _, p in ipairs(parts) do mine[p] = true end
    local params = OverlapParams.new()
    params.FilterType = Enum.RaycastFilterType.Exclude
    params.FilterDescendantsInstances = parts
    for _, p in ipairs(parts) do
        for _, h in ipairs(workspace:GetPartsInPart(p, params)) do
            if not mine[h] then return true end
        end
    end
    return false
end)
GET.DistributedGameTime = function(self) if not isWorkspace(self) then return false end return true, time() end
keptProperty("FallenPartsDestroyHeight", isWorkspace, -500, number)
keptProperty("AllowThirdPartySales", isWorkspace, false, boolean)
keptProperty("SignalBehavior", isWorkspace, "Deferred", enum)
keptProperty("TouchesUseCollisionGroups", isWorkspace, false, boolean)
-- (Parts that fall below FallenPartsDestroyHeight are taken away, like Roblox.)
do
    local RunService = game:GetService("RunService")
    local wait = 0
    RunService.Heartbeat:Connect(function(dt)
        wait = wait + (dt or 0)
        if wait < 1 then return end
        wait = 0
        local floor = keep(workspace).FallenPartsDestroyHeight
        if floor == nil then return end   -- (only once a script sets it: our own fall reset handles the rest)
        for _, d in ipairs(workspace:GetDescendants()) do
            if isA(d, "BasePart") and not d.Anchored and d.Position.Y < floor then
                local model = d:FindFirstAncestorWhichIsA("Model")
                if not (model and model:FindFirstChildOfClass("Humanoid")) then pcall(d.Destroy, d) end
            end
        end
    end)
end

-- ===== Lighting ======================================================================

local function clockText(hours)
    hours = hours % 24
    local h = math.floor(hours)
    local m = math.floor((hours - h) * 60)
    local s = math.floor(((hours - h) * 60 - m) * 60 + 0.5)
    if s == 60 then s, m = 0, m + 1 end
    if m == 60 then m, h = 0, (h + 1) % 24 end
    return string.format("%02d:%02d:%02d", h, m, s)
end
GET.TimeOfDay = function(self) if not isLighting(self) then return false end return true, clockText(self.ClockTime) end
SET.TimeOfDay = function(self, v)
    if not isLighting(self) then return false end
    local h, m, s = tostring(v):match("^%s*(%-?%d+):?(%d*):?(%d*)")
    if not h then error("TimeOfDay looks like \"14:30:00\"", 3) end
    self.ClockTime = (tonumber(h) + (tonumber(m) or 0) / 60 + (tonumber(s) or 0) / 3600) % 24
    return true
end
method("GetMinutesAfterMidnight", isLighting, "Lighting", function(self) return self.ClockTime * 60 end)
method("SetMinutesAfterMidnight", isLighting, "Lighting", function(self, minutes)
    self.ClockTime = ((tonumber(minutes) or 0) / 60) % 24
end)
-- The way to the sun (a unit Vector3): up at noon, down at midnight, rising in the east.
local function sunDirection(lighting)
    local angle = (lighting.ClockTime - 6) / 24 * math.pi * 2   -- 0 at 6 AM (east), up at noon
    local lat = math.rad(keep("Lighting").GeographicLatitude or 41.73)
    local d = Vector3.new(-math.cos(angle), math.sin(angle) * math.cos(lat), math.sin(angle) * math.sin(lat))
    return d.Unit
end
method("GetSunDirection", isLighting, "Lighting", function(self) return sunDirection(self) end)
method("GetMoonDirection", isLighting, "Lighting", function(self) return -sunDirection(self) end)
-- Lighting is one object: keep its settings under one name (not per Lua value).
local function keptLighting(name, default, check)
    GET[name] = function(self) if not isLighting(self) then return false end
        local v = keep("Lighting")[name] if v == nil then v = default end return true, v end
    SET[name] = function(self, v) if not isLighting(self) then return false end
        if check then v = check(v, name) end keep("Lighting")[name] = v return true end
end
keptLighting("GeographicLatitude", 41.73, number)
keptLighting("EnvironmentDiffuseScale", 1, number)
keptLighting("EnvironmentSpecularScale", 1, number)
keptLighting("ExposureCompensation", 0, number)
keptLighting("ShadowSoftness", 0.2, number)
keptLighting("Technology", "Future", enum)
keptLighting("ColorShift_Top", Color3.new(0, 0, 0))
keptLighting("ColorShift_Bottom", Color3.new(0, 0, 0))
keptLighting("LightingStyle", "Realistic", enum)
keptLighting("PrioritizeLightingQuality", true, boolean)
-- OutdoorAmbient is the light everything outside gets: ours is Ambient.
GET.OutdoorAmbient = function(self) if not isLighting(self) then return false end return true, self.Ambient end
SET.OutdoorAmbient = function(self, v) if not isLighting(self) then return false end self.Ambient = v return true end
-- FogStart / FogEnd: Roblox's fog fades in between two distances; ours thickens with distance,
-- so FogEnd sets how thick it is (gone at about that far) and turns it on.
GET.FogEnd = function(self) if not isLighting(self) then return false end
    local v = keep("Lighting").FogEnd return true, v or 100000 end
SET.FogEnd = function(self, v)
    if not isLighting(self) then return false end
    v = number(v, "FogEnd")
    keep("Lighting").FogEnd = v
    if v >= 100000 then self.FogEnabled = false
    else
        self.FogEnabled = true
        self.FogDensity = math.clamp(3 / math.max(v, 1), 0, 1)
    end
    return true
end
keptLighting("FogStart", 0, number)

-- ===== Game UI =======================================================================

local TextService = game:GetService("TextService")
GET.TextBounds = function(self)
    if not isText(self) then return false end
    local ok, size = pcall(function() return self.TextSize end)
    local width = math.huge
    pcall(function() width = self.AbsoluteSize.X end)
    local v = TextService:GetTextSize(self.Text, ok and size or 14, self.Font, Vector2.new(width, math.huge))
    return true, v
end
GET.TextFits = function(self)
    if not isText(self) then return false end
    local ok, fits = pcall(function()
        local b = TextService:GetTextSize(self.Text, self.TextSize, self.Font, Vector2.new(math.huge, math.huge))
        return b.X <= self.AbsoluteSize.X and b.Y <= self.AbsoluteSize.Y
    end)
    return true, not ok or fits
end
keptProperty("MaxVisibleGraphemes", isText, -1, number)
keptProperty("TextTruncate", isText, "None", enum)
keptProperty("LineHeight", isText, 1, number)
keptProperty("TextDirection", isText, "Auto", enum)
keptProperty("OpenTypeFeatures", isText, "", tostring)
keptProperty("FontFace", isText, function(self)
    local ok, f = pcall(function() return Font.fromEnum(self.Font) end)
    return ok and f or Font.new("rbxasset://fonts/families/SourceSansPro.json")
end)
keptProperty("ClearTextOnFocus", function(o) return classOf(o) == "TextBox" end, true, boolean)
keptProperty("MultiLine", function(o) return classOf(o) == "TextBox" end, false, boolean)
keptProperty("TextEditable", function(o) return classOf(o) == "TextBox" end, true, boolean)
keptProperty("ShowNativeInput", function(o) return classOf(o) == "TextBox" end, true, boolean)
keptProperty("ImageRectOffset", isImage, Vector2.new(0, 0))
keptProperty("ImageRectSize", isImage, Vector2.new(0, 0))
keptProperty("SliceCenter", isImage, Rect.new(0, 0, 0, 0))
keptProperty("SliceScale", isImage, 1, number)
keptProperty("ScaleType", isImage, "Stretch", enum)
keptProperty("TileSize", isImage, UDim2.new(1, 0, 1, 0))
keptProperty("ResampleMode", isImage, "Default", enum)
keptProperty("HoverImage", function(o) return classOf(o) == "ImageButton" end, "", tostring)
keptProperty("PressedImage", function(o) return classOf(o) == "ImageButton" end, "", tostring)
GET.IsLoaded = function(self) if not isImage(self) then return false end return true, true end
keptProperty("AutomaticSize", isGuiObject, "None", enum)
keptProperty("Interactable", isGuiObject, true, boolean)
keptProperty("Selectable", isGuiObject, false, boolean)
keptProperty("SelectionImageObject", isGuiObject, nil)
keptProperty("SelectionOrder", isGuiObject, 0, number)
keptProperty("NextSelectionUp", isGuiObject, nil)
keptProperty("NextSelectionDown", isGuiObject, nil)
keptProperty("NextSelectionLeft", isGuiObject, nil)
keptProperty("NextSelectionRight", isGuiObject, nil)
keptProperty("SizeConstraint", isGuiObject, "RelativeXY", enum)
keptProperty("BorderMode", isGuiObject, "Outline", enum)
keptProperty("Style", function(o) local c = classOf(o) return c == "Frame" or c == "TextButton" end, "Custom", enum)

-- ===== The Camera ====================================================================

GET.DiagonalFieldOfView = function(self)
    if not isCamera(self) then return false end
    local v = self.ViewportSize
    local half = math.tan(math.rad(self.FieldOfView) / 2)
    local aspect = v.Y > 0 and v.X / v.Y or 16 / 9
    return true, math.deg(2 * math.atan(half * math.sqrt(1 + aspect * aspect)))
end
GET.MaxAxisFieldOfView = function(self)
    if not isCamera(self) then return false end
    local v = self.ViewportSize
    local aspect = v.Y > 0 and v.X / v.Y or 16 / 9
    local half = math.tan(math.rad(self.FieldOfView) / 2)
    return true, math.deg(2 * math.atan(half * math.max(1, aspect)))
end
GET.NearPlaneZ = function(self) if not isCamera(self) then return false end return true, -0.1 end
keptProperty("HeadLocked", isCamera, true, boolean)
keptProperty("HeadScale", isCamera, 1, number)
keptProperty("VRTiltAndRollEnabled", isCamera, false, boolean)
method("GetPartsObscuringTarget", isCamera, "Camera", function(self, points, ignore)
    local from = self.CFrame.Position
    local params = RaycastParams.new()
    params.FilterType = Enum.RaycastFilterType.Exclude
    params.FilterDescendantsInstances = ignore or {}
    local out, seen = {}, {}
    for _, p in ipairs(points or {}) do
        local hit = workspace:Raycast(from, p - from, params)
        while hit and hit.Instance and not seen[hit.Instance] do
            seen[hit.Instance] = true
            out[#out + 1] = hit.Instance
            local list = params.FilterDescendantsInstances
            list[#list + 1] = hit.Instance
            params.FilterDescendantsInstances = list
            hit = workspace:Raycast(from, p - from, params)
        end
    end
    return out
end)
-- camera:Interpolate(endCFrame, endFocus, seconds): glides there (the old way to tween it).
method("Interpolate", isCamera, "Camera", function(self, cf, focus, duration)
    local ok = pcall(function()
        game:GetService("TweenService"):Create(self, TweenInfo.new(tonumber(duration) or 1), { CFrame = cf }):Play()
    end)
    if not ok then self.CFrame = cf end
end)
-- camera:ZoomToExtents(cframe, size): backs off so a box of that size fills the view.
method("ZoomToExtents", isCamera, "Camera", function(self, cf, size)
    local radius = size.Magnitude / 2
    local dist = radius / math.tan(math.rad(self.FieldOfView) / 2)
    local look = self.CFrame.LookVector
    self.CFrame = CFrame.lookAt(cf.Position - look * dist, cf.Position)
end)
method("GetRenderCFrame", isCamera, "Camera", function(self) return self.CFrame end)
method("GetRoll", isCamera, "Camera", function(self) local _, _, z = self.CFrame:ToOrientation() return z end)
method("SetRoll", isCamera, "Camera", function(self, roll)
    local cf = self.CFrame
    self.CFrame = cf * CFrame.Angles(0, 0, (tonumber(roll) or 0) - select(3, cf:ToOrientation()))
end)

-- ===== Humanoid ======================================================================

local function isHumanoid(o) return classOf(o) == "Humanoid" end
keptProperty("BreakJointsOnDeath", isHumanoid, true, boolean)
keptProperty("RequiresNeck", isHumanoid, true, boolean)
keptProperty("EvaluateStateMachine", isHumanoid, true, boolean)
keptProperty("UseJumpPower", isHumanoid, true, boolean)
keptProperty("AutomaticScalingEnabled", isHumanoid, true, boolean)
keptProperty("NameOcclusion", isHumanoid, "OccludeAll", enum)
keptProperty("HealthDisplayType", isHumanoid, "DisplayWhenDamaged", enum)
keptProperty("HealthDisplayDistance", isHumanoid, 100, number)
keptProperty("NameDisplayDistance", isHumanoid, 100, number)
keptProperty("CameraOffset", isHumanoid, Vector3.new(0, 0, 0))
-- humanoid:SetStateEnabled / GetStateEnabled: kept (the states we run don't read them yet).
method("SetStateEnabled", isHumanoid, "Humanoid", function(self, state, on)
    local t = keep(self).states or {}
    keep(self).states = t
    t[enumName(state)] = on and true or false
end)
method("GetStateEnabled", isHumanoid, "Humanoid", function(self, state)
    local t = keep(self).states
    if t and t[enumName(state)] == false then return false end
    return true
end)
method("GetAccessories", isHumanoid, "Humanoid", function(self)
    local out = {}
    local character = self.Parent
    if character then
        for _, c in ipairs(character:GetChildren()) do
            if isA(c, "Accessory") or isA(c, "Accoutrement") or classOf(c) == "Hat" then out[#out + 1] = c end
        end
    end
    return out
end)
method("RemoveAccessories", isHumanoid, "Humanoid", function(self)
    for _, a in ipairs(self:GetAccessories()) do a:Destroy() end
end)

-- ===== Services ======================================================================

local function try(fn) pcall(fn) end

try(function()
    local RunService = game:GetService("RunService")
    RunService.PreRender = RunService.PreRender or RunService.RenderStepped
    RunService.PreAnimation = RunService.PreAnimation or RunService.Stepped
    RunService.PreSimulation = RunService.PreSimulation or RunService.Stepped
    RunService.PostSimulation = RunService.PostSimulation or RunService.Heartbeat
    -- BindToRenderStep(name, priority, fn): fn(dt) every frame, lowest priority first.
    local bound, order = {}, {}
    local function sort()
        order = {}
        for name, b in pairs(bound) do order[#order + 1] = b end
        table.sort(order, function(a, b) return a.priority < b.priority end)
    end
    local hooked = false
    function RunService:BindToRenderStep(name, priority, fn)
        if type(fn) ~= "function" then error("BindToRenderStep needs a name, a priority and a function", 2) end
        bound[tostring(name)] = { priority = tonumber(priority) or 0, fn = fn }
        sort()
        if not hooked then
            hooked = true
            RunService.RenderStepped:Connect(function(dt)
                for _, b in ipairs(order) do
                    local ok, err = pcall(b.fn, dt)
                    if not ok then warn("BindToRenderStep: " .. tostring(err)) end
                end
            end)
        end
    end
    function RunService:UnbindFromRenderStep(name)
        bound[tostring(name)] = nil
        sort()
    end
end)

try(function()
    local U = game:GetService("UserInputService")
    local keys
    function U:GetKeysPressed()
        if not keys then
            keys = {}
            pcall(function() for _, k in ipairs(Enum.KeyCode:GetEnumItems()) do keys[#keys + 1] = k end end)
        end
        local out = {}
        for _, k in ipairs(keys) do
            local ok, down = pcall(U.IsKeyDown, U, k)
            if ok and down then out[#out + 1] = { KeyCode = k, UserInputType = "Keyboard", UserInputState = "Begin" } end
        end
        return out
    end
    function U:IsMouseButtonPressed(button)
        local name = enumName(button)
        local ok, down = pcall(function()
            if name == "MouseButton1" and U.IsMouseButton1Down then return U:IsMouseButton1Down() end
            if name == "MouseButton2" and U.IsMouseButton2Down then return U:IsMouseButton2Down() end
            return false
        end)
        return ok and down or false
    end
    function U:GetMouseButtonsPressed()
        local out = {}
        for _, b in ipairs({ "MouseButton1", "MouseButton2", "MouseButton3" }) do
            if U:IsMouseButtonPressed(b) then out[#out + 1] = { UserInputType = b, UserInputState = "Begin" } end
        end
        return out
    end
    if not U.GetMouseDelta then
        local last
        function U:GetMouseDelta()
            local now = U:GetMouseLocation()
            local d = last and (now - last) or Vector2.new(0, 0)
            last = now
            return d
        end
    end
    function U:GetLastInputType()
        if U.TouchEnabled and not U.KeyboardEnabled then return "Touch" end
        return "Keyboard"
    end
    function U:GetFocusedTextBox() return nil end
    function U:GetGamepadConnected() return false end
    function U:GetConnectedGamepads() return {} end
    function U:GetNavigationGamepads() return {} end
    function U:GetSupportedGamepadKeyCodes() return {} end
    function U:GetStringForKeyCode(key) return enumName(key) end
    function U:GetDeviceRotation() return "None", CFrame.new() end
    function U:GetDeviceGravity() return { Position = Vector3.new(0, -1, 0) } end
end)

try(function()
    local Players = game:GetService("Players")
    local function byId(id)
        for _, p in ipairs(Players:GetPlayers()) do if p.UserId == id then return p end end
    end
    -- (Guts&Bolts shows avatars on its own site; this gives an id Roblox-style code can keep.)
    function Players:GetUserThumbnailAsync(userId, kind, size)
        local w = tostring(enumName(size) or "Size420x420"):match("(%d+)x") or "420"
        return "rbxthumb://type=" .. tostring(enumName(kind) or "HeadShot") .. "&id=" .. tostring(userId) .. "&w=" .. w .. "&h=" .. w, true
    end
    function Players:GetNameFromUserIdAsync(userId)
        local p = byId(tonumber(userId))
        if p then return p.Name end
        error("Players:GetNameFromUserIdAsync: no player " .. tostring(userId) .. " here", 2)
    end
    function Players:GetUserIdFromNameAsync(name)
        local p = Players:FindFirstChild(tostring(name))
        if p then return p.UserId end
        error("Players:GetUserIdFromNameAsync: no player called " .. tostring(name) .. " here", 2)
    end
    function Players:GetFriendsAsync()
        local pages = { IsFinished = true }
        function pages:GetCurrentPage() return {} end
        function pages:AdvanceToNextPageAsync() end
        return pages
    end
end)

try(function()
    local DataStoreService = game:GetService("DataStoreService")
    function DataStoreService:GetRequestBudgetForRequestType() return 100 end
end)

try(function()
    local TweenService = game:GetService("TweenService")
    -- TweenService:SmoothDamp(current, target, velocity, smoothTime, maxSpeed?, dt?) -> value, velocity
    -- (numbers, Vector2, Vector3 or CFrame positions), a spring that never overshoots.
    function TweenService:SmoothDamp(current, target, velocity, smoothTime, maxSpeed, dt)
        smoothTime = math.max(tonumber(smoothTime) or 0.1, 0.0001)
        dt = tonumber(dt) or (1 / 60)
        local omega = 2 / smoothTime
        local x = omega * dt
        local e = 1 / (1 + x + 0.48 * x * x + 0.235 * x * x * x)
        local change = current - target
        if maxSpeed then
            local most = maxSpeed * smoothTime
            if type(change) == "number" then change = math.clamp(change, -most, most)
            elseif change.Magnitude > most then change = change.Unit * most end
        end
        local temp = (velocity + change * omega) * dt
        local newVelocity = (velocity - temp * omega) * e
        local out = (current - change) + (change + temp) * e
        return out, newVelocity
    end
end)

try(function()
    function TextService:GetTextBoundsAsync(params)
        local width = 0
        pcall(function() width = params.Width end)
        return TextService:GetTextSize(params.Text, params.Size, params.Font,
                                       Vector2.new(width > 0 and width or math.huge, math.huge))
    end
end)

if type(game) == "table" and rawget(game, "IsLoaded") == nil then
    rawset(game, "IsLoaded", function() return true end)   -- (a game is ready when its scripts run)
end
)LUA";

} // namespace

void install(lua_State* L) {
    if (luaL_loadbuffer(L, kMembers, std::char_traits<char>::length(kMembers), "=RobloxMembers") != LUA_OK) {
        lua_pop(L, 1);
        return;
    }
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.ExtraGet");
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.ExtraSet");
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        lua_getglobal(L, "warn");
        lua_pushvalue(L, -2);
        lua_pcall(L, 1, 0, 0);
        lua_pop(L, 1);
    }
}

} // namespace RobloxMembers
