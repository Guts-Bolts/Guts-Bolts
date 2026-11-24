// Roblox classes Instance.new can make although Guts&Bolts has no object of its own for
// them (src/scripting/StandIns.h): ObjectValue, Configuration, MeshPart, Atmosphere...
//   StandInClassesTests <game file>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what, const std::string& got = "") {
    std::cout << (ok ? "ok    " : "FAIL  ") << what;
    if (!ok && !got.empty()) std::cout << "   (got: " << got << ")";
    std::cout << "\n";
    if (!ok) ++failures;
}

// Run some Lua and give back what it printed ("ERROR ..." if it failed).
std::string run(GameSession& session, const std::string& code) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    std::string err;
    const bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    std::string out = ok ? "" : "ERROR " + err;
    for (const Log::Entry& e : lines) {
        if (e.level == Log::Level::Error) out += "ERROR " + e.text;
        else if (e.text.rfind("> ", 0) != 0) out += (out.empty() ? "" : "\n") + e.text;
    }
    return out;
}
void expect(GameSession& s, const std::string& what, const std::string& code, const std::string& want) {
    const std::string got = run(s, code);
    check(got == want, what, got);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: StandInClassesTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.start();
    auto step = [&] { for (int i = 0; i < 3; ++i) session.update(1.0f / 60.0f, 0.0f, false); };

    expect(session, "a stand-in that does nothing yet says so, once per class",
           "for _, c in ipairs({'Pose', 'BoxHandleAdornment', 'ImageHandleAdornment', 'UISizeConstraint', 'SelectionBox', 'Dialog', 'Pose'}) do Instance.new(c) end",
           "Pose can be made and keeps its properties, but it doesn't do anything in Guts&Bolts yet\n"
           "BoxHandleAdornment can be made and keeps its properties, but it doesn't do anything in Guts&Bolts yet\n"
           "ImageHandleAdornment can be made and keeps its properties, but it doesn't do anything in Guts&Bolts yet\n"
           "UISizeConstraint can be made and keeps its properties, but it doesn't do anything in Guts&Bolts yet\n"
           "SelectionBox can be made and keeps its properties, but it doesn't do anything in Guts&Bolts yet\n"
           "Dialog can be made and keeps its properties, but it doesn't do anything in Guts&Bolts yet");
    expect(session, "ObjectValue holds an object and is a ValueBase",
           "_G.o = Instance.new('ObjectValue') _G.o.Value = workspace print(_G.o.ClassName, _G.o:IsA('ValueBase'), _G.o.Value == workspace)",
           "ObjectValue true true");
    run(session, "_G.got = 'none' _G.fired = false _G.o.Changed:Connect(function(v) _G.got = v end) "
                 "_G.o:GetPropertyChangedSignal('Value'):Connect(function() _G.fired = true end) _G.o.Value = game.Lighting");
    step();
    expect(session, ".Changed gives the new Value, GetPropertyChangedSignal fires",
           "print(_G.got == game.Lighting, _G.fired)", "true true");
    expect(session, "ObjectValue.Value = nil", "_G.o.Value = nil print(_G.o.Value)", "nil");
    expect(session, "CFrameValue", "local c = Instance.new('CFrameValue') c.Value = CFrame.new(1, 2, 3) print(c.Value.Position, c.Value == CFrame.new(1, 2, 3))",
           "1, 2, 3 true");
    expect(session, "BrickColorValue keeps a colour name", "local b = Instance.new('BrickColorValue') print(b.Value)", "Medium stone grey");
    expect(session, "Configuration is a folder, and its class isn't an attribute",
           "local c = Instance.new('Configuration', workspace) Instance.new('IntValue', c).Name = 'Speed' "
           "print(c.ClassName, c.Speed.ClassName, next(c:GetAttributes()), workspace:FindFirstChildOfClass('Configuration') == c)",
           "Configuration IntValue nil true");
    expect(session, "MeshPart is a BasePart (not a Part) with MeshId and the usual part properties",
           "local m = Instance.new('MeshPart', workspace) m.Size = Vector3.new(2, 3, 4) "
           "print(m.ClassName, m:IsA('BasePart'), m:IsA('Part'), m.MeshId, m.Size, m.Anchored, m.RenderFidelity)",
           "MeshPart true false  2, 3, 4 false Automatic");
    expect(session, "UnionOperation and CornerWedgePart", "print(Instance.new('UnionOperation'):IsA('PartOperation'), Instance.new('CornerWedgePart').ClassName)",
           "true CornerWedgePart");
    expect(session, "Atmosphere in Lighting sets the fog",
           "local a = Instance.new('Atmosphere') a.Density = 0.5 a.Color = Color3.new(1, 0, 0) a.Parent = game.Lighting "
           "print(a.Density, game.Lighting.FogColor)", "0.5 1, 0, 0");
    expect(session, "changing an Atmosphere in Lighting changes the fog",
           "game.Lighting.Atmosphere.Color = Color3.new(0, 0, 1) print(game.Lighting.FogColor)", "0, 0, 1");
    expect(session, "Clouds put in workspace.Terrain go to Lighting and set the clouds",
           "local c = Instance.new('Clouds') c.Cover = 0.9 c.Parent = workspace.Terrain print(c.Parent == game.Lighting, c.Cover)", "true 0.9");
    expect(session, "BodyColors colours its character",
           "local m = Instance.new('Model') local h = Instance.new('Part') h.Name = 'Head' h.Parent = m "
           "local b = Instance.new('BodyColors') b.HeadColor3 = Color3.new(1, 0, 0) b.Parent = m print(h.Color)", "1, 0, 0");
    expect(session, "UnreliableRemoteEvent is a BaseRemoteEvent",
           "local u = Instance.new('UnreliableRemoteEvent') print(u.ClassName, u:IsA('BaseRemoteEvent'), u:IsA('RemoteEvent'), typeof(u.FireAllClients))",
           "UnreliableRemoteEvent true false function");
    expect(session, "SurfaceLight is a Light", "local s = Instance.new('SurfaceLight') print(s.ClassName, s:IsA('Light'), s.Face, s.Brightness ~= nil)",
           "SurfaceLight true Front true");
    expect(session, "Texture is a Decal with tiling", "local t = Instance.new('Texture') t.StudsPerTileU = 4 print(t.ClassName, t:IsA('Decal'), t.StudsPerTileU, t.Texture)",
           "Texture true 4 ");
    expect(session, "CanvasGroup is a GuiObject", "local c = Instance.new('CanvasGroup') print(c.ClassName, c:IsA('GuiObject'), c.GroupTransparency, c.Size)",
           "CanvasGroup true 0 {0, 100}, {0, 100}");
    expect(session, "a stand-in's own CFrame / Size / Vector2 / object properties",
           "local p = Instance.new('Pose') p.CFrame = CFrame.new(0, 5, 0) local b = Instance.new('BoxHandleAdornment') b.Size = Vector3.new(2, 2, 2) "
           "local s = Instance.new('ImageHandleAdornment') s.Size = Vector2.new(3, 4) local u = Instance.new('UISizeConstraint') u.MaxSize = Vector2.new(100, 50) "
           "local x = Instance.new('SelectionBox') x.Adornee = workspace "
           "print(p.CFrame.Y, b.Size, s.Size, u.MaxSize, x.Adornee == workspace)",
           "5 2, 2, 2 3, 4 100, 50 true");
    expect(session, "Enum items are kept by name", "local d = Instance.new('Dialog') d.Purpose = Enum.DialogPurpose.Quest print(d.Purpose)", "Quest");
    expect(session, "a property of the wrong type is an error",
           "print(pcall(function() Instance.new('Clouds').Cover = 'lots' end))",
           "false CommandBar:1: bad argument #3 to 'newindex' (number expected, got string)");
    expect(session, "DataStoreSetOptions keeps its metadata",
           "local o = Instance.new('DataStoreSetOptions') o:SetMetadata({tag = 'x'}) print(o:GetMetadata().tag)", "x");
    expect(session, "Animator / AnimationController can load animations for the rig they're in",
           "local m = Instance.new('Model', workspace) local p = Instance.new('Part', m) p.Name = 'HumanoidRootPart' "
           "local c = Instance.new('AnimationController', m) local a = Instance.new('Animator', c) "
           "local t = a:LoadAnimation(Instance.new('Animation')) print(a.ClassName, typeof(t.Play))",
           "Animator function");
    expect(session, "an unknown class is still an error",
           "local ok, e = pcall(Instance.new, 'NotAThing') print(ok, e:find(\"unknown class 'NotAThing'\") ~= nil)",
           "false true");

    if (failures) { std::cout << failures << " FAILED\n"; return 1; }
    std::cout << "all good\n";
    return 0;
}
