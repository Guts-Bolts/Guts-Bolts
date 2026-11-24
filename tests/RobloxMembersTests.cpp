// Roblox members written in Lua (src/scripting/RobloxMembers.cpp): Model:MoveTo /
// GetBoundingBox / ScaleTo, part:Resize, Attachment.WorldCFrame, Lighting.TimeOfDay,
// RunService:BindToRenderStep, TextBounds, Camera helpers...
//   RobloxMembersTests <game file>
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
    if (argc < 2) { std::cerr << "usage: RobloxMembersTests <game file>\n"; return 2; }
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
    auto step = [&](int frames) { for (int i = 0; i < frames; ++i) session.update(1.0f / 60.0f, 0.0f, false); };

    // A little model far from the obby: two parts side by side.
    run(session, "local m = Instance.new('Model') m.Name = 'RmModel' "
                 "local a = Instance.new('Part') a.Name = 'A' a.Anchored = true a.Size = Vector3.new(2, 2, 2) a.Position = Vector3.new(500, 10, 500) a.Parent = m "
                 "local b = Instance.new('Part') b.Name = 'B' b.Anchored = true b.Size = Vector3.new(2, 2, 2) b.Position = Vector3.new(504, 10, 500) b.Parent = m "
                 "m.Parent = workspace");
    expect(session, "Model:GetBoundingBox and GetExtentsSize",
           "local cf, size = workspace.RmModel:GetBoundingBox() print(cf.Position, size, workspace.RmModel:GetExtentsSize())",
           "502, 10, 500 6, 2, 2 6, 2, 2");
    expect(session, "Model:TranslateBy moves every part",
           "workspace.RmModel:TranslateBy(Vector3.new(0, 5, 0)) print(workspace.RmModel.A.Position, workspace.RmModel.B.Position)",
           "500, 15, 500 504, 15, 500");
    expect(session, "Model:MoveTo puts its middle there",
           "workspace.RmModel:MoveTo(Vector3.new(600, 50, 600)) print((workspace.RmModel:GetBoundingBox()).Position)",
           "600, 50, 600");
    expect(session, "Model:ScaleTo grows the parts and spreads them out; GetScale reads it back",
           "local m = workspace.RmModel m:ScaleTo(2) print(m:GetScale(), m.A.Size, m:GetExtentsSize()) m:ScaleTo(1) print(m.A.Size)",
           "2 4, 4, 4 12, 4, 4\n2, 2, 2");
    expect(session, "part:Resize grows one face and keeps the other side",
           "local p = workspace.RmModel.A local before = p.Position print(p:Resize(Enum.NormalId.Top, 2), p.Size, p.Position - before)",
           "true 2, 4, 2 0, 1, 0");
    expect(session, "GetConnectedParts follows welds",
           "local w = Instance.new('WeldConstraint') w.Part0 = workspace.RmModel.A w.Part1 = workspace.RmModel.B w.Parent = workspace.RmModel.A "
           "local c = workspace.RmModel.A:GetConnectedParts() print(#c, c[1] == workspace.RmModel.B)",
           "1 true");
    expect(session, "CanQuery / CanTouch / Massless are kept",
           "local p = workspace.RmModel.A p.CanQuery = false p.Massless = true print(p.CanQuery, p.CanTouch, p.Massless)", "false true true");
    expect(session, "Attachment.WorldCFrame and WorldPosition agree",
           "local a = Instance.new('Attachment') a.Position = Vector3.new(0, 1, 0) a.Parent = workspace.RmModel.B "
           "print((a.WorldCFrame.Position - a.WorldPosition).Magnitude < 0.001, a.WorldAxis) "
           "a.WorldCFrame = CFrame.new(1, 2, 3) * CFrame.Angles(0, math.pi / 2, 0) "
           "print((a.WorldPosition - Vector3.new(1, 2, 3)).Magnitude < 0.001, (a.WorldAxis - Vector3.new(0, 0, -1)).Magnitude < 0.001)",
           "true 1, 0, 0\ntrue true");
    expect(session, "Lighting.TimeOfDay and minutes after midnight follow ClockTime",
           "game.Lighting.ClockTime = 14.5 print(game.Lighting.TimeOfDay, game.Lighting:GetMinutesAfterMidnight()) "
           "game.Lighting.TimeOfDay = '06:15:00' print(game.Lighting.ClockTime) game.Lighting:SetMinutesAfterMidnight(720) print(game.Lighting.ClockTime)",
           "14:30:00 870\n6.25\n12");
    expect(session, "the sun is up at noon and down at midnight",
           "game.Lighting.ClockTime = 12 local up = game.Lighting:GetSunDirection().Y game.Lighting.ClockTime = 0 "
           "print(up > 0.5, game.Lighting:GetSunDirection().Y < -0.5, game.Lighting:GetMoonDirection().Y > 0.5)",
           "true true true");
    expect(session, "FogEnd turns the fog on and thickens it",
           "game.Lighting.FogEnd = 300 print(game.Lighting.FogEnabled, game.Lighting.FogEnd, game.Lighting.FogDensity > 0)", "true 300 true");
    expect(session, "OutdoorAmbient is our Ambient",
           "game.Lighting.OutdoorAmbient = Color3.new(1, 0, 0) print(game.Lighting.Ambient)", "1, 0, 0");
    run(session, "_G.rs = {} game:GetService('RunService'):BindToRenderStep('Second', 200, function() table.insert(_G.rs, 'b') end) "
                 "game:GetService('RunService'):BindToRenderStep('First', 100, function() table.insert(_G.rs, 'a') end)");
    step(1);
    expect(session, "BindToRenderStep runs every frame, lowest priority first",
           "print(_G.rs[1], _G.rs[2]) game:GetService('RunService'):UnbindFromRenderStep('First') game:GetService('RunService'):UnbindFromRenderStep('Second') _G.rs = {}",
           "a b");
    step(2);
    expect(session, "UnbindFromRenderStep stops it", "print(#_G.rs)", "0");
    expect(session, "UserInputService helpers answer",
           "local U = game:GetService('UserInputService') print(#U:GetKeysPressed(), U:IsMouseButtonPressed(Enum.UserInputType.MouseButton1), U:GetLastInputType(), U:GetFocusedTextBox())",
           "0 false Keyboard nil");
    expect(session, "TextLabel.TextBounds measures the text",
           "local t = Instance.new('TextLabel') t.Text = 'Hello' t.TextSize = 20 print(t.TextBounds.Y, t.TextBounds.X > 0)", "20 true");
    expect(session, "kept GUI properties read back",
           "local i = Instance.new('ImageLabel') i.ImageRectOffset = Vector2.new(16, 32) i.ScaleType = Enum.ScaleType.Slice "
           "print(i.ImageRectOffset, i.ScaleType, i.SliceCenter, i.AutomaticSize)",
           "16, 32 Slice 0, 0, 0, 0 None");
    expect(session, "workspace extras", "print(workspace.FallenPartsDestroyHeight, workspace:GetRealPhysicsFPS(), type(workspace:GetServerTimeNow()), workspace.DistributedGameTime >= 0)",
           "-500 60 number true");
    expect(session, "Camera extras",
           "local c = workspace.CurrentCamera print(c.DiagonalFieldOfView > c.FieldOfView, type(c:GetPartsObscuringTarget({Vector3.new(0, 0, 0)}, {})), c.NearPlaneZ)",
           "true table -0.1");
    expect(session, "TweenService:SmoothDamp moves toward the target without passing it",
           "local v, vel = game:GetService('TweenService'):SmoothDamp(0, 10, 0, 0.3) print(v > 0 and v < 10, vel > 0)", "true true");
    expect(session, "Players / DataStoreService / TextService / game extras",
           "print(select(2, game.Players:GetUserThumbnailAsync(1, Enum.ThumbnailType.HeadShot, Enum.ThumbnailSize.Size100x100)), "
           "game:GetService('DataStoreService'):GetRequestBudgetForRequestType(Enum.DataStoreRequestType.GetAsync), "
           "game:GetService('TextService'):GetTextBoundsAsync(Instance.new('GetTextBoundsParams')).Y, game:IsLoaded())",
           "true 100 14 true");
    expect(session, "a member nothing knows is still an error",
           "print(pcall(function() return workspace.RmModel.A.NotAThing end))",
           "false CommandBar:1: 'NotAThing' is not a valid member of Part \"A\"");
    expect(session, "a Model method on a part says what it's for",
           "print((select(2, pcall(function() workspace.RmModel.A:ScaleTo(2) end))):find('Model method') ~= nil)", "true");

    if (failures) { std::cout << failures << " FAILED\n"; return 1; }
    std::cout << "all good\n";
    return 0;
}
