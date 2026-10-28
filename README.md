# Guts and Bolts

A Roblox-style game engine and platform with a Happy Wheels streak. It comes
as two apps:

- **Guts and Bolts Studio** (`GutsAndBolts`) is the editor. You build worlds out
  of parts, write Lua scripts, set up lighting and test with **Play**.
- **Guts&Bolts Player** (`GutsAndBoltsPlayer`) is the platform. You browse
  games, dress up your avatar, and play alone or with friends (with chat).

## Installing (the easy way)

1. Download this repository (the green **Code** button, then **Download ZIP**)
   and unzip it.
2. Double-click the installer for your computer:
   - **Windows:** `Install.bat`
   - **Mac:** `Install.command` (if macOS blocks it, right-click it and choose **Open**)
   - **Linux:** run `./install.sh` in a terminal
3. Press **Install** in the window that opens.

The installer works out which operating system you have and sets everything
up for you:

| Your computer | What it does |
| --- | --- |
| Windows | Installs MSYS2 (a free compiler kit) with CMake, GLFW, GLEW and GLM |
| macOS | Uses Apple's developer tools and Homebrew (`cmake ninja glfw glew glm`) |
| Linux | Uses apt, dnf, pacman or zypper, asking for your password in a popup |

Then it downloads the rest of the engine (Dear ImGui, Lua, ...), builds both
apps and adds Desktop / menu shortcuts. The first build takes a few minutes.

> No window? Run `python3 install.py --cli` for the text version.
> `python3 install.py --check` just shows what it detected.

**Getting it:** players download the Player and Studio from
**<https://gutsandbolts.net/#download>**, which is the only place they come from.
The files live on Cloudflare (an R2 bucket, `worker/downloads.js`). Each release
uploads them there itself: GitHub signs a short-lived token for the release run,
and the site checks that signature, so no password is stored anywhere.

**Updates are automatic.** When the Player or Studio opens and gutsandbolts.net
has a newer version, an *Updating Guts&Bolts* screen counts down from 3,
downloads the new version, puts it over the old one and reopens the app, so
everyone plays on the same version. Your own games are kept. Studio waits
while you have unsaved work, and the Player waits until you leave a game.

## Android (experimental)

