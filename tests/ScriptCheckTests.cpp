// ScriptCheck: the Script editor's red and orange wavy lines. Each mistake is found on the
// right line and place, with a "did you mean" fix; good code gets no lines at all.
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scripting/ScriptCheck.h"

#include <filesystem>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

std::string show(const std::vector<ScriptCheck::Mark>& marks) {
    std::string out;
    for (const auto& m : marks)
        out += "\n      line " + std::to_string(m.line) + " col " + std::to_string(m.col) + " len " + std::to_string(m.len) +
               (m.error ? " error: " : " warning: ") + m.text + (m.fix.empty() ? "" : " [fix: " + m.fix + "]");
    return out;
}

// One mark expected: on this line, under this text (found on the line), with this fix.
void expect(const std::string& what, const std::string& code, int line, const std::string& under, const std::string& fix,
            bool error = false) {
    const auto marks = ScriptCheck::check(code);
    bool ok = marks.size() == 1;
    if (ok) {
        const auto& m = marks[0];
        size_t start = 0;
        for (int k = 1; k < line; ++k) start = code.find('\n', start) + 1;
        const std::string lineText = code.substr(start, code.find('\n', start) - start);
        ok = m.line == line && m.error == error && m.fix == fix && m.col >= 0 && m.col + m.len <= (int)lineText.size() &&
             lineText.substr((size_t)m.col, (size_t)m.len) == under;
    }
    check(ok, what + (ok ? "" : show(marks)));
}

void clean(const std::string& what, const std::string& code) {
    const auto marks = ScriptCheck::check(code);
    check(marks.empty(), what + show(marks));
}
} // namespace

int main(int argc, char** argv) {
    Mesh::headless = true;
    // ScriptCheckTests --scan a.lua b.lua ...: just show the marks in those files.
    if (argc > 1 && std::string(argv[1]) == "--scan") {
        for (int i = 2; i < argc; ++i) {
            std::ifstream f(argv[i]);
            std::stringstream ss;
            ss << f.rdbuf();
            const auto marks = ScriptCheck::check(ss.str());
            if (!marks.empty()) std::cout << argv[i] << show(marks) << "\n";
        }
        return 0;
    }

    // --- Mistakes Lua can't read (red) ---
    expect("a missing end points at the function it belongs to",
           "local function hi()\n    print(\"hi\")\n\nhi()\n", 1, "function", "", true);
    expect("= in an if: use ==",
           "local x = 1\nif x = 1 then\n  print(x)\nend\n", 2, "=", "", true);
    {
        const auto m = ScriptCheck::check("if x = 1 then end");
        check(!m.empty() && m[0].text.find("==") != std::string::npos, "...and it says so in plain words" + show(m));
    }
    expect("a string with no closing quote",
           "print(\"hello)\n", 1, "\"hello)", "", true);

    // --- Likely mistakes (orange) ---
    expect("an undefined name, with \"did you mean\"", "pirnt(\"hi\")\n", 1, "pirnt", "print");
    expect("wrong upper / lower case", "local p = Instance.new(\"Part\")\np.Parent = Workspace\nPrint(p)\n", 3, "Print", "print");
    expect("a library part that doesn't exist", "local n = math.flor(2.5)\n", 1, "flor", "floor");
    expect("task.wiat", "task.wiat(1)\n", 1, "wiat", "wait");
    expect("an unknown service", "local players = game:GetService(\"Plyers\")\n", 1, "Plyers", "Players");
    expect("game.Plyers", "local p = game.Plyers\n", 1, "Plyers", "Players");
    expect("Instance.new of something that doesn't exist", "local p = Instance.new(\"Prat\")\n", 1, "Prat", "Part");
    expect("Enum.KeyCode typo", "local k = Enum.KeyCode.Spase\n", 1, "Spase", "Space");
    expect("Enum.Material typo", "local m = Enum.Material.Neone\n", 1, "Neone", "Neon");
    expect("a dot where it needs a colon", "local p = workspace:FindFirstChild(\"Door\")\np.Destroy()\n", 2, ".Destroy", ":Destroy");
    expect("game.GetService with a dot", "local rs = game.GetService(\"RunService\")\n", 1, ".GetService", ":GetService");
    {
        const auto m = ScriptCheck::check("pirnt(1)\npirnt(2)\n");
        check(m.size() == 1, "the same unknown name is only underlined once" + show(m));
    }
    {
        const auto m = ScriptCheck::check("local a = mth.floor(1)\nlocal b = Vector3.nwe(1, 2, 3)\n");
        check(m.size() == 2 && m[0].line == 1 && m[1].line == 2 && m[1].fix == "new", "two mistakes, in order" + show(m));
    }

    // --- Good code: no lines ---
    clean("a normal script", R"(
local Players = game:GetService("Players")
local part = Instance.new("Part")
part.Size = Vector3.new(4, 1, 4)
part.Anchored = true
part.Parent = workspace
local function onTouched(hit)
    local hum = hit.Parent:FindFirstChild("Humanoid")
    if hum then hum:TakeDamage(10) end
end
part.Touched:Connect(onTouched)
for i, v in ipairs(workspace:GetChildren()) do print(i, v.Name) end
task.wait(1)
print(math.floor(2.5), string.upper("hi"), Enum.KeyCode.E, Enum.Material.Neon)
)");
    clean("Luau extras (types, +=, continue, `strings`)", R"(
local score: number = 0
type Point = { x: number, y: number }
for i = 1, 10 do
    if i % 2 == 0 then continue end
    score += i
end
local function add(a: number, b: number): number return a + b end
print(`score is {score}`, add(1, 2))
)");
    clean("globals the script sets itself, and its own library parts", R"(
counter = 0
a, b = 1, 2
function string.shout(s) return s:upper() .. "!" end
function bump() counter = counter + a + b end
bump()
print(string.shout("hi"))
local t = { first = 1, second = 2 }
print(t.first)
)");
    clean("a module's own Destroy is fine", R"(
local Thing = {}
function Thing.Destroy(self) end
Thing.Destroy(Thing)
return Thing
)");
    clean("names inside strings and comments are not checked", "-- pirnt this\nprint(\"pirnt\") --[[ mth.flor ]]\n");

    // --- The sample games' scripts: not one line under good code ---
    if (argc > 1) {
        int scripts = 0;
        std::string found;
        for (const auto& e : std::filesystem::directory_iterator(argv[1])) {
            if (e.path().extension() != ".gbscene") continue;
            Scene scene;
            std::string err;
            if (!Serializer::loadGameFile(scene, e.path().string(), &err)) { found += "\n      can't load " + e.path().string(); continue; }
            scene.forEach([&](SceneNode* n) {
                if (!n->isScript()) return;
                ++scripts;
                const auto marks = ScriptCheck::check(n->source);
                if (!marks.empty()) found += "\n      " + e.path().filename().string() + " " + n->fullName() + show(marks);
            });
        }
        check(scripts > 10 && found.empty(), "the sample games' " + std::to_string(scripts) + " scripts have no lines" + found);
    }

    // --- "Did you mean" ---
    check(ScriptCheck::closest("pirnt", {"print", "pairs"}) == "print", "closest: two letters swapped");
    check(ScriptCheck::closest("xyzzy", {"print", "pairs"}).empty(), "closest: nothing close, no guess");

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
