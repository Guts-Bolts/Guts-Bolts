// Studio's AI tools (see AiTools.h): what each one is for, written for an AI that
// has never seen Guts and Bolts before, and the guide it reads first. The tools
// themselves run in AiToolsRun.cpp.
#include "AiTools.h"

#include "Version.h"

#include <map>
#include <string>
#include <vector>

using nlohmann::json;

namespace {

json schema(json props, std::vector<std::string> required) {
    return {{"type", "object"}, {"properties", std::move(props)}, {"required", required}, {"additionalProperties", false}};
}
const json kObject = {{"type", "string"},
                      {"description", "Which object. Best: its object_id like \"#42\" (stable, never ambiguous; every tool "
                                      "returns them). Also works: a path that starts with a service, like \"Workspace.Castle.Door\", "
                                      "\"StarterPack.Sword\", \"ServerScriptService.Main\" or \"game.ReplicatedStorage.Events\"."}};
// Where something goes: an object, or a service.
const json kParent = {{"type", "string"},
                      {"description", "Where it goes: an object (\"#42\" or a path) or a service: Workspace, ReplicatedFirst, "
                                      "ReplicatedStorage, ServerScriptService, ServerStorage, StarterGui, StarterPack, StarterPlayer, "
                                      "StarterPlayer.StarterPlayerScripts, StarterPlayer.StarterCharacterScripts, Teams, SoundService, "
                                      "Chat, TextChatService. A service's folder is made the first time something goes in it."}};
// Which play session's Output lines (get_output / get_errors).
const json kRun = {{"type", {"string", "integer"}},
                   {"description", "Which play session: \"all\" (default), \"latest\" (the newest playtest, running or "
                                   "finished: use this to ignore old errors), \"current\" (the running one), \"editing\" "
                                   "(lines from outside playtests) or a run number. Every line says its run (0 = editing)."}};

// How risky a tool is (spec: none < low < medium < high < critical).
enum class Risk { None, Low, Medium, High, Critical };
const char* riskName(Risk r) {
    switch (r) {
        case Risk::None: return "none";
        case Risk::Low: return "low";
        case Risk::Medium: return "medium";
        case Risk::High: return "high";
        default: return "critical";
    }
}

// One tool's description, in the same sections for every tool so an AI knows
// where to look: what it does, when (and when not) to pick it, what goes in and
// comes out, what must be true before and after, and what usually comes next.
struct Doc {
    const char* purpose;
    const char* use;        // user intents and phrases that mean this tool
    const char* dont;       // when another tool is the right one
    const char* inputs;
    const char* output;
    const char* before;     // preconditions
    const char* after;      // postconditions / side effects
    const char* workflow;   // what usually comes before and after
    const char* examples;   // requests that should pick this tool
};

struct Tool {
    const char* name;
    const char* title;
    const char* domain;     // SCENE, OBJECTS, SCRIPTING, RUNTIME, DEBUG, VIEW, HISTORY
    bool        readOnly;
    bool        destructive;
    bool        idempotent;
    Risk        risk;
    Doc         doc;
    json        input;
};

std::string describe(const Tool& t) {
    std::string s = t.doc.purpose;
    auto add = [&](const char* label, const char* text) {
        if (text && *text) s += std::string("\n\n") + label + ": " + text;
    };
    add("WHEN TO USE", t.doc.use);
    add("DO NOT USE", t.doc.dont);
    add("INPUTS", t.doc.inputs);
    add("OUTPUT", t.doc.output);
    add("PRECONDITIONS", t.doc.before);
    add("POSTCONDITIONS", t.doc.after);
    add("COMMON WORKFLOW", t.doc.workflow);
    add("EXAMPLES", t.doc.examples);
    s += std::string("\n\nDOMAIN: ") + t.domain + ". " + (t.readOnly ? "READ-ONLY (changes nothing)" : "CHANGES THE GAME") +
         (t.destructive ? ", DESTRUCTIVE" : "") + ". RISK: " + riskName(t.risk) + ".";
    return s;
}

const std::vector<Tool>& tools() {
    static const std::vector<Tool> all = {
        // ----------------------------------------------------------------- DOCS
        {"get_engine_info", "Learn how Guts and Bolts works", "DOCS", true, false, true, Risk::None,
         {"Reference about the engine itself: what Guts and Bolts is and what these tools can do (capabilities, a "
          "machine-readable manifest of what is and isn't supported), the concept dictionary (objects, classes, "
          "parts, scripts, physics, navigation, editor vs runtime), the coordinate system (axes, units, rotations), "
          "common multi-step workflows, and every class insert_object can make. The same text is in the MCP "
          "resources gutsbolts://capabilities, gutsbolts://concepts, gutsbolts://coordinates, gutsbolts://workflows "
          "and gutsbolts://classes.",
          "At the start of a session if you don't know the engine; before positioning or rotating things "
          "(coordinates); before promising a feature (capabilities); when unsure which class to insert (classes) "
          "or which tools to chain (workflows).",
          "To see the person's actual game use get_game_tree / get_object: this tool describes the engine, not the game.",
          "topic (required: overview, capabilities, concepts, coordinates, workflows, examples, classes or all).",
          "{topic, text} (capabilities also has `manifest`, classes also has `classes`).",
          "None.", "Nothing changes.",
          "get_engine_info -> get_game_tree -> plan -> act.",
          "\"Can Guts and Bolts do ragdolls?\" \"Which way is up?\" \"What can I insert?\""},
         schema({{"topic", {{"type", "string"}, {"enum", {"overview", "capabilities", "concepts", "coordinates", "workflows", "examples", "classes", "all"}}}}}, {"topic"})},

        // ---------------------------------------------------------------- SCENE
        {"get_game_tree", "Show the game's object tree", "SCENE", true, false, true, Risk::None,
         {"Lists the objects in the game as an indented tree: name, class and object_id (#number) for each. With no "
          "root it shows EVERY service like Studio's Explorer: Workspace, Players, Lighting, ReplicatedFirst, "
          "ReplicatedStorage, ServerScriptService, ServerStorage, StarterGui, StarterPack, StarterPlayer (with "
          "StarterPlayerScripts and StarterCharacterScripts), Teams, SoundService, Chat, TextChatService (empty ones say so). "
          "This is the real, current content of the game; never guess what is in it.",
          "Start of almost every task. \"What's in the game/scene?\", \"show me the map\", \"what objects are there\", "
          "\"find the door\" (when you don't know its name yet), before creating something to see where it should go.",
          "To read one object's properties use get_object. To search by name or class across a big game use find_objects.",
          "root (optional object or service, e.g. \"StarterPack\"; default the whole game); depth (optional, levels to show, default 4).",
          "{tree: indented text, object_count, truncated, services: [{name, class, object_id, child_count, can_insert}]}. "
          "Deeper levels are summarised as \"... N more inside\".",
          "None.", "Nothing changes.",
          "get_game_tree -> get_object / find_objects -> (change something) -> get_object to verify.",
          "\"What is in my game?\" \"Is there a spawn point?\" \"What tools does everyone start with?\" (root StarterPack)"},
         schema({{"root", kObject}, {"depth", {{"type", "integer"}, {"minimum", 0}, {"description", "How many levels deep (default 4)"}}}}, {})},
        {"find_objects", "Search for objects", "SCENE", true, false, true, Risk::None,
         {"Searches the whole game (or one part of it) for objects by name, class and/or tag, and returns their object_ids. "
          "Use the returned object_id in later calls: names can repeat, ids never do.",
          "\"Find all the barrels\", \"where are the scripts\", \"which parts are red/unanchored\" (find, then get_object), "
          "\"locate the enemy model\", any time you need the id of something you know by name.",
          "To see the overall layout use get_game_tree. To read one object's details use get_object.",
          "name (optional, case-insensitive text the name contains); class (optional, exact class like Part, Model, Script, "
          "PointLight, TextLabel); tag (optional, a CollectionService tag); under (optional object to search inside); "
          "limit (optional, default 50, max 500). Give at least one of name / class / tag.",
          "{matches: [{object_id, name, class, path}], count, truncated}. Searches every service, not just the Workspace.",
          "None.", "Nothing changes.",
          "find_objects -> get_object / diagnose_object -> set_property ...",
          "\"Find every part called Barrel.\" \"List all LocalScripts.\" \"Which objects are tagged Enemy?\""},
         schema({{"name", {{"type", "string"}}}, {"class", {{"type", "string"}}}, {"tag", {{"type", "string"}}}, {"under", kObject},
                 {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}}}, {})},
        {"insert_object", "Create an object", "SCENE", false, false, false, Risk::Low,
         {"Creates a NEW object in the game and returns its object_id. kind is a class or a ready-made thing: Part, Sphere, "
          "Cylinder, Model, Folder, Script, LocalScript, ModuleScript, PointLight, SpotLight, Sound, SoundGroup, "
          "ReverbSoundEffect/EchoSoundEffect/EqualizerSoundEffect/... (inside a Sound or SoundGroup), SpawnLocation, "
          "TrussPart, Seat, Water, WaterSource, Attachment, ProximityPrompt, ClickDetector, Highlight, Trail, Beam, ParticleEmitter, Fire, Smoke, "
          "Sparkles, Tool, Decal, Rig, "
          "Sky, ColorCorrectionEffect, SunRaysEffect, DepthOfFieldEffect, BloomEffect, BlurEffect (these always go in Lighting), "
          "RemoteEvent, RemoteFunction, IntValue/NumberValue/StringValue/BoolValue, ScreenGui, Frame, TextLabel, TextButton, "
          "TextBox, ImageLabel, ScrollingFrame, BillboardGui, SurfaceGui, UICorner, UIStroke, UIListLayout, UIGradient, UIScale, "
          "UIAspectRatioConstraint, ViewportFrame, Camera, WorldModel, BodyVelocity, "
          "AlignPosition, VectorForce..., and ready-made things by name: \"Kill Brick\", \"Coin\", \"Jump Pad\", \"Exploding Barrel\", "
          "\"Zombie\", \"Moving Platform\", \"Lamp Post\"... (the same list as Studio's Insert Object window). It only creates the object: "
          "it does not position it, colour it or give it behaviour.",
          "\"Create/add/spawn/place/insert/make a part (cube, ball, light, sound, button, script holder...)\".",
          "To change something that already exists, find it (get_game_tree / find_objects) and use set_property. "
          "For a script with code in it, create_script does both steps at once.",
          "kind (required, see above; Team goes in Teams by itself); parent (optional object or service, default "
          "Workspace: e.g. a Tool in \"StarterPack\", a Script in \"ServerScriptService\", a ScreenGui in \"StarterGui\", "
          "a LocalScript in \"StarterPlayer.StarterPlayerScripts\", templates in \"ReplicatedStorage\" / \"ServerStorage\", "
          "a Sound in \"SoundService\"); name (optional).",
          "{object_id, name, class, path, parent_id, resulting_state: the new object's key properties (Lua names)}. An "
          "unknown kind fails with UNKNOWN_CLASS and error.valid_choices (every kind) / did_you_mean.",
          "parent, if given, must exist (services are made on demand). Lighting and Players can't hold objects. Scripts "
          "and character parts can't hold new objects, except that scripts can go in StarterPlayer.StarterCharacterScripts "
          "(the character).",
          "The object exists, is selected in Studio, and has the class defaults (parts: anchored, 2x1x1 near the camera).",
          "insert_object -> set_property (Position, Size, Color, Anchored...) -> get_object to verify.",
          "\"Add a red ball.\" \"Put a light in the lamp.\" \"Make a button on the screen.\""},
         schema({{"kind", {{"type", "string"}}}, {"parent", kParent}, {"name", {{"type", "string"}}}}, {"kind"})},
        {"delete_object", "Delete an object", "SCENE", false, true, false, Risk::High,
         {"Deletes one object AND everything inside it. Can be undone with undo, but only while Studio stays open.",
          "Only when the person clearly asked to delete/remove/destroy/get rid of that specific thing.",
          "Never to \"clean up\" things that look unused or badly named, never to replace something (change it with "
          "set_property instead), never on a guess. If the request is ambiguous (\"remove the trees\" when there are "
          "many), ask first or list what you would delete.",
          "object (required).",
          "{deleted: {object_id, name, class}, affected_ids: it and everything inside it}.",
          "The object exists (inspect it first with get_object to be sure it's the right one). Workspace itself can't be deleted.",
          "The object and its descendants are gone. Scripts referring to it by name will no longer find it.",
          "find_objects / get_object (confirm) -> delete_object -> get_game_tree to verify.",
          "\"Delete the old wall.\" \"Remove Part #57.\""},
         schema({{"object", kObject}}, {"object"})},

        // -------------------------------------------------------------- OBJECTS
        {"get_object", "Read an object's properties", "OBJECTS", true, false, true, Risk::None,
         {"Returns everything about one object: object_id, class, path, parent, children, and its properties under the "
          "SAME names scripts use (Position, Size, Color, Material, Anchored, CanCollide, Transparency, BackgroundColor3, "
          "Text...), read from the engine; plus attributes and tags. set_property takes these names back. While "
          "playtesting it also has runtime values (velocity, spin) under \"runtime\". object \"Lighting\" gives the "
          "Lighting service's settings (ClockTime, Brightness, FogEnabled, Ambient...).",
          "Before changing an unfamiliar object, to answer \"what colour/size/where is X\", to check why something "
          "doesn't work, and AFTER a change to verify it took effect.",
          "To list many objects use get_game_tree / find_objects. For a ready-made list of likely problems use diagnose_object.",
          "object (required).",
          "{object_id, name, class, path, parent_id, children: [{object_id, name, class}], properties: {LuaName: value}, "
          "attributes?, tags?, saved_fields (how the file stores it, internal names), runtime?}. Values: numbers, booleans, "
          "text, [x, y, z] (Vector3 / Color3 0-1), [[xScale, xOffset], [yScale, yOffset]] (UDim2), \"#id\" (an object).",
          "The object exists.", "Nothing changes.",
          "find_objects -> get_object -> set_property -> get_object.",
          "\"How big is the platform?\" \"Is the door anchored?\" \"What's inside the Tool?\""},
         schema({{"object", kObject}}, {"object"})},
        {"set_property", "Change one property", "OBJECTS", false, false, true, Risk::Low,
         {"Changes ONE property of an existing object, exactly like a script would (obj.Property = value), then reads it "
          "back so you can see the value the engine really has. Property names are the ones scripts use (the same as "
          "get_object's properties); other spellings of the same name (can_collide, cancollide) are accepted and "
          "corrected. Common properties: Position [x,y,z], Size [x,y,z], Orientation [x,y,z] (degrees), Color [r,g,b] "
          "(0-1 each), Transparency (0-1), Anchored (false = falls with gravity in play), CanCollide, CollisionGroup (a registered group's name), Material (\"Plastic\", "
          "\"SmoothPlastic\", \"Metal\", \"DiamondPlate\", \"Foil\", \"Neon\", \"Wood\", \"Glass\", \"Ice\", \"Concrete\", "
          "\"Brick\", \"Cobblestone\", \"Slate\", \"Granite\", \"Marble\", \"Sand\", \"Grass\", \"Fabric\"; Enum.Material.Neon "
          "works too), Name, Parent (an object or a service like \"StarterPack\"), Enabled, Brightness, Range, Text, "
          "Visible... object \"Lighting\" changes the Lighting service (ClockTime, Brightness, FogEnabled, FogDensity, "
          "FogColor, Ambient, SunColor, GlobalShadows). Setting a script's Source is passed on to edit_script.",
          "\"Move/resize/recolour/rename X\", \"make it red/glow/see-through\", \"make it fall\" (Anchored false), "
          "\"make it solid / walk-through\" (CanCollide), \"put X inside Y\" (Parent).",
          "To create something new use insert_object. To change a script's code use edit_script. To change many objects "
          "at once, use find_and_change.",
          "object (required, or \"Lighting\"); property (required, the name scripts use); value (required: number, "
          "true/false, text, [x, y, z] for Vector3 / Color3, [x, y] Vector2, [[xs, xo], [ys, yo]] or [xs, xo, ys, yo] UDim2, "
          "\"#id\" or a path for object properties like Parent / Adornee / Part0 / Attachment0).",
          "{object_id, changed_properties: {Property: {requested, now}}, resulting_state}. `now` is read back from the "
          "engine (same value format as get_object). A corrected name is noted in warnings.",
          "The object exists and has that property. A wrong name fails with UNKNOWN_PROPERTY, a wrong enum value with "
          "INVALID_ENUM_VALUE; both list error.valid_choices.",
          "The property is changed (undoable). Physics changes (Anchored...) only show their effect while playtesting.",
          "get_object -> set_property -> get_object / screenshot.",
          "\"Make the floor blue.\" \"Move the coin 5 units up.\" \"Let the crate fall.\""},
         schema({{"object", kObject}, {"property", {{"type", "string"}, {"description", "The name scripts use (Position, CanCollide...)"}}},
                 {"value", {{"description", "number, boolean, string, [x, y, z], [x, y], UDim2 [[xs, xo], [ys, yo]], or an object (\"#42\")"}}}},
                {"object", "property", "value"})},
        {"find_and_change", "Find many objects and change them all", "OBJECTS", false, false, false, Risk::Medium,
         {"Studio's Find and Change: finds every object that matches (kind, name, tags, and a rule on a property's value) "
          "and, optionally, changes one property on all of them at once: set it, add to it or multiply it, or add / remove a "
          "tag. Values are read and set exactly like a script would (the same property names). Without apply it only "
          "previews: each match with its value now and what it would become. With apply it changes them all as ONE undo step.",
          "\"Make every light dimmer\", \"all the crates 50% heavier\", \"turn off CanCollide on every part tagged Ghost\", "
          "\"which parts are lighter than 2?\", \"tag every coin\".",
          "One object: set_property. Finding by name only: find_objects.",
          "kind (class name, IsA: \"BasePart\" = any part; empty = any), name_contains, has_tag, not_tag, "
          "where {property, op: = | is not | < | > | <= | >= | contains, value}, change {property, op: \"Set to\" | \"Add\" | "
          "\"Multiply by\" | \"Add tag\" | \"Remove tag\", value}, apply (default false = preview), inside_selection. "
          "Values are text the way the window takes them: 1.5, true, Neon, \"1, 2, 3\" (Vector3), \"255, 0, 0\" (Color3 0-255).",
          "{count, changed (with apply), results: [{object_id, name, now, becomes, error?}] (first 200)}.",
          "Not playtesting when applying.",
          "Preview: nothing changes. Apply: every match is changed (one Ctrl+Z / undo puts all of them back).",
          "find_and_change (preview) -> find_and_change apply -> get_object / screenshot.",
          "\"Make every PointLight half as bright.\" \"Unanchor all the parts in the Tower model.\""},
         schema({{"kind", {{"type", "string"}}}, {"name_contains", {{"type", "string"}}}, {"has_tag", {{"type", "string"}}},
                 {"not_tag", {{"type", "string"}}},
                 {"where", {{"type", "object"}, {"properties", {{"property", {{"type", "string"}}}, {"op", {{"type", "string"}}},
                                                                 {"value", {{"type", "string"}}}}}}},
                 {"change", {{"type", "object"}, {"properties", {{"property", {{"type", "string"}}}, {"op", {{"type", "string"}}},
                                                                  {"value", {{"type", "string"}}}}}}},
                 {"apply", {{"type", "boolean"}}}, {"inside_selection", {{"type", "boolean"}}}}, {})},
        {"select", "Select objects in Studio", "OBJECTS", false, false, true, Risk::None,
         {"Selects objects in Studio so the person can see them highlighted (or clears the selection). Doesn't change the game.",
          "\"Show me which one\", \"select the doors\", pointing the person at what you changed.",
          "Selection is only for the person; other tools never need it.",
          "objects (optional list of objects; empty or missing clears the selection).",
          "{selected: [object_id...], missing: [refs not found]}.", "None.", "The Studio selection changes; the game doesn't.",
          "find_objects -> select.", "\"Highlight the parts you just made.\""},
         schema({{"objects", {{"type", "array"}, {"items", kObject}}}}, {})},

        // ------------------------------------------------------------ SCRIPTING
        {"create_script", "Create a script with code", "SCRIPTING", false, false, false, Risk::Medium,
         {"Creates a Script, LocalScript or ModuleScript containing the Lua source you give. Script = runs on the game's "
          "server when the game starts. LocalScript = runs on each player's own computer (UI, input, camera; talks to "
          "Scripts with RemoteEvents). ModuleScript = only runs when require()d.",
          "When behaviour is needed that the engine's built-in objects and properties can't give (game rules, "
          "scoring, doors that open, enemies that chase), or when the person asks for a script.",
          "Don't write a script for things a property or built-in object already does (falling: Anchored = false; "
          "glowing: Material Neon or a PointLight; a sound: a Sound object). To change an existing script use edit_script.",
          "source (required, Lua 5.4; Luau `+=`, `continue`, type annotations and `{x}` strings work too); parent (optional "
          "object or service, default Workspace; `script.Parent` is this object. Game rules: \"ServerScriptService\"; per-player "
          "LocalScripts: \"StarterPlayer.StarterPlayerScripts\"; in every character: \"StarterPlayer.StarterCharacterScripts\"; "
          "UI code: inside the ScreenGui); name (optional); type (Script, LocalScript or ModuleScript).",
          "{object_id, name, class, parent_id, syntax: {ok, error?}}. A syntax error is reported but the script is still made.",
          "parent exists.", "The script exists. It does NOT run until a playtest starts (or the game is played).",
          "create_script -> diagnose_object (syntax) -> playtest start -> get_errors / get_output -> fix with edit_script -> playtest stop.",
          "\"Make the door open when touched.\" \"Add a score leaderboard.\""},
         schema({{"parent", kParent}, {"name", {{"type", "string"}}}, {"source", {{"type", "string"}}},
                 {"type", {{"type", "string"}, {"enum", {"Script", "LocalScript", "ModuleScript"}}}}}, {"source"})},
        {"read_script", "Read a script's code", "SCRIPTING", true, false, true, Risk::None,
         {"Returns the Lua source of a Script, LocalScript or ModuleScript.",
          "Before editing a script, when asked what a script does, when debugging an error that names a script.",
          "For non-script objects use get_object.",
          "object (required, a script).", "{object_id, class, source, lines}.", "The object is a script.", "Nothing changes.",
          "read_script -> edit_script -> diagnose_object.", "\"What does the KillBrick script do?\""},
         schema({{"object", kObject}}, {"object"})},
        {"edit_script", "Change a script's code", "SCRIPTING", false, false, false, Risk::Medium,
         {"Changes a script's code: replaces the text `old` with `new` (old must appear exactly once), or replaces "
          "the whole script with `source`. Checks the result for Lua syntax errors.",
          "Fixing a script error, changing what a script does, adding a feature to an existing script.",
          "To make a new script use create_script. Don't rewrite a whole script with `source` when a small `old`/`new` edit will do. "
          "This (not set_property Source) is how a script's code is changed.",
          "object (required, a script); old + new (exact text swap) OR source (whole new code).",
          "{object_id, syntax: {ok, error?}}.",
          "The object is a script; `old` is copied exactly from read_script (spaces and line breaks matter).",
          "The script's source is changed (undoable). A running playtest keeps the old code until it is restarted.",
          "read_script -> edit_script -> playtest -> get_errors.", "\"Make the lava do 50 damage instead of 100.\""},
         schema({{"object", kObject}, {"old", {{"type", "string"}}}, {"new", {{"type", "string"}}},
                 {"source", {{"type", "string"}}}}, {"object"})},
        {"run_lua", "Run Lua now (Command Bar)", "SCRIPTING", false, false, false, Risk::Medium,
         {"Runs Lua code in Studio right now, like the Command Bar. Outside a playtest it changes the saved game "
          "(build things); during a playtest it runs inside the running game (runtime only: thrown away on stop). "
          "The full Roblox-style API is there: Instance.new, workspace, game:GetService, Effects, PathfindingService...",
          "Building or changing many things at once (a staircase, a grid of coins, recolour every part), reading values "
          "no other tool gives, testing something during a playtest.",
          "For one property use set_property; for one object use insert_object (they verify and report ids for you). "
          "Don't use it to fake physics or movement by teleporting parts every frame: use Anchored = false, forces and "
          "the engine's own systems.",
          "code (required, Lua 5.4; Luau syntax works too).",
          "{output: what it printed, errors: [...]}. print() whatever you need to know back (e.g. print(part:GetFullName())).",
          "None. Code that waits (wait(), events) only keeps running during a playtest.",
          "Whatever the code did (undoable when not playtesting).",
          "run_lua -> get_game_tree / screenshot to verify.", "\"Build a 10-step staircase.\" \"Make every part in Trees green.\""},
         schema({{"code", {{"type", "string"}}}}, {"code"})},

        // -------------------------------------------------------------- RUNTIME
        {"playtest", "Start or stop a playtest", "RUNTIME", false, false, true, Risk::Medium,
         {"Starts or stops playtesting (Studio's Play / Run / Stop), or pauses it. While playing, scripts run, physics "
          "simulates (unanchored parts fall, explosions push) and a character walks around. Stopping puts the whole "
          "game back exactly how it was before Play.",
          "\"Test it\", \"run the game\", \"try it out\", \"play\", \"does it work?\", to see scripts or physics in action, "
          "and \"stop\" when you have seen enough.",
          "Don't keep a playtest running while you build: changes made during a playtest are lost when it stops.",
          "action (required): start (Play, with your character), simulate (Run: physics and scripts, no character), "
          "stop, pause (freeze physics and scripts), resume, step (move on one frame while paused).",
          "{playing, mode, paused}.",
          "None.", "start: the game is running. stop: the saved game is back exactly as before Play.",
          "playtest start -> get_output / get_errors / screenshot / get_object (runtime) -> playtest stop -> fix -> repeat.",
          "\"Play the game and tell me if the door works.\""},
         schema({{"action", {{"type", "string"}, {"enum", {"start", "simulate", "stop", "pause", "resume", "step"}}}}}, {"action"})},
        {"multiplayer_test", "Test with several players", "RUNTIME", false, false, false, Risk::Medium,
         {"Studio's \"Test with N players\": starts the game (as it is now in Studio, saved or not) on a local test "
          "server on this computer and opens N Player windows that join it as Player1, Player2... Server Scripts run "
          "on the test server, LocalScripts run in each player's window, and RemoteEvents / RemoteFunctions really go "
          "between them. Everything the server and each player print is collected (output) and also shows in Studio's "
          "Output as [Server] / [Player1] lines. Stopping closes the server and every window.",
          "\"Test it with 2 players\", \"does the RemoteEvent reach the clients?\", \"check the LocalScript\", "
          "\"what happens when someone joins / leaves\", anything about multiplayer, PlayerAdded, FireClient / "
          "FireAllClients / FireServer, or leaderstats with more than one person.",
          "For a quick single-player check use playtest (it's faster and lets you inspect objects while it runs). "
          "This tool can't move the test players or read objects inside the test: check things with print() in "
          "your scripts and read them back with action output.",
          "action (required): start, stop, status, output or add_player. players (start: 2-4, default 2). "
          "lines (output: how many, default 60, max 400). from (output: only \"Server\" or \"Player1\"... default everyone).",
          "{running, ready (the server has loaded the game; players join after this), port, server, players: "
          "[{name, running}], output: [{from, level, text, time}] (output and status)}.",
          "None. It uses the game as it is in Studio right now; a running playtest is stopped first.",
          "start: a test server and N windows on this computer (players join a few seconds later; check with status "
          "or output). stop: all of them are closed. Edits made in Studio during the test don't reach it: stop and "
          "start again.",
          "create_script (Script + LocalScript + RemoteEvent) -> multiplayer_test start -> wait a few seconds -> "
          "multiplayer_test output (look for each player's lines and errors) -> multiplayer_test stop -> fix -> repeat.",
          "\"Test my game with 3 players.\" \"Does everyone get the round message?\" \"Stop the multiplayer test.\""},
         schema({{"action", {{"type", "string"}, {"enum", {"start", "stop", "status", "output", "add_player"}}}},
                 {"players", {{"type", "integer"}, {"minimum", 2}, {"maximum", 4}, {"description", "start: how many player windows (default 2)"}}},
                 {"lines", {{"type", "integer"}, {"minimum", 1}, {"maximum", 400}, {"description", "output: how many lines (default 60)"}}},
                 {"from", {{"type", "string"}, {"description", "output: only lines from \"Server\" or one player (\"Player1\"...)"}}}},
                {"action"})},

        // ---------------------------------------------------------------- DEBUG
        {"get_output", "Read the Output window", "DEBUG", true, false, true, Risk::None,
         {"The latest lines of Studio's Output window: print() lines, warnings, script errors and Studio messages, with "
          "times. Each line says which play session (run) it came from, so old lines aren't mistaken for the current test.",
          "After running code or playtesting, to see what scripts printed. run \"latest\" shows only the newest playtest.",
          "To see only problems use get_errors.",
          "lines (optional, default 40, max 400); run (optional: all, latest, current, editing or a number).",
          "{lines: [{time, level, text, run}], latest_run, current_run, playing, run}.", "None.", "Nothing changes.",
          "playtest start -> get_output {run: \"latest\"}.", "\"What did the script print?\""},
         schema({{"lines", {{"type", "integer"}, {"minimum", 1}, {"maximum", 400}, {"description", "How many (default 40)"}}},
                 {"run", kRun}}, {})},
        {"get_errors", "Read errors and warnings", "DEBUG", true, false, true, Risk::None,
         {"Only the errors (and warnings) from the Output window: script errors with their line numbers, failed loads, "
          "warn() calls.",
          "\"Why doesn't it work?\", \"is anything broken?\", after every playtest and every script change.",
          "For everything printed use get_output.",
          "lines (optional, how many recent Output lines to look through, default 200); include_warnings (optional, "
          "default true); run (optional: all, latest, current, editing or a number; \"latest\" = only the newest playtest, "
          "so errors fixed in an earlier run don't show).",
          "{errors: [{time, text, run}], warnings: [{time, text, run}], error_count, warning_count, latest_run, current_run}.",
          "None.", "Nothing changes.",
          "playtest start -> get_errors {run: \"latest\"} -> read_script -> edit_script.", "\"Are there any script errors?\""},
         schema({{"lines", {{"type", "integer"}, {"minimum", 1}, {"maximum", 2000}}}, {"include_warnings", {{"type", "boolean"}}},
                 {"run", kRun}}, {})},
        {"diagnose_object", "Find problems with an object", "DEBUG", true, false, true, Risk::None,
         {"Inspects one object (and what's inside it) and lists likely problems with a suggested fix for each: "
          "anchored parts that are expected to fall, unanchored parts with CanCollide off (fall through the floor), "
          "invisible parts, Lua syntax errors, disabled scripts, constraints/beams/trails missing their attachments, "
          "lights/sounds/prompts/decals not inside a part, tools without a Handle, UI not inside a ScreenGui...",
          "\"Why doesn't X fall/move/show/work?\", \"check this\", and after building something, to validate it.",
          "For the whole game at once use validate_scene. It reports problems; it does not fix them.",
          "object (required); recursive (optional, also check everything inside, default true).",
          "{object_id, findings: [{object_id, severity: error|warning|info, code, message, fix}], error_count, warning_count}.",
          "The object exists.", "Nothing changes.",
          "find_objects -> diagnose_object -> set_property / edit_script -> diagnose_object again.",
          "\"The barrel doesn't fall.\" \"Why can't I see my sign?\""},
         schema({{"object", kObject}, {"recursive", {{"type", "boolean"}}}}, {"object"})},
        {"analyze_scene_performance", "Audit likely scene performance hotspots", "DEBUG", true, false, true, Risk::None,
         {"Performs a read-only static audit of scene complexity: parts, unanchored physics bodies, lights, particle emitters, enabled scripts and source sizes. Reports threshold-based hotspots and the largest scripts. This is an estimate from scene contents, not a live CPU/GPU profiler or a substitute for measuring frame time in Play mode.",
          "\"Why is my game laggy?\", \"audit performance\", \"find performance hotspots\", before publishing or optimizing a large scene.",
          "For measured frame time or hardware GPU/CPU timing, use an external profiler; this tool only counts scene content.",
          "No inputs.",
          "{metrics, findings, largest_scripts, note}.",
          "None.",
          "Nothing changes; scripts are not executed.",
          "analyze_scene_performance -> inspect findings -> simplify scene or scripts -> playtest and measure again.",
          "\"Audit my scene for likely performance issues.\""},
         schema(json::object(), {})},
        {"analyze_script_dependencies", "Map script and ModuleScript dependencies", "DEBUG", true, false, true, Risk::None,
         {"Scans script source without running it and builds a best-effort dependency map for require(...) references to ModuleScripts. Reports resolved edges, unresolved module references and ModuleScripts that are not statically referenced. This is a static heuristic, not proof of runtime usage; dynamic require expressions may not be resolved.",
          "\"Map my script dependencies\", \"which modules require what?\", \"find unused ModuleScripts\", before refactoring or publishing.",
          "For syntax/runtime errors use validate_scene, get_errors or playtest.",
          "No inputs.",
          "{script_count, module_count, dependencies: [{from, to, expression}], unresolved: [{from, expression}], unreferenced_modules: [...], note}.",
          "None.",
          "Nothing changes; scripts are never executed.",
          "analyze_script_dependencies -> inspect findings -> edit_script -> analyze_script_dependencies again.",
          "\"Show me the ModuleScript dependency graph.\""},
         schema(json::object(), {})},
        {"validate_scene", "Check the whole game for problems", "DEBUG", true, false, true, Risk::None,
         {"Runs diagnose_object over the whole game and adds up the results: script syntax errors, broken constraints, "
          "misplaced objects, invisible or falling-through parts, plus recent errors in the Output.",
          "\"Check my game\", \"is everything set up right?\", before and after big changes, before publishing.",
          "For one object use diagnose_object.",
          "include_info (optional, also list harmless info notes, default false); limit (optional, most findings to list, default 100).",
          "{object_count, script_count, findings: [...], error_count, warning_count, recent_output_errors}.",
          "None.", "Nothing changes.", "validate_scene -> fix -> validate_scene.", "\"Is my game ready to publish?\""},
         schema({{"include_info", {{"type", "boolean"}}}, {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}}}, {})},