The Player also runs on Android phones and tablets (Android 7.0+). Download
`GutsAndBoltsPlayer-Android.apk` from
[gutsandbolts.net](https://gutsandbolts.net/#download),
open it on your phone and allow installing it. It has the thumbstick / jump
button touch controls, swipe scrolling, the Back button, the sample games
built in, and it can host or join games with phones and computers on the same
Wi-Fi. Studio (making games) stays on computers.

To build the APK yourself you need the Android SDK and NDK:

```sh
cd android
./gradlew assembleRelease     # -> app/build/outputs/apk/release/app-release.apk
```

To try the phone version on a Linux computer:
`cmake -S . -B build-mobile -DGB_MOBILE=ON && cmake --build build-mobile`, then
run `GB_TOUCH_SCREEN=1 GB_UI_SCALE=2 ./build-mobile/GutsAndBoltsPlayer`.

## Guts and Bolts Studio (the editor)

Studio is laid out like Roblox Studio, with the same dark theme and a
**ribbon** across the top:

- **HOME**: clipboard, the Select / Move / Scale / Rotate tools, insert a Part
  or any Object, group / lock / anchor, and Play / Play Here / Simulate / Stop.
- **MODEL**: snap-to-grid (studs and degrees), parts, **MeshPart** and
  **Edit Mesh** (Modeling mode), constraints, scripts, **Pivot to middle** for models, and **Align** (line the selection up on X,
  Y or Z by their min, center or max).
- **TEST**: Play, Play Here, Simulate, Pause, Step and Stop, plus the Player
  and Lighting settings.
- **MESH** (only in Modeling mode): pick corners / edges / faces, and the
  shape tools.
- **VIEW**: show or hide each panel, reset the camera, and the shortcut list.

The panels:

- **Viewport**: the 3D view. Click to select, drag the gizmo. Clicking a part
  inside a Model picks the whole Model (hold **Alt** to pick just the part),
  just like Roblox. Locked parts can't be clicked.
- **Explorer**: everything in your game, with a Roblox-style icon for each
  kind of object. Drag an object onto another to put it inside, double-click a
  Script to edit it, and use the **+** button on a row to insert something.
  The **filter box** (`Ctrl+Shift+X`) searches names, and also understands
  Roblox's search words: `c:Script` (kind of object), `is:Script`, `tag:Enemy`,
  `name:Door`, property checks like `Anchored=false` or `Transparency>0.5`, and
  `or` to match either side.
- **Toolbox** (laid out like Roblox's): **Library** (everyone's public
  models, decals and audio, plus Studio's own parts and ready-made objects),
  **Inventory** (your own uploads, private ones too) and **Recent** (what you
  inserted lately). Pick a category, search, and click a picture to insert it.
  Library models show how many people liked them (a green **92%** tag; point
  at it for the number of votes), and **Sort: Best rated** lists the best ones first.
  Every item has a picture: Studio photographs models on their own, and
  things made by Guts&Bolts staff (and Studio's built-in objects) carry a gold
  **official** badge, so you know they're safe to use. It has
  parts, scripts, lights and **ready-made** objects that already
  contain scripts: kill brick, coin, jump pad, spinner, moving and fading
  platforms, speed pad, click button, lamp post, disco floor, landmine, saw
  blade, spike trap, exploding barrel and a day/night cycle. There are also
  physics toys: swinging rope, wrecking ball, windmill, seesaw, a drivable
  **Motor Cart**, domino run, crate pyramid and trampoline.
- **Insert Object** (`Ctrl+I` or the **+** in the Explorer): a searchable list
  of every kind of object you can add.
- **Constraints** (in the Toolbox): pick Rope, Rod, Spring, Weld, Hinge,
  Motor, Ball Socket, Prismatic or Cylindrical, then click two parts in the
  Viewport to join them.
- **Properties**: colour, material, size, anchored, can-collide and so on.
  There are 18 materials, each with its own look drawn by the engine (no
  image files) and its own default weight, grip and bounce: **Plastic**,
  **SmoothPlastic** (glossier, no grain), **Metal**, **DiamondPlate** (raised
  non-slip stripes), **Foil** (crinkly and shiny), **Neon** (glows), **Wood**,
  **Glass**, **Ice** (slippery), **Concrete**, **Brick** (rows of bricks with
  mortar, lined up with the part's edges), **Cobblestone**, **Slate**,
  **Granite** (speckled), **Marble** (polished, with veins), **Sand**,
  **Grass** and **Fabric** (woven). Scripts use `part.Material =
  Enum.Material.Brick` or `"Brick"`; a name that doesn't exist is an error
  that lists the real ones. Imported Roblox places keep these materials;
  other Roblox materials get the closest one (Rock, Basalt and Asphalt look
  like Concrete, Pebble like Cobblestone, LeafyGrass like Grass).
- **Lighting**: sun, shadows, sky light, clouds, stars, fog, exposure,
  bloom, ambient occlusion and colour grading, plus a **time of day** slider
  and presets (Day, Sunset, Night, Overcast, Horror).
- **Player**: walk speed, jump power, health, outfit and hat, plus the game's
  **Death & Gore** rules.
- **Script Editor**: Lua code with a live mistake checker, an
  **Insert code...** menu of ready-to-use snippets, **autocomplete** (suggestions
  pop up as you type; `Tab` uses one, `Ctrl+Up/Down` picks), **Find / Replace**
  (`Ctrl+F` / `Ctrl+H`, `F3` for the next one) and **Find in All Scripts**
  (`Ctrl+Shift+F`: click a result to jump straight to it).
- **Output**: what your scripts `print()`, plus any errors (in red, with the
  line number).
- **Command Bar**: type a line of Lua and press Enter to run it on your game
  right away, e.g. `workspace.Baseplate.Transparency = 0.5`. Shift+Enter adds
  a new line, Ctrl+Up / Ctrl+Down bring back earlier commands, and it can be
  undone with Ctrl+Z.

**File** menu: New, Open, Save (games go in the `games` folder next to the
app), **Import / Export Roblox** files, Game Settings (the title and
description shown in the Player app) and **Play in Guts&BoltsPlayer**.
**Edit** menu: Undo / Redo, Copy / Paste, Duplicate, Delete. **View >
Settings** covers the frame rate and graphics.

### 3D models, pictures and sounds (and dragging files in)

**File > Import 3D Model, Picture, Sound...** (or the **Import** button on the
HOME tab) brings in 3D models made in other programs: **.fbx**, **.obj** (with
its .mtl colours), **.gltf / .glb** (including squashed ones: Draco and meshopt,
like Sketchfab downloads), **.stl**, **.ply**, **.dae** (Collada), **.3ds**,
**.x**, **.3mf**, **.lwo**, **.ac**, **.off**, **.dxf**, **.b3d**, **.ms3d**,
**.md2 / .md3 / .md5mesh**, **.ase**, **.smd**, **.x3d**, **.amf**, **.ogex**,
**.irrmesh**, **.cob**, **.xgl**, **.pmx** and a few more. Program save files
(.blend, .max, .ma, .c4d, .skp), USD and CAD files (.step) can't be opened:
Output tells you which format to export from that program instead. Each object in the
file becomes a mesh part (split by material, in that material's colour; a
textured material gets its picture's main colour), all inside a Model named
after the file. 1 metre is 1 stud; something far too big or too small (a model
made in millimetres) is resized to about 10 studs, and Output says so.

Or just **drag files from your computer onto Studio**. Where you let go
matters:
- a 3D model lands standing on the spot under the mouse;
- a picture (.png / .jpg / .gif / .bmp / .tga) becomes a Decal on the side of
  the part it's dropped on, or a sign facing you if it lands on the ground.
  Other pictures (.webp, .svg, .psd, .hdr, .ppm) are saved as a PNG in the games
  folder's **Imported Pictures** folder first, so every Player can show them;
- a sound (.wav / .mp3 / .ogg / .flac) becomes a Sound (inside the part it's dropped on);
- a script (.lua / .luau) becomes a Script;
- a Roblox model is inserted, and a game or Roblox place is opened.

Drop several files at once and they're lined up side by side.

### Roblox files (.rbxl, .rbxlx, .rbxm, .rbxmx)

Open a Roblox place (`.rbxl` / `.rbxlx`) and it becomes a Guts and Bolts
game. Import a model (`.rbxm` / `.rbxmx`) and it's added to your game. Both
the binary and XML kinds work. What comes across: parts (blocks, spheres,
cylinders, wedges, trusses), models (with their PrimaryPart) and folders, SpawnLocations
and Teams, CustomPhysicalProperties, scripts (Roblox's **Luau** code works as it is), lights, sounds, attachments and
constraints, value objects, attributes and tags. Things we don't have yet
(meshes, terrain, GUIs) are skipped, and Output tells you what was left out.

**File > Export Roblox** saves your game (or just the selection) as a Roblox
file that Roblox Studio can open, so you can take your work either way.
Roblox characters are twice our size, so everything is scaled to match.

### Modes (like Blender)

The menu in the Viewport's top-left corner (or the **Mode** menu) switches
between four modes:

- **Build** (the normal one): place, move, resize and change objects.
- **Modeling** (`Tab`): reshape a part yourself, like Blender's Edit Mode.
  Select a part and press `Tab`. It becomes a **MeshPart** and you can pick
  its corners (`1`), edges (`2`) or faces (`3`). Click to pick, Shift+click
  to add, or drag a box. Move, turn or stretch what you picked with the
  gizmo. The **MESH** tab has the tools:
  - **Extrude** (`E`): pull faces or edges out into new ones.
  - **Inset** (`I`): put a smaller face inside each picked face.
  - **Subdivide**: cut faces into smaller ones.
  - **Merge** (`M`): squash the picked corners into one.
  - **Fill** (`F`): make a face between picked corners.
  - **Delete** (`X`).
  - **Flip**: turn faces inside out.
  - **Smooth / Flat** shading.
  - **X-Ray** (`Alt+Z`): see and pick through the mesh.

  Press `Tab` again when you're done. To start from scratch, use
  **MODEL > MeshPart** or Insert Object > MeshPart. MeshParts save with your
  game, work in the Player app and collide like any part.
- **Simulate** (`F8`): physics and scripts run live, but there's no
  character. Fly the camera around, click things to see their properties
  change as they move, and drag them with the gizmo while everything keeps
  running. `F6` pauses and `F7` steps one frame at a time.
- **Play** (`F5`): a real playtest. You spawn at a SpawnLocation, walk with
  **WASD**, jump with **Space** and look around with the right mouse button.
  **Play Here** starts you where the camera is looking. `F6` pauses here too.

**Stop** (`Shift+F5` or `Esc`) goes back to Build mode, and everything goes
back exactly how it was before you pressed Simulate or Play.

### Level Generator (dungeons and obbies)

Press **MODEL > Generate** (or View > Level Generator) to make a whole level from a
few rules:

- **Dungeon**: rooms joined by corridors, with doors, crates and coins.
- **Obby**: a course of platforms to jump along, with checkpoints, coins and lava
  underneath.

Every level has a **seed** (a number). The same seed always makes the same level,
so you can share a good one by telling someone its number. **Random** picks one.

Before you see it, each level is checked: everything fitted, nothing is inside
anything else, and a robot (the same navmesh NPCs use) walks and jumps from the
spawn to the goal. **Find one that works** tries up to 20 seeds and keeps the first
that passes, and **Test 10 seeds** lists 10 seeds with how long each route is so
you can pick one. The level goes into a Model next to your game; press Play and
walk to the green goal. **Replace the last one** swaps it for the new one, and one
Ctrl+Z takes it away.

The Assistant can make levels too ("make me an obby with 30 platforms"), with the
`generate_level` tool.

### Compare and Merge (two versions of a game)

Press **TEST > Compare** (or View > Compare and Merge) and pick another saved copy of
your game. Studio lists every difference, object by object: **Added** (only in the other
version), **Removed** (not in the other version), **Moved** and **Changed** (open a row
to see each property before and after), plus the game settings. Objects keep their id
when a game is saved, so a renamed or moved object is still matched with itself. Click a
row to select that object, tick the ones you want and press **Bring in the ticked ones**:
only those become like the other version (one Ctrl+Z puts them back). That's handy for
getting one thing back from an old backup without losing everything else.

To merge two people's work, tick **I have the version we both started from** and pick
that one too. Then only the other person's changes are listed, and an object you both
changed is marked as a **Conflict** (not ticked, so you decide). The Assistant can do all
this with the `compare_scenes` tool.

### Testing with more than one player

Play is just you. To see how your game works with other people, use
**TEST > Players** (or the **Mode** menu) and pick **Test with 2 / 3 / 4
players**. Studio starts your game, as it is right now (saved or not), on a
test server on your own computer (`GutsAndBoltsGameServer --place`, the same
local network server as "Host on LAN") and opens a small Player window for
each player, which joins it as **Player1**, **Player2** and so on. No friends,
account or internet needed.

- Server **Scripts** run on the test server and **LocalScripts** run in each
  player's window, so RemoteEvents and RemoteFunctions really go between them.
- What the server and every player print (and their errors) shows in Studio's
  **Output**, marked `[Server]`, `[Player1]`... The server also says when each
  player joins and leaves.
- **Add a player** (in the same menu) opens one more window, to test someone
  joining late. Close a player's window to test someone leaving.
- **End Test** (or **Stop multiplayer test**) closes the test server and every
  player window. Quitting Studio does too.
- Changes you make in Studio while the test runs don't reach it: end the test
  and start it again.

Studio needs `GutsAndBoltsGameServer` and `GutsAndBoltsPlayer` next to it
(they're built and installed with it). The test server uses a free port from
7790 up; the player windows are on this computer only.

### Assistant (AI help)

The **Assistant** tab (HOME > Assistant) is Studio's AI helper, like Roblox
Studio's Assistant. Two ways to use it:

- **Chat with Claude.** Paste your Anthropic API key (from
  console.anthropic.com) into Chat settings. It stays on your computer. Then ask
  for things like "make an obby with 10 jumps" or "why doesn't my door script
  work?". The Assistant can look at your game, build, write and fix scripts,
  run Lua, playtest, read the Output and take screenshots to check its work.
  Ctrl+Z undoes anything it does.
- **Connect AI apps with MCP.** Tick "Let AI apps use Studio (MCP)" and apps
  on your computer that speak MCP (the Model Context Protocol) can use the same
  tools: Claude Code, Claude Desktop, and other AI coding tools. Only programs
  on this computer can connect. The tab shows copy-and-paste set-up for each:
  - Claude Code: `claude mcp add --transport http guts-and-bolts http://127.0.0.1:44755/mcp`
  - Claude Desktop and apps that start a program: run `GutsAndBolts --mcp` (it
    passes messages to the Studio that's open).

The tools, by area (each one's description says when to use it, when not to,
what it needs, what it gives back and how risky it is):

- Scene: `get_game_tree`, `find_objects`, `insert_object`, `delete_object`
- Objects: `get_object`, `set_property`, `find_and_change` (many objects at once), `select`
- Scripting: `create_script`, `read_script`, `edit_script`, `run_lua`
- Runtime: `playtest` (start, simulate, stop, pause, resume, step), `get_runtime_state`,
  `multiplayer_test` (start a test with 2-4 players, `add_player`, `status`,
  `output` to read what the server and each player printed, `stop`)
- Physics and navigation: `create_physical_object`, `spawn_explosion`,
  `bake_navmesh`, `find_path`, `walk_character_to`
- Docs: `get_engine_info` (overview, capabilities, concepts, coordinates,
  workflows, classes)
- Debug: `get_output`, `get_errors`, `diagnose_object`, `validate_scene`, `analyze_script_dependencies` (static ModuleScript dependency map, unresolved references and potentially unreferenced modules), `analyze_scene_performance` (static counts for parts, dynamic physics bodies, lights, particle emitters and script sizes; not a live profiler), `script_history` (record what scripts change during a playtest: each property set with before / after and the script line, and each event that ran a script)
- History: `ai_activity` (everything the AI changed since the person last pressed Keep, a log of every AI tool call, and `undo_all`). With "Ask me first" on (the default), `delete_object` and `checkpoint` restore / delete wait for the person to press Allow in the AI Activity window and fail with `NEEDS_PERSON_OK` until then.
- View and history: `screenshot`, `undo`, `redo`, `checkpoint` (named save
  points to try things and roll back)
- Publish: `publish_game` (only when you ask for it)

**Services.** The tools see the whole game, not just the Workspace:
`get_game_tree` lists every service like the Explorer (Players, Lighting,
ReplicatedFirst, ReplicatedStorage, ServerScriptService, ServerStorage,
StarterGui, StarterPack, StarterPlayer with StarterPlayerScripts and
StarterCharacterScripts, Teams, SoundService, Chat, TextChatService), and a
service's name works as a parent or at the start of a path: a Tool in
`StarterPack`, a Script in `ServerScriptService`, a LocalScript in
`StarterPlayer.StarterPlayerScripts`, `ReplicatedStorage.Events`. A service's
folder is made the first time something goes in it. StarterCharacterScripts is
the character itself (scripts there run in every player's character). Lighting
holds settings, so `get_object` / `set_property` with object `"Lighting"`
read and change ClockTime, Brightness, fog and so on.

**Property names are the script names.** `get_object` lists an object's
properties under the names Lua uses (`CanCollide`, `BackgroundColor3`...), read
from the engine, and `set_property` takes the same names back (older spellings
like `can_collide` still work and are corrected). Enum values work as `"Neon"`
or `"Enum.Material.Neon"`. Setting a script's `Source` is passed on to
`edit_script`, which is the way to change code.

**Play sessions in the Output.** Every Output line is marked with the playtest
it came from (`run`: 1, 2, 3...; 0 = while editing). `get_output` and
`get_errors` take `run: "latest"` (or `"current"`, `"editing"`, a number) so an
AI checking its fix doesn't trip over errors from an earlier run.

**Bad names list the choices.** An unknown class, property, enum value or
service fails with `UNKNOWN_CLASS`, `UNKNOWN_PROPERTY`, `INVALID_ENUM_VALUE` or
`SERVICE_NOT_FOUND`, and `error.valid_choices` lists what would have worked.

**Publishing.** `publish_game` publishes exactly like File > Publish to
Guts&Bolts: as the account Studio is signed in with, through the same request,
with the server's own checks and limits. It needs `confirm: true`, refuses with
`NOT_SIGNED_IN` when Studio isn't signed in (it never signs in by itself), and
finishes in the background: `publish_game {action: "status"}` says how it went.

`tools/test_ai_tools.py` starts Studio with MCP on (under `xvfb-run` when
there's no screen) and checks all of this over JSON-RPC:
`python3 tools/test_ai_tools.py`. It also runs every file in
`tools/ai_tool_tests/` (one per feature; see the README there).

Every tool answers with JSON: `success`, the `object_id` (`#42`) of what it
touched, the state read back from the engine, `warnings`, and for failures an
`error` with a `code` (like `OBJECT_NOT_FOUND`), whether it's `recoverable` and a
`suggested_action`. MCP apps also get it as `structuredContent`, and each tool
is marked read-only or not (`readOnlyHint`, `destructiveHint`) with a
`gutsbolts/risk_level` in its `_meta`. The guide the AI reads first (MCP's
server instructions, the Assistant's system prompt) teaches it to look first,
use the engine's own systems, check its work and never invent things.

MCP apps can also read the engine reference as resources: `gutsbolts://guide`,
`gutsbolts://overview`, `gutsbolts://capabilities` (a JSON manifest of what the
tools can and can't do), `gutsbolts://concepts`, `gutsbolts://coordinates`
(Y up, -Z forward, units, rotations), `gutsbolts://workflows`,
`gutsbolts://examples`, `gutsbolts://classes` (insertable classes and the
services they can go in), and live ones:
`gutsbolts://scene/tree`, `gutsbolts://runtime` and `gutsbolts://errors`.
MCP prompts (`make_physical`, `navigate_enemies`, `explosion_push`,
`fix_not_working`, `describe_scene`) start common jobs with the right workflow. Objects are best named by id (`#42`); a path like
`Workspace.Twin` is refused with `AMBIGUOUS_NAME` and a list of candidates when
two objects share that name.

### Plugins

Plugins are small Lua files that add buttons to Studio's **PLUGINS** tab.
They go in the `plugins` folder next to Studio, and two samples come with it
(**Random Colors** and **Stack Tower**). A plugin can use everything scripts
can, plus `plugin:Button(name, tooltip, function)` to add a button and
`Selection:Get()` / `Selection:Set({...})` for what you've picked:

```lua
plugin:Button("Paint Red", "Make the selected parts red", function()
    for _, part in ipairs(Selection:Get()) do
        part.Color = Color3.new(1, 0, 0)
    end
end)
```

Anything a plugin changes can be undone with Ctrl+Z. With a server,
**PLUGINS > Library** lets you install plugins other people published
(buying them first if they cost Bolts), add uploaded audio to your game, and
publish your own plugins.

Studio's **File** menu also has **Publish to Guts&Bolts...**. It puts your
game on the server so everyone can play it, and later **updates** it with
your new version.

### Move, Scale and Rotate (increments)

The **HOME** tab has Roblox Studio's increment boxes: **Move** (studs) and
**Rotate** (degrees). Scaling snaps to the Move step too. While you drag a
handle, a little label by the mouse shows how far you've gone (like *+4
studs* or *45°*). Untick an increment and that tool moves freely, like
Blender.

Right-click in the Explorer for **Insert Object** (every kind of object, with
a search box). **Lighting** and **StarterPlayer** are in the Explorer too.

**Handles, like Roblox.** Selected parts get a light blue box. **Move** has an
arrow on each of the six sides (along the part's own axes, or the world's with
*World* on): drag one and everything selected slides that way. **Scale** has an
orb on each side: drag one and only that side moves (hold **Ctrl** to grow both
sides). **Rotate** turns everything selected together. Click empty space and
drag to **box-select**.

**Unions** (MODEL tab > Solid Modeling, like Roblox): select parts and press
**Union** (Ctrl+Shift+G) to join them into one shape. **Negate** (Ctrl+Shift+N)
turns a part into a see-through pink "hole": union it with other parts and it
gets cut out of them. **Intersect** (Ctrl+Shift+I) keeps only where parts
overlap. **Separate** (Ctrl+Shift+U) gives back the parts a union was made from.

**Script Analysis** (VIEW tab, or View > Script Analysis) checks every script
without running anything: red errors are things Lua can't read (a missing
`end`), yellow warnings are names that aren't defined anywhere (usually a typo,
like `pirnt`). Click one to jump to it.

**AutoSave**: every 5 minutes Studio keeps a backup of unsaved work (your game
file isn't touched). If Studio crashes, it offers to open the backup next time.

### Controls

These match Roblox Studio. Press **F1** (or **View > Shortcuts**) in Studio
to see them all.

| Action | Input |
| --- | --- |
| Select / Move / Scale / Rotate tool | `Ctrl+1` / `Ctrl+2` / `Ctrl+3` / `Ctrl+4` |
| Local / world gizmo | `Ctrl+L` |
| Fly the camera | Hold right mouse + `W` `A` `S` `D`, `Q` / `E` down / up, `Shift` faster |
| Look / pan / zoom | Right-drag / middle-drag / wheel (Shift + middle-drag orbits) |
| Zoom to the selection (parts, models, tools) | `F` |
| Pick the part inside a Model | `Alt+click` |
| Pick several things | `Ctrl+click` or `Shift+click` |
| Select all / parent / children / nothing | `Ctrl+A` / `Ctrl+Up` / `Ctrl+Down` / `Esc` |
| Expand / collapse selected (everything inside) | `Ctrl+Right` / `Ctrl+Left` |
| Collapse the whole Explorer | `Ctrl+Shift+Left` |
| Search the Explorer | `Ctrl+Shift+X` |
| Insert Object | `Ctrl+I` |
| Rename / hide | `F2` / `H` |
| Group into a Model / ungroup | `Ctrl+G` / `Ctrl+U` |
| Lock / anchor | `Alt+L` / `Alt+A` |
| Copy / Cut / Paste / Paste into | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` / `Ctrl+Shift+V` |
| Duplicate / Delete | `Ctrl+D` / `Del` |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Y` |
| Save / Save as / Open / New | `Ctrl+S` / `Ctrl+Shift+S` / `Ctrl+O` / `Ctrl+N` |
| Play / Simulate / Stop | `F5` / `F8` / `Shift+F5` |
| Pause / step one frame (while testing) | `F6` / `F7` |
| Modeling mode on / off | `Tab` |

Copy, paste, duplicate, delete, hide, lock, anchor and the move tool all work
on everything you've selected at once.

**Snapping, the grid and collisions** (MODEL tab > Snap to Grid, or the
buttons in HOME > Edit):

- **Move** snaps moving (and resizing) to steps of however many studs you type,
  and the floor grid follows that size. **Rotate** snaps turning to steps of
  so many degrees. **Grid** shows or hides the floor grid.
- **Collisions**: parts you move stop flush against other parts instead of
  going through them, and can't be turned or resized into them. (Things
  already overlapping when you start can still move apart.)

## Scripting (Lua)

Scripts are written in Lua and work a lot like Roblox scripts. Put a Script
inside a part and `script.Parent` is that part:

```lua
-- Make this part a kill brick
script.Parent.Touched:Connect(function(hit)
    local humanoid = hit.Parent:FindFirstChild("Humanoid")
    if humanoid then
        humanoid.Health = 0
    end
end)
```

Scripts can be written in plain Lua or in **Luau**: `x += 1` (and `-=`, `*=`, `..=` ...),
`continue`, type annotations (`local n: number`, `type Point = {...}`) and `` `Hi {name}` ``
strings all work, in any script, and error messages keep the right line numbers.

What you can use:

- **Objects:** `workspace`, `script`, `Instance.new("Part" | "Ball" | "Cylinder" | "Model" | "PointLight" | "SpotLight", parent)`,
  `:FindFirstChild`, `:WaitForChild`, `:GetChildren`, `:GetDescendants`, `:Clone`, `:Destroy`, `:IsA`
- **Part properties:** `Name`, `Parent`, `Position`, `Orientation`, `Size`, `CFrame`, `Color`, `Transparency`,
  `Material`, `Shape`, `Anchored`, `CanCollide`, `CollisionGroup`, `Velocity`
- **Values:** `Vector3.new`, `Color3.new` / `fromRGB` / `fromHSV` / `fromHex`, `CFrame.new` / `Angles` / `lookAt`
- **Time:** `wait(seconds)`, `spawn(fn)`, `delay(seconds, fn)`, `time()`, `tick()`
- **Keys:** `ContextActionService:BindAction(name, fn, touchButton, Enum.KeyCode.E, ...)` (see *Key bindings* below)
- **Events:** `part.Touched`, `part.Clicked`, `clickDetector.MouseClick` (see *ClickDetectors* below), `RunService.Heartbeat`, `UserInputService.InputBegan` / `InputEnded`,
  `humanoid.Died`, `Players.PlayerAdded` / `PlayerRemoving`, `player.CharacterAdded` / `CharacterRemoving`
  (fired on every spawn and respawn), each with `:Connect`, `:Once` and `:Wait`
- **Players:** `game.Players.LocalPlayer.Character`, `Players:GetPlayers()`, `humanoid.Health` / `WalkSpeed` /
  `JumpPower` / `:TakeDamage(n)`, `character:BreakJoints()`
- **Teams:** `player.Team`, `player.TeamColor`, `player.Neutral` (setting one keeps the others right),
  `Teams:GetTeams()`. A **SpawnLocation** has `Neutral`, `TeamColor` and `AllowTeamChangeOnTouch`: players
  spawn on their team's SpawnLocations (or Neutral ones), and touching one with `AllowTeamChangeOnTouch`
  puts you on its team
- **Models:** `model.PrimaryPart`, `model:GetPivot()` / `:PivotTo(cframe)` (they go by the PrimaryPart),
  `:SetPrimaryPartCFrame(cframe)` / `:GetPrimaryPartCFrame()`
- **Camera:** `workspace.CurrentCamera` with `CFrame`, `Focus`, `FieldOfView`, `CameraType`
  (`Enum.CameraType.Custom` / `Scriptable` / `Fixed`), `CameraSubject`, `ViewportSize`,
  `:WorldToViewportPoint` / `:WorldToScreenPoint` and `:ViewportPointToRay` / `:ScreenPointToRay`;
  `player.CameraMode` (`Enum.CameraMode.Classic` / `LockFirstPerson`), `player.CameraMinZoomDistance` and
  `CameraMaxZoomDistance` (see *Scripting the camera* below)
- **Lighting:** `Lighting.ClockTime`, `Brightness`, `FogEnabled`, `FogColor`, `Ambient`, ...
- **Screen text:** `Gui.Label("Score", "Score: 5")`, `Gui.Message("You win!", 3)`
- **Mayhem:** `Explode(position, radius, power, options)` (see *Explosions* below), `Effects.Blood(pos, amount)`, `Effects.Oil(...)`, `Effects.Gibs(...)`,
  `Effects.Sparks(...)`
- **Physics:** `part:ApplyImpulse(v)`, `AssemblyLinearVelocity`, `AssemblyAngularVelocity`, `Density`,
  `Friction`, `Elasticity`, `part.CustomPhysicalProperties = PhysicalProperties.new(density, friction,
  elasticity, frictionWeight, elasticityWeight)` (or `PhysicalProperties.new(Enum.Material.Ice)`; `nil` goes back
  to the material's), `part.CurrentPhysicalProperties`, and constraint properties such as `rope.Length` or
  `hinge.AngularVelocity`
- **Collision groups:** `PhysicsService:RegisterCollisionGroup`, `CollisionGroupSetCollidable` and
  `part.CollisionGroup` (see *Collision groups* below)
- **Sound:** `Sounds.Play("coin", position)` (built in: jump, coin, oof, explosion, splat, click, hit, win,
  boing, spawn) or a Sound object with `:Play()` / `:Stop()`
- **Attributes:** `obj:SetAttribute("Damage", 25)`, `obj:GetAttribute("Damage")`, `obj:GetAttributes()`,
  `obj.AttributeChanged`, `obj:GetAttributeChangedSignal("Damage")` (you can also add them in the
  **Attributes** section of the Properties panel)
- **Tags:** `obj:AddTag("Lava")`, `:HasTag`, `:RemoveTag`, `:GetTags`, and `CollectionService:GetTagged("Lava")`,
  `:GetInstanceAddedSignal("Lava")` / `:GetInstanceRemovedSignal` (or add tags in the Properties panel)
- **Modules:** `require(workspace.MyModule)` runs a ModuleScript once and hands back what it returns
  (a module can `wait()` while it starts; anyone else asking waits for it). On the server,
  `require(1234)` gets model 1234 from the Library and requires its ModuleScript named
  **MainModule**, like Roblox (public models, or your own private ones)
- **Game rules:** `workspace.Gravity`, `workspace.DeathStyle = "Classic" | "Ragdoll"`, `workspace.Gore = "Off" | "Oil" | "Blood"`
- **Rays:** `workspace:Raycast(origin, direction, params)` gives back `Instance`, `Position`, `Normal` and
  `Distance` (or nil). `RaycastParams.new()` with `FilterDescendantsInstances` and `FilterType`
  (`Exclude` / `Include`), `CollisionGroup` (the ray skips parts that group passes through) and
  `RespectCanCollide`. The old `Ray.new` + `workspace:FindPartOnRay(ray, ignore)` works too, and
  `workspace:GetPartBoundsInRadius(position, radius)` finds parts near a point
- **Tweens:** `TweenService:Create(part, TweenInfo.new(2, Enum.EasingStyle.Bounce), { Position = ... }):Play()`
  smoothly changes numbers, `Vector3`, `Color3`, `CFrame`, `UDim2` and `Vector2` properties. Every easing
  style (Linear, Quad, Cubic, Quart, Quint, Sine, Exponential, Circular, Back, Elastic, Bounce), In / Out /
  InOut, repeats, reverses, delays, `:Pause()`, `:Cancel()` and the `Completed` event
- **JSON:** `HttpService:JSONEncode(t)` / `:JSONDecode(text)` / `:GenerateGUID()` (games can't reach other
  websites)
- **Randomness:** `Random.new(seed)` with `:NextInteger`, `:NextNumber`, `:NextUnitVector`, `:Shuffle`
  (the same seed gives the same numbers), and `math.noise(x, y, z)` for smooth hills and wobbles
- **Scripts talking to each other:** `Instance.new("BindableEvent")` (`.Event:Connect`, `:Fire`) and
  `Instance.new("BindableFunction")` (`.OnInvoke`, `:Invoke`)
- **Storage:** `game.ReplicatedStorage` / `game.ServerStorage`: hidden folders for things you clone
  later (Roblox files keep theirs there too). `Debris:AddItem(obj, seconds)` throws something away later

`script:Destroy()` or `script.Disabled = true` stops a script (and all of its
events), just like in Roblox.

A script that loops forever without `wait()` is stopped after 5 seconds with a
friendly error, so it can't freeze your game. Scripts can't touch files on
your computer.

### Visual Scripting (blocks)

Don't want to type code? Open **VIEW > Blocks** (or View > Visual Scripting),
select a part and press **New block script**. Then add blocks from the left and
drag from a block's dot on the right to the block that comes next:

- **Events** start a chain: When the game starts, When touched, When clicked,
  When the prompt is used, When a player joins, Every few seconds.
- **Actions** change things: set colour, see-through, any property, move, destroy,
  copy, play a sound, print, wait.
- **Players**: leaderboard stats, give points, hurt or teleport the player.
- **Control**: If it's a player, If it has a tag, If an attribute is, If the player
  has points, If lucky, Repeat, and Only every few seconds (so one touch doesn't
  count 50 times).

A block's object can be "This part" (the script's parent), "The part that
touched", "The player's character" or a path like `Workspace.Door`.

The blocks turn into normal Lua, shown on the right, so you can see how they work.
A block script is a normal Script: the blocks are kept in a comment at the top of
its code. Changing that code by hand is fine, but changing the blocks again writes
over it (the window warns you, and can turn it into a plain script instead).

## Decals (pictures on parts)

A **Decal** puts a picture on one side of a part. Insert one inside a part
(*Insert Object > Decal*), then set it up in Properties:

- **Texture:** the picture. It can be a `.png` / `.jpg` in the games folder
  (like `pics/logo.png`), a full path, or `gb:<id>` for one uploaded on the
  site's Create page. **Browse...** picks a file.
- **Face:** which side: Front, Back, Left, Right, Top or Bottom. The picture
  stretches to fill that side.
- **Color3** tints it (white shows it as it is), and **Transparency** fades it.
  See-through parts of a PNG stay see-through.

Scripts can change them too:

```lua
local d = Instance.new("Decal")
d.Texture = "gb:decal-1234abcd"
d.Face = Enum.NormalId.Top
d.Parent = workspace.Sign
```

## Tools (swords, bats, anything you can hold)

A **Tool** is something a character carries, like a Roblox Tool. Insert one
with *Insert Object > Tool*: it comes with a part called **Handle**. That's
what the character holds, with its long side (Y) pointing forward out of
the fist. Add more parts inside the tool (a blade, a guard...) and they
come along.

- **Picking up:** walk into a tool's Handle and it goes in your backpack.
- **Hotbar:** your tools show along the bottom of the screen. Press **1-9**
  (or click / tap a slot) to hold one; press the same number again to put it
  away. **Backspace** drops the one you're holding (unless *CanBeDropped* is
  off).
- **In StarterPack:** tick this and everyone spawns with that tool (and gets
  it again after respawning, like Roblox).
- **Using it:** clicking while you hold a tool swings your arm and fires the
  tool's `Activated` event.

```lua
-- A script inside the tool
local tool = script.Parent
tool.Activated:Connect(function()
    print("Swing!")
end)
tool.Handle.Touched:Connect(function(hit)
    -- the held Handle touching something (only while you hold it)
end)
tool.Equipped:Connect(function() end)      -- also Unequipped, Deactivated
```

Also: `tool.Enabled`, `tool.ToolTip`, `tool.CanBeDropped`, `tool.GripPos`,
`humanoid:EquipTool(tool)`, `humanoid:UnequipTools()` and
`Players.LocalPlayer.Backpack` (parent a tool there to give it). Try the
**Bat** in *Demolition Yard*. Tools work for everyone in an online game,
not just the host: the real tools (and their scripts) live on the game's
server, so `tool.Activated` and `Handle.Touched` fire there for whoever is
holding it, and everyone sees what everyone else is holding.

Tools are held in the right hand the Roblox way: a `RightGrip` weld joins the
hand to the `Handle`, placed by the tool's **Grip** (`GripPos`, `GripForward`,
`GripRight`, `GripUp`, also in the Properties panel). Roblox gear you import
(like the classic LinkedSword) is held exactly as it was there.

### Gear

Like Roblox's old gear: staff open **File > Make Gear** in Studio. Start
from a ready-made one (**Classic Sword**, **Rocket Launcher**, **Speed Coil**,
**Gravity Coil** or **Bomb**, also in the Toolbox for everyone) or your own
Tool, change it, press Play to try it (it's already in your backpack: press 1),
then **Publish as Gear**. (Or select a Tool and use **File > Publish Selection
to Library** with **Sell it in the catalog as Gear** ticked.) It shows up in the catalog under **Gear**. Buy it, then press
**Equip** (on the item, or on the app's Avatar page under *Gear*). You can have
up to 4 equipped. You get your equipped gear in your backpack (and again every
time you respawn) in games whose creator ticked **Allow gear** in the game's
settings on the website. Gear is off unless a game turns it on.

## Game UI (ScreenGui, buttons, labels)

Games can have their own on-screen UI, like Roblox's: menus, shop buttons, coin
counters and title screens.

- **Insert** (Ctrl+I) a **ScreenGui**. It goes in the **StarterGui** folder.
  Then insert **Frame**, **TextLabel**, **TextButton**, **ImageLabel** or
  **ImageButton**, **TextBox** or **ScrollingFrame** into it. Put a
  **UICorner** inside one to round its corners, or a **UIStroke** to give it
  an outline.
- The UI shows in the viewport while you build. Click it to pick it, drag it to
  move it, and drag the blue corner to resize it. Everything else is in
  **Properties**.
- Sizes and positions are **UDim2**, like Roblox: a fraction of the parent plus
  pixels. `UDim2.new(0.5, 0, 1, -60)` means "halfway across, 60 pixels up from
  the bottom". **AnchorPoint** picks which point of it sits there (0.5, 0.5 is
  the middle).

Scripts use the same names as Roblox:

```lua
local gui = Instance.new("ScreenGui")
gui.Parent = game:GetService("StarterGui")

local button = Instance.new("TextButton")
button.Size = UDim2.fromOffset(200, 50)
button.Position = UDim2.new(0.5, 0, 1, -40)
button.AnchorPoint = Vector2.new(0.5, 1)
button.Text = "Clicks: 0"
button.BackgroundColor3 = Color3.fromRGB(40, 180, 80)
button.Parent = gui
Instance.new("UICorner", button)

local clicks = 0
button.MouseButton1Click:Connect(function()
    clicks = clicks + 1
    button.Text = "Clicks: " .. clicks
end)
```

Buttons have `MouseButton1Click` (also called `Activated`), `MouseEnter` and
`MouseLeave`. Text has `TextScaled`, `TextWrapped`, alignment, an outline
(`TextStrokeTransparency`) and bold text (`label.Bold = true`, or a bold font such as `Enum.Font.SourceSansBold`).
`player.PlayerGui` and `game.StarterGui` are the same folder.

**Typing, scrolling and lining things up:**

- **TextBox**: a box players type in. `PlaceholderText` shows greyed out while
  it's empty. `FocusLost` fires when they press Enter (`enterPressed` is true)
  or click away. Scripts can also call `box:CaptureFocus()` and
  `box:ReleaseFocus()`. While someone's typing, their keys don't move the
  character.
- **ScrollingFrame**: holds more than fits. `CanvasSize` is how big the inside
  is, or set `AutomaticCanvasSize` to grow it to fit. Scroll with the mouse
  wheel, drag the bar, or drag with a finger on phones. `CanvasPosition` is how
  far it's scrolled.
- **UIListLayout**: put it next to some objects and they line up one after
  another, down (or across with `FillDirection`), `Padding` apart. Lower
  `LayoutOrder` comes first.
- **UIGridLayout**: the same, but in a grid of `CellSize` cells.
- **UIPadding**: keeps what's inside away from the edges.

```lua
local box = Instance.new("TextBox")
box.Size = UDim2.fromOffset(300, 40)
box.PlaceholderText = "Type your name"
box.Parent = gui
box.FocusLost:Connect(function(enterPressed)
    if enterPressed then print("Hello, " .. box.Text) end
end)

local shop = Instance.new("ScrollingFrame")
shop.Size = UDim2.fromOffset(260, 300)
shop.AutomaticCanvasSize = Enum.AutomaticSize.Y
shop.Parent = gui
Instance.new("UIListLayout", shop).Padding = UDim.new(0, 6)
for i = 1, 20 do
    local item = Instance.new("TextButton")
    item.Size = UDim2.new(1, -12, 0, 36)
    item.Text = "Item " .. i
    item.Parent = shop
end
```

**UI on parts:**

- **BillboardGui**: put it inside a part and it floats over it, always facing
  you: name signs, health bars, "Press E" hints. Its `Size` in pixels stays the
  same on screen; in scale it's studs, so it grows as you get closer.
  `StudsOffset` lifts it (0, 2, 0 is two studs up).
- **SurfaceGui**: put it inside a part and it's painted on one side (`Face`):
  shop signs, scoreboards, buttons on walls. Buttons on it can be clicked.
  `PixelsPerStud` (50) sets how big things on it look.
- Both hide when something's in front of them (`AlwaysOnTop` shows them
  anyway) and can stop showing past `MaxDistance`. In a script, `Adornee`
  puts one on any part without moving it there.

```lua
local sign = Instance.new("BillboardGui")
sign.Size = UDim2.fromOffset(160, 40)
sign.StudsOffset = Vector3.new(0, 2, 0)
sign.Parent = workspace.Shopkeeper.Head
local label = Instance.new("TextLabel")
label.Size = UDim2.fromScale(1, 1)
label.Text = "Shopkeeper"
label.TextScaled = true
label.Parent = sign
```

**Gradients, scaling, shapes and 3D previews:**

- **UIGradient**: put it inside a Frame, label, button or picture and its
  background (and its text and picture) fades from colour to colour. `Color`
  is a `ColorSequence`, `Transparency` a `NumberSequence`, `Rotation` turns it
  (0 = left to right, 90 = top to bottom) and `Offset` slides it along.
- **UIScale**: makes the object it's in, and everything inside it (text,
  corners and outlines too), `Scale` times bigger. It grows around its
  `AnchorPoint`. In a ScreenGui it scales everything on the screen.
- **UIAspectRatioConstraint**: keeps the object it's in the same shape on every
  screen. `AspectRatio` is width / height. `AspectType` `FitWithinMaxSize`
  makes it as big as fits in its `Size`; `ScaleWithParentSize` keeps its size
  along `DominantAxis` (`Width` or `Height`) and works out the other side.
- **ViewportFrame**: shows the parts and models inside it as a little 3D
  picture: an item in a shop, a character preview, a spinning trophy. Put a
  **Camera** in it and make it the frame's `CurrentCamera` (with no camera it
  frames what's inside by itself). `Ambient`, `LightColor` and
  `LightDirection` light it. The parts in it aren't in the world: they don't
  fall or collide and rays don't hit them. A **WorldModel** inside it can hold
  them too. The picture is only drawn while the frame is on screen, and only
  again when something in it changes.

```lua
local card = Instance.new("Frame")
card.Size = UDim2.fromScale(0.3, 0.3)
card.Parent = gui
local fade = Instance.new("UIGradient")
fade.Color = ColorSequence.new(Color3.fromRGB(255, 80, 0), Color3.fromRGB(120, 0, 255))
fade.Rotation = 90
fade.Parent = card
Instance.new("UIAspectRatioConstraint", card).AspectRatio = 1.5

local preview = Instance.new("ViewportFrame")
preview.Size = UDim2.fromScale(1, 1)
preview.BackgroundTransparency = 1
preview.Parent = card
local sword = workspace.Sword:Clone()
sword.Parent = preview
local cam = Instance.new("Camera")
cam.CFrame = CFrame.new(Vector3.new(0, 2, -6), sword:GetPivot().Position)
cam.Parent = preview
preview.CurrentCamera = cam
```

In multiplayer, everyone sees the same UI. When anyone clicks a button, the
host's scripts hear about it, and the same goes for what they type in a TextBox
(when they press Enter or click away). Roblox files keep their UI: ScreenGuis in a
place's StarterGui are imported, and exported back to Roblox (gradients, UIScales,
aspect ratio constraints and ViewportFrames with their cameras too).

## Animations (the Animation Editor)

Like Roblox's: make an animation in Studio, then play it from a script.

1. **AVATAR tab > Rig Builder** puts a dummy character in the world (or use
   any Model made of parts, like your own character).
2. **AVATAR tab > Animation Editor** opens the timeline along the bottom.
   Select the rig and press **Animate**, then **New** to make an Animation
   (it's saved inside the rig).
3. Move the red playhead to a time, click a body part, and turn it with the
   **Rotate** tool (or move it with Move). That adds a keyframe there. The
   handles sit on the joint, so arms turn at the shoulder, legs at the hip and
   heads at the neck. You can also type exact numbers in the panel on the right.
4. **Play** shows it moving. Drag keyframes to change their timing,
   right-click one for its **easing** (Linear, Constant, Cubic, Elastic,
   Bounce; In / Out / InOut), to name it, copy it or delete it. Tick **Loop**
   for things like idles and dances, and pick a **Priority** (Core, Idle,
   Movement, Action: higher wins when two animations move the same part).

While the editor is open the rig is only *shown* posed: saving, undo and
Play all see it exactly as you built it. Close the editor (or press
**Done**) to put it back.

Arms and legs hang off the torso like Roblox's R6 (and R15) characters, so
turning the Torso takes the arms and head with it. For your own models, a
part inside another part follows it, and an **Attachment named `Pivot`**
inside a part sets where it bends.

Play an animation from a script:

```lua
-- On your character (like a Roblox LocalScript):
local character = game.Players.LocalPlayer.Character
local humanoid = character:WaitForChild("Humanoid")
local animator = humanoid:WaitForChild("Animator")
local wave = animator:LoadAnimation(workspace.Wave)   -- an Animation object
wave:Play()                 -- :Play(fadeTime, weight, speed)
wave.Stopped:Wait()

-- On any rig (an NPC, a door, a machine): model:LoadAnimation(animation)
local dance = workspace.Rig:LoadAnimation(workspace.Rig.Dance)
dance.Looped = true
dance:Play()
dance.KeyframeReached:Connect(function(name) print("reached", name) end)
```

An AnimationTrack has `Play`, `Stop(fadeTime)`, `AdjustSpeed`,
`AdjustWeight`, `GetTimeOfKeyframe`, and `IsPlaying`, `Length`, `Looped`,
`Speed`, `TimePosition`, `Priority`, plus the `Stopped`, `Ended`, `DidLoop`
and `KeyframeReached` events. Animations play on top of walking: an
animation that moves the arms takes over the arms, and the legs keep walking.

### LocalScripts and RemoteEvents

In a game with other people, a **Script** runs on the server (the host, or a game
server machine) and a **LocalScript** runs on each player's own computer. Use
LocalScripts for things only that player sees or does: their UI, their keys,
their camera. Players never get the code inside your Scripts, so secrets stay
on the server.

They talk with a **RemoteEvent** (Insert Object > Scripts > RemoteEvent puts one
in ReplicatedStorage, where both sides can find it):

```lua
-- LocalScript (in a button): tell the server
local buy = game.ReplicatedStorage.BuySword
script.Parent.MouseButton1Click:Connect(function()
    buy:FireServer("Sword")
end)

-- Script: the server checks and answers
local buy = game.ReplicatedStorage.BuySword
buy.OnServerEvent:Connect(function(player, item)
    print(player.Name .. " wants " .. item)
    buy:FireClient(player, "You bought " .. item)   -- or buy:FireAllClients(...)
end)

-- back in the LocalScript
buy.OnClientEvent:Connect(function(message) print(message) end)
```

A **RemoteFunction** asks and waits for an answer:
`local coins = getCoins:InvokeServer()` in a LocalScript, and
`getCoins.OnServerInvoke = function(player) return 10 end` in a Script.

- You can send numbers, text, true/false, tables, objects, players, Vector3,
  Vector2, Color3, CFrame and UDim2 (up to 64 KB at a time). Functions arrive as nil.
- The first thing OnServerEvent gets is always the player who sent it. Don't trust
  what players send: check it in the Script before giving them anything.
- Playing alone or hosting from the app, you're the server and a player at once,
  so both kinds of script run on your computer.
- DataStores only work in Scripts. InvokeClient isn't supported (a player could
  freeze the server by never answering); use a RemoteEvent for that instead.

### ProximityPrompts

A **ProximityPrompt** inside a part shows a little "**E** Open" card when a
player walks up to it (Insert Object > Characters & Tools > ProximityPrompt, with
the part selected). Pressing the key, or clicking or tapping the card, fires
`Triggered` with the player who did it:

```lua
local prompt = script.Parent.ProximityPrompt
prompt.ActionText = "Open"
prompt.ObjectText = "Door"
prompt.Triggered:Connect(function(player)
    script.Parent.Transparency = 0.8
    script.Parent.CanCollide = false
end)
```

- `HoldDuration` makes players hold the key while a ring fills up
  (`PromptButtonHoldBegan` / `PromptButtonHoldEnded` tell you when they start and stop).
- `KeyboardKeyCode` picks the key (`Enum.KeyCode.F`), `MaxActivationDistance` how
  close they need to be, and `RequiresLineOfSight` hides it behind walls.
- `ProximityPromptService.PromptTriggered:Connect(function(prompt, player) ... end)`
  hears every prompt in the game at once.
- In a multiplayer game the server checks the player really is close before
  `Triggered` fires.

### ClickDetectors

A **ClickDetector** inside a part lets players click it: buttons, levers,
"click to buy" signs (Insert Object > Characters & Tools > ClickDetector, with the
part selected). Put one inside a model and every part in the model is clickable.
The mouse pointer turns into a hand over it, and clicking fires `MouseClick` with
the player who clicked:

```lua
local button = script.Parent
button.ClickDetector.MouseClick:Connect(function(player)
    button.Color = Color3.fromRGB(0, 200, 0)
    print(player.Name .. " pressed the button")
end)
```

- `RightMouseClick`, `MouseHoverEnter` and `MouseHoverLeave` work the same way
  (each passes the player).
- `MaxActivationDistance` is how close the player's character has to be (16, the
  same as Roblox's 32 studs; 0 = it can't be clicked). `CursorIcon` is an optional
  picture to show instead of the hand.
- Server Scripts hear clicks in multiplayer games too (the server checks the
  player really is close enough), and LocalScripts hear their own player's.
- On a touch screen, tap the part. With a controller, aim the middle of the screen
  at it and press RT.
- Like Roblox, it doesn't react while the player is holding a tool (the click
  uses the tool). `part.Clicked` still works for any part, with or without one.
- Roblox places (.rbxl / .rbxm) keep their ClickDetectors when you import them.

### Highlights

A **Highlight** colours a part or a whole model and draws an outline round it,
like Roblox's. Put one inside what you want to stand out (Insert Object >
Effects & Lights > Highlight, with the part or model selected), or make one in a
script:

```lua
local h = Instance.new("Highlight")
h.FillColor = Color3.fromRGB(255, 60, 60)
h.OutlineColor = Color3.new(1, 1, 1)
h.FillTransparency = 0.5
h.Parent = player.Character      -- see them through walls
```

- `DepthMode` is `Enum.HighlightDepthMode.AlwaysOnTop` (seen through walls, the
  default) or `Occluded` (only where you can see it).
- `FillTransparency = 1` gives just the outline; `OutlineTransparency = 1` just the fill.
- `Adornee` points it at something else, so one Highlight in a LocalScript can
  follow whatever the mouse is over.
- Up to 31 show at once. Highlights put on characters by the server show for
  everyone in a multiplayer game.

### Trails and Beams

Both are ribbons that run between two **Attachments** (Insert Object > Effects &
Lights). A **Trail** leaves a ribbon behind as its attachments move, fading out
over `Lifetime` seconds: put two attachments in a sword's blade or a car's back and
it streaks. A **Beam** joins two attachments right now: lasers, zip lines, light
rays. `CurveSize0` / `CurveSize1` bend it along each attachment's X axis.

```lua
local trail = Instance.new("Trail")
trail.Attachment0 = blade.TipAttachment
trail.Attachment1 = blade.BaseAttachment
trail.Color = ColorSequence.new(Color3.new(1, 0.8, 0), Color3.new(1, 0, 0))
trail.Transparency = NumberSequence.new(0, 1)      -- fades out as it gets older
trail.Lifetime = 0.5
trail.Parent = blade

local beam = Instance.new("Beam")
beam.Attachment0 = gun.Muzzle
beam.Attachment1 = target.Attachment
beam.Width0, beam.Width1 = 0.2, 0.2
beam.LightEmission = 1                            -- glows
beam.Parent = gun
```

- `ColorSequence` and `NumberSequence` work like Roblox's: one value, a start and an
  end, or a list of `ColorSequenceKeypoint.new(time, color)` /
  `NumberSequenceKeypoint.new(time, value)`.
- Trails also have `WidthScale`, `MinLength`, `MaxLength` and `:Clear()`; Beams have
  `Width0`, `Width1`, `Segments`, `Texture`, `TextureSpeed` (scrolling) and `TextureMode`.
- `FaceCamera` keeps either one turned towards you.
- In multiplayer they show for everyone, including ones the server puts on characters.

### Particles: ParticleEmitter, Fire, Smoke and Sparkles

Put one inside a part (Insert Object > Particles, with the part selected) and it
just works, in Studio while you build and in the game. A **ParticleEmitter** sends
particles out of anywhere inside the part, or out of one point when it's in an
**Attachment**. **Fire**, **Smoke** and **Sparkles** are the classic ready-made ones.

```lua
local embers = Instance.new("ParticleEmitter")
embers.Rate = 30                                   -- particles a second
embers.Lifetime = NumberRange.new(1, 2)            -- seconds (each gets a random one)
embers.Speed = NumberRange.new(4, 8)
embers.SpreadAngle = Vector2.new(20, 20)           -- degrees each way
embers.Color = ColorSequence.new(Color3.fromRGB(255, 200, 60), Color3.fromRGB(255, 40, 0))
embers.Size = NumberSequence.new(0.4, 0)           -- shrinks away
embers.Transparency = NumberSequence.new(0, 1)
embers.LightEmission = 1                           -- glows
embers.Acceleration = Vector3.new(0, 3, 0)
embers.Parent = torch

embers:Emit(50)    -- 50 at once (an explosion, a hit)
embers:Clear()     -- all of its particles vanish

local fire = Instance.new("Fire")                  -- Color, SecondaryColor, Size, Heat
fire.Parent = campfire
local smoke = Instance.new("Smoke")                -- Color, Opacity, RiseVelocity, Size
smoke.Parent = chimney
local sparkles = Instance.new("Sparkles")          -- SparkleColor
sparkles.Parent = coin
```

- A ParticleEmitter also has `Enabled`, `Drag` (how quickly they slow down),
  `EmissionDirection` (`Enum.NormalId.Top` and so on, turned with the part),
  `Rotation` / `RotSpeed` (NumberRanges, degrees), `LockedToPart` (they move with
  the part) and `Texture`: a built-in look (`""` a soft glow, `"smoke"`, `"fire"`,
  `"sparkle"`) or your own picture (`gb:<id>` or a file in the games folder).
- `NumberRange.new(min, max)` is new for these (`NumberRange.new(5)` is 5 to 5);
  a plain number works anywhere a NumberRange or NumberSequence is wanted.
- They're only for looks: every player's computer makes its own particles from the
  emitter's settings, so they show for everyone in multiplayer (and `:Emit` /
  `:Clear` from the server happen on everyone's screen) without slowing the game down.
- At most 1500 particles from one emitter and 6000 in all are out at once (new
  ones wait until some fade), so a huge `Rate` or `Emit(1000000)` can't freeze anything.
- Saved in your game, editable in Properties, and they come across from imported
  Roblox files (and go back out when you export).

### Your character's moves (the Animate script)

Every character gets a 2011-style **Animate** script: idle, walk, run, jump,
fall, climb, sit and holding a tool. To change a move, put an Animation in the
matching value of the Animate script (for example `Animate.walk.WalkAnim.AnimationId`):
your clip plays instead of the built-in one.

### Emotes

Everyone has the emotes **dance**, **dance2**, **dance3**, **wave**, **point**,
**cheer** and **laugh**: type `/e dance` (or `/e wave`, ...) in the chat, or press
**B** for the **emote menu** (on a phone, tap the smiley above the jump button) and
click one. Everyone in the server sees it. Moving or jumping stops it; dances keep
going until you do. Emotes are real keyframed animations (the same clips as the
Animation Editor's), played on the character's Animator as AnimationTracks, so a
script can find them with `animator:GetPlayingAnimationTracks()`.

**From the catalog:** the catalog sells **Emotes** and **Animation Packs** (the
*Emotes* group). An animation pack changes how you stand, walk, run, jump and fall.
On the website's **Avatar** page (or the app's Avatar page, under *Emotes*) put up
to 8 emotes in your emote menu's slots and pick one pack. They come with you to
every game: your emotes fill the menu (slots you leave empty show the built-in
ones), and `/e` works with their names too (`/e jumpingjacks`). The ones there to
start with:

| Item | Kind | Price |
|---|---|---|
| Salute, Shrug, Robot | Emote | Free |
| Jumping Jacks | Emote | 15 Bolts |
| Sprinkler | Emote | 25 Bolts |
| Bouncy Animation Pack | Animation Pack | Free |
| Robot Animation Pack | Animation Pack | 40 Bolts |

Only Guts&Bolts staff make emotes and packs. Their catalog pictures are real
renders of the character mid-move, made by Studio: `GutsAndBolts --emote-pictures <dir>`,
then `python3 tools/make_emotes.py --pictures <dir>` (the starter moves themselves
are in `tools/make_emotes.py`).

**For scripts:**

```lua
humanoid:PlayEmote("cheer")        -- true if it played (needs to be standing on the ground)
-- Turn the emote menu off in your game (emotes from scripts still work):
game:GetService("StarterGui"):SetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu, false)
-- Open or close it yourself:
game:GetService("GuiService"):SetEmotesMenuOpen(true)
print(game:GetService("GuiService"):GetEmotesMenuOpen())
```

`humanoid.Jump = true` makes your own character jump, and `humanoid:Move(direction)`
walks it that way until you call it with `Vector3.new()` (keys pushed win).
`SetCoreGuiEnabled` remembers the other types (PlayerList, Health, Backpack,
Chat, ...) and `GetCoreGuiEnabled` reads them back, but only `EmotesMenu` hides
anything so far.

`humanoid.PlatformStand = true` makes the character go limp and fall over,
like 2011 Roblox, and hard hits can trip or fling you. `humanoid:ChangeState`
and `humanoid:GetState()` work too.

## Leaderboard, checkpoints and saved data

The player list folds away like old Roblox's: press **Tab**, or click the little
arrow on its title bar. Click someone's name on it to **Add Friend** or
**Follow** them right there in the game (following is one way, no asking; your
followers show on your profile).

**Leaderstats**, the Roblox way: put a folder called `leaderstats` inside a
player, with IntValues (or other values) in it, and they show as columns on
the player list, for everyone in an online game too.

```lua
local Players = game:GetService("Players")
local function setup(player)
    local stats = Instance.new("Folder")
    stats.Name = "leaderstats"
    stats.Parent = player
    local coins = Instance.new("IntValue")
    coins.Name = "Coins"
    coins.Parent = stats
end
setup(Players.LocalPlayer)
Players.PlayerAdded:Connect(setup)          -- people joining an online game
-- later: player.leaderstats.Coins.Value = player.leaderstats.Coins.Value + 1
```

Value objects (**IntValue, NumberValue, StringValue, BoolValue**) can also be
added in Studio (*Insert Object*). Scripts use `.Value` and `.Changed`.

**Checkpoints:** add the ready-made *Checkpoint* pad from the Toolbox (or
name any part `Checkpoint`). Touching it makes it your respawn point. Scripts
can also set `player.RespawnLocation = somePart`.

**Saved data** (`DataStoreService`) keeps things between visits:

```lua
local store = game:GetService("DataStoreService"):GetDataStore("Stats")
local visits = store:IncrementAsync("visits_" .. player.UserId, 1)
store:SetAsync("best", { score = 42 })
print(store:GetAsync("best").score)
```

It also has `UpdateAsync` and `RemoveAsync`. Where the data lives depends on
who runs the game:

- **On a game server machine, or when you (the game's creator) host it:** on
  the Guts&Bolts server. Every server of the game sees the same data, so coins
  saved in one server are there when you join another.
- **Anyone else hosting, or playing a game file by yourself:** in that
  computer's account folder (`savedata`). Other people's computers can't write
  your game's online data, so nobody can hand themselves free coins.

Like Roblox, `GetAsync` can answer from what it read in the last few seconds,
and `IncrementAsync` adds on the server in one step, so two servers adding at
the same time both count. Limits: names and keys up to 100 letters, 256 KB per
value, 100,000 keys per game.

### Talking between servers (MessagingService)

`MessagingService` sends a message to **every running server of the same
game**, the one that sent it included. Other games never see it.

```lua
local MessagingService = game:GetService("MessagingService")

local conn = MessagingService:SubscribeAsync("Announcements", function(msg)
    print("Got", msg.Data, "sent at", msg.Sent)   -- Sent is unix time
end)
MessagingService:PublishAsync("Announcements", { text = "Double coins!", minutes = 10 })
-- later: conn:Disconnect()
```

The message can be a string, number, boolean or table (anything that turns into
JSON). `PublishAsync` waits until it's sent and raises a clear error when it
can't be. Limits, like Roblox's:

- **1 KB per message** (after it's turned into JSON).
- **Topic names up to 80 letters.**
- **Per server, a minute:** 150 + 60 per player messages sent, and
  20 + 8 per player topics subscribed at once.

How it travels depends on who runs the game, the same way as saved data:

- **On a game server machine, or when you host it online:** through the
  Guts&Bolts server, which passes it to each server of the game (it checks the
  sender really runs a server of that game). Messages arrive about a second
  later, and are kept for 30 seconds.
- **Studio play tests and local multiplayer tests:** through a folder on this
  computer (`messaging` in your account folder), so two play tests of the same
  game on one computer hear each other right away.

### Sound effects

Most built-in sounds are made in code, but a few are recordings in
`assets/sounds` (built into the apps):

| File | Name in scripts | When it plays |
| --- | --- | --- |
| `jump.wav` | `"jump"` | the character jumps |
| `spawn.wav` | `"spawn"` | you arrive in a game (a splat) |
| `respawn.wav` | `"respawn"` | you come back after dying |

Swap in your own .wav files with the same names and rebuild.

### Sound groups and effects

A **SoundGroup** is a volume knob for many sounds at once (music, effects,
voices...). Put it in **SoundService** (Studio: *Insert > SoundGroup*), then
set each Sound's **SoundGroup** in Properties, or in a script:

```lua
local music = Instance.new("SoundGroup")
music.Volume = 0.5
music.Parent = game:GetService("SoundService")
sound.SoundGroup = music     -- the sound now plays at sound.Volume x 0.5
```

A group inside another group multiplies again (Master 0.5 > Music 0.8 > a Sound
at 1 plays at 0.4). Volumes go from 0 to 10.

**Effects** go inside a Sound or a SoundGroup (*Insert*, in the Sound section).
Effects in a group change every sound in it. Each has **Enabled** and
**Priority** (higher goes first; a Sound's own effects come before its group's):

| Effect | What it does | Settings |
| --- | --- | --- |
| ReverbSoundEffect | a room or hall around the sound | DecayTime, Density, Diffusion, DryLevel, WetLevel |
| EchoSoundEffect | repeats that fade away | Delay, Feedback, DryLevel, WetLevel |
| EqualizerSoundEffect | louder or quieter lows, middles and highs | LowGain, MidGain, HighGain |
| DistortionSoundEffect | crunchy, clipped sound | Level |
| PitchShiftSoundEffect | higher or lower without speeding up | Octave (0.5 to 2) |
| ChorusSoundEffect | a thicker, "many voices" sound | Depth, Mix, Rate |
| CompressorSoundEffect | evens out loud and quiet parts | Attack, GainMakeup, Ratio, Release, Threshold |
| FlangeSoundEffect | a sweeping jet-plane whoosh | Depth, Mix, Rate |
| TremoloSoundEffect | the volume pulses | Depth, Duty, Frequency |

They start with the same settings as Roblox's, are saved with the game,
copied to everyone in online games, and come along when you open or export a
Roblox file. Not done: the Compressor's **SideChain** (ducking one sound under
another) isn't there yet, and the Reverb is a simple made-up room rather than a
recorded one.

### Climbing and swimming

- **Climb:** walk into a **TrussPart** (Insert > TrussPart), or any part called
  *Ladder*, or one with the tag or attribute `Climbable`. You go up hand over
  hand. Let go of the keys to hang on, and press jump to leap off.
- **Swim:** a part called **Water** (Insert > Water: see-through, CanCollide
  off), or one with the tag or attribute `Water`, or deep enough real liquid.
  You float at the surface with your head out and move a bit slower. Hold
  **C** (or **Ctrl**) to dive, or swim forward while looking down; look up or
  hold jump to come back up (jump at the surface hops out). Underwater the
  view goes blue and hazy. No fall damage when you land in water.
- **Float or sink:** each part's **Density** (Properties, or `part.Density`)
  decides. Water is 1.3: lighter parts float (Wood 0.7 floats half under),
  heavier ones sink (Metal 3.0, Concrete 2.4).
- Scripts can check with `humanoid:GetState()`, which returns "Climbing",
  "Swimming", "Freefall", "Running" or "Dead".

### Render Distance

Settings > **Render Distance** (1 to 10) is how far away things are drawn.
Further things fade into the sky and aren't drawn, and water waves, liquid
taps, lights and effects out there rest until you come closer. Lower numbers
are faster; the quality presets set it (Low 4, Medium 6, High 8, Ultra 10 =
everything).

### Gutstober and timed items

October is **Gutstober**, Guts&Bolts' Halloween month. The website wears a
Halloween theme (orange and purple, a pumpkin and some bats) all month; anyone
can turn it off or back on in *Settings > Site theme* (it's remembered in that
browser).

**Timed items** go off sale at a set time. The item's creator or staff set
*Goes off sale* on the item's page (leave it empty to sell it for good). Before
then the item says *Off sale in N days*; after, it says *Off sale* and can't be
bought any more (people who have it keep it, and limiteds can still be resold).
Catalog items with "Pumpkin" in their name are Gutstober items: when the server
starts it gives each one an off-sale time of midnight UTC on November 1st (once;
staff can change it afterwards).

### Explosions

Explosions are more than a puff of fire:

- **The shockwave** races outwards (things far away get hit a moment later). It
  breaks joints, flings loose parts (light ones further than heavy ones), knocks
  people over, hurts them less the further away they are, and shakes the camera.
- **Fire and smoke:** a fireball that rolls up into billowing smoke, and fires
  that keep burning on the ground for a while (they hurt if you stand in them).
- **Mushroom clouds:** big blasts send up a column of smoke into a cap that
  rolls over on itself, with a ring of dust rushing out along the ground.
- **Water:** a blast in or near water blows a crater in the surface, throws up a
  column of spray and sends out rings of waves. A big enough bomb in big enough
  water makes a **tsunami**: a wall of water that rolls outwards, lifts boats,
  sweeps players and loose parts along and runs up onto the shore.

From a script:

```lua
-- position, radius, power (1 = normal), and options (all optional)
local hits = Explode(Vector3.new(0, 2, 0), 30, 2, {
    Fire = 10,             -- seconds fires keep burning (0 = none)
    Smoke = true,          -- smoke left hanging in the air
    MushroomCloud = true,  -- true / false (left out: only for really big blasts)
    Destroy = true,        -- rips anchored parts loose near the middle
    JointBreak = 0.5,      -- breaks joints out to half the radius
    Visible = true,        -- false: no fire or smoke, just the push
    Hurts = true,          -- harms characters
})
for _, h in ipairs(hits) do print(h[1].Name, h[2]) end   -- {part, distance}
```

Or the Roblox way: `Instance.new("Explosion")`, set `Position`, `BlastRadius`,
`BlastPressure` (500000 is normal), `DestroyJointRadiusPercent`, `Visible` (and
our extras `Smoke`, `Fire`, `Destroy`, `MushroomCloud`), connect `Hit`, then set
its `Parent` to make it go off.

Studio's premades have a **Time Bomb** (counts down, then blows), a **Nuke**
(with a mushroom cloud) and a **Depth Charge** (sinks in water and goes off
underwater).

In multiplayer the host works out what got hit, and everyone sees the same fire,
smoke, waves and shaking.

### Terrain

Hills, valleys, beaches and mountains, like Roblox's Terrain. Open the **TERRAIN**
tab in Studio:

- **Generate** makes rolling hills (each click makes different ones). The **Hills**
  slider is how bumpy (0 = gentle, 1 = mountains) and the box next to it is how big
  (256, 512 or 1024 studs across). It paints itself: sand down low, rock on steep
  slopes, snow on high tops, grass and dirt everywhere else.
- **Raise**, **Lower**, **Smooth**, **Flatten** and **Paint** are brushes: pick one and
  drag on the ground. **Size** and **Strength** are next to them, and so is the
  material Paint uses (Grass, Dirt, Sand, Rock, Snow or Mud). Esc puts the brush
  down. A whole stroke is one Ctrl+Z. No terrain yet? The first stroke makes a flat
  512-stud square of grass.
- **Clear** removes it all.

Characters walk on it (slopes steeper than about 53 degrees are too steep to walk
up), loose parts land and roll on it, raycasts and clicks hit it, it casts shadows,
pathfinding walks over it, and it's saved with the place and sent to everyone who
joins. It's a height map, so there are no caves or overhangs (build those with
parts). For water, put a Water part where you want the lake.

Scripts use `workspace.Terrain`:

```lua
local T = workspace.Terrain
T:Generate(7, 0.6, 512)                                   -- seed, hills (0..1), size in studs
T:FillBlock(CFrame.new(0, 0, 50), Vector3.new(20, 10, 20), Enum.Material.Rock)   -- a rock plateau
T:FillBlock(CFrame.new(0, 0, -50), Vector3.new(16, 8, 16), Enum.Material.Air)    -- dig a pit
T:FillBall(Vector3.new(40, 0, 0), 12, Enum.Material.Grass)  -- a round hill
T:Sculpt("Smooth", Vector3.new(0, 0, 0), 20, 1)            -- brush, position, radius, strength, material
print(T:GetHeight(10, 20), T:GetMaterial(Vector3.new(10, 0, 20)))
T:Clear()
```

`workspace:Raycast` returns `workspace.Terrain` as the `Instance` when it hits the
ground, with the terrain's `Material`. Changes made while the game runs are sent to
every player (at most twice a second, since the terrain can be big). The terrain is
in `Terrain.h` / `Terrain.cpp` (`src/scene`), drawn by `SceneRenderer::terrainMesh`,
and sculpted in `ViewportTerrain.cpp`.

### Real water

Water isn't just a see-through box you can swim in. While the game runs:

- **Waves and ripples.** The surface moves. Drop something in and rings of
  ripples spread out and bounce off the sides of the pool. Swimming or walking
  through it leaves a wake.
- **Floating and sinking.** Loose (unanchored) parts float or sink depending on
  their material, like real life: **Wood**, **Ice**, **Plastic** and **Neon**
  float (wood highest), as do **Fabric** and **Grass**, while **Metal**,
  **Glass**, **Concrete** and the stones (**Brick**, **Slate**, **Granite**,
  **Marble**, **Cobblestone**, **Sand**) sink. Set a
  part's Density to choose exactly. A long plank tips over and floats flat, and
  boats rock on the waves.
- **Splashes.** Things (and people) that fall in throw up spray, make a splash
  sound and a dip in the water. Big splashes, and waves slapping a wall, throw
  real liquid drops into the air that fall back and soak in, and leave a ring
  of foam.
- **Ocean swell.** Give a water part a number attribute called `Waves` (like
  `0.5`) for big rolling waves that lift everything floating on them. They're
  Gerstner waves: sharp crests and wide troughs, with whitecaps on big ones.
  Floating things and swimmers ride exactly the waves you see.
- **Clarity.** A number attribute from 0 (murky) to 1 (crystal clear). The water
  is clear where it's shallow and fades to deep blue where it's deep, things
  under it are bent by the water (like real refraction), and sunlight makes
  wobbly bright lines (caustics) on the bottom.
- **Currents.** Give it a Vector3 attribute called `Flow` (like `4, 0, 0`) and
  it carries swimmers and floating things along: rivers, rapids, lazy rivers.

Scripts can make, move or resize water while playing (a rising flood works).

**FluidVolume** (Insert Object, or from a script) is a block of water with
these settings ready to change:

```lua
local river = Instance.new("FluidVolume")
river.Size = Vector3.new(100, 5, 20)
river.Position = Vector3.new(0, 10, 0)
river.FlowVelocity = Vector3.new(5, 0, 0)   -- a current along X
river.Clarity = 0.8                         -- 0 murky .. 1 crystal clear
river.WaveScale = 1.2                       -- how tall the waves are (1 = half a stud)
river:ParentTo(workspace)                   -- (same as river.Parent = workspace)
```

### Flowing water (WaterSource)

Insert > **WaterSource** makes a spout that pours water while the game runs.
The water really flows: it runs downhill, spreads across the floor, fills
holes and pools, piles up behind walls and pours over the edges when they're
full. You can swim in it, things float on it, and fast-moving water carries
you along.

- **Rate** attribute: how much water it pours each second (default 8). Set it
  to 0 from a script to turn the tap off, and back up to turn it on.
- **FloodSize** attribute: how big an area the water can spread over (default
  80 units, up to 200). Water that runs past the edge is gone.
- Any part called WaterSource works, or give one the tag `WaterSource`.

Try a flooding-room obby, a dam you blow up, or a sinking ship.

### Real liquid (FluidSource)

For water you can really watch move, like a Blender fluid simulation: a part
called **FluidSource** (or tagged `FluidSource`) pours out actual liquid from
its front (the way its LookVector points) while the game runs. See the Mega
Water Slide sample game.

How it works, in short:

1. **The physics is lots of tiny drops.** Each drop has a position and a
   speed. Every step, gravity moves them, then they push each other apart
   wherever they're squashed together (water doesn't squash), so they flow,
   pile up, fill dips and splash. This is called *Position Based Fluids*. The
   drops bump into parts of every shape (turned any way), into the real
   triangles of Mesh parts you build in Modeling mode, and into people.
2. **It runs on the graphics card** (compute shaders) on computers with OpenGL
   4.3 and phones with OpenGL ES 3.1: up to about a million drops on a
   computer and 262,144 on a phone. Older computers (and Macs, which stop at
   OpenGL 4.1) run the same physics on the processor instead, with up to
   14,000 drops.
3. **Drawing it as one liquid, not marbles.** Each frame the drops are drawn
   as soft balls into a depth picture, which is smoothed until they melt into
   one surface. From that surface's slope in each pixel the shader lights it
   like real water:
   - **Fresnel:** a mirror (reflecting the sky and sun) when you look across
     it, clear when you look straight down. Straight on it reflects 2%, like
     real water.
   - **Refraction (Snell's law):** things under it look bent, using water's
     real refractive index of 1.333.
   - **Absorption (Beer's law):** it soaks up red light first, so the deeper
     it is, the darker and bluer what's behind it looks.
   - Fast, thin water turns white (foam).

Attributes on the FluidSource:

- **Speed:** how fast it pours out (default 8). 0 turns it off; a script can
  turn it back on.
- **Rate:** the most drops it makes each second (default 600). The stream is
  as wide and tall as the part, so a bigger part pours more.

**From a script (FluidSystem and FluidEmitter).** You never deal with single
drops: a *FluidSystem* is a kind of liquid, and a *FluidEmitter* pours it out.

```lua
local water = Instance.new("FluidSystem")
water.Color = Color3.fromRGB(30, 144, 255)
water.Viscosity = 0.1          -- 0 runs like water, 1 oozes like honey
water.SurfaceTension = 0.05    -- how much drops stick together (beads, strands)
water.Parent = workspace

local tap = Instance.new("FluidEmitter")
tap.Rate = 500                         -- drops per second
tap.Velocity = Vector3.new(0, -10, 0)  -- pour straight down
tap.Size = Vector3.new(1, 1, 1)        -- the box it pours out of
tap.Position = Vector3.new(0, 20, 0)
tap.FluidSystem = water                -- (leave it out for plain water)
tap.Enabled = true
tap.Parent = workspace
```

You can also add both from Studio's Insert Object list and set them in
Properties. Up to 16 kinds of liquid can be in a game at once, each with its
own colour, thickness and stickiness; where they meet, their colours mix. If a
small emitter is asked for more drops than fit through it, the stream sprays
out wider.

**Performance cap.** There are never more than `workspace.MaxFluidParticles`
drops (100,000 unless a script changes it, and at most what the computer can
do). When it's full and something is still pouring, the oldest drops are
recycled first, so new liquid keeps coming and the game stays smooth.

What the liquid does in the game:

- It carries people along when it's faster than them, and if it gets deep
  enough you swim in it. Loose parts float on it and get pushed around.
- Anything it runs over stays **wet** (slippery) for 20 seconds.
- When it pours into a pool of normal Water, it joins it (with ripples and
  spray).
- A drop left on its own dries up after a few seconds, so puddle splashes
  don't pile up forever.

### Slippery parts and tilted water

- Tag a part `Slippery` and people slide on it like a wet water slide or ice:
  slopes carry you down and you keep your speed.
- A Water part can be tilted (a sloping river or a water slide). You swim in
  its real, turned box, and its `Flow` attribute carries you along.

## NPCs (zombies and other characters)

Any Model built like a character (with a **HumanoidRootPart**, **Torso** and
**Head** inside) becomes an NPC when the game starts, or as soon as a script
puts one in the world (for example, cloning a zombie out of ServerStorage). It
has a Humanoid, stands on the ground, walks with swinging arms and legs, and
falls apart (or ragdolls) when it dies.

The Toolbox has a ready-made **Zombie**. It chases the nearest player, walks
around walls and bites. Open its script to see how it works.

Steer an NPC from a script through its Humanoid, like Roblox:

```lua
local npc = workspace.Guard
local humanoid = npc.Humanoid

humanoid.WalkSpeed = 8
humanoid:MoveTo(Vector3.new(10, 0, 20))   -- walk there
humanoid.MoveToFinished:Wait()            -- true if it got there (it gives up after 8 seconds)
humanoid.Jump = true                      -- hop once
humanoid.Died:Connect(function() print("got him!") end)
```

### Navmesh and pathfinding

The engine bakes a **navigation mesh**: every floor a character can stand on,
which floors join up, and where you can jump up a ledge, drop off an edge or
jump across a gap. It takes a few milliseconds and re-bakes by itself when
anchored parts change. See it in Studio with **MODEL > Navigation > Navmesh**
(blue = walkable, yellow arcs = jumps, orange lines = drops). **Bake** re-bakes
it right now.

The easy way: let the engine do the walking.

```lua
local ok = humanoid:PathfindTo(workspace.Treasure)   -- waits until it gets there (true) or gives up (false)
humanoid:PathfindStart(player.Character)            -- or don't wait: it keeps following a moving target
print(humanoid.PathfindStatus)                      -- "Walking", "Arrived", "Failed" or "Idle"
humanoid:StopPathfinding()
```

It jumps where the route says, finds a new way when something blocks it or it
gets stuck, and keeps up with a target that moves.

The Roblox way works too:

```lua
local PathfindingService = game:GetService("PathfindingService")
local path = PathfindingService:CreatePath({
    AgentRadius = 0.6, AgentHeight = 2.7, AgentCanJump = true, WaypointSpacing = 2,
    Costs = { Water = 10, DangerZone = math.huge },   -- materials or PathfindingLabel names
})
path:ComputeAsync(npc.HumanoidRootPart.Position, target.Position)
if path.Status == Enum.PathStatus.Success then
    path.Blocked:Connect(function(index) print("something's in the way at waypoint", index) end)
    for _, point in ipairs(path:GetWaypoints()) do
        if point.Action == Enum.PathWaypointAction.Jump then humanoid.Jump = true end
        humanoid:MoveTo(point.Position)
        humanoid.MoveToFinished:Wait()
    end
end
```

Sizes are in Guts&Bolts studs (characters are half Roblox's size). A table
copied from a Roblox game (AgentHeight 5) is halved for you.

| More from PathfindingService | |
|---|---|
| `:IsWalkable(pos)` | can someone stand here? |
| `:FindClosestPoint(pos, range)` | the nearest walkable spot |
| `:GetRandomPoint(near, radius)` | a random spot you can walk to (wandering NPCs) |
| `:CanWalkStraight(a, b)` | true, or false and how far you'd get |
| `:Bake()` | re-bake now; returns milliseconds and the number of floor cells |
| `:SetBakeSettings({CellSize, MaxSlope, StepHeight, JumpHeight, JumpGap, MaxDrop})` | how it bakes |

Part attributes work like Roblox's PathfindingModifier: **PathfindingLabel**
(a name to use in `Costs`) and **PathfindingPassThrough** (true = paths ignore
this part, e.g. a door that opens).

Give the model a **WalkSpeed** or **MaxHealth** attribute to set those without
a script. NPCs whose name has "Zombie" in it (or tagged `Zombie`) walk with
their arms out. In multiplayer the host moves the NPCs, and everyone sees them.

The Command Bar now runs inside the game while you're playing, so you can poke
at NPCs (and anything else) live.

## Physics

Loose (un-anchored) parts are real rigid bodies: they tumble, spin, stack,
slide and bounce. Each part has **Density** (heavier), **Friction** (grippier)
and **Elasticity** (bouncier) in the Properties panel, with sensible defaults
per material (ice is slippery, metal is heavy); scripts set all three at once with
`part.CustomPhysicalProperties`, whose weights say whose friction and bounce win when two parts touch. Parts that stop moving go to
sleep so big piles stay fast, and your character can shove light things around.

Constraints join parts together through **Attachments** (little points on a part):

| Constraint | What it does |
| --- | --- |
| Rope | Keeps two points no further apart than its length (it can go slack) |
| Rod | Keeps two points exactly the same distance apart |
| Spring | Pulls back towards its length, with stiffness and damping |
| Weld | Glues two parts together |
| Hinge | Lets a part swing around one axis, like a door or a wheel |
| Motor | A hinge that spins by itself (`AngularVelocity`, `MotorMaxTorque`) |
| Ball Socket | Holds two points together and lets them turn any way, like a shoulder |
| Prismatic | A slider: the second part slides along a line and can't turn |
| Cylindrical | Slides along a line and spins around it, like a piston or a drill |

### Physics Replay (record, watch again, test)

Press **MODEL > Replay** (or View > Physics Replay) and **Record**: Studio runs only
the physics (no scripts, no players) on a copy of your scene for a few seconds.
Your scene doesn't change. Then scrub the slider, step frame by frame or play it in
slow motion: light blue outlines show where every loose part was, and a yellow line
shows the selected part's path. **Keep this frame** moves your parts to that moment
(handy for letting a pile settle), and one Ctrl+Z puts them back.

**Save as test** keeps the recording in ServerStorage > PhysicsTests (it saves with
your game). **Run** records again from the same starting places and fails if any part
ends up further off than it's allowed (0.1 studs unless you change it), is turned
differently, or falls out of the world differently, and says which part and when.
If the new way is right, **Save again**. The Assistant can do all of this with the
`physics_replay` tool.

### Ball sockets, sliders and slide-and-spin joints

`BallSocketConstraint`, `PrismaticConstraint` and `CylindricalConstraint`
work like Roblox's: make them from scripts (`Instance.new`), Insert Object or
the Constraints tool, and set `Attachment0` / `Attachment1`. They're saved in
your game, show up for everyone in multiplayer, and come in and go out with
`.rbxl` / `.rbxlx` files.

- **Ball socket:** `LimitsEnabled` + `UpperAngle` (degrees) keep Attachment1's
  axis within a cone around Attachment0's; `TwistLimitsEnabled` +
  `TwistLowerAngle` / `TwistUpperAngle` stop it twisting too far;
  `Restitution` (0 to 1) makes it bounce off the limits.
- **Prismatic:** slides along Attachment0's axis (its X; turn the attachment
  to aim it). `LimitsEnabled` + `LowerLimit` / `UpperLimit` (studs).
  `ActuatorType` `Motor` pushes at `Velocity` with up to `MotorMaxForce`;
  `Servo` goes to `TargetPosition` at `Speed` with up to `ServoMaxForce`.
  `CurrentPosition` says where it is now.
- **Cylindrical:** everything a prismatic has, plus spinning:
  `AngularActuatorType` `Motor` (`AngularVelocity`, `MotorMaxTorque`) or
  `Servo` (`TargetAngle` in degrees, `AngularSpeed`, `ServoMaxTorque`),
  `AngularLimitsEnabled` + `LowerAngle` / `UpperAngle`, and
  `InclinationAngle` to tilt the spin axis. `CurrentAngle` says how far it's
  turned.

```lua
-- A lift: the platform slides up and down a post.
local slide = Instance.new("PrismaticConstraint")
slide.Attachment0 = post.Attachment      -- its X axis points up
slide.Attachment1 = platform.Attachment
slide.ActuatorType = Enum.ActuatorType.Servo
slide.ServoMaxForce = 50000
slide.Speed = 4
slide.TargetPosition = 10
slide.Parent = post
```

Like hinges, the parts these join don't collide with each other. Forces start
at 0 (as in Roblox), so set `MotorMaxForce` / `ServoMaxForce` /
`MotorMaxTorque` / `ServoMaxTorque` or the actuator won't move anything.

### The character's body

- **Hitbox like Roblox R6:** the body collides as a box as wide as the torso
  (2 studs) and half as deep (1 stud), from the feet to the top of the head, and
  it turns with the character. Arms and legs don't collide, so you fit through
  gaps sideways and brush past corners.
- **Touched per body part:** like Roblox, each part of the body that touches
  something fires `Touched` on its own (`hit` is that arm, leg, head...), and
  the body part's own `Touched` fires too.
- **Walk cycle follows your real speed:** a higher `WalkSpeed` takes quicker,
  longer strides; walking into a wall doesn't run on the spot.
- **Getting hit:** loose parts that crash into you shove you back, harder the
  heavier and faster they are. A big hit knocks you off your feet.
- **Moving platforms:** they carry you, spinning ones turn you with them, and
  jumping off keeps their speed.

### Names and whispers

Everyone's name floats above their head (NPCs too, like "Zombie"), with a
small health bar when they're hurt. Your own name hides when you zoom into
first person.

To send a private message, type `/w PlayerName message` (or `/whisper`) in
chat. Only you and that player see it, with no speech bubble. Capitals don't
matter, and the start of a name is enough if only one player's name starts
that way.

### Voice chat

In multiplayer games you can talk to the other players out loud. It's off
until you turn it on: **Esc > Settings > Voice chat**. Then **hold V** to talk
(on phones, tap the microphone button next to Chat, and tap it again to stop).
Turn on **Open Mic** to send whenever you make a sound instead.

Voices come out of the speaker's character, so people close by are loud and
people across the map fade away (nothing is sent past 100 studs). A little
speaker shows over the head of whoever is talking, and next to their name on
the player list. Click a name on the player list and press **Mute Voice** to
stop hearing someone (just for you; they aren't told).

Guests can't use voice chat, the same as the text chat. A game's creator can
turn it off for their game in **Game Settings > Players > Voice Chat**, and
scripts can too: `workspace.VoiceChatEnabled = false`.

How it works: the microphone is squeezed with Opus (about 24 kbps) and sent to
the game's host in small packets, 60 ms of sound each. The host checks the game
allows voice and that the speaker has an account, and passes it on to everyone
nearby who has voice chat on. It travels through the same connection as
everything else, so online games still never share anyone's IP address.

### The play camera

Like Roblox, the camera orbits your character's **head**. Right-drag to look
around and use the mouse wheel to zoom. As the camera comes close your
character fades away so it doesn't block the view. Scroll all the way in for
**first person**: the camera sits in your head, your body is invisible (you
still see the tool you're holding), your character turns to face where you
look, and the mouse looks around by itself with a dot in the middle of the
screen. Scroll out to go back. On phones, pinch to zoom in and out.

**Shift Lock**: press Shift to lock the mouse in the middle of the screen. The
camera moves over your right shoulder and your character always faces where
you look (a ring shows in the middle). Press Shift again to turn it off. It
works in the Player and in Studio's play test, and you can switch it off in
the in-game menu.

### Scripting the camera

Games can take over the camera like in Roblox, from a LocalScript (or a Script
when you play by yourself). It works the same in the Player and in Studio's
play test, and games that don't touch it keep the normal camera.

```lua
local camera = workspace.CurrentCamera
local player = game.Players.LocalPlayer

-- A cutscene: the engine stops moving the camera and shows exactly this.
camera.CameraType = Enum.CameraType.Scriptable
camera.CFrame = CFrame.new(Vector3.new(0, 40, 40), Vector3.new(0, 0, 0))
wait(3)
camera.CameraType = Enum.CameraType.Custom      -- back to following the character

camera.FieldOfView = 80                         -- degrees top to bottom (1 to 120, 60 to start with)
camera.CameraSubject = workspace.Boss           -- follow a part (or another character's Humanoid)
camera.CameraSubject = player.Character.Humanoid

player.CameraMode = Enum.CameraMode.LockFirstPerson   -- always first person
player.CameraMinZoomDistance = 5                      -- the mouse wheel stays between these
player.CameraMaxZoomDistance = 30
```

- `CameraType`: **Custom** (the normal camera, following `CameraSubject`),
  **Scriptable** (only scripts move it: the mouse, wheel and controller leave it
  alone) and **Fixed** (it stays where it is, but the mouse can still turn it).
  Roblox's Follow, Track, Watch, Attach and Orbital act like Custom.
- `CFrame` is where the camera is and which way it looks (`camera.CFrame.LookVector`).
  The engine keeps it up to date while it follows the character; setting it in
  Custom turns the camera to look the same way.
- `CameraSubject` starts as your own Humanoid. Give it a part, a character model or
  a Humanoid and the camera follows that instead (your character stays visible);
  `nil` or a part that's destroyed puts it back on you.
- `Focus` is the point the camera orbits (your head, normally).
- `camera:WorldToViewportPoint(position)` gives the pixel on screen (`X`, `Y`, and
  how far in front of the camera as `Z`) plus whether it's on screen;
  `camera:ViewportPointToRay(x, y)` is the ray from the camera through a pixel
  (`ViewportSize` is the view's size in pixels). The Screen versions are the same.
- `player.CameraMode`: **Classic** (zoom in and out; all the way in is first person)
  or **LockFirstPerson** (always through your character's eyes: your character is
  hidden except the tool you hold, the mouse is locked in the middle and your
  character faces where you look).
- `player.CameraMinZoomDistance` / `CameraMaxZoomDistance` (0.5 and 60 to start
  with) are how close and how far the mouse wheel, pinch and D-pad can zoom. A
  minimum of 1.2 or more means no first person; a maximum under 1.2 means first
  person only.

The game's starting **Camera Mode**, **Min Zoom Distance** and **Max Zoom
Distance** are in Studio's **Player** panel, under Camera (Roblox's StarterPlayer
settings). In an online game, a Script on the host can change a player's
`CameraMode` and zoom limits too: they're sent to that player. The camera itself
(`CurrentCamera`) is each player's own, so change it from a LocalScript.

`tools/test_camera.lua` checks all of this in a real play test:

```
xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_camera.lua)" --screenshot /tmp/camera.png --frames 900
```

### Your own materials (Material Editor)

Like Roblox's Material Manager: a **MaterialVariant** in **MaterialService** is a
material you make from pictures, on top of a built-in one. Open **VIEW > Materials**,
press **New**, pick its **BaseMaterial** and add pictures:

- **ColorMap**: the colours (the part's Color tints it; white shows it as it is)
- **NormalMap**: bumps and grooves
- **RoughnessMap** / **MetalnessMap**: where it's rough or shiny, metal or not
- **StudsPerTile**: how big one copy of the pictures is
- **Roughness**, **Metalness**, **Glow** (ours): for what has no picture

A part shows it when its **Material** is the BaseMaterial and its
**MaterialVariant** is the variant's name ("Use on selected parts" sets both). The
part still weighs and grips like the BaseMaterial. **Save as preset** keeps a
material on this computer for your other games. Scripts work the same way:

```lua
local v = Instance.new("MaterialVariant")
v.Name = "Lava"
v.BaseMaterial = Enum.Material.Slate
v.ColorMap = "images/lava.png"
v.Glow = 2
v.Parent = game:GetService("MaterialService")

part.Material = Enum.Material.Slate
part.MaterialVariant = "Lava"
```

### Sky and screen effects

Like Roblox, you change the sky and the look of the whole screen with objects
**right inside Lighting**. In Studio, right-click **Lighting** in the Explorer >
Insert Object > Sky & Screen Effects (they always go in Lighting), then change
them in Properties. Scripts make them the same way:

```lua
local cc = Instance.new("ColorCorrectionEffect")
cc.Saturation = -1                  -- black and white
cc.TintColor = Color3.fromRGB(255, 230, 200)
cc.Parent = game.Lighting

local blur = Instance.new("BlurEffect")
blur.Size = 16
blur.Parent = workspace.CurrentCamera   -- only on this player's screen (a LocalScript)
```

- **ColorCorrectionEffect**: `Brightness`, `Contrast`, `Saturation` (-1 = grey) and `TintColor`.
- **SunRaysEffect**: `Intensity` and `Spread` - rays of light from the sun, past whatever is in front of it.
- **DepthOfFieldEffect**: `FocusDistance` (studs), `InFocusRadius`, `NearIntensity`, `FarIntensity` - blurs what's nearer or further than the focus.
- **BloomEffect**: `Intensity`, `Size`, `Threshold` - bright things glow. While there's one, it's used instead of the bloom in the Lighting settings.
- **BlurEffect**: `Size` - blurs everything (pause menus, being dizzy).
- **Sky**: `SkyboxBk` / `Dn` / `Ft` / `Lf` / `Rt` / `Up` pictures, `SunTextureId`,
  `MoonTextureId`, `StarCount` (0 to 5000), `CelestialBodiesShown`,
  `SunAngularSize`, `MoonAngularSize`. Pictures are ids like a Decal's (an
  uploaded picture `gb:<id>`, or a file in the games folder). The skybox shows
  when all six pictures load; otherwise the Lighting settings' sky stays and the
  Sky still sets the stars, sun and moon. Ft faces -Z, Bk +Z, Rt +X, Lf -X. Clouds
  are hidden behind a skybox (paint them into the pictures).

Every effect has `Enabled`, and several stack (two ColorCorrections add up, two
Blurs blur more). They work in Lighting and in `workspace.CurrentCamera` (the
camera's are only on that player's screen and aren't saved); a Sky only works in
Lighting. Lighting's effects and Sky are saved with the game, sent to everyone in
a multiplayer game, and come in from and go out to `.rbxl` / `.rbxlx` files
(Roblox's own built-in sky pictures can't load here, so those use our sky).
`Lighting:GetChildren()`, `Lighting:FindFirstChildOfClass("Sky")` and
`Lighting.Sky` work like Roblox.

On phones and slow computers: colour correction, blur and bloom are cheap and
always on (bloom follows the Bloom graphics setting). **Sun rays and depth of
field switch off on Low graphics**, and on phones the sun rays take half as many
steps. Blur and depth of field are worked out on small copies of the picture.

### Collision groups

Collision groups decide which parts bump into which, like Roblox's
PhysicsService. Every part is in one group (`part.CollisionGroup`, **Default**
to start with), and a table says which groups collide. Two groups that don't
collide pass straight through each other: loose parts falling, characters
walking, and raycasts that ask for a group.

```lua
local PhysicsService = game:GetService("PhysicsService")

-- Players walk through each other (but not through walls).
PhysicsService:RegisterCollisionGroup("Players")
PhysicsService:CollisionGroupSetCollidable("Players", "Players", false)
local function joinGroup(player)   -- (a character keeps its group when it respawns)
    for _, part in ipairs(player.Character:GetDescendants()) do
        if part:IsA("BasePart") then part.CollisionGroup = "Players" end
    end
end
for _, player in ipairs(game.Players:GetPlayers()) do joinGroup(player) end
game.Players.PlayerAdded:Connect(joinGroup)

-- A door only the red team walks through.
PhysicsService:RegisterCollisionGroup("RedTeam")
PhysicsService:RegisterCollisionGroup("RedDoor")
PhysicsService:CollisionGroupSetCollidable("RedTeam", "RedDoor", false)
workspace.RedDoor.CollisionGroup = "RedDoor"
```

- `RegisterCollisionGroup(name)`, `UnregisterCollisionGroup(name)`,
  `RenameCollisionGroup(old, new)` (its parts move to the new name),
  `IsCollisionGroupRegistered(name)`, `GetRegisteredCollisionGroups()` (a list of
  `{ name, id, mask }`), `CollisionGroupSetCollidable(a, b, true/false)`,
  `CollisionGroupsAreCollidable(a, b)` and `GetMaxCollisionGroups()` (32,
  Default included). A new group collides with everything until you say
  otherwise. The older `CreateCollisionGroup`, `GetCollisionGroupId`,
  `SetPartCollisionGroup` and `part.CollisionGroupId` work too.
- Mistakes are errors that say what's wrong: an unknown group in
  `CollisionGroupSetCollidable`, a 33rd group, renaming or unregistering Default,
  a name that isn't a string (or is empty, or over 100 characters).
- A part in a group that isn't registered (or was unregistered) acts as Default.
- A character is in its **HumanoidRootPart**'s group (or its Torso's). The
  terrain is in Default.
- `RaycastParams.CollisionGroup` (Default to start with) makes a ray skip what
  that group passes through; `RespectCanCollide = true` skips CanCollide-off parts.
- Touched still fires between parts that pass through each other.

In Studio, **MODEL > Collision Groups** (or **View > Collision Groups**) opens a
small window: add, rename (double-click) and delete groups, and tick or untick
the grid to say which pairs collide. Pick a part's group with **Collision Group**
in Properties. The groups are saved with the place, sent to everyone who joins
(and again when a script changes them), and read from Roblox files (the
Workspace's groups and each part's `CollisionGroup` or `CollisionGroupId`);
Export to Roblox writes them back.

`tools/test_collision_groups.lua` checks all of this in a real play test:

```
xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_collision_groups.lua)" --screenshot /tmp/groups.png --frames 1500
```

### Controllers

Plug in an Xbox, PlayStation, Switch Pro or other game controller and play with
it, in the Player and in Studio's play test:

| Button | Does |
|---|---|
| Left stick | Walk (push it a little to walk slowly) |
| Right stick | Look around (click it for Shift Lock) |
| A / Cross | Jump |
| LT / L2 | Dive while swimming |
| RT / R2 | Use the tool you're holding |
| LB / RB | Last / next tool (B / Circle puts it away) |
| X / Square | Use the nearest ProximityPrompt (the card shows **X**) |
| D-pad up / down | Zoom in / out |
| Start | The in-game menu (the D-pad and A pick in it, B closes it) |
| Back / Select | Show or hide the player list |

The D-pad and A also move around the Player's own pages (games, catalog,
settings...). Scripts see the buttons too: `UserInputService.InputBegan` fires
with `Enum.KeyCode.ButtonA` (and `UserInputType` `Gamepad1`), and
`UserInputService.GamepadEnabled`, `IsGamepadButtonDown` and `GetGamepadState`
(the sticks and triggers) work like Roblox's.

### Key bindings (ContextActionService)

A LocalScript can bind an action to keys, controller buttons, the mouse or
fingers, and give it its own round button on phones, like Roblox's
`ContextActionService`:

```lua
local CAS = game:GetService("ContextActionService")

local function dash(actionName, state, input)
    if state == Enum.UserInputState.Begin then
        print("Dash!", input.KeyCode, input.UserInputType)
    end
    return Enum.ContextActionResult.Sink   -- keep it (Pass: let others have it too)
end

-- Q on a keyboard, X on a controller, and a button on touch screens.
CAS:BindAction("Dash", dash, true, Enum.KeyCode.Q, Enum.KeyCode.ButtonX)
CAS:SetTitle("Dash", "Dash")
```

- **Inputs:** any `Enum.KeyCode` the engine knows (letters, numbers, Space,
  Shift, arrows, controller buttons like `ButtonA` or `DPadUp`...), or a whole
  `Enum.UserInputType`: `Keyboard`, `Gamepad1`, `MouseButton1` (a click in the
  game view) or `Touch` (a finger on the game view).
- **The function** gets `(actionName, state, inputObject)`. `state` is
  `Enum.UserInputState.Begin` when the key goes down, `End` when it comes up,
  and `Cancel` when a newer binding takes the key over while it's held. The
  input object has `KeyCode`, `UserInputType`, `UserInputState` and `Position`.
- **Sink or Pass:** the newest binding (or the one with the highest priority)
  hears an input first. Returning `Enum.ContextActionResult.Sink` (or nothing)
  keeps it: lower bindings don't hear it, the game's own controls ignore it
  (bind Space with Sink and the character can't jump; bind MouseButton1 and a
  click doesn't use the held tool), and `UserInputService.InputBegan` says
  `gameProcessedEvent = true`. Returning `Pass` hands it on down.
- **Priority:** `BindActionAtPriority(name, fn, touchButton, priority, ...)`,
  with a number or `Enum.ContextActionPriority.Low / Medium / Default / High.Value`
  (1000 / 2000 / 2000 / 3000). `BindAction` uses Default.
- `UnbindAction(name)`, `UnbindAllActions()`, `GetBoundActionInfo(name)` and
  `GetAllBoundActionInfo()` (with `inputTypes`, `priorityLevel`,
  `createTouchButton`, `stackOrder`, `title`, `description`, `image`). Binding a
  name again replaces it, and a LocalScript's actions go away when it stops.
- **Touch buttons:** with `createTouchButton = true`, phones and tablets get a
  round button for the action, stacked round the jump button (the first ones
  closest). `SetTitle`, `SetImage` (`rbxassetid://` or an id), `SetDescription`
  and `SetPosition` (a `UDim2` on the game view, for the button's top-left
  corner) change it, and `GetButton(name)` gives an ImageButton-like object
  whose `Image`, `Position` and `ActionTitle.Text` can be read and changed (nil
  when the action has no touch button). Pressing it runs the action with
  `UserInputType` `Touch`.

It works the same in the Player, Studio's play test and online games (each
player's own LocalScripts hear their own input). `tools/test_context_actions.lua`
checks it in a real play test:

```
xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_context_actions.lua)" --screenshot /tmp/actions.png --frames 2400
```

### The in-game menu

Press **Esc** (or the Menu button) for a Roblox-style menu with three tabs:

- **Players**: everyone in the server, with Verified and Staff tags.
- **Settings**: Shift Lock Switch, camera sensitivity, invert camera, volume,
  fullscreen, graphics quality, show FPS, blood and gore, touch controls, and
  a button for the advanced graphics settings.
- **Help**: the controls.

Along the bottom: **[R] Reset Character**, **[L] Leave Game** (both ask "Are
you sure?" first) and **[Esc] Resume Game**.

### The Developer Console

Press **F9** (or type **/devconsole** in the chat) for the Developer Console,
like Roblox's. The **Client** tab shows what happened on your own computer
(errors in red, warnings in yellow), with a search box.

The **Server** tab is only for the game's **creator** (the account that published
it). It shows the server's log, including what the game's scripts `print()`,
and has a command bar that runs Lua on the server. The host checks who you are
(with your account's signature) before sending you any of it, and ignores
commands from anyone else, so other players never see or use that tab. Playing
someone else's game alone doesn't show it either.

The command bar runs like a script: `wait()` works, and so does `require`,
both for ModuleScripts in the game and Library models by ID
(`require(1234).load("YourName")`). Type an expression (`workspace.Gravity`)
and its value gets printed.

On the leaderboard, the game's creator has a little **hammer** next to their name.

### Player collisions

Players bump into each other like in Roblox, and you can stand on someone's
head. Turn it off in **Game Settings > Damage & Blood > Player Collisions**
(or from a script: `workspace.PlayerCollisions = false`) and everyone walks
straight through each other.

## Death, ragdolls and gore

**Game Settings > Damage & Blood** (also in the Player panel) has all of it:
death style, fall damage on or off, how hard a fall has to be to hurt (Safe
Fall Speed) and how much it hurts (Fall Damage Strength), gore (off, oil &
bolts, or blood), dismemberment, and the blood itself: its **color** (red,
slime green, alien blue, ink, or anything), how much sprays out, and how long
pools last.

Blood is a liquid: drops stretch as they fly, splash into pools that spread
out and join together, and blood that hits a wall runs down it in drips.

Scripts can change these too: `workspace.FallDamage = false`,
`workspace.SafeFallSpeed`, `workspace.FallDamageScale`,
`workspace.BloodColor = Color3.new(0.2, 0.7, 0.1)`, `workspace.BloodAmount`, and
`Effects.Blood(position, amount, direction)` sprays blood (the direction is
optional, e.g. `Vector3.new(0, 2, -8)` to splatter a wall).

Each game picks its own rules in the Player panel (**Death & Gore**):

- **Death style:** *Classic* makes the character fall to pieces like in
  Roblox. *Ragdoll* makes the body go limp and tumble with real joints.
- **Gore:** *Off*, *Oil & Bolts* (robot style) or *Blood*. Blood and oil
  splat on floors and walls.
- **Dismemberment:** explosions, saw blades and long falls can knock limbs off.
- **Fall damage:** landing too hard hurts.

Players can switch blood and gore off in every game with **Settings > Show
blood & gore**.

### The character model

Everyone's character is built from `assets/models/player.obj`: a Blender
model with six objects named `Torso`, `Head`, `Left_Arm`, `Right_Arm`,
`Left_Leg` and `Right_Leg`, at Roblox size (a 2 x 2 x 1 torso, feet at 0).
The model is built into the apps when they're compiled, so to change how
characters look, edit that file (keep the names and sizes) and rebuild.
Games saved with the old blocky character get the new one when they load.

## Guts&Bolts Player

- **Home:** every game in the `games` folder, each with a rendered preview.
  Press **Play** to jump into a **public server** of that game.
- **Servers** (on a game's page, in the app and on the website): cards for
  every running server with the faces of the people in it, how full it is,
  **Join** to hop into that exact server and **Share** to copy a link to it.
- **Create a server** (on a game's page): a **private server** (friends and
  people with its code). The same window lists the servers running now and has
  a box for joining with a code.
- **Always online:** everyone plays on the main Guts&Bolts server; there's no
  offline or local-network play. Without internet the site says *Can't reach
  Guts&Bolts* with a **Try again** button. (Studio still builds and
  play-tests without internet.)
- **Friends:** see "Friends and servers" below.
- **Catalog:** hats, shirts and pants for your avatar. Only the official staff
  account can add items (see below), so it starts out empty.
- **Avatar:** display name, outfits, body colours and hats. You wear these in
  every game. It also shows your **badges** and your **account ID**, and has a
  box for redeeming badge codes.
- **In game:** `/` to chat (speech bubbles show over heads), `Esc` for the
  menu (Resume, Reset Character, Settings, Leave). The player list in the top
  right shows everyone in the game.
- **Touch controls:** on phones and tablets you get a thumbstick (it appears
  under your thumb, bottom-left), a jump button (bottom-right, with any buttons
  the game adds stacked round it) and Chat / Menu
  buttons (top-left). Drag anywhere else to turn the camera, pinch to zoom and
  tap things to click them. Try them on a computer with **Settings > Touch
  controls > Always on** (the mouse acts as one finger).

### Accounts, badges and staff

Every copy of Guts&Bolts makes its own account key the first time it runs. The
public half is your **account ID** (safe to share). The secret half stays in
`account.key` in your user folder and proves it's you when you join a game.

- **Guts** is the official staff account. Nobody else can use that name, and
  it wears the **Administrator** badge: a red shield with a bolt that floats
  next to its name in the player list and chat.
- **Official badges** (Administrator, Tester, Bug Hunter, Featured Creator,
  Verified, Staff) are signed by the staff account, so they can't be faked or
  copied to someone else. Staff make a badge code on the **Staff** page for a
  player's account ID; the player pastes it into **Avatar > Redeem**. (With a
  server, staff can do it straight from the Staff page instead - see below.)
- **Verified** people get a blue check next to their name everywhere (player
  list, chat, the site, their creations). On a server, Verified creators can
  publish anything: uploading hats, shirts, pants, audio and plugins is
  **free** for them, they have no daily upload limit, and they can **sell**
  what they make for Bolts. Only staff can verify people.
- **Staff** badge: the official Guts account can make other people Staff.
  Staff can verify people too (their Verified badges carry their own signed
  Staff badge, so everyone can check them).
- **Catalog items** are signed files in the `catalog` folder. The app ignores
  any item the staff account didn't sign. Each item is free or has a price in
  **Bolts** (see below).

### Guts&Bolts server (storing things online)

The **official Guts&Bolts server runs on Cloudflare** (with the website), so it's
online even when nobody's computer is on. The apps always use it:
**<https://gutsandbolts.net>** (`www.gutsandbolts.net` works too, and the old
`workers.dev` address still answers for older apps). (Clicking the status
pill on the site's banner, or Studio's **File > Guts&Bolts Server... >
Reconnect**, tries again if the connection dropped.)

How it works:

- `worker/server.js` is the server, rewritten in JavaScript for Cloudflare. It
  uses the same requests and rules as the C++ server in `src/server`, and keeps
  its data in a Cloudflare Durable Object's own database.
- The apps talk to it over **HTTPS** (normal requests) and **secure WebSockets**
  (multiplayer). They use mbedTLS for that, and trust Mozilla's list of
  certificate authorities (`assets/certs/cacert.pem`).
- **Multiplayer** still runs each game on the host player's computer, and
  Cloudflare only passes messages between players. So no game runs on Cloudflare
  when nobody's playing, and players never see each other's addresses.
- **The staff account:** in the Cloudflare dashboard, open
  **Workers & Pages → project-guts-and-bolts → Settings → Variables and
  Secrets**, and add `OFFICIAL` = your account ID (the Player's Staff page:
  **Copy official ID**). That account is then user #1, **Guts**.
- Everything fits Cloudflare's free plan for a small community: about 100,000
  requests a day, and 5 GB of storage.

For developers, the C++ server can still run on a computer (for example to
test server changes). The apps only use it when started with the `GB_SERVER`
environment variable set to its address:

1. **Start it.** Double-click `tools/Start Server.bat` (Windows) or
   `tools/Start Server.command` (Mac), or run `python3 install.py --server`.
   A window opens and says which address and port (7780) it's on. Keep that
   window open while people play. Everything it stores goes in the
   `server_data` folder; back that folder up to keep it safe.
2. **Connect the apps.** Start them with `GB_SERVER` set to the server's
   address, like `GB_SERVER=192.168.1.20` or `GB_SERVER=myserver.com:7780`.
3. **Let friends outside your house connect** (optional). Either forward port
   7780 on your router to the server computer and give friends your public
   IP, or run the server on a rented Linux server (a "VPS") and give out its
   address. On Linux you can also run `GutsAndBoltsServer --data /path/to/data`
   directly (`--port`, `--name "My Server"` and `--official <staff account ID>`
   work too).

The staff account is found automatically on the computer where you set it up.
On another computer, start the server once with `--official <your account ID>`
(it remembers it in `server_data/server.json`).

**What the server keeps:**

- **Accounts:** names, badges (Verified, Staff...), and a Bolts balance with
  its history.
- **Uploads:**
  - hats, shirts and pants (the online **Catalog**);
  - audio (in Studio's **PLUGINS > Library**; sounds play as `gb:<id>`);
  - **decals**, pictures for Decal objects (`.png` or `.jpg`, up to 4 MB;
    used as `gb:<id>`);
  - **plugins** (Studio's PLUGINS > Library);
  - **games** (Studio's **File > Publish to Guts&Bolts**; they show up under
    **Online Games** on the site's home page).
- **Who owns what.**

### The website

The site also works in a web browser, hosted free on Cloudflare (see
`website/README.md`). The front page shows what Guts&Bolts is. **Enter the
site** (`/app/`) to do what the Player's site pages do:

- sign up and log in (the same accounts as the apps);
- browse games and see who's playing;
- buy from the catalog;
- upload decals, audio, clothes, plugins and games, and rename your games;
- use friends, people, groups and Bolts;
- change your **avatar** (colours, hat, and clothes from the catalog). It's saved
  on the server, so the app and the website always match;
- **staff:** a Staff page to verify people, make staff, give Bolts, ban, and
  answer ban appeals;
- **Settings:** email, two-step, privacy, password, and deleting your account.

Games published from Studio get a **picture** (Studio takes it from the spawn
point when you publish), shown on the website's game cards. A red number on
**Friends** means friend requests are waiting.

Playing games still happens in the app. The website's **Play** button opens the
app on that game with a `gutsandbolts://play/<game>` link. The Player sets that
up by itself the first time you open it on Windows, Linux and Mac, and the
Android app has it built in. (On a Mac it makes a small **Guts&Bolts Player**
app in your user's Applications folder that runs the Player you opened, because
macOS only lets apps open links. Keep the Player where it is, or open it once
again after moving it.) Visitors who aren't signed in pick **Play As Boy** or
**Play As Girl** first and play as a guest.

Visitors who aren't signed in can see every page. Buying, claiming Bolts,
saving an avatar, adding friends, joining groups and uploading pop up *"You need
to log in"* with Sign Up and Log In buttons.

The website talks to the Guts&Bolts server on Cloudflare (see below). Your
password never leaves the browser.

### The Create page

**Configure** a published game on the website (Create > My Games > Configure):
change its name and description, choose **who can play** (*Public*, *Friends
only* or *Private*), upload a **thumbnail** (cropped to 16:9) and a square
**icon**, or upload a new version. Private and friends-only games are hidden
from everyone else: the server won't list them, send them or start servers
for them.

**Screenshots:** the Configure page also has a **Screenshots** box for up to 5
pictures of the game: `.png`, `.jpg` or `.webp`, up to 2 MB each (anything else
gets a clear message, before uploading and again from the server). Move one
earlier or later with the arrows, or delete it. The game's page shows the
thumbnail and the screenshots together: big picture, arrows, a strip of small
pictures to pick from, and a click (or tap) opens a big view (arrow keys and Esc
work, and you can swipe on phones). Like the thumbnail they're the creator's own
pictures and aren't held for a staff check, but staff can delete one (it goes in
the staff log). The pictures are served from `/shot/<game id>/<screenshot id>`;
every screenshot gets a new random id, so browsers can keep them for good.
(`shots.add`, `shots.delete`, `shots.order` and `shots.get` in
`worker/server.js` and `src/server/ServerItems.cpp`. The C++ server keeps them
as `files/shot-<game>-<id>`; like thumbnails, its pictures reach the apps
through requests, since only the main server has the `/shot/` addresses.)

#### Version history (putting an old version back)

Publishing a game never throws its old version away. Every time you publish
(Studio's **File > Publish**, saving a published game in Studio, or **New
version** on the Configure page), the server keeps the version it replaces. The
Configure page has a **Version history** box listing them, newest first: the
version number, when it was published, who published it and how big it is.
The live one says **Live now**.

**Restore** puts an old version back. It doesn't rewind anything: the old
version is published again as a new, newest version (marked "restored from
3", say), so the version you restored over is still in the list if you change
your mind. Players get it the next time they join.

- Only the game's creator can see the list or restore.
- The newest **50** versions are kept, and at most **100 MB** of them per game;
  older ones are deleted, oldest first. The live version is never deleted.
- Saving the exact same file again doesn't make a new version.
- Games published before version history existed show their current file as
  version 1. It's really kept as version 1 the next time the game is published.
- Deleting a game deletes its history too.

Both servers do this: the website's (`game.versions` and `game.restore` in
`worker/server.js`, files `ver:<game>:<version>`) and the one you can run on
your own computer (`src/server`, files `files/ver-<game>-<version>`).

The site's **Create** page has one tab per kind of thing you make:

- **My Games:** every game you made in Studio (on this computer, plus the ones
  you published). Each has a picture and three buttons:
  - **Edit name** renames it. A published game gets the new name on the
    server too.
  - **Open in Studio** opens it in Studio to keep building.
  - **Play** plays it.
  Studio signs a new game with your username when you save it, so it shows up
  here.
- **Decals, Audio, Hats, Shirts, Pants, Plugins:** an upload form (with a
  **Browse...** button to pick the file, and a preview for pictures and
  clothes), then a list of what you've uploaded. Decals and audio show their
  `gb:<id>`, with a **Copy ID** button: paste it into a Decal's **Texture** or a
  Sound's **File** in Studio.

Uploading costs:

- **Verified creators:** free, with no limit. They can set a price, and they
  get 70% of every sale.
- **Everyone else:** a small fee (5 Bolts for decals, 10 for clothes, 20 for
  audio and plugins), 5 uploads a day, and everything they make is free.

Who can make what:

- **Hats** are for Verified creators only, and are made in Studio's Accessory
  window (see Accessories and faces below). **Shirts and pants** can be made by
  anyone with an account, and are always a picture painted on the template.
  Items don't have a colour of their own. Guests can only play games.
- **Decals and audio are always free**, even from Verified creators: they're
  free-use assets anyone can put in their games.

The **Staff** page gets a server section where you can search for people and
**Verify** / **Unverify** them with one click. The official account can also
make people **Staff**, give or take Bolts, and ban. Banning asks for a reason
(sexual content, violent extremism, harassment, hate speech, threats, scams,
sharing personal info, exploiting, spam and so on) and an optional note; the
banned player sees them when they try to sign in.

**Ban appeals.** On their ban screen (on the website or in the Player) a banned
player can send staff one short note (up to 500 letters) asking to be unbanned.
There's one open appeal per ban: they can only send another once staff have
answered, or if the ban changes. Appeals show on the Staff page next to the
reports, with the ban's reason, the staff note, when and by whom they were
banned (from the staff action log) and what they wrote. Staff can **Deny** with
a short note, and the official account can **Accept**, which unbans them the
normal way. The player sees the answer on their ban screen, and every answer is
in the staff action log.

**Deleting an account.** In the website's **Settings** (or the Player's
**Avatar > Your account > Delete account...**) you can delete your account after
typing your password. That removes the password and key backup, email,
two-step settings, avatar, About me, posts, outfits, messages, friends and
follows (on both sides), favourites, notifications and logged-in devices.
Games and items you made stay up (so what other people bought keeps working),
shown as made by **Deleted user**; forum posts and game comments stay too, under
that name. The username (and any past usernames) and the user number are never
given to anyone else. Limited copies the account owned stay with it, off resale,
and can't be traded any more. The Guts account can't be deleted. The staff log
notes "Account #N was deleted" as a System event, without the name.

### The Library (sharing models)

Select some objects in Studio and use **File > Publish Selection to Library**.
Give it a name, and pick **Public** (everyone can find and use it) or
**Private** (only you). Verified creators can make as many models public as
they want; everyone else can make **5 public a week** (private ones don't
count). Studio takes a picture of the model for its thumbnail.

Find things in two places:

- **Studio's Toolbox > Library:** everyone's public models, decals and
  audio, with pictures and a search box (your own are under **Inventory**).
  Models uploaded without a picture get one taken when someone views them. Click a model to insert it, a decal to put
  it on the selected part, or a sound to add it.
- **The website's Create > Library tab:** all public models, decals, audio and
  plugins. **Create > Models** lists yours with a Public / Private switch.
- **Asset pages:** everything in the Library has its own page, found by its ID
  (the same `gb:decal-...` ID you paste into a game): `#/library/<ID>` on the
  website, or *Create > Library* in the Player app (paste an ID into *Got an ID?*
  to jump straight to it). Decals show their picture, audio has a **Listen**
  button, and **Copy ID** copies the ID to paste into a Decal's Texture or a
  Sound's File.

**Rating models.** Every Library model can get a thumbs up or down, on its page
(website, and *Create > Library* in the Player). One vote each: press the other
thumb to change it, or your own again to take it back.

- Only people who have **inserted the model** can rate it. The server remembers
  each Library model you fetch to use (Studio's Toolbox, or `require(id)` in a
  script; Studio fetching a picture-less model once to photograph it counts too),
  so it costs nothing extra. You can't rate your own models.
- The Library, the model's page and Studio's Toolbox show the like percentage and
  how many votes it has.
- **Best rated** (Library and Toolbox) sorts by the Wilson score: the lowest the real
  like rate is likely to be (95% sure). So one vote at 100% doesn't beat 500 votes
  at 95%, and models with no votes come last.
- `model.vote` in `worker/server.js` and `src/server/ServerItems.cpp` (`usedModels` is
  the list of models someone fetched; `wilsonScore` is in both servers).

### Gifts (Buy as a gift)

Paid catalog items have a **Buy as a gift** button under **Buy** on the website.
Pick one of your friends, write a short note if you like (up to 100 letters, through
the text filter), check what it costs and press **Send gift**.

- You pay the price (the creator gets their 70% like any sale). The item goes
  straight into your friend's inventory, and they get a notification with your note.
- Both Bolts histories show it: *Gift for amy: Party Hat* for you, *Gift from bob:
  Party Hat* for them (it costs them nothing).
- Friends only. You can't gift something they already have (if they hide their
  inventory from you, it just says you can't gift it to them), and not to someone
  who is banned or blocked.
- **No Limiteds.** Limited copies are numbered and only sold, resold and traded, so
  gifting them isn't supported (trade one instead). Free, off-sale, not-yet-checked
  and award items (the Verified Hat) can't be gifts either.
- It's as safe as buying for yourself: the server checks everything again, takes
  the Bolts and hands the item over in one go, and sending the same gift twice is
  turned down because they already have it.
- `gift.buy` in `worker/server.js` and `src/server/Server.cpp`. (Gifts are sent from
  the website; the Player app shows the notification and opens the item.)

### Limiteds, resale and trading

- **Edit item:** on an item's page on the website, its creator (or staff) can
  change its name, description, price or clothing picture. (Items don't have a
  colour: hats are Studio models and clothes are pictures.)
- **Limited items:** only the Guts account can make an item Limited, and only
  its own **accessories and faces** (never shirts, pants, audio, decals or
  models). A Limited has a fixed stock. Each copy gets a number (#1, #2, ...). While there's stock
  left it sells like normal; once it's **sold out**, the only way to get one
  is from another player.
- **Resale:** owners of a Limited copy can put it up for sale on the item's
  page, at any price. The seller gets 70% of the price. The cheapest copies
  show first under **Resellers**.
- **Trading:** press **Trade** on someone's profile, pick up to 4 of your
  Limiteds and up to 4 of theirs, and send the offer. They accept or decline
  on the **Trades** page. The swap only happens if both of you still have
  everything. A trade nobody answers runs out (**expires**) after 30 days, and
  whoever sent it gets a notification.
- **Past trades:** the Trades page has two tabs, **Open trades** and **Past
  trades**. Past trades lists every finished trade, newest first, 20 to a page:
  who it was with, the items each of you put in, the date it ended and how
  (Accepted, Declined, Cancelled, Expired, or "Couldn't happen" when someone no
  longer had an item). Only the two people in a trade can see it (not even staff
  get it from this list). Each one is saved with the names of the people and
  items as they were then, so it still reads right if an item is deleted later.
  They're kept for a year (at most 50,000 in all). Past trades started being
  kept with the *Paper Trail* update; before that, finished trades were only
  kept for a month, so the history starts with what the server still had then.
  Blocking someone cancels your open trades with them, and those show up as
  Cancelled. (`trade.history` in `worker/server.js`, which keeps them in its own
  `pasttrades` table, and `src/server/ServerItems.cpp`, which saves
  `pasttrades.json`.)
- **Inventory:** every profile (website and app) has an **Inventory** box with
  what that person owns from the catalog, newest first. **See all** shows the
  whole thing in groups (Accessories, Clothing, Faces, Gear, Limiteds), and a
  Limited shows the numbers of the copies they hold, so you know what to ask for
  in a trade. *Settings > Privacy > Who can see my inventory* hides it from
  everyone or keeps it for friends; people it's hidden from can't trade with you
  either (and the item page shows "Hidden" for copies you hold that aren't for
  sale). Staff can always see it, to sort out scams. (`profile.inventory` in
  `worker/server.js` and `src/server/Server.cpp`; the C++ server has no Limiteds,
  so its inventories are sorted by name.)

### Accessories and faces

Besides hats there are **hair**, **face**, **neck**, **shoulder** and **waist**
accessories. Verified creators make them in **Studio**:

1. Build the accessory (one Model or part).
2. Open **AVATAR > Accessories**. Press **Add mannequin** to get a character to
   try it on, pick the **type**, select your model and press **Move to the
   spot** (it jumps to where that type sits, like the top of the head).
3. Move, turn and stretch it until it looks right, then **Save position**.
4. Give it a name, description and price and press **Upload**. Its catalog
   picture is drawn from the accessory itself, worn by a mannequin.

Catalog pictures are never uploaded pictures: the website, the Player and Studio
all draw each item from its real shape (clothes, faces, hats, hair and
accessories on a plain mannequin, zoomed in on where they're worn; gear on its
own). The servers refuse pictures for catalog items, so nobody can swap one in.

A worn hat or hair takes the place of the built-in hat.

**Faces** are pictures drawn on the front of the head. Only the Guts account
can make them: on the website, **Create > Faces**, upload a square .png that's
see-through around the eyes and mouth.

### Changing your username

On the website's **Settings** page you can change your username for **1,000
Bolts**. Your old usernames show on your profile under **Past usernames**.
Usernames are never reused: nobody else can take one of your old names, but
you can switch back to it yourself (for the same price). Your password stays
the same.

The Guts account can also set its **join date** there (for example to when the
project really started); it shows on the profile.

### Clothing templates

Shirts and pants are a **picture**, like classic Roblox clothing (every shirt
and pants upload needs one). Get the
template (the website's Create page links **shirt template** and **pants
template**; the app's Create page has **Save the template**; they're also in
`assets/templates/`). It's a 585 x 559 picture with a box for every side of the
torso and each arm (shirts) or leg (pants): **R** and **L** are the
character's own right and left. Paint over the boxes, save it as a .png, and
pick it as the **Picture** when you upload the shirt or pants. Anything you
leave see-through shows the body underneath.

`tools/make_clothing_template.py` redraws the templates; the layout lives in
`src/scene/PlayerModel.cpp`.

### T-shirts

A **T-shirt** is just a picture (a .png or .jpg, up to 1024 x 1024) worn flat
on the front of the torso, like classic Roblox T-shirts, over the shirt if
you have one. No template needed: upload it from the website's or the app's
Create page (**T-Shirts**), then wear it from the Avatar page. Anyone with an
account can make them (10 Bolts, free for Verified creators). Square pictures
fit best; see-through bits show the shirt underneath.

### Seats

A **Seat** (Insert Object > Seat, or any part called Seat / VehicleSeat or
tagged "Seat") works like Roblox's: walk into it and you sit down, facing the
seat's front, and ride along if it moves. Jump to get up. Give it a Disabled
attribute set to true to switch it off. Scripts can use `Humanoid.Sit`
(`false` gets up), `Humanoid.SeatPart`, `seat.Occupant`, `seat.Disabled` and
`seat:Sit(humanoid)`.

### Vehicles

A **VehicleSeat** (Insert Object > VehicleSeat, or a part called VehicleSeat or
tagged "VehicleSeat") is a seat that drives whatever it's built into: the Model
it's in. Sit in it and **W/S** (or the touch stick, or a controller's left
stick) speed up and slow down, **A/D** steer. The Toolbox has a ready-made
**Car** and **Boat** to try straight away.

- **With wheels:** every HingeConstraint with a motor in the vehicle turns its
  wheel so the vehicle rolls forward, and the left and right wheels go at
  different speeds to turn (like a tank). Balls or cylinders on hinges make
  good wheels.
- **Without wheels** (a boat, a hovercraft): the seat pushes the whole thing
  along and turns it. Make the hull out of Wood so it floats.

Its settings are attributes: **MaxSpeed** (top speed, studs a second, 12),
**Torque** (how quickly it speeds up, 10) and **TurnSpeed** (1). Scripts can
read `seat.Throttle` and `seat.Steer` (-1, 0 or 1), `seat.ThrottleFloat` and
`seat.SteerFloat`, and `seat.Occupant`, and can set Throttle and Steer on an
empty seat to drive it themselves. In multiplayer the host runs the physics,
so each driver's keys are sent to it. Roblox files keep VehicleSeats (their
MaxSpeed is scaled like walking speed, 16 there and 6 here).

### Getting dressed (the Avatar page)

In the app, the Avatar page's **Wardrobe** tab shows everything you own,
sorted into Shirts, Pants, T-Shirts, Faces, Hats, Hair and Accessories. Click
something to wear it, and click it again (or its picture in the **Wearing**
row) to take it off. **Take everything off** clears the lot. Body colours,
colour sets and the classic hats are on the **Body & Colours** tab. What you
wear is saved on the server, so it's the same on the website and in every
game.

### Finding games: genres, votes and sorting

On the website, a game's **Configure** page lets its creator pick up to 3
**genres** and how many players fit in one server. The **Games** page has a
button for each genre, a search box that also looks at descriptions and
genres, and sorting by **Most played**, **Playing now**, **Top rated**,
**Newest** and **Recently updated**. Once you've played a game you can give it
a thumbs up or down on its page; cards show how liked each game is.

### Badges: Guts&Bolts badges and game badges

There are two kinds of badges:

- **Guts&Bolts badges** (like **Verified** and **Staff**) are for the whole
  platform. Only staff can give them.
- **Game badges** are made by game creators. On the website, open your game's
  **Configure** page, scroll to **Badges**, and make one (name, description,
  colour). Copy its ID and award it from a script in your game:

  ```lua
  local BadgeService = game:GetService("BadgeService")
  game.Players.PlayerAdded:Connect(function(player)
      BadgeService:AwardBadge(player, "badge-1a2b3c4d5e")
  end)
  ```

  Badges are only given in online servers of the published game (the server
  checks that the request comes from that server's host, for someone who's in
  it). Guests can't earn them. `BadgeService:UserHasBadgeAsync(player, id)`
  knows about badges given during the current server.

Profiles show both kinds in separate boxes, and game pages list their badges.

### Developer products (buy again and again)

Like game passes, but players can buy them as many times as they want: coins, a
revive, a speed boost for a minute. Make them on the game's **Configure** page on
the website (under "Developer products"); each gets an ID. In a script:

```lua
local MarketplaceService = game:GetService("MarketplaceService")

-- pop up a Buy window for someone
MarketplaceService:PromptProductPurchase(player, 12)

-- hand out what they bought
MarketplaceService.ProcessReceipt = function(info)
    -- info.PlayerId, info.ProductId, info.PurchaseId, info.CurrencySpent
    local who = nil
    for _, p in ipairs(game.Players:GetPlayers()) do
        if p.UserId == info.PlayerId then who = p end
    end
    if not who then return Enum.ProductPurchaseDecision.NotProcessedYet end
    who.leaderstats.Coins.Value = who.leaderstats.Coins.Value + 100
    return Enum.ProductPurchaseDecision.PurchaseGranted
end
```

Every purchase is a receipt kept on the Guts&Bolts server. The game is handed each
waiting receipt (when the buyer is in one of its online servers) until
`ProcessReceipt` answers `PurchaseGranted`, so nobody loses what they paid for, even
if the game crashed. Only the buyer and whoever runs a server of that game can see
or close its receipts. The creator gets 70% of every sale.

### Teleporting between games

Games can send players to another published game, like Roblox's
TeleportService. The game's ID is the number in its link (the same one its
page shows):

```lua
local TeleportService = game:GetService("TeleportService")
TeleportService:Teleport(1234, player, { coins = 50 })        -- one player

local options = Instance.new("TeleportOptions")              -- or a group
options:SetTeleportData({ team = "Red" })
TeleportService:TeleportAsync(1234, { player1, player2 }, options)
```

The player's app leaves and joins that game the way **Play** does (an open
server if there is one, otherwise a new one). Over there, `player:GetJoinData()`
gives `TeleportData` (what you sent) and `SourcePlaceId` (the game they came
from), and a LocalScript can use `TeleportService:GetLocalPlayerTeleportData()`.
The data travels through the player's own app, so like on Roblox, don't trust
it for anything valuable (keep coins in a DataStore). A LocalScript can teleport
its own player too. In Studio's play test nobody moves; Output just says where
they would have gone.

### Signing up and logging in

When you connect to a Guts&Bolts server, the site asks you to **Sign Up** or
**Log In** first.

**No account? Play as Guest.** Guests get a name like *Guest 4821* and can play
every game, on their own or in public and private servers with other people.
They can't chat (the chat box is greyed out: *"Sign up to chat with other
users!"*), make friends, or buy and earn Bolts. The server tells a game's host
who's a guest, so a guest's messages never reach anyone. Signing up any time
keeps playing on the same device.

- **Usernames** are one of a kind and can never be used again, not even after
  someone's banned. They're 3-20 letters and numbers with at most one `_`.
  Online, your name everywhere (games, chat, the site) is your username, so
  nobody can pretend to be you.
- **User numbers** count up: the first person to sign up is #2, then #3, and so
  on. A number is never given out twice.
- **User #1 is Guts**, the official staff account. On the staff computer,
  open *Avatar > Your account > Set a password* (username `Guts`) so you can
  log in as Guts on your other devices too.
- Search for people by name, `@username` or `#number`.
- **Log out** is on the Avatar page (under *Your account*).

How logging in works, and why your password stays safe: your account is a
secret key on your device. When you sign up, the app locks a copy of that key
with your password and stores the locked copy on the server. Logging in on
another device downloads the copy and unlocks it there. The password itself
never leaves your device. The server only gets a scrambled token made from
it, so it can't unlock your key. After 5 wrong passwords, that username is
locked for 10 minutes.

**Terms of Service.** Signing up (on the website or in the app) means ticking
that you've read the **Terms of Service** (the link is at the bottom of every
page). In short: Guts&Bolts is a free hobby project nobody makes money from,
it has nothing to do with Roblox, and it's for **adults (18+)**: games can have
strong language, gore and grown-up jokes. The sign-up page says so up front.

### Email, forgot password and two-step verification

On the website, **Settings** (top right) has your account settings:

- **Email:** add one, and type the code we email you to confirm it.
- **Forgot your password?** (on the Log In page) emails a code. Type it with a
  new password. That browser takes over the account with a new key, and every
  other device is logged out: log in on them again with the new password. (The
  Guts account can't be reset by email.)
- **Two-step verification:** when it's on, logging in on a new device needs your
  password *and* a code from your email (the app asks for the code too).
- **Change password.**

**No email on your account, or the code never came?** Open a **support
ticket** instead: the *Support* link at the bottom of every page (or "Open a
support ticket" on the Forgot password page). You don't need to be logged in.
Staff see it on the **Staff** page (*Support tickets* box) and can:

- **Answer** it. The answer shows up on the person's Support page, in their
  notifications if they're logged in, and by email if they left one.
- **Send a reset code** (for "I forgot my password" tickets). The code shows up
  on their Support page with a "pick a new password" form. It works for 24
  hours, and only in the browser that sent the ticket. Only press it when you're
  sure it's really them: you know them, or they told you things only the owner
  would know (a game they made, when they joined, who their friends are).
- **Close** it.

The C++ server (`GutsAndBoltsServer`) has tickets too, but it can't send email
or reset passwords yet, so staff answer with what to do instead.

#### Turning email on

Emails need an email service, because Cloudflare can't send email to anyone
by itself. We use [Brevo](https://www.brevo.com): it's free (300 emails a day)
and only needs an API key.

1. Make a Brevo account.
2. Tell Brevo which address the emails come from. The best way is your own
   domain (like `noreply@gutsandbolts.net`): in Brevo, open *Senders, Domains &
   Dedicated IPs > Domains*, add the domain and follow the steps (Brevo can add
   the DNS records to Cloudflare for you). Emails from your own domain are much
   less likely to land in spam. The quick way is *Senders > Add a sender* with
   an address you already have, and click the link Brevo emails you; it works,
   but more emails end up in spam.
3. Under *SMTP & API > API Keys*, make an API key and copy it.
4. In the Cloudflare dashboard, open the Worker: *Settings > Variables and
   Secrets*. Add a **Secret** `BREVO_API_KEY` (the key) and a **Text**
   variable `MAIL_FROM` (the sender address from step 2). You can also run
   `npx wrangler secret put BREVO_API_KEY`. (Optional: `MAIL_NAME`, the name
   emails come from, "Guts&Bolts" if you leave it out.)
5. Check it works: on the website's **Staff** page, the *Email* box says
   whether email is on. Type your address and press **Send test**, then reload
   the page: it says "sent", or exactly what Brevo didn't like (a wrong key, a
   sender that isn't confirmed yet...).

Resend works too: set `RESEND_API_KEY` instead of the Brevo key (it needs a
domain you own). Until a key *and* `MAIL_FROM` are set, the settings page says
email isn't switched on yet. For testing with `wrangler dev`, put `MAIL_DEBUG=1`
in a `.dev.vars` file: emails are printed in the terminal instead of sent.

#### DDoS and bot protection

**What Cloudflare already does for free:** the site sits behind Cloudflare, so
the huge floods (the kind that try to knock a whole website offline) are
soaked up by Cloudflare before they reach anything of ours, and the website's
own files (pages, pictures in `website/`) are served straight from Cloudflare.

**What the site adds on top**, a risk check in three steps (`worker/shield.js`):

1. **Score every request cheaply.** Before a request reaches the server, the
   worker looks at how fast its IP is going and whether it looks like a real
   browser or the Guts&Bolts apps. No User-Agent, a scripting tool's
   User-Agent, being sent from another website or going very fast each add
   points. More than 1200 requests a minute from one IP is a flood and is
   simply blocked for the rest of the minute.
2. **Challenge the doubtful ones.** A browser with too many points gets a
   "Quick check" popup (Cloudflare Turnstile, usually a tick that passes by
   itself) and then a pass for an hour. The apps can't show a popup, so a
   doubtful app request gets a much tighter speed limit instead. Signing up on
   the website always needs the check (once Turnstile is on).
3. **Protect the slow, expensive steps.** Signing up, logging in, emails,
   uploads, buying, posting and the rest have their own limits inside the
   server, per account and per IP (`COSTLY` in `worker/server.js`). New
   accounts get half; staff have no limit. Pictures are kept by Cloudflare for
   5 minutes, so a flood of picture requests never reaches the server.

Real players shouldn't notice any of it: the limits are well above what a
person (or a whole school sharing one IP) does. To tune them, the speed limits
are in `wrangler.jsonc` (`ratelimits`, keep `SHIELD.limits` in
`worker/shield.js` the same), the points in `SHIELD`, and the expensive-step
limits in `COSTLY` (and `src/server/ServerShield.cpp` for the C++ server).

**Turning the "Quick check" (Turnstile) on.** Until you do, everything above
works except the popup (doubtful browsers get the tighter speed limit instead).

1. In the Cloudflare dashboard, open **Turnstile** (in the left menu) and press
   **Add widget**. Name it "Guts&Bolts", add the hostname `gutsandbolts.net`,
   leave the mode on **Managed** and press **Create**.
2. It shows a **Site Key** and a **Secret Key**. Open the Worker
   (*Workers & Pages > project-guts-and-bolts > Settings > Variables and
   Secrets*) and add a **Text** variable `TURNSTILE_SITEKEY` (the site key)
   and a **Secret** `TURNSTILE_SECRET` (the secret key). Don't put them under
   *Build* variables: the running site never sees those.
3. Done. Check it: sign up for a test account on the website, and the "Quick
   check" popup shows for a second before the account is made. (Using another
   address too, like the workers.dev one? Add it to the widget and list both in
   an optional `TURNSTILE_HOSTNAMES` variable, split by commas.)

**Dashboard settings to know about:**

- **Leave Bot Fight Mode OFF** (*Security > Settings > Bot traffic*). It
  challenges anything that isn't a browser, and the Player and Studio aren't
  browsers, so it would stop them from logging in or playing online. On the
  free plan there's no way to make an exception for them.
- **"Under Attack" mode** challenges every visitor. Only use it in an
  emergency, and know that the apps can't get through while it's on.
- **Optional extra wall:** a free rate limiting rule (*Security > Security
  rules > Create rule > Rate limiting rules*): when *URI Path* equals `/api`,
  with the same IP, more than **200 requests in 10 seconds**, then **Block**
  for 10 seconds. It stops a flood even before the worker runs.

### Authenticator app and the Verified Hat

For more account safety, turn on an **authenticator app** (Google
Authenticator, Microsoft Authenticator, Authy, 2FAS, ...) in **Settings** on
the website: scan the QR code (or type the secret), then type the 6-digit code
the app shows to switch it on. After that, logging in on a new device asks for
the newest code from the app as well as your password. Each code works once.

**Confirm your email** in Settings and you get the **Verified Hat**, a navy cap
with the Guts&Bolts check badge, free. It's an award: it can't be bought.

### Friends and servers (no IP addresses)

Online games go **through the Guts&Bolts server**. Someone's computer still
runs the game (the host), but everyone, host included, only ever connects to
the Guts&Bolts server, and it passes the game's messages along. So:

- nobody sees anybody else's IP address, and
- nobody has to open ports on their router.

**Play** asks the server for the fullest public server of that game that still
has room. If nobody's playing, you quietly become the host of a new public
server, and the next person to press Play joins you.

**Game server machines (nobody has to host).** Leave
`GutsAndBoltsGameServer` running on a computer that stays on (a spare PC, or a
cheap Linux cloud server; no screen or graphics card needed) and it runs games
by itself, with nobody playing on it. When someone presses **Play** and no
server of that game has room, the Guts&Bolts server asks a free game server
machine to start one, and the player joins it a few seconds later. Because
nobody is the host, **the game keeps going whoever leaves**, like a Roblox
server. A game nobody is in closes after a minute. If no machine is free (or
none is running), Play falls back to making you the host, as above.

```
GutsAndBoltsGameServer --slots 4     # run up to 4 games at once
```

It signs in with the account on that computer (the same key the Player app
uses), which must be a **staff** account. It builds with the rest of the
programs. Each game runs in its own copy of the program, so one broken game
can't take the others down. Game scripts on these servers have no
`Players.LocalPlayer` (nobody plays there): use `Players.PlayerAdded` and
`Players:GetPlayers()`, like Roblox server scripts. **Private servers** run on
a game server machine too when one is free (with the same code, and the same
"friends of its owner" rule), so they stay up after their owner leaves. And if a
player was hosting (because no machine was free) and leaves, a free machine takes
that server over and everyone follows, so from then on nobody is the host.

**Setting one up on a cloud server (easiest).** Rent the smallest Linux server
from any cloud company (Ubuntu 22.04 or 24.04; about $5 a month is plenty for a
few games at once). Log in to it and paste this one line:

```
curl -fsSL https://raw.githubusercontent.com/useofscript/Project-Guts-And-Bolts/main/tools/game-server-setup.sh | bash
```

It installs everything, starts the game server, and makes it start by itself
whenever the server reboots (and fetch the newest Guts&Bolts each time). At the
end it prints the machine's **account ID**. In the Player, open the **Staff**
page, search for the start of that ID and press **Make Staff**. A minute later
it's running games. (`journalctl -u gutsandbolts-games -f` shows what it's doing.
`SLOTS=8` in front of `bash` lets it run 8 games at once.) The machine has its own
account, so your own account key never leaves your computer.

**Hat pictures:** a hat or gear made from an imported Roblox file uses a picture
file. Studio uploads it when you publish. For older items whose pictures never got
uploaded (they look plain white on the website), use **File > Upload Missing Item
Pictures** in Studio on the computer that made them.

**When the host leaves**, nobody gets kicked. If a game server machine is
free, it takes the server over (see below). Otherwise the Guts&Bolts server picks
whoever has been in the game longest, their Player starts a new server of the
same game (same name, same private code), and everyone else follows them there
on their own. There's no loading screen: the world stays on screen with a note
at the top for a few seconds. The new host **carries on in the world as it
was**: parts stay where they were, things that broke stay broken, and things
people built are still there. The game's server scripts start again from the
top, though (a round timer starts over), so a script that adds things to the
world when it starts adds them again. If that player can't do it within about
12 seconds, the next one in line is asked.

**Private servers** get a 6-letter code (it shows in the pause menu). Your
friends can join straight from their Friends list; anyone else needs the
code. Private servers never show up for strangers, and Play never puts
strangers in them.

**Paid private servers.** A game's creator can charge Bolts for private
servers (the **Private server price** on the game's Configure page; 0 keeps them
free). Then:

- Players buy one on the game's page (website) or in its **Servers** window (app).
  It lasts **30 days**; buying again adds 30 more.
- Only the person who bought it pays. Friends, and anyone with the code, join free.
- The creator gets 70% of each sale, like other sales. Private servers of your own
  game are always free for you (and for staff).
- The server checks this when a private server starts, so nobody can skip paying
  (`servers.buyPrivate` and `privateBlocked` in `worker/server.js` and
  `src/server/ServerRelay.cpp`).

The **Friends** page has three tabs:

- **Friends:** who's online, who's playing what (with a **Join** button);
- **Requests:** friend requests to accept or decline, and the ones you sent;
- **Add Friends:** search by name.

Profiles also have an **Add Friend** button. Anyone can look at someone's
**Friends**, **Following** and **Followers** lists from their profile (click the
counts, or *See all*); every name in them is a blue link to that person's profile.
Nothing on the site shows anyone's address or when they were last on.

**Messages.** Everyone signed up has an inbox (*Messages* in the top bar; a red
number shows unread ones). Press **Send Message** on someone's profile, or
*New message* and type their user number (like #5). Open a message to read it,
then **Reply** or **Delete**. *Settings > Privacy* sets who can message you:
everyone, friends only or no one. Guests can't use messages, and each account
can send 40 a day.

**Outfits.** On the Avatar page, *My outfits* saves your whole look and puts it
back on in one click (things you no longer own are left off). Up to 30.

**Favourites and Continue Playing.** The star on a game's page adds it to your
favourites. The home page shows your favourites and the games you played last.

All three work the same in the Player app: *Messages* in its menu bar, the
*Outfits* tab on the Avatar page, and the Continue Playing and Favorites rows on
its home page (open a game and press *Favorite*). In October the Player wears the
Gutstober theme like the website; turn it off in *Settings > Gutstober theme*.

**About me, status and My Feed.** Press *Edit* in the About box on your own
profile to write an *About me* and a *Right now I'm...* status (up to 140
letters; 30 a day). The home page's **My Feed** box shows your status and the
latest ones from your friends and the people you follow, newest first. You can
post a new status straight from it.

**Player Badges** come by themselves: *Creator* (make a game), *Builder* (100
visits to your games), *Architect* (1,000 visits), *Friendly* (20 friends),
*Collector* (own 10 catalog items) and *Old Timer* (a year on Guts&Bolts).
Profiles show all six; the grey ones are still locked.

All of this is in the Player app too, on profiles and the home page.

**Join.** If someone's playing and lets you join, their profile (on the site
and in the app) shows *Playing ...* with a **Join** button that puts you in the
same server.

**Privacy** (Settings on the website, or *Avatar > Privacy* in the app):

- **Who can see when I'm online and what I'm playing:** Everyone, Friends
  only, or No one (you look offline to everybody else);
- **Who can join me:** Everyone, Friends only, or No one.
- **Who can see my inventory (and trade with me):** Everyone, Friends only, or
  No one.

**Reporting and blocking.** Profiles, messages, games, catalog and Library items
and groups have a **Report** link: pick what's wrong and (if you like) say what
happened. Nobody is told who reported them, and a reported message is copied for
staff, so deleting it doesn't hide it. Staff work through reports in the
**Reports** box on the Staff page (**Done** or **Nothing wrong**; closing one closes
every report about the same thing). Each account can send 20 reports a day.

**Block** someone from their profile (or tick *Block them too* when reporting
them). Blocking ends your friendship and follows, removes their messages from
your inbox, cancels open trades between you, and after that you can't message,
friend, follow, trade with or join each other, or see each other online. Staff
can't be blocked. *Settings > Blocked people* lists them, with **Unblock**.
All of this works the same in the Player app (blocked people are under
*Avatar > Your account*).

**Following** is one way: no asking. Press **Follow** on a profile (or click
a name on the in-game player list). Profiles show followers and following.
Like Builderman back in the day, **Guts follows everybody**.

### People and Groups

When you're connected to a server, the site gets two more pages:

- **People:** search for players by name. Click someone to see their
  **profile**: their badges (with the blue Verified check), the groups they're
  in, and everything they've published.
- **Groups:** communities anyone can make. Making one costs 50 Bolts (free for
  Verified people). Each group has a colour emblem, an "about" text, a
  **shout** (a message pinned at the top), and a **wall** where members can
  post. Groups can be open (anyone joins) or "ask to join" (an admin lets
  people in).
  - The **Owner** can do everything: change ranks, hand the group to someone
    else, change the settings, or delete the group.
  - Staff can clean up any group.

#### Lyreto servers

Guts&Bolts partners with [Lyreto](https://lyreto.com/), a free chat app (servers,
voice and hanging out). A game (on its **Configure** page, under Community) or a
group (in its **Settings**, for ranks with Manage) can paste its Lyreto server's
invite link. The page then shows a Lyreto card with a **Join on Lyreto** button.
Only `lyreto.com` links are accepted. The website server reads the invite page's
preview (its name, blurb and picture, the same tags chat apps use for link
previews) to fill in the card; the C++ server keeps the link but shows the plain
card, since it doesn't fetch web pages.

#### Ranks

Every group starts with three ranks: **Owner**, **Admin** and **Member**. The owner
can rename them and add more (up to 10 in all). Each rank has a **level** from 1 to
255 (higher is more in charge) and a set of permissions:

| Permission | What it lets them do |
|---|---|
| Shout | Post the group's shout |
| Manage | Let people in, remove people, delete wall posts, change the about text |
| Ranks | Change other people's ranks |
| Add games | Put their own games in the group |
| Group Bolts | See the group's Bolts and pay members from them |

You can only remove people, or change their rank, when they're **below** you, and
only to a rank below yours. Deleting a rank makes everyone in it a Member.

#### Group games and group Bolts

A game can belong to a group. On the game's Configure page (website), or the
group's Games tab (app), pick the group. You need a rank with **Add games**.

- The game shows as the group's on its page and in lists ("by Builders Inc").
- When someone buys one of its **game passes** or **developer products**, the
  seller's share goes into the **group's Bolts** instead of to the person who made it.
- People with the **Group Bolts** permission see the balance and its history, and
  can **pay** any member from it. Everyone's payouts are written down in the history.
- The owner can take any game out of the group; whoever added a game can take theirs
  out too. Deleting a group gives its games back to their makers, but its Bolts are gone.

Every request the apps send is signed with the player's account key, so the
server always knows who is asking. Nobody can spend someone else's Bolts or
pretend to be staff. The server doesn't use encryption (TLS), so treat
everything on it as public: don't upload anything secret.

### The text filter

Things people write for other people to read go through a text filter, on the
website server (`worker/textfilter.js`) and the C++ server (`src/core/TextFilter.cpp`)
alike. That covers in-game chat, messages, profile blurbs and statuses, names and
descriptions of uploads, game passes, badges, groups and servers, and group posts
and shouts.

If something has any of these in it, the whole thing is replaced with
`[ Content Deleted ]`, like classic Roblox:

- Hateful slurs, even when written with spaces, dots or look-alike characters
  (like `1` for `i`).
- Links, except links to gutsandbolts.net.
- Email addresses and phone numbers, so nobody shares personal info.

- Usernames can't contain slurs at all: "That username isn't allowed." A guest name
  in a game that the filter would catch becomes "Player".
- Searches, staff notes and report reasons aren't filtered, so staff see exactly
  what was written.

### Upload review

New decals, sounds and T-shirts from creators who aren't Verified wait for a staff
check before anyone else can see or hear them, like Roblox's moderation queue.

- Until then, only the creator and staff can see it. Its page says "Waiting for a
  staff check", and in games it doesn't show up (or play) for other players yet.
- Staff find the list under **Uploads to check** on the Staff page (website and
  Player). They can look or listen, then press **OK** or **Turn down** (with a
  reason the creator sees).
- Replacing the picture or sound of a checked upload sends it back for a new check.
- Verified creators and staff skip the check.

### Notifications (the bell)

A bell next to your name (top of the website, and top right in the Player) shows
how many new things happened. Click it to see them; that marks them read.

You get one when someone sends you a friend request, becomes your friend, follows
you, buys something you made, sends or answers a trade, asks to join your group or
lets you in, sends you a gift, and when staff check something you uploaded. Each one takes you to the
right page. The newest 50 are kept. Both servers do the same (`notify` in
`worker/server.js` and `src/server/Server.cpp`).

### The staff action log

Everything staff do is written down: bans and unbans, warnings, badges (like
Verified and Staff), Bolts given or taken, upload checks, closed reports, and
games, items, comments or game screenshots they deleted that weren't theirs. The **Staff** page (on
the website and in the Player) shows the newest 200; the **Log** button next to a
person shows just what was done by or to them. The newest 3,000 are kept
(`staffDid` in `worker/server.js` and `src/server/ServerSafety.cpp`, which saves
`stafflog.json`).

### Featured games

Staff can pick games to show off. On a game's page, staff see a **Feature** button
(and **Unfeature** to take it back off). Featured games get a gold "Featured" tag,
show up in the **Featured Games** row at the top of the home page (website and
Player), and can be picked with the **Featured** sort on the Games page. Only public
games can be featured, and the game's creator gets a notification when it happens.
Newest picks come first. (`admin.feature` in `worker/server.js` and
`src/server/Server.cpp`; it's written to the staff action log too.)

### Comments under games

Every game's page has comments (the website's game page, and the game window in the
Player). Anyone can read them; signed-up players can write one, up to 200 letters,
one every 15 seconds. The text filter applies, like everywhere else.

- You can delete your own comments. A game's creator and staff can delete any comment
  on it, and the creator can turn comments off on the game's Configure page.
- Each comment has a **Report** link, and staff see a copy of it with the report.
- The creator gets a notification when someone comments. Blocked people's comments
  are hidden from you, and they can't comment on your games.
- The newest 500 comments are kept (`comments.*` in `worker/server.js` and
  `src/server/ServerSocial.cpp`).

### The Forum

A classic forum, on the website (the **Forum** tab) and in the Player app. It's
split into boards:

| Board | What it's for |
|---|---|
| News & Announcements | Updates from the Guts&Bolts team (only staff can start threads here) |
| Help | Stuck? Ask how to do something |
| Scripting Helpers | Lua questions and scripts that won't work |
| Building & Studio | Tips, tricks and things you built |
| Game Ads | Show off a game you made |
| Trading | Find people to trade Limiteds with |
| Off Topic | Anything else |

- Signed-up players can start a thread (a title up to 80 letters, a post up to
  3000) once a minute, and reply once every 15 seconds. The text filter applies.
- Threads show 20 posts a page. The newest reply bumps a thread to the top of
  its board. A thread holds up to 500 posts, and each board keeps 1000 threads.
- You can delete your own posts. Deleting the first post deletes the whole thread.
- Every post has a **Report** link. Staff can delete any post, **pin** a thread to
  the top of its board, or **lock** it so nobody can reply.
- Whoever started a thread gets a notification when someone replies.
- Blocked people's posts are hidden from you, and they can't reply to your threads.
- The code is `forum.*` in `worker/server.js` and `src/server/ServerForum.cpp`.

### Creator stats (how your stuff is doing)

**Create > Stats** (on the website and in the Player) shows how everything you made
is doing: plays, people playing right now, favorites, likes, sales and Bolts earned,
with little bar charts for each of the last 30 days. Hover a bar to see its day.

- A **play** counts when someone else opens your game (you opening your own doesn't).
- Passes and developer products bought inside a game also count on that game, so a
  game's chart shows everything it earned.
- Days are counted in UTC and kept for 60 days (`tally` and `creator.stats` in
  `worker/server.js` and `src/server/Server.cpp`).

### Bolts (the currency)

(With a server, your Bolts live on the server instead, and everything below
works the same, just online.)

**Bolts** are the Guts&Bolts currency, like Robux on the old Roblox site. Your
balance shows in the top-right of the site; click it (or **Bolts** in the nav
bar) to open the Bolts page.

- **Earning:**
  - 100 Bolts to start.
  - 25 Bolts every day (click **Claim**).
  - 5 Bolts for every 5 minutes you spend playing games, up to 50 a day.
  - Bolts codes from the staff.
- **Spending:** catalog items can have a price. Staff set it when they make
  the item, and it's part of the item's signature, so nobody can change it.
  Buy an item once and it's yours to wear whenever you like.
- **Bolts codes:** staff make them on the **Staff** page for a player's account
  ID. Each code works once, and only for that account. Players paste it on the
  Bolts page.

Your Bolts are kept in `bolts.json` next to your account key, signed with that
key. Editing the file by hand breaks the signature, and the app resets it. The
same honest limit as above applies: with no central server, someone who changes
the app's code could still cheat on their own computer.

**Setting up the staff account (project owner only):** double-click
`tools/Staff Setup.bat` (Windows) or `tools/Staff Setup.command` (Mac), or run
`python3 install.py --staff`. That makes your computer's account the "Guts"
account, and the Player then shows **Staff** and **Create Item**. Back up the
`account.key` file it tells you about. To make every copy of the game recognise
you, your public ID goes into `src/core/OfficialKey.h`.

> Honest limits: there's no central server yet, so the host of a game checks
> everyone who joins, and players check the host. A modified copy of the game
> could still lie about *other* players in a game it hosts.

Three sample games come with it: **Obby of Doom**, **Demolition Yard** and
**Night Plaza** (built by `tools/make_sample_games.py`).

## Log in with Guts&Bolts (for partner apps)

Partner apps (just [Lyreto](https://lyreto.com) for now) can let people link their
Guts&Bolts account, like "Connect Discord" buttons. It's standard OAuth 2.0 (the
authorization code kind), and the app only ever gets a user's **id** (their user
number), **username** and **avatar** picture.

1. Send the person to `https://gutsandbolts.net/api/oauth/authorize?response_type=code&client_id=<id>&redirect_uri=<your callback>&state=<random>`.
   They log in if they need to and press **Approve**, and come back to
   `<your callback>?code=...&state=...` (or `?error=access_denied` if they press Cancel).
2. From your server, swap the code (it lasts 5 minutes and works once):
   `POST https://gutsandbolts.net/api/oauth/token` with a form (or JSON) body
   `grant_type=authorization_code&code=...&redirect_uri=...&client_id=...&client_secret=...`
   (or the id and secret as HTTP Basic auth). You get `{ access_token, token_type: "Bearer", expires_in, scope }`.
3. `GET https://gutsandbolts.net/api/oauth/userinfo` with `Authorization: Bearer <access_token>` gives
   `{ "id": "5", "username": "kermit", "avatar": "https://gutsandbolts.net/avatar/5.png?v=..." }`
   (`avatar` can be `null`). Tokens last a year, or until the person disconnects the app in Settings.

The apps and their callback addresses are listed in `CONNECT_APPS` (worker/server.js) and
`kConnectApps` (src/server/ServerConnect.cpp). Each app's **client secret** is made on the
website's Staff page (Connected apps box, official Guts account only): it's shown once,
so copy it and send it to the app's owner privately. Making a new one stops the old one working.

## Graphics & performance

**Settings** (in both apps) has VSync, an FPS limit (or unlimited), an FPS
counter, quality presets (Low / Medium / High / Ultra), shadow detail and
style, ambient occlusion, bloom, anti-aliasing and render scale. They're
saved to `settings.json`.

The renderer uses HDR with physically based shading, soft contact-hardening
sun shadows, point and spot lights, sky reflections, SSAO, bloom, ACES tone
mapping and FXAA. It needs OpenGL 4.1, so it runs on Windows, macOS and Linux.

## Tests (checking nothing broke)

The `worker/test` folder has automatic checks for the website's server. They
start a real copy of the server in memory (no internet needed) and try things the
way a player would: signing up, getting the daily Bolts, buying an item, sending
a gift, rating a Library model, commenting, the filter, a game's version history
and restoring, and staff tools like warnings and bans. They also check
that the update log is filled in properly, that it says "Library" and not
"Marketplace", and that the C++ filter has the same word list as the website.

Run them with Node 22.5 or newer:

```
node --no-warnings --import ./worker/test/register.mjs --test 'worker/test/*.test.mjs'
```

GitHub runs them (plus a check of every website script for typing mistakes) on
every change, as the **Tests** job. (Before they start, the update log is gathered
from `worker/updates/`, one file per update, and the built-in roadmap from
`worker/roadmap/`, one file per item, by `worker/gather-updates.mjs`.
Cloudflare does the same before each deploy.) To add a test, copy one of the
`*.test.mjs` files; `makeServer()` in `helpers.mjs` gives you a fresh server and
`s.call(player, 'op', {...})` asks it to do something.

Studio's multiplayer test has its own check (Linux, after building, needs
`xvfb-run`). It makes a small game whose server Script prints when each player
joins and answers them through a RemoteEvent, starts a 2-player test with no
screen, adds a third player halfway, closes Player2's window, and checks every
player's LocalScript got the answer, the server saw Player2 leave, and nothing
is left running:

```
python3 tools/test_multiplayer.py            # --players 3 --seconds 40 to change it
```

Some engine features have a check script that runs inside a play test with no
screen (after building, Linux, needs `xvfb-run`). Each prints `PASS`/`FAIL`
lines and a `DONE (N failed)` line:

```
xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_messaging.lua)" --screenshot /tmp/m.png --frames 1500
xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_sound_groups.lua)" --screenshot /tmp/s.png --frames 600
```

To check messages reach *another* running server, start a second copy first with
`--test-command "MESSAGING_PARTNER = true $(cat tools/test_messaging.lua)"` and
`--frames 2400` (it waits around to answer); the first copy then also checks it hears the partner.

## Building by hand

You need CMake, a C++20 compiler, and GLFW, GLEW and GLM where `find_package`
can see them. Dear ImGui, ImGuizmo, Lua 5.4, nlohmann/json, miniaudio and Monocypher are
downloaded automatically.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/GutsAndBolts          # the editor
./build/GutsAndBoltsPlayer    # the player app
```

On Windows with MSYS2, add `-DCMAKE_PREFIX_PATH="C:/msys64/mingw64"`. The
runtime DLLs are copied next to the programs automatically.

Handy command-line options: `GutsAndBolts --open file.gbscene --play`,
`GutsAndBoltsPlayer file.gbscene`, `GutsAndBoltsPlayer --join 192.168.1.20`.

## Layout

```
install.py, Install.bat, Install.command, install.sh    the installer
android/                the Android app (Gradle project around the same C++ Player)
games/                  sample games (copied next to the apps)
catalog/                official catalog items (signed; copied next to the apps)
tools/                  make_sample_games.py, test_multiplayer.py
src/
  core/                 AppWindow, Log, Settings, Paths, CrashHandler, Account (keys / signatures)
  renderer/             SceneRenderer + Shaders (HDR, shadows, post-fx), Mesh, Camera, ...
  scene/                Scene tree, Player (R6 rig), Physics, Ragdoll, Particles,
                        Effects, Serializer (save files / undo)
  scripting/            Lua engine and Roblox-style API
  game/                 GameSession (play mode), Hud, Profile, SettingsWindow, Badges, Catalog,
                        TouchControls
  net/                  TCP sockets and multiplayer host / client
  editor/               Studio: Editor, Premades, panels/
  player/               Guts&BoltsPlayer app
```
