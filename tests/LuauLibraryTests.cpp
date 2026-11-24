// The rest of Luau's standard library (scripting/LuauLibs.cpp): bit32, buffer, vector,
// table.clone / freeze, math.sign / map, utf8.graphemes, task.cancel, debug.traceback /
// info, the old globals, the small data types and Parallel Luau (Actors, SharedTable).
//
//   LuauLibraryTests <a game file with a baseplate>
//   GB_PROBE=some.lua LuauLibraryTests <game file>    (just runs that file and prints what it says)
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
    if (argc < 2) { std::cerr << "usage: LuauLibraryTests <game file>\n"; return 2; }
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

    if (const char* probe = std::getenv("GB_PROBE")) {
        std::ifstream f(probe);
        std::stringstream ss;
        ss << f.rdbuf();
        std::cout << run(session, ss.str()) << "\n";
        return 0;
    }

    // --- bit32 ---
    expect(session, "bit32.band / bor / bxor / bnot", "print(bit32.band(0xFF, 0x0F), bit32.bor(1, 2, 4), bit32.bxor(5, 1), bit32.bnot(0))",
           "15 7 4 4294967295");
    expect(session, "bit32 shifts and rotates", "print(bit32.lshift(1, 31), bit32.rshift(0x80000000, 31), bit32.arshift(-8, 1), bit32.lrotate(0x80000001, 1), bit32.rrotate(1, 1))",
           "2147483648 1 4294967292 3 2147483648");
    expect(session, "bit32.extract / replace / btest", "print(bit32.extract(0xABCD, 4, 8), bit32.replace(0, 0xF, 8, 4), bit32.btest(2, 3), bit32.btest(4, 3))",
           "188 3840 true false");
    expect(session, "bit32.countlz / countrz / byteswap", "print(bit32.countlz(1), bit32.countrz(8), bit32.countlz(0), string.format('%x', bit32.byteswap(0x11223344)))",
           "31 3 32 44332211");
    expect(session, "bit32 takes negative numbers modulo 2^32", "print(bit32.band(-1, 0xFFFF))", "65535");

    // --- buffer ---
    expect(session, "buffer: write and read numbers",
           "local b = buffer.create(32) buffer.writeu8(b, 0, 255) buffer.writei16(b, 1, -2) buffer.writeu32(b, 3, 4000000000) "
           "buffer.writef32(b, 7, 1.5) buffer.writef64(b, 11, math.pi) "
           "print(buffer.len(b), buffer.readu8(b, 0), buffer.readi8(b, 0), buffer.readi16(b, 1), buffer.readu32(b, 3), buffer.readf32(b, 7), buffer.readf64(b, 11) == math.pi)",
           "32 255 -1 -2 4000000000 1.5 true");
    expect(session, "buffer: strings in and out", "local b = buffer.fromstring('hello') buffer.writestring(b, 0, 'J') print(buffer.tostring(b), buffer.readstring(b, 1, 3))",
           "Jello ell");
    expect(session, "buffer: copy and fill", "local a = buffer.fromstring('abcdef') local b = buffer.create(6) buffer.fill(b, 0, 46) buffer.copy(b, 1, a, 2, 3) print(buffer.tostring(b))",
           ".cde..");
    expect(session, "buffer: bits", "local b = buffer.create(4) buffer.writebits(b, 3, 5, 21) print(buffer.readbits(b, 3, 5), buffer.readu8(b, 0))", "21 168");
    expect(session, "buffer: reading past the end is an error", "print(pcall(buffer.readu32, buffer.create(2), 0))", "false buffer access out of bounds");
    expect(session, "typeof(buffer) is 'buffer'", "print(typeof(buffer.create(1)))", "buffer");
    expect(session, "buffer: vectors", "local b = buffer.create(12) buffer.writevectorf32x3(b, 0, Vector3.new(1, 2, 3)) local v = buffer.readvectorf32x3(b, 0) print(v.X, v.Y, v.Z)",
           "1 2 3");

    // --- vector ---
    expect(session, "vector.create / dot / cross / magnitude",
           "local a, b = vector.create(1, 0, 0), vector.create(0, 1, 0) print(vector.dot(a, b), vector.cross(a, b).Z, vector.magnitude(vector.create(3, 4, 0)))",
           "0 1 5");
    expect(session, "vector.floor / clamp / max / angle",
           "local v = vector.floor(vector.create(1.7, -1.2, 0)) local c = vector.clamp(vector.create(5, -5, 0), vector.zero, vector.one) "
           "print(v.X, v.Y, c.X, c.Y, vector.max(vector.one, vector.create(2, 0, 0)).X, string.format('%.3f', vector.angle(vector.create(1,0,0), vector.create(0,1,0))))",
           "1 -2 1 0 2 1.571");

    // --- table ---
    expect(session, "table.clone is a shallow copy", "local t = {1, 2, x = {}} local c = table.clone(t) c[1] = 9 print(t[1], c[1], c.x == t.x)", "1 9 true");
    expect(session, "table.create", "local t = table.create(3, 'a') print(#t, t[3])", "3 a");
    expect(session, "table.freeze stops changes",
           "local t = table.freeze({a = 1, 2, 3}) local ok, e = pcall(function() t.b = 1 end) print(table.isfrozen(t), t.a, #t, ok, e:find('readonly') ~= nil)",
           "true 1 2 false true");
    expect(session, "a frozen table still loops", "local t = table.freeze({10, 20, k = 'v'}) local n = 0 for _ in pairs(t) do n += 1 end local s = 0 for _, v in ipairs(t) do s += v end print(n, s, next(t) ~= nil)",
           "3 30 true");
    expect(session, "table.insert into a frozen table fails", "print((pcall(table.insert, table.freeze({}), 1)))", "false");
    expect(session, "table.freeze keeps a class's metatable working",
           "local C = {} C.__index = C function C.hi() return 'hi' end local o = table.freeze(setmetatable({n = 1}, C)) print(o.hi(), o.n, table.isfrozen({}))",
           "hi 1 false");
    expect(session, "table.maxn / getn / foreach", "local t = {1, 2, [10] = 3} local seen = 0 table.foreachi({1, 2}, function() seen += 1 end) print(table.maxn(t), table.getn({1, 2}), seen)",
           "10 2 2");

    // --- numbers print like Luau ---
    expect(session, "a whole number kept as a float prints without .0", "print(10 / 2, 'Coins: ' .. 9 / 3, tostring(2.0), 2.5, math.type(10 / 2))",
           "5 Coins: 3 2 2.5 float");

    // --- math / string / utf8 ---
    expect(session, "math.sign / map / isnan / isinf / isfinite", "print(math.sign(-3), math.sign(0), math.map(5, 0, 10, 0, 100), math.isnan(0/0), math.isinf(math.huge), math.isfinite(1))",
           "-1 0 50 true true true");
    expect(session, "math.tau / phi / sqrt2", "print(math.tau == 2 * math.pi, string.format('%.3f %.3f', math.phi, math.sqrt2))", "true 1.618 1.414");
    expect(session, "utf8.graphemes keeps an accent with its letter",
           "local s = 'e\\204\\129a' local n = 0 for i, j in utf8.graphemes(s) do n += 1 end print(n)", "2");
    expect(session, "string.split", "print(table.concat(('a,b,,c'):split(','), '|'))", "a|b||c");

    // --- task ---
    expect(session, "task.spawn gives back the thread", "local th = task.spawn(function() end) print(type(th))", "thread");
    expect(session, "task.cancel stops a delayed thread", "_G.ran = false local th = task.delay(0, function() _G.ran = true end) task.cancel(th) print(coroutine.status(th))",
           "dead");
    expect(session, "task.desynchronize / synchronize are there", "task.desynchronize() task.synchronize() print('ok')", "ok");

    // --- debug ---
    expect(session, "debug.traceback names the line", "print(debug.traceback('oops'):match('^oops\\nstack traceback:') ~= nil)", "true");
    expect(session, "xpcall with debug.traceback", "local ok, msg = xpcall(function() error('bad') end, debug.traceback) print(ok, msg:find('bad') ~= nil)", "false true");
    expect(session, "debug.info on a function", "local function f(a, b, ...) end local n, va = debug.info(f, 'a') print(n, va)", "2 true");
    expect(session, "debug.info on the current line", "print(debug.info(1, 'l'))", "1");
    expect(session, "debug.profilebegin / profileend / memory categories",
           "debug.profilebegin('x') debug.profileend() debug.setmemorycategory('Mine') print(debug.getmemorycategory()) debug.resetmemorycategory()", "Mine");
    expect(session, "the real debug library stays out", "print(debug.getregistry == nil, debug.sethook == nil, debug.setupvalue == nil)", "true true true");

    // --- coroutine.finally ---
    expect(session, "coroutine.finally runs when the coroutine ends",
           "local co = coroutine.create(function() coroutine.yield() end) local done = false coroutine.finally(co, function() done = true end) "
           "coroutine.resume(co) local a = done coroutine.resume(co) print(a, done)", "false true");

    // --- old globals ---
    expect(session, "unpack / gcinfo / newproxy / elapsedTime / ypcall",
           "print(select('#', unpack({1, 2, 3})), type(gcinfo()), type(newproxy(true)), getmetatable(newproxy(true)) ~= nil, elapsedTime() >= 0, ypcall(function() end))",
           "3 number userdata true true true");
    expect(session, "settings() and UserSettings()", "print(settings().Rendering.QualityLevel, UserSettings():GetService('UserGameSettings').MasterVolume)", "Automatic 1");

    // --- small data types ---
    expect(session, "Axes and Faces", "local a = Axes.new(Enum.Axis.X, 'Top') local f = Faces.new('Top', 'Front') print(a.X, a.Y, a.Z, f.Top, f.Back, typeof(f), tostring(f))",
           "true true false true false Faces Top, Front");
    expect(session, "Vector3int16 wraps around", "local v = Vector3int16.new(1, 2, 40000) + Vector3int16.new(1, 1, 1) print(v.X, v.Y, v.Z, typeof(v))", "2 3 -25535 Vector3int16");
    expect(session, "Vector2int16 and Region3int16", "local r = Region3int16.new(Vector3int16.new(0,0,0), Vector3int16.new(4,4,4)) print(Vector2int16.new(5, 6).Y, r.Max.X, typeof(r))",
           "6 4 Region3int16");
    expect(session, "FloatCurveKey", "local k = FloatCurveKey.new(1, 5, Enum.KeyInterpolationMode and Enum.KeyInterpolationMode.Linear or 'Linear') print(k.Time, k.Value, typeof(k))",
           "1 5 FloatCurveKey");

    // --- data types: the rest of their methods ---
    expect(session, "Vector3:Abs / Floor / Sign / Max / FuzzyEq / Angle",
           "local v = Vector3.new(-1.5, 2.5, 0) print(v:Abs().X, v:Floor().X, v:Sign().X, v:Max(Vector3.one).Z, v:FuzzyEq(Vector3.new(-1.5, 2.5, 0.000001)), "
           "string.format('%.3f', Vector3.xAxis:Angle(Vector3.yAxis)), Vector3.FromNormalId(Enum.NormalId.Top).Y, (Vector3.new(7, 8, 9) // 2).X)",
           "1.5 -2 -1 1 true 1.571 1 3");
    expect(session, "Vector2:Cross / Angle / Max / axes", "print(Vector2.xAxis:Cross(Vector2.yAxis), string.format('%.3f', Vector2.xAxis:Angle(Vector2.yAxis)), Vector2.new(1, 5):Max(Vector2.new(3, 2)).X)",
           "1 1.571 3");
    expect(session, "CFrame.identity / XVector / FuzzyEq / AngleBetween",
           "local a = CFrame.Angles(0, math.pi / 2, 0) print(CFrame.identity == CFrame.new(), string.format('%.2f', a.XVector.Z), a:FuzzyEq(a), string.format('%.3f', CFrame.new():AngleBetween(a)))",
           "true -1.00 true 1.571");
    expect(session, "CFrame:ToAxisAngle / fromEulerAngles / ToEulerAngles",
           "local axis, ang = CFrame.fromAxisAngle(Vector3.yAxis, 1):ToAxisAngle() local x, y, z = CFrame.fromEulerAngles(0.1, 0.2, 0.3, Enum.RotationOrder.YXZ):ToEulerAngles(Enum.RotationOrder.YXZ) "
           "print(string.format('%.2f %.2f %.2f %.2f %.2f', axis.Y, ang, x, y, z))", "1.00 1.00 0.10 0.20 0.30");
    expect(session, "CFrame.lookAlong / fromMatrix / fromRotationBetweenVectors / Orthonormalize",
           "local l = CFrame.lookAlong(Vector3.zero, Vector3.new(0, 0, -5)) local m = CFrame.fromMatrix(Vector3.new(1, 2, 3), Vector3.xAxis, Vector3.yAxis) "
           "local r = CFrame.fromRotationBetweenVectors(Vector3.xAxis, Vector3.yAxis) * Vector3.xAxis "
           "print(l.LookVector.Z, m.Y, string.format('%.2f', r.Y), m:Orthonormalize() == m)", "-1 2 1.00 true");
    expect(session, "CFrame.new with a quaternion or 12 numbers", "local q = CFrame.new(1, 2, 3, 0, 0, 0, 1) local f = CFrame.new(0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1) print(q.X, q == CFrame.new(1, 2, 3), f == CFrame.identity)",
           "1 true true");
    expect(session, "Color3:ToHSV / ToHex / Color3.toHSV", "local h, s, v = Color3.new(0, 1, 0):ToHSV() print(string.format('%.3f', h), s, v, Color3.fromRGB(255, 136, 0):ToHex(), select(3, Color3.toHSV(Color3.new(0.5, 0, 0))))",
           "0.333 1 1 FF8800 0.5");
    expect(session, "DateTime.fromUniversalTime / fromIsoDate / FormatUniversalTime",
           "local d = DateTime.fromUniversalTime(2024, 3, 5, 14, 7, 9) local i = DateTime.fromIsoDate('2024-03-05T14:07:09Z') "
           "print(d.UnixTimestamp, i == d, d:FormatUniversalTime('YYYY-MM-DD HH:mm:ss', 'en-us'), d:FormatUniversalTime('LLL', 'en-us'), d:FormatUniversalTime('dddd [at] h A'))",
           "1709647629 true 2024-03-05 14:07:09 March 5, 2024 2:07 PM Tuesday at 2 PM");
    expect(session, "Rect / Region3 / Font / PathWaypoint",
           "local r = Rect.new(0, 0, 4, 3) local g = Region3.new(Vector3.new(0, 0, 0), Vector3.new(2, 4, 6)) local f = Font.fromEnum(Enum.Font.SourceSansBold) local w = PathWaypoint.new(Vector3.one) "
           "print(r.Width, r.Height, g.Size.Y, g.CFrame.Z, f.Bold, typeof(f), w.Action, g:ExpandToGrid(4).Size.Z)",
           "4 3 4 3 true Font Walk 8");
    expect(session, "an Enum item's Name and IsA", "print(Enum.KeyCode.E.Name, Enum.Material.Plastic:IsA('Material'), Enum.Material.Plastic:IsA('KeyCode'))", "E true false");
    expect(session, "params:AddToFilter and ray:ClosestPoint", "local p = RaycastParams.new() p:AddToFilter(workspace) p:AddToFilter({workspace}) local r = Ray.new(Vector3.zero, Vector3.new(10, 0, 0)) "
           "print(#p.FilterDescendantsInstances, r:ClosestPoint(Vector3.new(5, 3, 0)).X, r:Distance(Vector3.new(5, 3, 0)))", "2 5 3");
    expect(session, "math.e / shared / getfenv / loadstring", "shared.x = 1 print(string.format('%.3f', math.e), shared.x, getfenv() == _G, loadstring('return 1 + 1')())", "2.718 1 true 2");
    expect(session, "Instance.fromExisting leaves the children out", "local m = Instance.new('Model') Instance.new('Part').Parent = m local c = Instance.fromExisting(m) print(#m:GetChildren(), #c:GetChildren(), c.ClassName)",
           "1 0 Model");

    // --- Parallel Luau ---
    expect(session, "SharedTable", "local st = SharedTable.new({a = 1}) st.b = 2 SharedTable.increment(st, 'a', 5) print(st.a, st.b, SharedTable.size(st), typeof(st))",
           "6 2 2 SharedTable");
    expect(session, "a frozen SharedTable refuses changes", "local st = SharedTable.cloneAndFreeze(SharedTable.new({a = 1})) print(SharedTable.isFrozen(st), (pcall(function() st.a = 2 end)))",
           "true false");
    expect(session, "Actors pass messages", "local a = Instance.new('Actor') a.Parent = workspace local s = Instance.new('Script') s.Parent = a "
           "local got a:BindToMessage('Hi', function(x) got = x end) a:SendMessage('Hi', 42) print(a.ClassName, s:GetActor() == a, workspace:GetActor(), got) a:Destroy()",
           "Actor true nil 42");
    expect(session, "signal:ConnectParallel", "local c = game:GetService('RunService').Heartbeat:ConnectParallel(function() end) print(c.Connected) c:Disconnect()", "true");

    std::cout << (failures ? "\nSOME CHECKS FAILED\n" : "\nall good\n");
    return failures ? 1 : 0;
}
