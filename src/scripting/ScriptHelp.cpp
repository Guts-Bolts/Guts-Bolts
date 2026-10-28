#include "ScriptHelp.h"

#include <cctype>

namespace ScriptHelp {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
} // namespace

const std::vector<Entry>& all() {
    static const std::vector<Entry> list = {
        // --- Guts&Bolts' own helpers ---
        {"getmouse", "getmouse() -> Vector2",
         "Where the mouse is on the game screen, in pixels (LocalScripts only)", true},
        {"getmouse3d", "getmouse3d(distance?, ignore?) -> Vector3, BasePart?, Vector3",
         "The spot in the world under the mouse, the part there (nil = sky) and the way that surface faces. "
         "e.g. local pos, part = getmouse3d()", true},
        {"Explode", "Explode(position, radius?, power?, options?)",
         "A blast at a Vector3. options = {Fire = 1, Smoke = true, Destroy = false, Hurts = true, ...}", true},
        {"Effects.Explosion", "Effects.Explosion(position, radius?, power?, options?)", "Same as Explode", true},
        {"Effects.Blood", "Effects.Blood(position, amount?, direction?)", "A spray of blood (when gore is on)", true},
        {"Effects.Oil", "Effects.Oil(position, amount?, direction?)", "A spray of oil (when gore is on)", true},
        {"Effects.Gibs", "Effects.Gibs(position, count?)", "Bits flying out (when gore is on)", true},
        {"Effects.Sparks", "Effects.Sparks(position, count?)", "A shower of sparks", true},
        {"Sounds.Play", "Sounds.Play(name, position?, volume?)", "Plays a built-in sound once. Sounds.List() names them", true},
        {"Sounds.List", "Sounds.List() -> {string}", "The names of the built-in sounds", true},
        {"Gui.Message", "Gui.Message(text, seconds?)", "Shows a big message in the middle of the screen", true},
        {"Gui.Label", "Gui.Label(key, text?)", "A line of text in the corner; Gui.Label(key) takes it away", true},
        {"Gui.Clear", "Gui.Clear()", "Takes away every message and label", true},

        // --- Lua ---
        {"print", "print(...)", "Writes to the Output window", true},
        {"warn", "warn(...)", "Writes a yellow warning to the Output window", true},
        {"error", "error(message, level?)", "Stops the script with an error", true},
        {"pairs", "pairs(table) -> iterator", "for key, value in pairs(t) do ... end", true},
        {"ipairs", "ipairs(list) -> iterator", "for i, value in ipairs(list) do ... end (in order, 1, 2, 3...)", true},
        {"tostring", "tostring(value) -> string", "Turns anything into text", true},
        {"tonumber", "tonumber(text) -> number?", "Turns text into a number (nil if it isn't one)", true},
        {"type", "type(value) -> string", "\"number\", \"string\", \"table\", \"function\", \"nil\"...", true},
        {"pcall", "pcall(function, ...) -> ok, result", "Runs a function and catches its error instead of stopping", true},
        {"require", "require(moduleScript) -> any", "Runs a ModuleScript once and gives back what it returns", true},
        {"select", "select(index, ...) -> ...", "select(\"#\", ...) counts the values", true},
        {"wait", "wait(seconds?) -> number", "Old name for task.wait", true},
        {"spawn", "spawn(function)", "Old name for task.spawn", true},
        {"delay", "delay(seconds, function)", "Old name for task.delay", true},
        {"tick", "tick() -> number", "Seconds since 1970 (good for timing)", true},
        {"time", "time() -> number", "Seconds since the game started", true},
        {"task.wait", "task.wait(seconds?) -> number", "Pauses this script for a while (one frame if left empty)", true},
        {"task.spawn", "task.spawn(function, ...)", "Runs a function right away, side by side with this script", true},
        {"task.delay", "task.delay(seconds, function, ...)", "Runs a function after a while", true},
        {"math.random", "math.random(min?, max?) -> number", "A random number: math.random(1, 6) is a dice roll", true},
        {"math.floor", "math.floor(x) -> number", "Rounds down", true},
        {"math.ceil", "math.ceil(x) -> number", "Rounds up", true},
        {"math.abs", "math.abs(x) -> number", "Takes away the minus sign", true},
        {"math.clamp", "math.clamp(x, min, max) -> number", "Keeps x between min and max", true},
        {"math.min", "math.min(a, b, ...) -> number", "The smallest", true},
        {"math.max", "math.max(a, b, ...) -> number", "The biggest", true},
        {"math.sqrt", "math.sqrt(x) -> number", "Square root", true},
        {"math.sin", "math.sin(radians) -> number", "Sine (math.rad(degrees) turns degrees into radians)", true},
        {"math.cos", "math.cos(radians) -> number", "Cosine", true},
        {"math.rad", "math.rad(degrees) -> number", "Degrees to radians", true},
        {"string.format", "string.format(pattern, ...) -> string", "string.format(\"%d coins\", 5) -> \"5 coins\"", true},
        {"string.sub", "string.sub(text, from, to?) -> string", "A piece of the text", true},
        {"string.lower", "string.lower(text) -> string", "all small letters", true},
        {"string.upper", "string.upper(text) -> string", "ALL BIG LETTERS", true},
        {"string.split", "string.split(text, separator) -> {string}", "\"a,b\":split(\",\") -> {\"a\", \"b\"}", true},
        {"table.insert", "table.insert(list, value) / table.insert(list, index, value)", "Adds to a list", true},
        {"table.remove", "table.remove(list, index?) -> value", "Takes out of a list (the last one if no index)", true},
        {"table.find", "table.find(list, value) -> number?", "Where the value is in the list (nil if it isn't)", true},
        {"table.sort", "table.sort(list, lessThan?)", "Puts a list in order", true},
        {"table.concat", "table.concat(list, separator?) -> string", "Glues a list of text together", true},

        // --- Making things ---
        {"Instance.new", "Instance.new(className) -> Instance", "Makes a new object, e.g. Instance.new(\"Part\"). Set .Parent to put it in the game", true},
        {"Vector3.new", "Vector3.new(x, y, z) -> Vector3", "A position or direction in the world", true},
        {"Vector2.new", "Vector2.new(x, y) -> Vector2", "A position on the screen", true},
        {"CFrame.new", "CFrame.new(position) / CFrame.new(position, lookAt) -> CFrame", "A position plus which way it faces", true},
        {"CFrame.Angles", "CFrame.Angles(rx, ry, rz) -> CFrame", "A turn, in radians (use math.rad(90))", true},
        {"CFrame.lookAt", "CFrame.lookAt(position, target) -> CFrame", "At position, facing target", true},
        {"Color3.new", "Color3.new(r, g, b) -> Color3", "A colour, each from 0 to 1", true},
        {"Color3.fromRGB", "Color3.fromRGB(r, g, b) -> Color3", "A colour, each from 0 to 255", true},
        {"Color3.fromHSV", "Color3.fromHSV(hue, saturation, value) -> Color3", "A colour from hue (0 to 1)", true},
        {"UDim2.new", "UDim2.new(xScale, xOffset, yScale, yOffset) -> UDim2", "A size or position for UI (scale = part of the screen)", true},
        {"UDim2.fromScale", "UDim2.fromScale(x, y) -> UDim2", "UI size / position as parts of the screen (0 to 1)", true},
        {"UDim2.fromOffset", "UDim2.fromOffset(x, y) -> UDim2", "UI size / position in pixels", true},
        {"TweenInfo.new", "TweenInfo.new(seconds?, style?, direction?, repeats?, reverses?, delay?) -> TweenInfo",
         "How a tween moves", true},
        {"RaycastParams.new", "RaycastParams.new() -> RaycastParams", "Settings for workspace:Raycast (FilterDescendantsInstances...)", true},
        {"OverlapParams.new", "OverlapParams.new() -> OverlapParams", "Settings for GetPartsInPart / GetPartBoundsInBox...", true},

        // --- Services and globals ---
        {"game", "game", "The whole game. game:GetService(\"Players\")", true},
        {"workspace", "workspace", "Everything in the 3D world", true},
        {"script", "script", "This script itself (script.Parent is the thing it's in)", true},
        {"Enum", "Enum", "Named choices, e.g. Enum.KeyCode.E, Enum.Material.Neon", true},

        // --- Methods (obj:Name(...)) ---
        {"GetService", "game:GetService(name) -> Service", "A service, e.g. game:GetService(\"Players\")", false},
        {"FindFirstChild", "obj:FindFirstChild(name) -> Instance?", "The child with that name, or nil", false},
        {"FindFirstChildOfClass", "obj:FindFirstChildOfClass(className) -> Instance?", "The first child of that class, or nil", false},
        {"WaitForChild", "obj:WaitForChild(name, timeout?) -> Instance", "Waits until a child with that name is there", false},
        {"GetChildren", "obj:GetChildren() -> {Instance}", "Everything directly inside it", false},
        {"GetDescendants", "obj:GetDescendants() -> {Instance}", "Everything inside it, all the way down", false},
        {"Destroy", "obj:Destroy()", "Removes it for good", false},
        {"Clone", "obj:Clone() -> Instance", "A copy (set its Parent to put it in the game)", false},
        {"IsA", "obj:IsA(className) -> boolean", "part:IsA(\"BasePart\") is true for any part", false},
        {"IsDescendantOf", "obj:IsDescendantOf(other) -> boolean", "Is it somewhere inside other?", false},
        {"GetFullName", "obj:GetFullName() -> string", "\"Workspace.Model.Part\"", false},
        {"GetPivot", "model:GetPivot() -> CFrame", "Where a model (or part) is", false},
        {"PivotTo", "model:PivotTo(cframe)", "Moves a model (or part) there", false},
        {"GetAttribute", "obj:GetAttribute(name) -> any", "A value you stored on it", false},
        {"SetAttribute", "obj:SetAttribute(name, value)", "Stores a value on it", false},
        {"Connect", "event:Connect(function) -> Connection", "Runs the function every time it happens", false},
        {"Once", "event:Once(function) -> Connection", "Runs the function the next time only", false},
        {"Wait", "event:Wait() -> ...", "Pauses until it happens", false},
        {"Disconnect", "connection:Disconnect()", "Stops a :Connect", false},
        {"ApplyImpulse", "part:ApplyImpulse(force)", "A push (a Vector3)", false},
        {"TakeDamage", "humanoid:TakeDamage(amount)", "Takes away health (not through a ForceField)", false},
        {"MoveTo", "humanoid:MoveTo(position)", "Walks the character to a Vector3", false},
        {"Raycast", "workspace:Raycast(origin, direction, params?) -> RaycastResult?",
         "Shoots a line; result.Instance, .Position, .Normal (nil if it hits nothing)", false},
        {"GetPartBoundsInRadius", "workspace:GetPartBoundsInRadius(position, radius, params?) -> {BasePart}", "Parts near a spot", false},
        {"GetPartBoundsInBox", "workspace:GetPartBoundsInBox(cframe, size, params?) -> {BasePart}", "Parts in a box", false},
        {"GetPartsInPart", "workspace:GetPartsInPart(part, params?) -> {BasePart}", "Parts touching a part", false},
        {"Create", "TweenService:Create(object, tweenInfo, {Property = goal}) -> Tween", "Smoothly changes properties. Call :Play() on it", false},
        {"Play", "tween:Play() / sound:Play() / track:Play()", "Starts it", false},
        {"GetMouse", "player:GetMouse() -> Mouse", "mouse.Hit, mouse.Target, mouse.Button1Down (LocalScripts only)", false},
        {"GetMouseLocation", "UserInputService:GetMouseLocation() -> Vector2", "Where the mouse is on the screen", false},
        {"IsKeyDown", "UserInputService:IsKeyDown(keyCode) -> boolean", "Is that key held? e.g. Enum.KeyCode.W", false},
        {"FireServer", "remote:FireServer(...)", "LocalScript -> Script: OnServerEvent gets (player, ...)", false},
        {"FireClient", "remote:FireClient(player, ...)", "Script -> that player's LocalScripts: OnClientEvent gets (...)", false},
        {"FireAllClients", "remote:FireAllClients(...)", "Script -> every player's LocalScripts", false},
        {"InvokeServer", "remoteFunction:InvokeServer(...) -> ...", "Asks the server and waits for OnServerInvoke's answer", false},
        {"BindAction", "ContextActionService:BindAction(name, function(name, state, input), touchButton, inputs...)",
         "Runs the function for keys or mouse buttons, e.g. Enum.KeyCode.E, Enum.UserInputType.MouseButton1", false},
        {"BindActionAtPriority", "ContextActionService:BindActionAtPriority(name, function, touchButton, priority, inputs...)",
         "BindAction that goes before (higher number) or after others", false},
        {"UnbindAction", "ContextActionService:UnbindAction(name)", "Stops a BindAction", false},
        {"GetPlayerFromCharacter", "Players:GetPlayerFromCharacter(model) -> Player?", "Whose character this is (nil if nobody's)", false},
        {"GetPlayers", "Players:GetPlayers() -> {Player}", "Everyone in the game", false},
        {"LoadAnimation", "animator:LoadAnimation(animation) -> AnimationTrack", "Get a track you can :Play()", false},

        // --- Events (obj.Name:Connect(...)) ---
        {"Touched", "part.Touched:Connect(function(otherPart) ... end)", "Something bumped into the part", false},
        {"Clicked", "part.Clicked:Connect(function(player) ... end)", "Someone clicked the part", false},
        {"Heartbeat", "RunService.Heartbeat:Connect(function(dt) ... end)", "Every frame; dt = seconds since the last one", false},
        {"Died", "humanoid.Died:Connect(function() ... end)", "The character died", false},
        {"PlayerAdded", "Players.PlayerAdded:Connect(function(player) ... end)", "Someone joined", false},
        {"CharacterAdded", "player.CharacterAdded:Connect(function(character) ... end)", "Their character (re)spawned", false},
        {"InputBegan", "UserInputService.InputBegan:Connect(function(input, gameProcessed) ... end)",
         "A key, mouse button or finger went down. input.KeyCode, input.UserInputType", false},
        {"InputEnded", "UserInputService.InputEnded:Connect(function(input, gameProcessed) ... end)", "A key, button or finger let go", false},
        {"MouseButton1Click", "button.MouseButton1Click:Connect(function() ... end)",
         "A TextButton / ImageButton was clicked (an event on the button, not something to bind)", false},
        {"Activated", "tool.Activated:Connect(function() ... end)", "The tool was used (or a button pressed)", false},
        {"OnServerEvent", "remote.OnServerEvent:Connect(function(player, ...) ... end)", "A LocalScript did FireServer", false},
        {"OnClientEvent", "remote.OnClientEvent:Connect(function(...) ... end)", "The server did FireClient / FireAllClients", false},
        {"Triggered", "prompt.Triggered:Connect(function(player) ... end)", "Someone used the ProximityPrompt", false},
        {"MouseClick", "clickDetector.MouseClick:Connect(function(player) ... end)", "Someone clicked the ClickDetector's part", false},
        {"Changed", "obj.Changed:Connect(function(propertyName) ... end)", "One of its properties changed", false},
    };
    return list;
}

const Entry* find(const std::string& name) {
    const std::string want = lower(name);
    // The whole thing first ("task.wait"), then just the last part ("FindFirstChild").
    for (const Entry& e : all()) if (lower(e.name) == want) return &e;
    const size_t cut = name.find_last_of(".:");
    if (cut == std::string::npos) return nullptr;
    const std::string last = lower(name.substr(cut + 1));
    for (const Entry& e : all()) if (!e.global && lower(e.name) == last) return &e;
    return nullptr;
}

std::string rightCase(const std::string& name) {
    const Entry* e = find(name);
    if (!e || !e->global || name == e->name || lower(name) != lower(e->name)) return "";
    return e->name;
}

} // namespace ScriptHelp