        // ------------------------------------------------------------- COMPOUND
        {"create_physical_object", "Create a physics object in one step", "PHYSICS", false, false, false, Risk::Low,
         {"Creates a part that physics moves (unanchored by default), places, sizes, colours it and sets its "
          "material and physical properties in ONE call, then checks it with diagnose_object. Saves chaining "
          "insert_object + several set_property calls.",
          "\"Add a crate/ball/barrel that falls\", \"drop a ball from up there\", \"spawn a physics box\", "
          "\"make a bouncy ball\" (elasticity), \"a heavy block\" (density), stacks of things to knock over.",
          "For something that should NOT move (floors, walls, platforms) use anchored: true, or insert_object. To make an "
          "EXISTING part physical use set_property Anchored false instead.",
          "shape (Part = box, Sphere, Cylinder; default Part); position [x,y,z] (its centre; default near the camera); "
          "size [x,y,z] (default 2x2x2); color [r,g,b] 0-1; material (Plastic, SmoothPlastic, Metal, DiamondPlate, Foil, Neon, Wood, Glass, Ice, "
          "Concrete, Brick, Cobblestone, Slate, Granite, Marble, Sand, Grass, Fabric); "
          "name; parent (object); anchored (default false); can_collide (default true); density (mass per volume, "
          "default from material); friction (0-2); elasticity (bounciness 0-1). The script names work too (Position, Size, "
          "Color, Material, CanCollide, Anchored...).",
          "{object_id, resulting_state, findings (from diagnose_object), warnings}.",
          "parent, if given, exists. Positions in units (Y up).",
          "The part exists with those settings (one undo step each). It only falls / bounces during a playtest.",
          "get_game_tree -> create_physical_object -> playtest start -> get_object (runtime) -> playtest stop.",
          "\"Put a red bouncy ball 10 units above the spawn.\" \"Stack 3 wooden crates.\""},
         schema({{"shape", {{"type", "string"}, {"enum", {"Part", "Sphere", "Cylinder"}}}},
                 {"position", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                 {"size", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                 {"color", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                 {"material", {{"type", "string"}}}, {"name", {{"type", "string"}}}, {"parent", kParent},
                 {"anchored", {{"type", "boolean"}}}, {"can_collide", {{"type", "boolean"}}},
                 {"density", {{"type", "number"}}}, {"friction", {{"type", "number"}}}, {"elasticity", {{"type", "number"}}}}, {})},
        {"spawn_explosion", "Set off an explosion now", "PHYSICS", false, false, false, Risk::Medium,
         {"Sets off the engine's own explosion at a point in the RUNNING game: a shockwave that pushes unanchored parts, "
          "hurts characters and breaks joints, with fire and smoke (optional craters). Returns every part it reached.",
          "\"Blow it up\", \"test the explosion\", \"does the barrel get pushed?\", \"make an explosion here\" (to try it now).",
          "To make explosions part of the game (a bomb, a landmine), write a script using Instance.new(\"Explosion\") or "
          "insert a ready-made \"Exploding Barrel\" / \"Landmine\". Never fake an explosion by moving parts by hand.",
          "position [x,y,z] (required); radius (default 8 units); power (1 = normal, 2 = twice as hard); destroy (rip "
          "anchored parts loose near the middle: craters, default false); fire (seconds of fire left burning, default 0); "
          "visible (default true; false = just the push); hurts (hurts characters, default true).",
          "{hits: [{object_id, name, distance}], hit_count}.",
          "A playtest is running (playtest start). Only UNANCHORED parts get pushed.",
          "The blast has happened in the running game (it's thrown away when the playtest stops).",
          "set_property Anchored false on targets -> playtest start -> spawn_explosion -> get_object (velocity) / screenshot -> playtest stop.",
          "\"Set off an explosion next to the crates and tell me what moved.\""},
         schema({{"position", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                 {"radius", {{"type", "number"}, {"minimum", 0.5}, {"maximum", 500}}}, {"power", {{"type", "number"}, {"minimum", 0}, {"maximum", 100}}},
                 {"destroy", {{"type", "boolean"}}}, {"fire", {{"type", "number"}}}, {"visible", {{"type", "boolean"}}},
                 {"hurts", {{"type", "boolean"}}}}, {"position"})},

        // ----------------------------------------------------------- NAVIGATION
        {"bake_navmesh", "Bake the navigation mesh", "NAVIGATION", false, false, true, Risk::Medium,
         {"Rebuilds the navigation mesh (where characters can walk: every floor of anchored, solid parts, plus jump "
          "and drop links) from the game as it is now, and reports how big it is. Optionally shows it in Studio's 3D view.",
          "\"Bake the navmesh\", \"bake/rebuild/regenerate navigation\", \"update the pathfinding mesh\", after moving "
          "floors or walls, before testing paths.",
          "To ask for ONE route between two points use find_path (it bakes by itself when needed). Games rebake "
          "automatically while running, so scripts don't need this.",
          "show (optional, draw it in Studio's 3D view: green floors, jump links; default false).",
          "{spans: walkable floor cells, bake_ms, baked: true, settings: {cell, max_climb, max_slope, jump_height, jump_gap, max_drop}}.",
          "None (works in the editor and during playtests).",
          "The navmesh is fresh. The saved game doesn't change.",
          "build floors -> bake_navmesh -> find_path -> (script) humanoid:PathfindTo / walk_character_to.",
          "\"Rebuild navigation so enemies can walk around the new walls.\""},
         schema({{"show", {{"type", "boolean"}}}}, {})},
        {"find_path", "Find a walking route", "NAVIGATION", true, false, true, Risk::None,
         {"Asks the navigation mesh for a walking route between two points (or objects) and returns its status and "
          "waypoints, with which ones need a jump. Tests whether characters CAN get somewhere.",
          "\"Can enemies reach the tower?\", \"is there a path from A to B\", \"test pathfinding\", checking a level is "
          "walkable after building it.",
          "To actually make a character walk, use walk_character_to (playtest) or a script with humanoid:PathfindTo. "
          "To rebuild the whole mesh use bake_navmesh.",
          "from, to (required: [x,y,z] feet positions or objects); agent_radius (default 0.6); agent_height (default 2.7); "
          "can_jump (default true).",
          "{status: Success | ClosestNoPath | NoPath | FailStartNotEmpty | FailFinishNotEmpty, waypoints: [{position, "
          "action: Walk|Jump}], length}.",
          "The points are on or near floors made of anchored, solid parts.", "Nothing changes.",
          "bake_navmesh -> find_path -> fix the level if NoPath -> find_path again.",
          "\"Check the zombies can reach the spawn.\""},
         schema({{"from", {{"description", "[x, y, z] or an object"}}}, {"to", {{"description", "[x, y, z] or an object"}}},
                 {"agent_radius", {{"type", "number"}}}, {"agent_height", {{"type", "number"}}}, {"can_jump", {{"type", "boolean"}}}},
                {"from", "to"})},
        {"walk_character_to", "Make a character walk somewhere", "NAVIGATION", false, false, false, Risk::Low,
         {"During a playtest, makes a Humanoid character (an NPC like a Zombie or Rig) walk to a point or object by itself "
          "using the engine's pathfinding (jumping, going around walls, re-planning when blocked), like humanoid:PathfindTo.",
          "\"Make the zombie walk to the door\", \"test that the guard can reach X\", \"move the NPC over there\".",
          "For permanent behaviour (enemies that always chase) write a Script that calls humanoid:PathfindTo. Don't "
          "teleport characters or write your own path-following code.",
          "character (required, the character Model); to (required, [x,y,z] or an object).",
          "{started: true/false, status: Walking | Arrived | ...}. Check later with get_object on the character (position).",
          "A playtest is running and the character is a Model with a Humanoid (HumanoidRootPart, Torso, Head).",
          "The character is walking (runtime only).",
          "playtest start -> walk_character_to -> get_runtime_state / get_object -> playtest stop.",
          "\"Send the zombie to the tower and see if it gets there.\""},
         schema({{"character", kObject}, {"to", {{"description", "[x, y, z] or an object"}}}}, {"character", "to"})},
        {"get_runtime_state", "See the running game's state", "RUNTIME", true, false, true, Risk::None,
         {"A summary of what's happening right now: whether a playtest is running (and paused), frame rate and frame "
          "time, the player's character (position, health, speed), parts that are moving, characters (NPCs) and where "
          "they are walking, the navmesh, and recent errors.",
          "\"Is the game running?\", \"why is it laggy?\" (fps), \"what's moving?\", \"did the zombie get there?\", "
          "checking the result of a playtest action.",
          "For one object's details use get_object (it also shows velocity while playing).",
          "moving_limit (optional, most moving parts to list, default 20).",
          "{playing, mode, paused, fps, frame_ms, player?, moving_parts, moving_count, characters, navmesh, recent_errors}.",
          "None.", "Nothing changes.",
          "playtest start -> (act) -> get_runtime_state -> playtest stop.", "\"What's happening in the game right now?\""},
         schema({{"moving_limit", {{"type", "integer"}, {"minimum", 0}, {"maximum", 200}}}}, {})},

        // -------------------------------------------------------------- HISTORY
        {"checkpoint", "Save or restore a save point", "HISTORY", false, true, false, Risk::Medium,
         {"Named save points of the whole game, for trying things safely (like a transaction): save one, experiment, "
          "then keep the result or restore the save point. Restoring is itself undoable.",
          "Before a risky or experimental change (\"try making the explosion bigger\"), before a big run_lua build, "
          "\"put it back how it was before\".",
          "For just the last change use undo. Checkpoints only last while Studio is open; they don't save the file.",
          "action (required: save, restore, list or delete); name (save / restore / delete; default \"default\").",
          "save: {name, objects}. restore: {name, restored: true}. list: {checkpoints: [names]}.",
          "restore: not playtesting, and the checkpoint exists.",
          "restore replaces the whole game with the save point (one undo step brings the current version back).",
          "checkpoint save -> change -> playtest -> (bad) checkpoint restore / (good) checkpoint delete.",
          "\"Try a few layouts for the arena and keep the best one.\""},
         schema({{"action", {{"type", "string"}, {"enum", {"save", "restore", "list", "delete"}}}}, {"name", {{"type", "string"}}}}, {"action"})},
        {"redo", "Redo what was undone", "HISTORY", false, false, false, Risk::Low,
         {"Redoes the last change that undo took back (like Ctrl+Y).",
          "\"Actually, put it back\" right after an undo.", "Anything else.",
          "None.", "{redone: true, redo_steps_left}.", "Not playtesting; something was undone.",
          "The game is one step forward again.", "undo -> redo.", "\"Redo that.\""},
         schema(json::object(), {})},

        // ----------------------------------------------------------------- VIEW
        {"screenshot", "Take a picture of the 3D view", "VIEW", true, false, true, Risk::None,
         {"A picture of what Studio's 3D view shows right now (the editor camera, or the game camera while playtesting).",
          "To see what something looks like, to check a build visually, while playtesting to watch what happens.",
          "Don't use it to find objects or read properties: get_game_tree / get_object give exact data.",
          "None.", "An image plus {width, height}.", "None.", "Nothing changes.",
          "(change) -> screenshot.", "\"Show me what it looks like.\""},
         schema(json::object(), {})},

        // -------------------------------------------------------------- PUBLISH
        {"publish_game", "Publish the game to Guts&Bolts", "PUBLISH", false, true, false, Risk::High,
         {"Publishes the open game to Guts&Bolts exactly like Studio's File > Publish to Guts&Bolts: as the account "
          "Studio is signed in with, through the same server request, with the server's own checks and limits. With "
          "mode auto it updates the game's published copy if it has one, else publishes it as a new game. It refuses "
          "cleanly (NOT_SIGNED_IN) when Studio isn't signed in, and never signs in by itself. action status reports how "
          "the last publish went (it finishes in the background).",
          "Only when the person explicitly asks to publish / release / put the game online / update the published game.",
          "Never on your own initiative, to \"save\" work, or to test something: publishing makes the game public. "
          "Saving the file is the person's job (File > Save).",
          "action (publish (default) or status); confirm (required true for publish: the person asked for it); name "
          "(optional, default the game's title); description (optional, default the current one); mode (auto, new or update; "
          "default auto).",
          "publish: {state: \"publishing\", name, update}. status: {state: none | publishing | published | failed, "
          "published_id, error?}.",
          "Signed in (not a guest), no playtest running, no other upload going. Run validate_scene first.",
          "The server has the game (on success); the website lists it. The game remembers its published id.",
          "validate_scene -> fix -> publish_game {confirm: true} -> publish_game {action: \"status\"}.",
          "\"Publish my game.\" \"Update the published version.\""},
         schema({{"action", {{"type", "string"}, {"enum", {"publish", "status"}}}}, {"confirm", {{"type", "boolean"}}},
                 {"name", {{"type", "string"}}}, {"description", {{"type", "string"}}},
                 {"mode", {{"type", "string"}, {"enum", {"auto", "new", "update"}}}}}, {})},

        // -------------------------------------------------------------- HISTORY
        {"undo", "Undo the last change", "HISTORY", false, false, false, Risk::Low,
         {"Undoes the last change to the game (like Ctrl+Z): a tool call, a script edit, a run_lua build...",
          "When a change you made was wrong or the person says \"undo that\" / \"put it back\".",
          "Doesn't work during a playtest (stop it first; stopping already throws away playtest changes).",
          "steps (optional, how many changes to undo, default 1).", "{undone: number of steps, undo_steps_left}.",
          "Not playtesting, and there is something to undo.",
          "The game is back to how it was one step earlier.", "set_property -> (wrong) -> undo -> try again.",
          "\"Undo that.\""},
         schema({{"steps", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}}, {})},
    };
    return all;
}

// Which tools usually come right before and after each one (MCP _meta "gutsbolts/workflow").
json workflowOf(const std::string& name) {
    static const std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> flows = {
        {"get_engine_info", {{}, {"get_game_tree"}}},
        {"get_game_tree", {{"get_engine_info"}, {"find_objects", "get_object"}}},
        {"find_objects", {{"get_game_tree"}, {"get_object", "diagnose_object", "set_property"}}},
        {"insert_object", {{"get_game_tree"}, {"set_property", "get_object"}}},
        {"delete_object", {{"find_objects", "get_object"}, {"get_game_tree"}}},
        {"get_object", {{"find_objects", "get_game_tree"}, {"set_property", "diagnose_object"}}},
        {"set_property", {{"get_object"}, {"get_object", "screenshot", "playtest"}}},
        {"select", {{"find_objects"}, {}}},
        {"find_and_change", {{"find_objects"}, {"find_and_change", "get_object"}}},
        {"create_script", {{"get_game_tree"}, {"diagnose_object", "playtest"}}},
        {"read_script", {{"find_objects"}, {"edit_script"}}},
        {"edit_script", {{"read_script"}, {"diagnose_object", "playtest"}}},
        {"run_lua", {{"get_game_tree"}, {"get_game_tree", "screenshot"}}},
        {"playtest", {{"validate_scene"}, {"get_errors", "get_output", "get_object", "screenshot"}}},
        {"multiplayer_test", {{"create_script", "playtest"}, {"multiplayer_test", "get_errors", "edit_script"}}},
        {"get_output", {{"playtest", "run_lua"}, {}}},
        {"get_errors", {{"playtest"}, {"read_script", "edit_script"}}},
        {"diagnose_object", {{"find_objects"}, {"set_property", "edit_script"}}},
        {"validate_scene", {{}, {"diagnose_object", "playtest"}}},
        {"screenshot", {{"set_property", "insert_object", "playtest"}, {}}},
        {"undo", {{}, {"get_object"}}},
        {"redo", {{"undo"}, {}}},
        {"create_physical_object", {{"get_game_tree"}, {"playtest", "get_object"}}},
        {"spawn_explosion", {{"playtest"}, {"get_object", "get_runtime_state", "screenshot"}}},
        {"bake_navmesh", {{"get_game_tree"}, {"find_path"}}},
        {"find_path", {{"bake_navmesh"}, {"walk_character_to"}}},
        {"walk_character_to", {{"playtest", "find_path"}, {"get_runtime_state", "get_object"}}},
        {"get_runtime_state", {{"playtest"}, {"get_object", "playtest"}}},
        {"checkpoint", {{}, {"playtest", "checkpoint"}}},
        {"publish_game", {{"validate_scene"}, {"publish_game"}}},
    };
    auto it = flows.find(name);
    if (it == flows.end()) return {{"before", json::array()}, {"after", json::array()}};
    return {{"before", it->second.first}, {"after", it->second.second}};
}

} // namespace

namespace AiTools {

const char* guide() {
    return R"GUIDE(# Guts and Bolts Studio assistant

You are an AI assistant working INSIDE Guts and Bolts Studio, a game engine (Roblox-like: parts, models, Lua scripts, physics, characters) through its tools. Guts and Bolts is a game engine, not a file editor: the tools act directly on the real game that is open in Studio. Treat every tool as a real engine operation, never as an example.

Your loop: UNDERSTAND the request -> INSPECT the game -> PLAN -> ACT with tools -> VALIDATE the result -> REPORT what really happened.

## Core behaviour
- When asked to change the game, do it with the tools. Don't just explain how the person could do it themselves when a tool can.
- Look before you change: get_game_tree / find_objects / get_object show what really exists. Never invent object names, ids, classes, properties, scene contents or results.
- Refer to objects by object_id ("#42"). Names can repeat; ids never do. Every tool returns ids.
- Prefer small, checkable steps. After an important change, verify it (get_object, diagnose_object, screenshot).
- Never say something worked unless the tool returned "success": true. Tool results are JSON; read them.

## Picking tools (by what the person wants, not keywords)
- "What's in the game?" -> get_game_tree (never guess).
- "Make a red ball" -> insert_object Sphere, then set_property Color / Position.
- "Make it fall" / "add physics" / "make it react to forces" -> set_property Anchored = false (parts are anchored by default), check CanCollide is true, then playtest to see it.
- "Why doesn't it move/work?" -> diagnose_object first, then fix, then check again.
- "Add a crate that falls" -> create_physical_object (one call: shape, place, size, colour, physics).
- "Make enemies walk to the player" -> the engine's PathfindingService / humanoid:PathfindTo in a Script, not hand-made movement code. To test now: playtest start, walk_character_to.
- "Bake/rebuild the navmesh" -> bake_navmesh. "Can they get there?" -> find_path.
- "Make an explosion" -> for the game: an Explosion in a script; to try it now: playtest start, spawn_explosion. Never move parts by hand.
- "Is it running? Is it laggy? What's moving?" -> get_runtime_state.
- "Try something risky" -> checkpoint save first; checkpoint restore if it goes wrong.
- Many similar changes at once -> run_lua with a loop.
- "Test it" -> playtest start, then get_errors / get_output {run: "latest"} / screenshot, then playtest stop.
- "Test it with 2 players" / RemoteEvents / LocalScripts / joining and leaving -> multiplayer_test start, wait a few seconds, multiplayer_test output, then multiplayer_test stop.
- "Give everyone a sword" -> insert_object Tool with parent "StarterPack". Game rules -> create_script in "ServerScriptService". UI -> ScreenGui in "StarterGui". Per-player LocalScripts -> "StarterPlayer.StarterPlayerScripts". In every character -> "StarterPlayer.StarterCharacterScripts".
- "Publish my game" (only when asked) -> validate_scene, then publish_game {confirm: true}, then publish_game {action: "status"}.

## Services
The game is the Workspace plus services, like Roblox: Players, Lighting, ReplicatedFirst, ReplicatedStorage, ServerScriptService, ServerStorage, StarterGui, StarterPack, StarterPlayer (StarterPlayerScripts, StarterCharacterScripts), Teams, SoundService, Chat, TextChatService. get_game_tree shows them all. Use a service's name as a parent or at the start of a path ("StarterPack.Sword"); its folder is made the first time something goes in. Lighting holds settings (get_object / set_property "Lighting"); Players fills itself while the game runs.

## Property names
Tools use the same property names as scripts (CanCollide, BackgroundColor3...). get_object lists them with their values; set_property takes them back. A bad class, property, enum value or service fails with error.valid_choices: pick from that list, don't guess again. A script's code is changed with edit_script, not set_property Source.

## Output and play sessions
Every Output line says which playtest (run) it came from; 0 means outside a playtest. After testing, read get_errors / get_output with run "latest" so errors from an earlier run aren't mistaken for new ones.

## Use the engine's own systems
- Physics: Anchored = false, forces (part:ApplyImpulse, BodyVelocity, VectorForce...), constraints. DON'T fake physics by teleporting parts every frame.
- Navigation: PathfindingService and humanoid:PathfindTo. DON'T write your own path-walking code when it can do the job.
- Explosions: the Explosion object / Explode(). DON'T push every nearby part by hand.
- Emotes: humanoid:PlayEmote("wave") plays a built-in keyframed emote everyone sees (dance, dance2, dance3, wave, point, cheer, laugh; moving or jumping stops it). Players also have the B emote menu; a game turns it off with game:GetService("StarterGui"):SetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu, false). DON'T animate a character by moving its arms in a loop.
- Player input ("press E to ...", a sprint key, a mobile button): ContextActionService:BindAction(name, fn, createTouchButton, Enum.KeyCode.E, ...) in a LocalScript; fn(name, state, input) returns Enum.ContextActionResult.Sink (keep it: the game's own controls ignore it) or Pass. createTouchButton = true adds a phone button (SetTitle / SetImage).
- Only write scripts for behaviour the built-in objects and properties can't give, or when the person asks for code.

## Editor vs playtest
- Outside a playtest you edit the saved game. Physics doesn't run; scripts don't run.
- During a playtest (playtest start) scripts run and physics simulates. Everything that changes during a playtest is THROWN AWAY when it stops. Build outside playtests.

## Errors
When a tool fails, read error.code / message / suggested_action, fix the cause (wrong id, missing object, wrong order) and try a changed call. Don't repeat the same failing call unchanged. If it can't be done, say exactly what failed.

## Safety
- Each tool says its RISK. delete_object is HIGH: only delete what the person clearly asked to delete. Never delete things because they look unused or oddly named.
- If a destructive request is ambiguous ("remove the trees" when there are 40 trees and some are in a model), ask or list what you'd remove first.
- Everything can be undone with undo (outside playtests). Publishing can't: publish_game only when the person asks, with confirm: true.

## Don't invent capabilities
A request mentioning something doesn't mean Guts and Bolts has it. Only use the tools you were given and the engine API that exists. Don't make up tools, classes, properties, paths or features. If no tool can do something, say the current tools can't do it.

## Examples
- "Make this barrel physical." -> find_objects barrel -> get_object -> set_property Anchored false (CanCollide stays true) -> diagnose_object -> playtest start -> get_runtime_state / get_object -> playtest stop -> "The barrel now falls and gets pushed by explosions."
- "Make the enemies navigate this level." -> get_game_tree -> bake_navmesh -> find_path from an enemy to the player spawn -> create_script with humanoid:PathfindTo -> playtest start -> get_errors -> playtest stop.
- "Make this explosion push nearby objects." -> find the targets -> set_property Anchored false on them -> playtest start -> spawn_explosion at the spot -> check the hits and their velocity -> playtest stop.
- "What is in the scene?" -> get_game_tree, and answer only from what it returned.
More in get_engine_info topic "examples".

## Learn the engine
get_engine_info (or the gutsbolts:// resources) explains what the engine can do (capabilities), its concepts, the coordinate system, common workflows and every class you can insert. Check capabilities before promising a feature.

## Quick facts
Y is up, X is right, -Z is an object's forward (LookVector). Position is a part's centre. Orientation is in degrees; CFrame.Angles takes radians. Colours are [r, g, b] from 0 to 1. Scripts are Lua 5.4 with a Roblox-style API (Instance.new, part.Touched, game:GetService, RunService.Heartbeat, wait(), Players.LocalPlayer, workspace.CurrentCamera, humanoid:MoveTo, PathfindingService, Effects.Explosion/Blood, Sounds.Play). Luau syntax (`+=`, `continue`, type annotations, interpolated strings) works too. Also: player.CharacterAdded/CharacterRemoving, player.Team/TeamColor/Neutral, SpawnLocation.Neutral/TeamColor/AllowTeamChangeOnTouch (players spawn on their team's spawns), model.PrimaryPart with GetPivot/PivotTo/SetPrimaryPartCFrame, part.CustomPhysicalProperties = PhysicalProperties.new(density, friction, elasticity, frictionWeight, elasticityWeight), TextLabel.Bold. 1 unit = 2 Roblox studs; a character is about 2.6 units tall and walks 6 units a second.

## Talking to the person
Keep it short and friendly: they may be new to making games. Say what changed, not every tool call. If something couldn't be done, say what and why.)GUIDE";
}

const json& list() {
    static const json out = [] {
        json a = json::array();
        for (const Tool& t : tools()) a.push_back({{"name", t.name}, {"description", describe(t)}, {"input_schema", t.input}});
        return a;
    }();
    return out;
}

const json& mcpTools() {
    static const json out = [] {
        json a = json::array();
        for (const Tool& t : tools())
            a.push_back({{"name", t.name}, {"title", t.title}, {"description", describe(t)}, {"inputSchema", t.input},
                         {"annotations", {{"title", t.title}, {"readOnlyHint", t.readOnly}, {"destructiveHint", t.destructive},
                                          {"idempotentHint", t.idempotent}, {"openWorldHint", false}}},
                         {"_meta", {{"gutsbolts/domain", t.domain}, {"gutsbolts/read_only", t.readOnly},
                                    {"gutsbolts/mutates_state", !t.readOnly}, {"gutsbolts/destructive", t.destructive},
                                    {"gutsbolts/risk_level", riskName(t.risk)},
                                    {"gutsbolts/workflow", workflowOf(t.name)}}}});
        return a;
    }();
    return out;
}

} // namespace AiTools

// ---------------------------------------------------------------------------
// Engine reference (get_engine_info and the gutsbolts:// MCP resources). Keep
// these true to the engine: an AI believes every word.

namespace {

const char* kOverview = R"DOC(# Guts and Bolts

Guts and Bolts is a game engine and platform in the style of Roblox: games are trees of objects (parts, models, scripts, lights, sounds, UI) under Workspace, scripted in Lua 5.4 with a Roblox-style API, with real physics, characters, explosions, water and multiplayer. Studio is its editor; the Player app plays published games.

Guts and Bolts is a game engine, not a file editor. These tools act on the real game open in Studio right now: its object tree, properties, scripts, physics and playtests.

What the tools give you:
- Look: get_game_tree, find_objects, get_object, read_script, screenshot, get_output, get_errors.
- Change: insert_object, set_property, delete_object, create_script, edit_script, run_lua (any Lua, like the Command Bar).
- Run and check: playtest (play, simulate, pause, step), multiplayer_test (a local server and 2-4 player windows), get_runtime_state, diagnose_object, validate_scene.
- Physics and navigation: create_physical_object, spawn_explosion, bake_navmesh, find_path, walk_character_to.
- Stay safe: undo / redo, and checkpoint (save points to try things and roll back).
- Share: publish_game (File > Publish, as the signed-in account; only when asked).
- Learn: get_engine_info (this text, capabilities, concepts, coordinates, workflows, examples, classes).

Work in this loop: UNDERSTAND -> INSPECT -> PLAN -> ACT -> VALIDATE -> REPORT.)DOC";

const char* kConcepts = R"DOC(# Concepts

OBJECT (Instance)
Everything in a game is an object in one tree under Workspace. Each has a name, a class, properties and children. Every object has an object_id like "#42": unique, and it stays the same through undo, saving and loading. Use ids, not names (names can repeat).

CLASS (instead of components)
Guts and Bolts is Roblox-style: there is no separate component system. What an object does comes from its class and its properties, and extra behaviour comes from objects put INSIDE it. What other engines call components map like this:
- Transform -> a part's Position (its centre), Orientation (degrees) and Size properties. Models move their contents with scripts (model:PivotTo / :MoveTo) or run_lua.
- Mesh renderer -> the Part itself: its shape (Part = box, Sphere, Cylinder, MeshPart = a mesh made in Studio's Modeling mode), Color, Material, Transparency. Materials: Plastic, SmoothPlastic, Metal, DiamondPlate, Foil, Neon (glows), Wood, Glass, Ice, Concrete, Brick, Cobblestone, Slate, Granite, Marble, Sand, Grass, Fabric; each has its own look and its own default weight, grip and bounce (Ice is slippery, Brick grippy, Metal heavy). Enum.Material.Brick and "Brick" both work.
- Collider -> the same Part: CanCollide on/off. Collision uses the part's own shape (boxes, also rotated; spheres; cylinders). There's no separate collider object.
- Rigidbody -> Anchored = false makes a part simulated (gravity, collisions, forces). Mass comes from Size x Density (Material sets the default; the Density, Friction and Elasticity properties override it).
- Joints -> Constraint objects between two Attachments (a Weld joins two Parts): RopeConstraint, RodConstraint, SpringConstraint, WeldConstraint, HingeConstraint (optional motor: ActuatorType Motor, AngularVelocity, MotorMaxTorque), and:
  - BallSocketConstraint: the attachments stay together and turn any way (ragdoll joints, chains). LimitsEnabled + UpperAngle (degrees) keeps Attachment1's X axis within a cone around Attachment0's; TwistLimitsEnabled + TwistLowerAngle / TwistUpperAngle limit twisting; Restitution 0-1 bounces off the limits.
  - PrismaticConstraint: a slider. Attachment1 slides along Attachment0's X axis (rotate the attachments to aim it) and can't turn. LimitsEnabled + LowerLimit / UpperLimit (studs). ActuatorType "Motor" (Velocity studs/s, MotorMaxForce) or "Servo" (TargetPosition studs, Speed, ServoMaxForce). Read CurrentPosition. Forces default to 0, so set MotorMaxForce / ServoMaxForce (e.g. 5000) or nothing moves.
  - CylindricalConstraint: slides like a prismatic AND spins around the axis. Same slide properties, plus AngularActuatorType "Motor" (AngularVelocity rad/s, MotorMaxTorque) or "Servo" (TargetAngle degrees, AngularSpeed rad/s, ServoMaxTorque), AngularLimitsEnabled + LowerAngle / UpperAngle, InclinationAngle (tilts the spin axis from X towards Y). Read CurrentAngle.
- Forces -> Mover objects inside a part (BodyVelocity, BodyPosition, BodyGyro, BodyForce, BodyThrust, BodyAngularVelocity, LinearVelocity, AngularVelocity, AlignPosition, AlignOrientation, VectorForce, Torque) or script calls (part:ApplyImpulse, part:ApplyAngularImpulse, part.AssemblyLinearVelocity).
- Character controller -> a character Model (HumanoidRootPart, Torso, Head...) with a Humanoid: the player's character, a Rig, or the ready-made "Zombie". Scripts drive it through humanoid (MoveTo, PathfindTo, Health, WalkSpeed, Jump).
- Navigation agent -> any Humanoid character + PathfindingService (CreatePath, ComputeAsync, GetWaypoints) or humanoid:PathfindTo(target), which walks the whole way by itself.
- Light -> a PointLight or SpotLight placed inside a part.
- Audio source -> a Sound object (inside a part = it plays from there), or Sounds.Play in scripts.
- Audio mixer -> SoundGroup objects (usually in SoundService): a Sound's SoundGroup property picks one, and the group's Volume multiplies the sound's (a group inside another group multiplies again).
- Audio effects -> ReverbSoundEffect, EchoSoundEffect, EqualizerSoundEffect, DistortionSoundEffect, PitchShiftSoundEffect, ChorusSoundEffect, CompressorSoundEffect, FlangeSoundEffect, TremoloSoundEffect placed inside a Sound (changes that sound) or a SoundGroup (changes all its sounds together). Each has Enabled and Priority (higher runs first) and Roblox's settings (DecayTime, Delay, Feedback, HighGain, Level, Octave...). CompressorSoundEffect.SideChain isn't supported.
- Script -> Script (game server), LocalScript (each player's computer), ModuleScript (require()).
- Input actions / key bindings -> ContextActionService in a LocalScript: BindAction / BindActionAtPriority (Enum.KeyCode, or Enum.UserInputType Keyboard / Gamepad1 / MouseButton1 / Touch), UnbindAction, GetBoundActionInfo; the newest or highest-priority binding hears an input first and returning Sink hides it from the rest (and from walking / jumping / tools). createTouchButton = true shows a round button on phones (SetTitle, SetImage, SetPosition, GetButton). UserInputService.InputBegan also works.
- VR headsets -> VRService in a LocalScript: VREnabled (true while the player plays in VR), GetUserCFrame(Enum.UserCFrame.Head / LeftHand / RightHand) gives a CFrame in studs from where VR started, GetUserCFrameEnabled(kind), UserCFrameChanged:Connect(function(kind, cframe) ... end) fires every frame. The player's character already follows their head and hands by itself.
- Particles -> a ParticleEmitter inside a part (particles come out of anywhere in it) or an Attachment (out of that point): Rate, Lifetime and Speed (NumberRange), SpreadAngle (Vector2), Color (ColorSequence), Size and Transparency (NumberSequence), Texture (built-in "", "smoke", "fire", "sparkle", or a picture), LightEmission, Acceleration, Drag, EmissionDirection, Rotation / RotSpeed, LockedToPart; emitter:Emit(n) and :Clear() in scripts. Ready-made: Fire (Color, SecondaryColor, Size, Heat), Smoke (Color, Opacity, RiseVelocity, Size) and Sparkles (SparkleColor) in a part. Every player sees them. Scripts can also make one-off effects with Effects.Sparks / Blood / Oil / Gibs and explosions.
- Camera -> no camera object to edit. Players' cameras follow their character (World settings can make it orthographic).
- Post-processing / skybox -> objects right inside Lighting (or, from a LocalScript, workspace.CurrentCamera): ColorCorrectionEffect (Brightness, Contrast, Saturation, TintColor), SunRaysEffect (Intensity, Spread), DepthOfFieldEffect (FarIntensity, FocusDistance, InFocusRadius, NearIntensity), BloomEffect (Intensity, Size, Threshold), BlurEffect (Size), each with Enabled; several add up. A Sky (SkyboxBk/Dn/Ft/Lf/Rt/Up and SunTextureId/MoonTextureId pictures, StarCount, CelestialBodiesShown, SunAngularSize, MoonAngularSize) changes the sky; its skybox shows only when all six pictures load (uploaded "gb:<id>" pictures or files), else the Lighting settings' sky stays. insert_object puts them in Lighting; set_property changes them. SunRays and DepthOfField are off on Low graphics.
- UI styling -> objects inside a Frame/label/button: UICorner, UIStroke, UIGradient (Color = ColorSequence, Transparency = NumberSequence, Rotation, Offset), UIScale (Scale: it and everything inside it), UIAspectRatioConstraint (AspectRatio = width/height, AspectType FitWithinMaxSize / ScaleWithParentSize, DominantAxis Width / Height).
- 3D in UI -> a ViewportFrame shows the parts/models inside it as a picture, seen from its CurrentCamera (a Camera object made with Instance.new("Camera") inside it; none = it frames its contents). Ambient, LightColor, LightDirection light it. Its parts are not in the world (no physics, rays miss them).

SCENE (place, game)
One game = the Workspace plus services, saved as one file. Folders and Models group things. Services (like Roblox's): ReplicatedFirst, ReplicatedStorage (shared templates, RemoteEvents), ServerScriptService (game scripts), ServerStorage (server-only templates), StarterGui (screen UI), StarterPack (tools everyone spawns with), StarterPlayer (StarterPlayerScripts: LocalScripts for every player; StarterCharacterScripts: the character itself, so scripts there run in every character), Teams (Team objects), SoundService, Chat, TextChatService. Lighting holds the sky/sun/fog settings and Players the people in the running game. Paths start with a service: StarterPack.Sword, Workspace.Castle.Door.

ASSET
Pictures, sounds and meshes a game uses: files in the game's folder, built-in sounds, or uploaded items ("gb:<id>"). The Library (everyone's shared models, decals and audio) is not reachable through these tools.

PREFAB
There are no linked prefabs. insert_object can make ready-made things ("Kill Brick", "Exploding Barrel", "Zombie"...) but each is an independent copy: changing one doesn't change others. To change many copies, find_objects them and change each (run_lua with a loop is quickest).

PHYSICS
Only runs during a playtest (or a published game). Parts are ANCHORED by default: they stay put. Anchored = false makes a part fall, collide and react to forces and explosions. A visible part with CanCollide off is walked through and lets unanchored parts fall through it. Gravity is workspace.Gravity (22 units/s^2 by default). Parts below FallenPartsHeight (-50) are destroyed.

COLLISION
CanCollide decides whether things bump into a part. A character touching a part fires Touched even when its CanCollide is off (coins, checkpoints). Water parts and FluidVolumes are swum through.
Collision groups (PhysicsService) decide which parts bump into which: every part has CollisionGroup ("Default" to start with); PhysicsService:RegisterCollisionGroup("Players") adds a group (32 at most, Default included), :CollisionGroupSetCollidable("Players", "Players", false) makes two groups pass through each other, :CollisionGroupsAreCollidable(a, b), :IsCollisionGroupRegistered, :GetRegisteredCollisionGroups, :RenameCollisionGroup, :UnregisterCollisionGroup. Unknown group names in SetCollidable are errors; a part in an unregistered group acts as Default. A character is in its HumanoidRootPart's group (set every BasePart of the character to be safe); the terrain is Default. Loose parts, character movement and RaycastParams.CollisionGroup all honour it; Touched still fires. "Players walk through each other" -> a Players group not collidable with itself. "A door only the red team can pass" -> RedDoor and RedTeam groups not collidable, the door part in RedDoor, red characters' parts in RedTeam. Groups are set up in Studio too (MODEL > Collision Groups) and saved with the place.

CLICKING
A ClickDetector inside a part (or a model: any part in it) lets players click it: buttons, levers, "click to buy" signs. Its events MouseClick, RightMouseClick, MouseHoverEnter and MouseHoverLeave pass the player; server Scripts get them in online games too, and the pointer turns into a hand over it. MaxActivationDistance is in units (default 16 = Roblox's 32 studs); CursorIcon is an optional pointer picture. Example: part.ClickDetector.MouseClick:Connect(function(player) ... end).

NAVIGATION
The navigation mesh (every floor a character can stand on) is baked from the anchored, solid parts and rebakes itself when they change. PathfindingService:Bake() (or the bake_navmesh tool) rebakes now; Studio's Navmesh view shows it. find_path tests a route. PathfindingLabel / PathfindingPassThrough attributes and path Costs work like Roblox's PathfindingModifier. The terrain counts as floor (its materials are the area names for Costs).

TERRAIN
workspace.Terrain is a height map (hills, not caves or overhangs) the user sculpts in Studio's TERRAIN tab. Scripts: Terrain:Generate(seed, hills 0..1, sizeInStuds), :FillBlock(cframe, size, material) raises the ground to the block's top ("Air" digs down to its bottom), :FillBall(center, radius, material), :Sculpt("Raise"|"Lower"|"Smooth"|"Flatten"|"Paint", position, radius, strength, material), :GetHeight(x, z), :GetMaterial(position), :Clear(). Materials: Grass, Dirt, Sand, Rock, Snow, Mud (Roblox names like Ground or Basalt map to the closest). Raycasts hit it (Instance is workspace.Terrain). Water on terrain is still a Water part.

EXPLOSIONS
Instance.new("Explosion") with Position, BlastRadius, BlastPressure (500000 = normal) parented to workspace, or Explode(position, radius, power) / Effects.Explosion(...). They push unanchored parts, hurt characters, can break joints and (Destroy = true) rip anchored parts loose. Use them; don't push parts by hand.

RUNTIME vs EDITOR
Editor state is the saved game. Runtime state is the game while it runs (playtest): scripts run, physics moves things, characters spawn. Changes during a playtest are thrown away when it stops. get_object says which one you're looking at ("state_source") and adds velocity while playing.

SCRIPT
Lua 5.4 with Roblox's API names (Instance.new, game:GetService, workspace, task.wait, RunService.Heartbeat, Players, TweenService, DataStoreService, MessagingService, RemoteEvents). MessagingService:PublishAsync(topic, message) / :SubscribeAsync(topic, function(msg) ... msg.Data, msg.Sent end) send messages to every running server of the same game (this one too); topics 1-80 letters, messages at most 1 KB as JSON, 150 + 60 per player sends a minute per server. In Studio playtests they go between the servers running on this computer. Luau syntax (+=, continue, type annotations) works too. Player.CharacterAdded/CharacterRemoving, Teams with team SpawnLocations, Model.PrimaryPart/PivotTo and PhysicalProperties are there. Prefer the engine's systems (properties, constraints, movers, pathfinding, explosions); write scripts for game rules and when asked.)DOC";

const char* kCoordinates = R"DOC(# Coordinates and units

AXES (right-handed, like OpenGL)
- +Y is UP. Gravity pulls toward -Y.
- +X is RIGHT (in the default camera view).
- An object's FORWARD is its -Z axis: CFrame.LookVector = -Z column. RightVector = +X, UpVector = +Y.
- In a new game the Baseplate's top surface is the ground at Y = 0 (check with get_object on the Baseplate: Position Y + Size Y / 2).

UNITS
- 1 unit = 2 Roblox studs. Distances, Size and Position are in units.
- A character is about 2.6 units tall, walks 6 units per second (WalkSpeed 6) and jumps about 3.6 units high (JumpHeight 3.6).
- Gravity: 22 units/s^2 (workspace.Gravity).
- Time: seconds.

POSITION AND SIZE
- Position is the CENTRE of a part, in world units. To rest a part of height h on the ground (Y = 0), set Position Y = h / 2.
- Size is the full width (X), height (Y) and depth (Z). Parts inside Models are still given world positions by the tools.

ROTATION
- Orientation (and Rotation) = [x, y, z] Euler angles in DEGREES. The part's matrix is built as Rz * Ry * Rx (X is applied first, then Y, then Z, about the world axes).
- Turning something to face left/right = change Orientation Y. Tipping it over = X or Z.
- In scripts, CFrame.Angles(x, y, z) / CFrame.fromEulerAnglesXYZ take RADIANS (math.rad(90)) and build Rx * Ry * Rz, like Roblox's. CFrame.lookAt(from, to) points -Z at the target.
- There's no quaternion API; use Orientation or CFrames.

COLOURS
- Color = [r, g, b], each 0 to 1 (Color3.new). Color3.fromRGB takes 0-255 in scripts.

UI
- UI sizes and positions are UDim2: {scale, offset} for X and Y; scale is a fraction of the parent (0-1), offset is pixels. (0, 0) is the top-left of the screen.)DOC";

const char* kWorkflows = R"DOC(# Workflows

Tools work in chains. Don't call a later step if an earlier one failed: read the error and fix that first.

CREATE AN OBJECT
get_game_tree (where should it go?) -> insert_object -> set_property Position / Size / Color / Material ... -> get_object or diagnose_object (verify) -> screenshot (optional).

MAKE SOMETHING PHYSICAL ("make the barrel fall / react to explosions")
find_objects (find it) -> get_object (inspect) -> set_property Anchored false -> make sure CanCollide is true (diagnose_object warns otherwise) -> optional Density / Friction / Elasticity -> playtest start -> get_object (runtime velocity / position) -> playtest stop -> report.

MAKE CHARACTERS NAVIGATE ("enemies chase the player")
get_game_tree (is there a floor? are the enemies Humanoid characters? insert_object "Zombie" or "Rig" if needed) -> create_script using humanoid:PathfindTo(target) or PathfindingService -> playtest start -> get_errors / screenshot -> playtest stop. Floors must be anchored and solid; the navmesh rebakes itself when anchored parts change (PathfindingService:Bake() forces it).

EXPLOSIONS ("make this explosion push nearby objects")
Make sure the things to push are unanchored (set_property Anchored false) -> an Explosion in a script (Instance.new("Explosion"), Position, BlastRadius, BlastPressure, Parent = workspace) or, to try it now, playtest start -> run_lua "Explode(Vector3.new(x, y, z), radius, power)" -> get_object on the targets (runtime velocity) -> playtest stop.

WRITE AND DEBUG A SCRIPT
get_game_tree -> create_script (check the returned syntax) -> playtest start -> get_errors / get_output -> read_script -> edit_script -> playtest stop and start again -> get_errors -> playtest stop.

FIX "X DOESN'T WORK"
find_objects -> diagnose_object (lists likely causes and fixes) -> get_object -> fix ONE thing (set_property / edit_script) -> diagnose_object again -> playtest to confirm -> report what was wrong.

BUILD MANY THINGS
get_game_tree -> run_lua with a loop (print what you made) -> get_game_tree / validate_scene -> screenshot.

TRY A CHANGE SAFELY
get_object (note the old value) -> set_property -> playtest to test -> playtest stop -> keep it, or undo and try a smaller change.

CHECK BEFORE FINISHING
validate_scene -> fix errors -> playtest start -> get_errors -> playtest stop -> report what changed and anything that couldn't be done.)DOC";

const char* kExamples = R"DOC(# Examples: requests and the tool calls they should become

"Make this barrel physical."
find_objects {name: "barrel"} -> get_object (anchored? can_collide?) -> set_property Anchored false -> (CanCollide true if it was off) -> diagnose_object -> playtest start -> get_object (runtime: falling, then resting) -> playtest stop. Report: it falls and reacts to forces now.

"Make the enemies navigate this level."
get_game_tree (find the enemies: Models with a Humanoid; insert_object "Zombie" if there are none and they asked for some) -> bake_navmesh -> find_path from an enemy to where they should go (status Success?) -> create_script inside each enemy (or one script looping over them) that calls script.Parent.Humanoid:PathfindTo(target) -> playtest start -> get_errors -> get_runtime_state (characters walking?) -> playtest stop.

"Make this explosion push nearby objects."
Find the explosion point (the bomb part's position) -> find_objects near it, set_property Anchored false on what should fly -> playtest start -> spawn_explosion {position, radius} -> read hits -> get_object on a hit part (velocity) -> playtest stop. For the real game, put it in a script: local e = Instance.new("Explosion") e.Position = bomb.Position e.BlastRadius = 12 e.Parent = workspace.

"What is in the scene?"
get_game_tree -> answer from it. Never add objects it didn't list.

"The barrel doesn't fall."
find_objects -> diagnose_object (ANCHORED? FALLS_THROUGH? BELOW_WORLD?) -> fix the one cause it names -> playtest start -> get_object runtime -> playtest stop -> say what was wrong.

"Try making the explosion more powerful."
checkpoint save -> edit_script (BlastPressure higher) -> playtest start -> spawn_explosion / watch -> playtest stop -> too strong? checkpoint restore and try a smaller number -> keep the good one.

"Build a staircase up to the platform."
get_object platform (top height) -> run_lua with a loop making anchored steps (print each step's name) -> get_game_tree -> screenshot.

"Add a ParticleEmitter to the torch."
find_objects {name: "torch"} -> insert_object ParticleEmitter (parent: the torch's top part) -> set_property Color, Size, Speed, Lifetime, LightEmission (or insert_object Fire for ready-made flames) -> screenshot.

"Delete the trees."
find_objects {name: "tree"} -> if it's clearly those objects, delete_object each (or run_lua over a list of ids) -> get_game_tree. If some are unclear (a "TreeHouse"?), list them and ask first.)DOC";

} // namespace

namespace AiTools {

json capabilities() {
    return {
        {"engine", "Guts and Bolts"},
        {"engine_version", GB_VERSION},
        {"mcp_server", "guts-and-bolts-studio"},
        {"mcp_version", "2.0"},
        {"style", "Roblox-like: objects in a tree, classes instead of components, Lua 5.4 scripts"},
        {"capabilities", {
            {"scene", true}, {"objects", true}, {"stable_object_ids", true}, {"search", true},
            {"components", false},                    // classes and child objects instead
            {"scripting", true}, {"lua_execution", true},
            {"runtime", true},                        // playtest start / stop
            {"runtime_inspection", true},             // get_runtime_state, get_object runtime values, errors
            {"physics", "via properties (Anchored, CanCollide, Density...), movers, constraints and scripts; simulates during playtests"},
            {"navigation", true},                     // bake_navmesh, find_path, walk_character_to; PathfindingService in scripts
            {"explosions", true},                     // spawn_explosion (playtest); Explosion objects in scripts
            {"rendering", "partial: part colour, material, transparency, lights, decals, particles (ParticleEmitter, Fire, Smoke, Sparkles); no shader or post-processing tools"},
            {"audio", "Sound objects and Sounds.Play; SoundGroups (volume) and sound effects (reverb, echo, equalizer, distortion, pitch shift, chorus, compressor, flange, tremolo)"},
            {"cross_server_messaging", "MessagingService (PublishAsync / SubscribeAsync) between a game's running servers"},
            {"ui", true},
            {"assets", false},                        // the Library isn't reachable from these tools
            {"prefabs", false},                       // ready-made things are copies, not linked
            {"undo", true}, {"redo", true},
            {"transactions", "checkpoints: save, experiment, restore (Studio session only)"},
            {"compound_tools", json::array({"create_physical_object", "spawn_explosion", "walk_character_to"})},
            {"screenshots", true},
            {"diagnostics", true},
            {"multiplayer_testing", "multiplayer_test: a local test server and 2-4 Player windows on this computer; read what each prints"},
            {"publishing", "publish_game: like File > Publish, as the signed-in account (refused when not signed in); only when asked"},
            {"services", json::array({"Workspace", "Players", "Lighting", "ReplicatedFirst", "ReplicatedStorage", "ServerScriptService",
                                      "ServerStorage", "StarterGui", "StarterPack", "StarterPlayer", "StarterPlayer.StarterPlayerScripts",
                                      "StarterPlayer.StarterCharacterScripts", "Teams", "SoundService", "Chat", "TextChatService"})},
            {"property_names", "the same as Lua scripts (get_object lists them; set_property accepts them and common misspellings)"},
            {"output_runs", "get_output / get_errors mark each line with its playtest run; run \"latest\" reads only the newest"},
        }},
        {"not_supported", json::array({"editing Library assets", "linked prefabs", "camera objects",
                                       "shaders", "moving or inspecting the players inside a multiplayer test", "signing in from the tools",
                                       "objects inside Lighting (it holds settings)"})},
        {"notes", "If a capability is false or missing, the tools can't do it: say so instead of pretending."},
    };
}

std::string engineDoc(const std::string& topic) {
    if (topic == "overview") return kOverview;
    if (topic == "concepts") return kConcepts;
    if (topic == "coordinates") return kCoordinates;
    if (topic == "workflows") return kWorkflows;
    if (topic == "capabilities") return "# Capabilities\n\n" + capabilities().dump(2);
    if (topic == "examples") return kExamples;
    return "";
}

// MCP prompts: ready-made requests that walk an AI through a common job.
const json& prompts() {
    static const json list = json::array({
        {{"name", "make_physical"}, {"title", "Make an object physical"},
         {"description", "Make an object fall, collide and react to forces and explosions."},
         {"arguments", json::array({{{"name", "object"}, {"description", "The object (id like #42, or its name)"}, {"required", true}}})}},
        {{"name", "navigate_enemies"}, {"title", "Make enemies navigate the level"},
         {"description", "Bake navigation, check routes and script enemies to walk with pathfinding."},
         {"arguments", json::array({{{"name", "target"}, {"description", "Where they should go (\"the player\", a part...)"}, {"required", false}}})}},
        {{"name", "explosion_push"}, {"title", "Make an explosion push things"},
         {"description", "Set up and test an explosion that throws nearby objects."},
         {"arguments", json::array({{{"name", "where"}, {"description", "The object or position it goes off at"}, {"required", true}}})}},
        {{"name", "fix_not_working"}, {"title", "Find out why something doesn't work"},
         {"description", "Inspect, diagnose, fix one cause and verify."},
         {"arguments", json::array({{{"name", "object"}, {"description", "The object"}, {"required", true}},
                                    {{"name", "problem"}, {"description", "What's wrong (\"doesn't fall\", \"script does nothing\")"}, {"required", true}}})}},
        {{"name", "describe_scene"}, {"title", "Describe what's in the game"},
         {"description", "Look at the real game and summarise it."}, {"arguments", json::array()}},
    });
    return list;
}

json prompt(const std::string& name, const json& args) {
    auto arg = [&](const char* k, const char* fallback) {
        return args.is_object() && args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(fallback);
    };
    std::string text;
    if (name == "make_physical")
        text = "Make " + arg("object", "the object") + " physical: it should fall, collide and react to forces and explosions.\n\n"
               "Workflow: find it (find_objects / get_object) -> check Anchored and CanCollide -> set_property Anchored false "
               "(keep CanCollide true; keep its position) -> diagnose_object -> playtest start -> get_object (runtime: it "
               "falls and comes to rest) -> playtest stop -> report what changed.";
    else if (name == "navigate_enemies")
        text = "Make the enemies navigate this level to reach " + arg("target", "the player") + ".\n\n"
               "Workflow: get_game_tree (find the enemies: Models with a Humanoid) -> bake_navmesh -> find_path from an enemy "
               "to the target (fix the level if NoPath) -> create_script using Humanoid:PathfindTo (don't hand-write path "
               "following) -> playtest start -> get_errors / get_runtime_state -> playtest stop -> report.";
    else if (name == "explosion_push")
        text = "Make an explosion at " + arg("where", "that spot") + " push the objects near it.\n\n"
               "Workflow: find what's nearby (find_objects / get_object) -> set_property Anchored false on what should fly -> "
               "playtest start -> spawn_explosion at the spot -> check the hits and their velocity (get_object) -> playtest "
               "stop. To make it part of the game, use an Explosion object in a script. Never move the parts by hand.";
    else if (name == "fix_not_working")
        text = arg("object", "The object") + ": " + arg("problem", "it doesn't work") + ". Find out why and fix it.\n\n"
               "Workflow: find_objects / get_object -> diagnose_object -> read_script if a script is involved -> fix the ONE "
               "cause you found (set_property / edit_script) -> diagnose_object again -> playtest start -> get_errors / "
               "get_object -> playtest stop -> say what was wrong and what you changed. Don't change things you didn't check.";
    else if (name == "describe_scene")
        text = "What is in my game?\n\nWorkflow: get_game_tree (and get_object for anything interesting) -> describe only what "
               "the tools returned. Never invent objects.";
    else return nullptr;
    return {{"description", name}, {"messages", json::array({{{"role", "user"}, {"content", {{"type", "text"}, {"text", text}}}}})}};
}

const json& resources() {
    static const json list = json::array({
        {{"uri", "gutsbolts://guide"}, {"name", "guide"}, {"title", "How to work in Guts and Bolts Studio"}, {"mimeType", "text/markdown"},
         {"description", "The assistant's operating rules (same as the server instructions)."}},
        {{"uri", "gutsbolts://overview"}, {"name", "overview"}, {"title", "What Guts and Bolts is"}, {"mimeType", "text/markdown"},
         {"description", "The engine and what these tools give access to."}},
        {{"uri", "gutsbolts://capabilities"}, {"name", "capabilities"}, {"title", "Capability manifest"}, {"mimeType", "application/json"},
         {"description", "What the tools can and can't do. Check it before promising a feature."}},
        {{"uri", "gutsbolts://concepts"}, {"name", "concepts"}, {"title", "Engine concepts"}, {"mimeType", "text/markdown"},
         {"description", "Objects, classes (instead of components), physics, navigation, runtime vs editor..."}},
        {{"uri", "gutsbolts://coordinates"}, {"name", "coordinates"}, {"title", "Coordinate system and units"}, {"mimeType", "text/markdown"},
         {"description", "Axes (Y up, -Z forward), units, rotations (degrees, order), colours."}},
        {{"uri", "gutsbolts://workflows"}, {"name", "workflows"}, {"title", "Common workflows"}, {"mimeType", "text/markdown"},
         {"description", "Which tools to chain for common jobs."}},
        {{"uri", "gutsbolts://examples"}, {"name", "examples"}, {"title", "Example requests and tool calls"}, {"mimeType", "text/markdown"},
         {"description", "Realistic requests and the tool workflow each should become."}},
        {{"uri", "gutsbolts://runtime"}, {"name", "runtime"}, {"title", "The running game (live)"}, {"mimeType", "application/json"},
         {"description", "Playtest state, fps, moving parts, characters, like get_runtime_state."}},
        {{"uri", "gutsbolts://classes"}, {"name", "classes"}, {"title", "Insertable classes"}, {"mimeType", "application/json"},
         {"description", "Every kind insert_object can make."}},
        {{"uri", "gutsbolts://scene/tree"}, {"name", "scene-tree"}, {"title", "The game's object tree (live)"}, {"mimeType", "application/json"},
         {"description", "The current game, like get_game_tree."}},
        {{"uri", "gutsbolts://errors"}, {"name", "errors"}, {"title", "Recent errors (live)"}, {"mimeType", "application/json"},
         {"description", "Recent errors and warnings from the Output, like get_errors."}},
    });
    return list;
}

} // namespace AiTools
