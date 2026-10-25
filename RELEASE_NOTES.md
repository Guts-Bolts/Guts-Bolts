# The Floor Is Now A Crime Scene (October 25, 2026)

The engine gore overhaul is being scoped around the existing R6 ragdoll and dismemberment systems, with the next iteration focused on gravity-driven blood streaks, persistent floor pooling, surface-aware splashes, configurable anatomical visual layers, and bounded multiplayer effects. The design prioritizes convincing results without unbounded particles or per-droplet network replication. This change records the implementation plan; the simulation features themselves are not yet implemented.

# The Bug Has a Paper Trail (October 25, 2026)

Studio can export a multiplayer test session bundle from **Mode > Players**. The JSON file combines the scene snapshot used for the test with a chronological timeline of server and Player-window output. Each line includes its source, severity, displayed log time, and seconds since the test started. The export path is printed in Studio Output and the bundle is saved under the temporary `GutsAndBoltsTestSessions` directory.

This is a diagnostic replay timeline, not deterministic gameplay playback: input events and physics state are not recorded. The scene snapshot may include unpublished game content or scripts, so review the file before sharing it.

# Your Game Before the Public Sees It (October 25, 2026)

Studio's publish dialog now includes a readiness review before a game goes public.

## Checks included
- Scene integrity: empty names, duplicate sibling names, non-finite transforms, near-zero part scale, and parts without geometry.
- Performance risk indicators for object counts, parts, unanchored physics, enabled lights, particle emitters, and script source size.
- Local texture references that do not resolve on this computer.
- Static script keyword checks for APIs that deserve manual security review.

## Workflow and limitations
- Open Scene Validator or Performance Doctor directly from the publish dialog.
- An empty game name disables the publish actions.
- Warnings are advisory. Static keyword matches can be false positives, remote and server-hosted assets are not verified, scripts are not executed, and the checklist cannot prove runtime safety or performance.

# Catalog Repair Shop (October 25, 2026)

Studio's **Catalog Repair Shop** lets a creator load an existing 3D accessory by catalog ID, adjust its placement on the accessory mannequin, edit its mesh in Modeling mode, and update the original listing.

## Workflow
- Open **Avatar > Accessories** and paste the existing accessory's catalog ID.
- Load it into the scene, then adjust its placement using the mannequin, viewport tools, or numeric offset/rotation/scale controls.
- Select a mesh part and use Modeling mode to repair the geometry.
- Save the placement and choose **Update existing item** to publish the change to the same item ID.

The server remains authoritative: creators can update their own items, and staff can repair accessories with an audit entry. Other users cannot update someone else's item. This workflow supports 3D accessories (hats, hair, face accessories, neck, shoulder, and waist items); clothing, bundles, and non-model catalog items are not editable through this window.

# Yesterday, But Make It Recoverable (October 25, 2026)

Project Time Machine adds named scene checkpoints to Studio, so large edits have a recovery point beyond the regular undo history.

## Features
- Create named checkpoints at important milestones.
- Compare a checkpoint with the current scene to see added, removed, and modified objects.
- Restore a checkpoint after automatically checkpointing the current state first.
- Store up to 15 checkpoints per project in a local file next to the application.
- Keep checkpoint history separate by project path (or project title for unsaved projects).

Checkpoints are local to this computer and are not uploaded or synced. Restoring changes the open scene in memory; save the project normally when ready.
# Your Scene Is Under Review (October 25, 2026)

Studio's **Scene Validator** checks common project-structure and reference mistakes before publishing.

## Checks included
- Broken or incorrectly typed references for model PrimaryParts, constraints, Trail/Beam attachments, SoundGroups, and fluid systems.
- Tool setup (a descendant Part named `Handle`), SoundEffect parent placement, and missing Part geometry.
- Non-finite transforms and zero/near-zero part scale.
- Duplicate sibling names that can make name-based script lookups ambiguous.
- Empty names and enabled scripts with no source, as informational or warning-level findings.

## Workflow
- Filter by errors, warnings, or informational notes.
- Search findings by object path, category, or message.
- Select the affected object in the scene.
- Copy a full report for review or collaboration.
- Findings are cached and rescanned automatically while the window is open, with a manual validation action.

## Limitations
The validator inspects serialized scene structure and references; it does not execute scripts, prove runtime behavior, or automatically repair objects. Review each recommendation before changing a project.

# Doctor, My Frame Rate Is Dying (October 25, 2026)

Studio's new **Performance Doctor** explains likely performance problems using recent frame timings and scene complexity.

## What it checks
- Frame pacing, including the 95th-percentile frame delta and the share of recent frames over 33 ms.
- Rolling frame samples reset when switching between Edit and Play/Simulate, avoiding a mixed-mode sample window.
- Editor-thread update/UI timing, viewport CPU timing and simulation update timing.
- Heuristic risk indicators for high part counts, unanchored physics bodies, enabled lights, particle emitters and large script source.
- The five largest script sources, with a direct action to open each in the editor.
- A copyable diagnosis report containing measurements, scene context, findings and recommended next steps.

## Accuracy notes
- Scene thresholds are risk indicators, not proof that an object count caused a slowdown.
- Script source size is not execution-time data.
- Frame deltas include waiting/presentation; CPU subtimings exclude GPU execution. Confirm suspected GPU bottlenecks with an appropriate external profiler.
- The doctor does not modify the project automatically.

# Who Depends on What? (October 24, 2026)

Studio now has a **Project Dependency Map** for understanding how scripts reference one another.

## What changed
- A searchable list of all scripts, with incoming and outgoing relationship counts.
- A detail view showing detected dependencies, scripts that reference the selected script, and unresolved `require()` expressions.
- Double-click a script in the list or use **Open script** to jump into the code editor.
- The map refreshes automatically and includes a manual refresh button.
- A website update entry and automated regression test keep the feature documented.

## Accuracy notes
- Relationships are inferred from static `require()` expressions and named `WaitForChild()` / `FindFirstChild()` lookups.
- This is not a runtime call graph. Dynamic module selection, aliases, and computed paths may not be resolvable; those `require()` expressions are reported as unresolved rather than confirmed missing scripts.

# Studio: Better Tools, Fewer Guessing Games (October 24, 2026)

Studio's diagnostics and everyday creation tools got a substantial pass.

## Studio improvements
- Script Analysis highlights likely missing references and dependency issues.
- Scene analysis flags potentially expensive scene setups before they become a performance problem.
- The Performance Monitor tracks rolling frame times, average and p95 frame time, frame-rate history, editor/viewport CPU timings, simulation update time and scene counts.
- Copy a performance summary to the clipboard to share measurements or compare before and after a change.
- The Accessory maker adds numeric mannequin-relative offset, rotation and scale controls, plus reset actions.
- The Bundle maker can select body parts and edit their position, rotation and size without repeatedly entering Modeling mode.
- The Properties panel adds property search and a compact two-column inspector for common properties.

## Notes
- The monitor's CPU timings are editor-thread timings, not GPU timestamp measurements. Frame delta includes waiting/presentation time.
- Scene and script diagnostics are advisory; review findings in context before changing a project.

# Guts&Bolts 0.7.4: Fuck the Language Barrier (October 21, 2026)

The world has approximately 7,000 languages, and we got tired of not
understanding any of them. Guts and Bolts now supports multiple languages, so
you can experience the exact same horrible physics in a language you actually
understand.

## Patch notes
- Added multiple language options.
- Translated menus, buttons, and other things we swear were English yesterday.
- Added international support. The Geneva Convention has not reviewed our
  methods.
- Fixed a bug where players couldn't understand each other. They can now
  insult each other internationally.
- Added several new ways to misunderstand instructions.
- English has been demoted from its position of global dictator.

## Also in this update
- A much fuller Terms of Service, still in plain words.
- Resizing and moving parts with the handles in Studio no longer shakes.
- Servers stay up without a host: when the host leaves, the game carries on
  in the same world, and private servers can run on game server machines.
- The download page counts VR: Guts&Bolts runs on 5 platforms.

# Guts&Bolts 0.7.3: Going Once, Going Twice, Going VR (October 20, 2026)

Put on a headset and you're inside your character. Studio grew a Blender
under its hat, with UV mapping, bones and a body bundle maker. Exclusive items
go to the highest bidder, and ragdolls now bleed, flail and blow apart.

## New
- **VR**: play any game in a VR headset (OpenXR). Your character's head and
  arms follow yours, menus and chat float in front of you, and you click by
  pointing. A keyboard pops up in the headset when you need to type.
- **Meta Quest without a PC**: the new GutsAndBoltsPlayer-Quest.apk download
  runs on Quest 2, 3, 3S and Pro by itself. Install it with SideQuest.
- **VRService for scripts**: `VREnabled`, `GetUserCFrame` and
  `UserCFrameChanged`, so a script can stick a sword to your hand.
- **Blender-style modeling**: G / R / S follow the mouse, right-click menus,
  loop cuts and a pile of new mesh tools.
- **UV mapping**: press U to unwrap, mark seams, use the new UV Editor and
  wrap your own picture around a MeshPart.
- **Armatures**: put bones inside a MeshPart, bind them with automatic
  weights, paint weights and pose them in the Animation Editor.
- **Body bundles**: staff can reshape the character's body in Studio's new
  Bundles window and sell it as a bundle.
- **Bundles in the catalog**, starting with the Headless Head (3,100 Bolts).
- **Auctions**: Exclusive items can go up for auction, and only the top
  bidders get one.
- **Log in with Guts&Bolts** on Lyreto: take your name and avatar with you.
- **Partner badge** for friends of Guts&Bolts.
- **Bot and flood protection**: a guard in front of the server stops request
  floods and bots, gives doubtful visitors a quick "are you a person?" check,
  and puts limits on sign-ups, logins, uploads, buying and posting.
- **Way more imports in Studio**: squashed Sketchfab models, about 30 more 3D
  formats, and new picture and sound types.

## Changed
- Blood pumps with the heartbeat, smears, leaves footprints and dries.
- Ragdolls catch themselves, clutch their wounds and get back up, and
  point-blank explosions blow bodies apart into chunks.
- Tall stacks stand still, boxes stay put on slopes, and fast things stop
  passing through walls.
- The front page and download page show off what the engine can do.
- Hats on the website are smooth-shaded like in the game.
- The Home, Avatar and Settings pages load faster, and so do item pictures.

## Fixed
- The chat filter no longer deletes Lua code like `workspace.Shop.Door`.
- Hats stay on your head when an animation turns it, and the Bouncy
  Animation Pack no longer sends your head flying.
- Pictures waiting for a staff check stay hidden properly.
- The Sign Up page, the Avatar page and the update log look right on phones.

# Guts&Bolts 0.7.2: Feature Creep (October 19, 2026)

We said "just a few things" and then added twenty-six. Games can dance, sell
emotes, light things on fire, paint the sky, shake the camera and talk across
servers. Studio can test with four players at once, and the website learned to
gift, rate, roll back and say sorry.

## New
- **Emotes**: `/e dance`, `wave`, `cheer` and more play real animations
  everyone sees. Press B for the emote wheel (phones get a smiley button). Buy
  emotes and animation packs in the catalog and equip them on your Avatar page.
- **Particles**: ParticleEmitter, Fire, Smoke and Sparkles. Drop one in a part
  and it sprays, with Rate, Color, Size, gravity and more, and `Emit(50)` for
  a burst.
- **ClickDetectors**: put one in a part or model and players can click it to
  make things happen.
- **Eleven new materials**, like Brick, Cobblestone, Marble and Diamond Plate,
  each with its own look and grip.
- **Three new joints**: ball sockets, sliders, and joints that slide and spin.
- **Collision groups**: choose which parts bump into which, so players can
  walk through each other and team doors let only one team in.
- **Your own controls**: games can bind keys and controller buttons, and give
  phones their own on-screen buttons.
- **Better game UI**: little 3D worlds inside UI, plus fading, growing and
  keeping shape on every screen.
- **Skies and screen effects**: paint your own Sky, and add bloom, blur,
  colour grading, sun rays and depth of field in Lighting.
- **Camera control**: cutscenes, locked first person, zoom limits and more.
- **Cross-server messages**: MessagingService lets every server of a game
  talk to the others.
- **Sound groups and effects**: turn whole sets of sounds up or down, and add
  echo, reverb, chorus, distortion and more.
- **Teams and spawns**: team SpawnLocations, CharacterAdded/CharacterRemoving,
  PrimaryPart and PivotTo, and scripts can use `+=`, `continue`, types and
  `{name}` strings.
- **Multiplayer testing in Studio**: test with 2, 3 or 4 players on your own
  computer.
- **Smarter AI helpers**: Studio's Assistant and connected AI apps reach every
  service, use the same names as your scripts, and can publish when you ask.
- **Gifts**: buy a catalog item for a friend, with a note.
- **Gift boxes and Exclusive items**: own a gift box when it goes off sale
  and a secret Exclusive item drops into your inventory.
- **Lyreto servers**: games and groups can show their Lyreto chat server on
  their page, with a Join button.
- **Ratings**: thumbs up or down on Library models, and a "Best rated" sort.
- **Screenshots**: games can show up to 5 screenshots on their page.
- **Past trades**: a new tab with every finished trade.
- **Version history**: every publish is kept, and one click puts an old
  version back.
- **Ban appeals and account deletion**: ask staff to look at a ban again, or
  delete your account from Settings.
- **Password help without an email**: ask staff on the new Support page.

## Changed
- Hats, shirts and pants lost the Colour box: hats come from Studio, and
  shirts and pants are always a picture made from the template.
- The server you can run on your own computer caught up with the main one
  (Limiteds, trades, votes, icons and more).

# Guts&Bolts 0.7.1: We Can Hear You. Unfortunately (October 7, 2026)

Voice chat is here, so now you can hear everyone. We also added terrain, a
forum, games owned by groups, paid private servers and inventories on
profiles.

## New
- **Voice chat**: turn it on in the in-game Settings, then hold V to talk
  (phones get a mic button, and there's an Open Mic option). Voices come from
  each player's character, loud up close and fading with distance. A speaker
  icon shows who's talking, and you can mute anyone from the player list.
  Creators can turn it off in Game Settings or with
  `workspace.VoiceChatEnabled`.
- **Terrain**: a new TERRAIN tab in Studio. Generate rolling hills, then
  Raise, Lower, Smooth, Flatten and Paint with brushes, in grass, dirt, sand,
  rock, snow and mud. Characters walk up slopes, loose parts roll down them,
  and pathfinding works on it. Scripts can change it with `workspace.Terrain`
  (`FillBlock`, `FillBall`, `Generate`, `Sculpt`, `GetHeight`).
- **Forum**: boards for Help, Scripting, Building, Game Ads, Trading and Off
  Topic, plus News & Announcements. Start threads, reply, get a notification
  when someone answers, and report posts. Staff can delete, pin and lock.
- **Group games**: put a game in your group and its game pass, product and
  private server sales go to the group's Bolts. Owners and treasurers can pay
  members, and every payout is written down. Groups can make up to 10 ranks of
  their own, each with its own permissions.
- **Paid private servers**: creators can set a price in Bolts for 30 days of a
  private server. Buying again adds 30 more days, friends still join free with
  the code, and creators get 70% of each sale.
- **Inventories**: every profile has an Inventory box and a See all page,
  sorted into Accessories, Clothing, Faces, Gear and Limiteds (with the copy
  numbers someone holds). You choose who can see yours. People it's hidden from
  can't trade with you.
- **Developer products**: sell things players can buy again and again, like
  coins or a revive, with `MarketplaceService:PromptProductPurchase` and
  `MarketplaceService.ProcessReceipt`. Every purchase is kept until the game
  hands it out.
- **Creator stats**: a Stats tab on the Create page with plays, players,
  favorites, likes, sales and Bolts for everything you made, and bar charts
  for the last 30 days.
- **Game comments**: comment under any game. Creators can delete comments or
  turn them off, and anyone can report one.
- **Featured Games**: staff can feature great games. They show first on the
  home page, with a gold Featured tag and a Featured sort.
- **Staff action log**: every ban, warning, badge, Bolts change, upload check
  and delete is written down on the Staff page.

## Changed
- The website's server is now tested automatically every time something
  changes.

## Fixed
- Saving an item's Edit box (like making it a timed item) works again, and the
  Colour box that didn't do anything is gone.

# Guts&Bolts 0.7.0: We Outsourced the Update (October 7, 2026)

An AI wrote most of this update. It taught other AIs to build your games, put
a bouncer on the uploads, washed the chat's mouth out with soap, and hung a
bell on the door.

## New
- **TeleportService**: games can send players to another game, and bring some
  data along, like Roblox. `TeleportService:Teleport` and `TeleportAsync` send
  one player or a group by game ID; `TeleportOptions:SetTeleportData` and
  `player:GetJoinData()` carry the data. Players land in an open server, or a
  new one if none is open.
- **Play links on a Mac**: the website's Play and Join buttons (and "Edit in
  Studio") open the Player on a Mac too. Open the Player once and it adds a
  small Guts&Bolts Player app to Applications, which macOS needs for links.
- **Text filter**: anything with slurs, links or personal info becomes
  `[ Content Deleted ]`, like classic Roblox. It covers chat, messages,
  profiles, group posts and the names and descriptions of things, and catches
  slurs hidden with spaces, dots or look-alike letters. Links to
  gutsandbolts.net are fine.
- **Upload review**: new decals, sounds and T-shirts from creators who aren't
  Verified wait for a staff check before anyone else sees them. Staff get an
  "Uploads to check" list; turned-down uploads say why.
- **Notification bell**: one place for friend requests, new friends and
  followers, sales, trades, group requests and upload checks, on the website
  and in the Player. Click one to go straight to it.
- **AI helpers really understand Studio**: the Assistant tab and outside AI
  apps connected through MCP (like Claude Desktop or Claude Code) get a full
  guide to how Guts&Bolts works, and every tool says when to use it, what it
  needs and what it gives back.
- **Exact answers**: tools reply with clear data (object ids like `#42`,
  positions, what really changed) and error codes that say what to try next.
  If two objects share a name, the AI asks which one instead of guessing.
- **Finding and fixing problems**: new `find_objects`, `get_errors`,
  `diagnose_object` ("why won't my barrel fall?") and `validate_scene` tools.
  Script edits are checked for Lua mistakes straight away.
- **The engine reference**: AIs can read what the engine can and can't do, its
  words for things, which way is up, and step-by-step recipes
  (`get_engine_info` and the `gutsbolts://` MCP pages).
- **Power tools**: make a physics object in one go, set off explosions, bake the
  navmesh, find walking routes and send characters walking.
- **Watching the game**: AIs can see the running playtest (frame rate, what's
  moving, where characters are) and pause it or step it one frame at a time.
- **Save points**: an AI can save the game, try something, and roll it back
  (plus redo).
- **Ready-made MCP prompts** for common jobs, like "make this physical" or
  "why isn't this working?".

## Changed
- Deleting is marked as risky, so AIs only delete what you clearly asked for.
- Usernames and guest names with slurs in them aren't allowed.

# Guts&Bolts 0.6.9: Total Goblin Mode (October 6, 2026)

## New
- **Game server machines**: games can run with nobody hosting, so nobody gets
  kicked when someone leaves. Leave GutsAndBoltsGameServer running on any
  computer (or a cheap Linux cloud server) and it starts games by itself.
- **LocalScripts and RemoteEvents**: LocalScripts run on each player's own
  computer and talk to the server with RemoteEvents and RemoteFunctions, like
  Roblox. Players who join no longer get the code inside your server Scripts.
- **Tools for everyone**: people who join someone else's game get StarterPack
  tools, can pick them up, hold them and swing them, and everyone sees it.
- **DataStores online**: saved game data lives on the Guts&Bolts server, so it
  follows players to every server of a game.
- **Game UI**: TextBox, ScrollingFrame, UIListLayout, UIGridLayout and
  UIPadding, plus BillboardGui (floating signs) and SurfaceGui (screens on
  walls).
- **ProximityPrompts**: walk up to something and a little "E Open" card pops
  up. Hold-to-use works too.
- **Highlights**: colour a part or character with an outline, even through
  walls.
- **Trails and Beams**: streaks behind swords and glowing lasers between two
  points, with ColorSequence and NumberSequence.
- **Controllers**: play with an Xbox, PlayStation or Switch controller,
  menus included. Scripts can read the buttons and sticks.
- **Report and block**: report anything that breaks the rules, block people,
  and staff get a Reports list. Works on the website and in the Player.
- **Creator hammer**: the game's creator has a little hammer next to their
  name on the leaderboard.
- **require in the server dev console**, plus `require(ID)` for Library
  models (their MainModule), like Roblox. The server side of the dev console
  stays for the game's creator only.

## Fixed
- Putting a tool away and taking it out again no longer drops it on the ground.

# Guts&Bolts 0.6.8: Old Faithful (October 6, 2026)

## New
- **Classic BodyMovers are back**: BodyVelocity, BodyPosition, BodyGyro,
  BodyAngularVelocity, BodyThrust and BodyForce, next to the newer movers
  (LinearVelocity, AlignPosition, AlignOrientation, AngularVelocity,
  VectorForce, Torque). They push whole welded builds and work on characters
  too, so old vehicles and flying scripts keep their handling. Roblox files
  bring them in with the right speeds and forces. New `part:GetMass()`.
- **Sit anywhere**: `Humanoid.Sit = true` or `/e sit` sits you down where you
  stand. Jump to get up.
- **Game UI**: UIShadow (soft drop shadows), UIBlur (frosted glass over the
  world) and UICorner corners rounded one at a time (TopLeft, TopRight,
  BottomRight, BottomLeft).
- **Orthographic camera** for 2D, isometric and puzzle games
  (`workspace.Orthographic`, or the Player tab in Studio). Studio's view can go
  orthographic too: the Ortho button or numpad 5.
- **Water pushes things**: flat sides push hard, edges hardly at all, so a
  river turns the new Water Wheel. Water parts have a Drag setting.
- **Game passes**: buy them on game pages or inside games
  (`MarketplaceService:PromptGamePassPurchase`, `UserOwnsGamePassAsync`).
- **Studio right-click menu** in the Explorer and the 3D view: cut, copy,
  paste, group, union, select, zoom to, anchor, lock, export, publish.

# Guts&Bolts 0.6.7: Window Shopping (October 6, 2026)

## New
- **Catalog and Library like the old catalog** (website and Player): a search
  bar with a category box, Browse by Category on the left, Price and Creators
  filters, Sort by, and an info box when you point at an item. The Library has
  a new **Animations** section.
- **Gear held the classic way**: arm straight out, the sword pointing up.
- **Studio right-click > Publish to Guts&Bolts**: share what you selected as a
  public or private model, from the Explorer or the 3D view.
- **Animation Editor like Roblox's**: a "..." menu (Load, Save, Save As,
  Import, Export, Create New, Set Animation Priority), Roblox-style play
  buttons, and Space to play or pause. Publish animations as public (other
  creators can use them) or private, and import them back by ID.
- **Explorer services**: ReplicatedStorage, ServerScriptService, StarterPack
  and Roblox's other services show in Studio's Explorer, and
  `game:GetService` knows them.
- **Edit in Studio** from the website's My Games: the app gets the game and
  opens Studio on it.

## Fixed
- An error on the Player's Create > Library page.

# Guts&Bolts 0.6.6: Home Delivery (October 6, 2026)

## New
- **Downloads from gutsandbolts.net**: the Download buttons give you the Player
  and Studio (and the Android app) straight from the site, and the apps update
  themselves from there (download, swap, restart; your own games are kept).
- **Upload Missing Item Pictures** (Studio, File menu): hats and gear made from
  imported Roblox files get their pictures uploaded, so they show up for
  everyone instead of plain white.
- **gutsandbolts.net**: the website and the server have their own address. The
  apps connect to it from this version on; the old `workers.dev` address still
  works for older apps.

# Guts&Bolts 0.6.5: Nobody Gets Left Behind (October 5, 2026)

## New
- **Nobody gets kicked when the host leaves**: the player who has been there
  longest starts a new server of the same game (same name, same private code)
  and everyone else follows them there by themselves. The world starts fresh,
  but everybody stays together.
- **Developer Console** (F9, or type `/devconsole` in the chat), like Roblox's:
  a Client tab for everyone, and a Server tab with the server's log and a Lua
  command bar that only the game's owner gets.
- **Script editor**: line numbers, Roblox-style colours, and the line with a
  mistake underlined in red.
- **Hat pictures on the website**: hats with a texture now look the same on
  the website as in the Player.
- **Studio, more like Roblox Studio**: Union / Negate / Intersect / Separate
  (Ctrl+Shift+G / N / I / U); Scale orbs on each side (drag one side, Ctrl for
  both) and Move arrows; rotating several things turns them all together; drag
  a box to select; blue selection boxes; **Script Analysis** (VIEW tab) lists
  script mistakes without running anything; **AutoSave** every 5 minutes with
  crash recovery.
- **Asset pages**: every decal, sound, model and plugin in the Library has its
  own page, found by its ID (`#/library/<ID>` on the website, *Create > Library*
  in the Player). Listen to audio, see decals full size and **Copy ID** to paste
  it into a game.
- **Numbered asset IDs**: every asset has a plain number counting up, like
  Roblox's. Type `123` into a Texture / SoundId (or `gb:123`); old IDs still work.
- **Make Gear** (staff, in Studio's File menu): start from a ready-made Classic
  Sword, Rocket Launcher, Speed Coil, Gravity Coil or Bomb, try it, and sell it
  in the catalog. The ready-made gear is in the Toolbox for everyone too.
- **Classic profiles**: an *About me* blurb and a *Right now I'm...* status
  (140 letters) on your profile, a **My Feed** box on the home page with your
  friends' and followed people's statuses, and **Player Badges** you earn on your
  own (Creator, Builder, Architect, Friendly, Collector, Old Timer). On the
  website and in the Player.
- **Messages**: a private inbox on the website (Messages in the top bar, with a
  red count of unread ones). Send a message from anyone's profile, reply, and
  delete. *Settings > Privacy* says who can message you (everyone, friends, no
  one). Guests can't send or get messages; 40 messages a day each.
- **Saved outfits** on the Avatar page: save your whole look (colours, hat and
  everything you wear) and wear it again in one click. Up to 30.
- **In the Player app too**: a Messages page (with the unread count in the
  menu and Send Message on profiles), an Outfits tab on the Avatar page, and
  Continue Playing and Favorites rows on the home page.
- **Gutstober in the Player**: orange and purple, a pumpkin and bats in October;
  turn it off in Settings (*Gutstober theme*).
- **Favourite games**: the star on a game's page. **Continue Playing**: the
  games you played last. Both are on the home page and at *Games > mine*.

# Guts&Bolts 0.6.4: Liquid Assets & Explosive Results (October 1, 2026)

## New
- **Explosions with a shockwave** that races outwards: it breaks joints, flings
  loose parts (light ones further), knocks people over, hurts less with distance
  and shakes the camera as it passes.
- **Fire, smoke and mushroom clouds**: fireballs roll up into billowing smoke,
  ground fires keep burning, and big blasts make a mushroom cloud with a dust ring.
- **Bombs and water**: a crater in the surface, a column of spray and rings of
  waves. Big bombs in big water make a **tsunami** that lifts boats, sweeps
  players and parts along and runs up onto the shore.
- **Scripts**: `Explode(position, radius, power, options)` (Smoke, Fire,
  MushroomCloud, Destroy, JointBreak, Visible, Hurts) gives back what it hit, and
  `Instance.new("Explosion")` works like Roblox's (BlastRadius, BlastPressure,
  DestroyJointRadiusPercent, Hit).
- **Premades**: Time Bomb, Nuke and Depth Charge.
- **Real item pictures**: catalog and inventory pictures are renders of the item
  on a character (gear on its own), never drawn icons, in the Player and on the
  website. Catalog items no longer take uploaded pictures at all (both servers
  refuse them), so a picture can never be swapped for something it isn't.
- **New item pages** on the website, laid out like the 2016 catalog: NEW ribbon,
  LIMITED tag, a buy box with what's left and a live off-sale countdown, and the
  creator's character next to their name. Hats and accessories now show up on characters on the website and on
  profiles in the Player.
- **Gutstober** (website): a Halloween theme for all of October (orange and
  purple, a pumpkin and bats), which you can turn off in *Settings > Site theme*.
- **Timed items**: an item can go off sale at a set time ("Off sale in 30 days",
  then "Off sale"); its creator or staff set the date on the item's page. Pumpkin
  items are Gutstober items and go off sale on November 1st.
- **Friends, Following and Followers lists** on everyone's profile, with blue
  links to each person's profile (website and Player).

## Fixed
- Walking up a slightly taller part is smooth instead of snapping up in one go.
- The Verified Hat didn't show up on the website's 3D characters.

# Guts&Bolts 0.6.3: Who Let Us Cook? (October 1, 2026)

## New
- **Navmesh and pathfinding** (the headline): Studio bakes a navigation mesh of
  every floor a character can stand on (re-baked by itself when anchored parts
  change; *MODEL > Navigation > Navmesh* shows it). NPCs walk around walls, jump up
  ledges, drop down and leap gaps. Scripts: Roblox's `PathfindingService`
  (`CreatePath`, `ComputeAsync`, `GetWaypoints`, `Blocked`, ...) or just
  `humanoid:PathfindTo(target)`.
- **Gear in the catalog**: staff publish a Tool from Studio as Gear; people buy
  it, equip up to 4, and get it in games whose creator ticked *Allow gear*.
- **2011-style characters**: an Animate script (idle, walk, run, jump, fall,
  climb, sit, tool), emotes (`/e dance`, wave, point, laugh, cheer),
  `PlatformStand`, tripping and flinging.
- **Tools held like Roblox's** (a `RightGrip` weld and the Grip properties), so
  imported Roblox gear such as the LinkedSword works, scripts and all.
- **Roblox hats** import with their real mesh and texture (mesh versions 1 to 7).
- **Studio**: Move / Rotate increments like Roblox Studio, a studs readout by the
  mouse while dragging, free movement with increments off, right-click
  *Insert Object*, and Lighting / StarterPlayer in the Explorer.
- **More Lua**: `TweenService`, `workspace:Raycast` (+ `Ray.new` /
  `FindPartOnRay`), `GetPartBoundsInRadius`, `HttpService` JSON, `Random.new`,
  `math.noise`, `BindableEvent` / `BindableFunction`, `ReplicatedStorage` /
  `ServerStorage`, `Debris`, `Teams`, `GetPropertyChangedSignal`.
- **User numbers** (#5) are the main way to name someone: add friends, search,
  staff tools and profile links all use them.
- **Leaderboard** folds away with Tab or its arrow; click a name to Friend or
  Follow. **Following** is new, and Guts follows everyone.
- **Join** buttons on profiles, and **privacy settings**: who sees you online
  and what you play (everyone, friends, no one), and who can join you.
- **Security**: authenticator app codes (TOTP) for logging in; confirming your
  email gives you the **Verified Hat**.
- **Terms of Service** and an 18+ heads-up on sign-up.
- **Automatic updates**: the Player and Studio update themselves when they open
  and a new version is out.

## Also new since 0.6.2 (Dress Code and Take a Seat)
- **Seats** (like Roblox): walk into a Seat and you sit on it, facing its front,
  riding along if it moves; jump to get up. Insert one from Insert Object (or
  tag any part "Seat"); Roblox Seats and VehicleSeats import working. Scripts:
  `Humanoid.Sit`, `Humanoid.SeatPart`, `seat.Occupant`, `seat.Disabled`,
  `seat:Sit(humanoid)`.
- **Moderation that works**: a banned account sees a ban screen (in the app
  and on the website) with the reason, staff's note and when it ends; bans can
  last 1, 3, 7 or 30 days or forever, and end by themselves. Staff can also send
  **warnings**, shown once until the player says they understand.
- The app's Avatar page has a **Wardrobe**: everything you own, by kind
  (Shirts, Pants, T-Shirts, Faces, Hats, Hair, Accessories). Click to wear,
  click again to take off, or take everything off. No trip to the Catalog
  needed. Colours and the classic hats are on the **Body & Colours** tab.

## Fixed
- Jumps were too low (about half your height). Now about 1.4 times your height,
  like Roblox; `Humanoid.JumpHeight` and `UseJumpPower` work like Roblox's.
- The play camera no longer goes through walls: it comes in front of them.
- Phones: the thumbstick stays in its corner instead of moving to your thumb.
- Your character on the home screen (the app's banner and the website's home
  page) showed up wearing nothing: shirts and pants are now downloaded and
  worn there too.
- Friends' pictures on profiles wear their outfits.

# Guts&Bolts 0.6.2: Hotfixes, Headaches & Hand Grenades (September 30, 2026)

## New

**Import 3D models, and drag files straight into Studio**
- File > Import 3D Model, Picture, Sound... (and the Import button) reads
  .fbx, .obj (+ .mtl), .gltf / .glb, .stl and .ply. Each object becomes a mesh
  part in its material's colour, grouped in a Model; hard edges stay hard and
  curved surfaces stay smooth. Huge or tiny models are resized to about 10
  studs.
- Drag files from your computer onto Studio: models land where you drop them,
  pictures become Decals on the part under the mouse (or a sign on the
  ground), sounds become Sounds, .lua / .luau files become Scripts, Roblox
  models are inserted and games are opened. Several at once line up side by
  side.

**Water parts, like real water**
- Gerstner waves (the `Waves` attribute, or `WaveScale` on a FluidVolume):
  sharp crests, wide troughs, whitecaps on big waves. Swimmers and floating
  things ride exactly the waves you see.
- The water reads how deep it is at every pixel: crystal clear at the shore,
  fading to deep blue in the deep (Beer-Lambert), set by the new `Clarity`
  (0 murky .. 1 clear).
- Things under the water are bent by it (refraction, index 1.333); reflections
  of the sky and the world; the sun's glint; caustics on the bottom; foam where
  it meets the shore; seen from underwater, the surface bends the world above
  and turns into a mirror at low angles.
- Big splashes and waves slapping walls throw real liquid drops that fall back
  and soak in, and leave a ring of foam.

**Rivers, wetness and diving ("Riptide")**
- Flow maps: a water part's `Flow` is bent around rocks and pillars that poke
  through it (it splits around them and speeds up past them, with white water
  on the sides). The ripples are carried along it, and swimmers and floating
  things follow it too.
- Caustics bend with the waves above them; underwater, everything gets them.
- Sun glints use a Cook-Torrance (GGX) highlight: brilliant sparkles on crests.
- Splashes and dripping swimmers leave things wet: darker and shinier, drying
  over about half a minute. Parts the liquid ran over look wet too.
- Underwater: distance blur and a slight colour split; after coming up for
  air, water runs down the screen for a few seconds.

**T-shirts ("Tee Time")**
- A new catalog item: any .png or .jpg worn flat on the front of the torso,
  over your shirt. Make one on the Create page (website or app), wear it from
  the Avatar page; the game and the website's 3D avatars show it.
- What you put on on the website now shows up properly in the game (clothes,
  faces, accessories and T-shirts), and the Avatar page swaps items of the
  same kind instead of stacking them.

**FluidVolume**
- `Instance.new("FluidVolume")` (or Insert Object): a block of water with
  `FlowVelocity`, `Clarity` and `WaveScale`. Any instance also has
  `:ParentTo(parent)`.

## Fixed
- Catalog pictures of shirts, pants and T-shirts show the real clothing on the
  mannequin (on the website and in the app), not just a plain colour.
- Water no longer flickers in stripes where it touches walls (z-fighting).
- The camera no longer slowly looks up by itself in Shift Lock, first person
  or fullscreen.

# Guts&Bolts 0.6.1: The Hydromania Update (September 30, 2026)

Real water you can pour, swim in, dive under and float things on.

## New

**Swimming, for real**
- You float at the surface with your head out, bobbing with the waves.
- Dive: hold **C** (or **Ctrl**), or swim forward while looking down. Look up
  (or hold **Space**) to come back up; Space at the surface hops you out.
  On phones, swimming follows where the camera looks.
- Underwater, everything goes blue and hazy (the further away, the more the
  water hides it, red first), darker the deeper you go, with a gentle wobble.
- Works in Water parts, waves, floods and the new real liquid.

**Floating and sinking (Density)**
- Every part has a **Density** (Properties, or `part.Density` in scripts).
  Water is 1.3: anything lighter floats, anything heavier sinks. Wood (0.7)
  floats half under, Metal (3.0) and Concrete sink. Works in pools and in
  real liquid.

**Render Distance**
- A new **Render Distance** bar in the in-game Settings (and a slider in
  Advanced graphics), from 1 (60 studs, fastest) to 10 (everything). Low /
  Medium / High / Ultra set it to 4 / 6 / 8 / 10: Ultra is the max.
- Things past it fade into the sky and aren't drawn, and water waves, liquid
  taps, lights and effects out there rest until you come closer: less lag and
  fewer FPS drops on big maps.

**Real liquid, like a Blender fluid simulation**
- A part called **FluidSource** pours out real liquid made of thousands of
  tiny drops. It flows downhill, piles up, fills dips, splashes off parts of
  every shape (and off the real triangles of Mesh parts), and pushes people
  and floating things along. Attributes: Speed and Rate.
- On computers with OpenGL 4.3 and phones with OpenGL ES 3.1 the physics runs
  on the graphics card (compute shaders): up to about a million drops. Other
  computers (and Macs) run it on the processor, with up to 14,000.
- It's drawn as one smooth, glassy surface: reflections with real Fresnel,
  bending with water's real refractive index (1.333), and deeper water soaks
  up red light first so it looks bluer (Beer's law).
- Things the liquid runs over stay wet (slippery) for a while. Stray drops
  dry up after a few seconds.

**Better-looking water ("Glass Lagoon")**
- Drops are drawn as flat shapes that follow the surface, so calm water looks
  calm, and you can see the pool floor clearly through it.
- The water mirrors the scene around it (walls, towers, players), not just
  the sky (Water Quality Medium and up).
- Water sits in the shade of things that block the sun, and caustics (wobbly
  lines of sunlight) play on the bottom of pools.
- No more glittery sparkles on little ripples.

**Updates page on the website**
- A new **Updates** tab lists every update, newest first, each with its own
  name. It checks for new updates by itself, and the tab says NEW when
  there's one you haven't seen. The home page shows the latest one.
- Staff can post updates straight from the page.

**Liquid from scripts**
- New `FluidSystem` (Color, Viscosity, SurfaceTension) and `FluidEmitter`
  (Rate, Velocity, Size, Position, FluidSystem, Enabled) objects, from
  `Instance.new` or Studio's Insert Object. Up to 16 kinds of liquid at once;
  their colours mix where they meet.
- `workspace.MaxFluidParticles` (default 100,000): when it's full, the oldest
  drops are recycled.

**Mega Water Slide (new sample game)**
- A 400-stud tube slide spiralling down an 80-stud tower, with real water
  running down it, a ride timer, a splash pool full of floating toys, a wave
  pool and a fountain.

**Graphics settings**
- New **Graphics API** setting: Auto (the newest OpenGL your graphics card
  has), OpenGL 4.6, OpenGL 4.3, or OpenGL 4.1 "safe mode" (phones: OpenGL ES
  3.2 / 3.1 / 3.0). If the one you pick doesn't work it falls back to the next.
  Settings shows which one is really running, on which graphics card.
- New **Water Quality** setting (Low / Medium / High / Ultra, set by the quality
  presets): how sharp the liquid is drawn, how smooth its surface is, and how
  many drops there can be (25,000 / 60,000 / 100,000 / up to a million).

**Flat faces**
- Faces are flat pictures painted onto the front of the head (like Roblox),
  not little 3D shapes: the classic smiley and catalog faces both hug the
  round head exactly. Old characters and NPCs switch over when a game loads.
  The website's 3D avatars match.

**Better shadows**
- No more light leaking under boxes sitting on the ground: shadows start
  right at an object's base, with no flicker where it touches.
- Sharper shadows close to you (a second, close-up shadow map).

**Publishing puts games on the website**
- Saving a published game in Studio updates it online too.
- The Player's My Games has a Publish button for games that are only on your
  computer. The staff page can put the sample games online as official games.

**Slippery parts**
- Tag a part `Slippery` and people slide on it (water slides, ice).
- Tilted Water parts work: you swim in their real, turned box.

# Guts&Bolts 0.6.0: water, NPCs and Roblox-style controls (September 30, 2026)

## New

**Game UI, like Roblox's**
- New objects: **ScreenGui**, **Frame**, **TextLabel**, **TextButton**,
  **ImageLabel**, **ImageButton**, **UICorner** and **UIStroke**. Games can have
  menus, shop buttons, coin counters and title screens.
- Build them in Studio: insert them, then click, drag and resize them right in
  the viewport. Or make them in a script with `UDim2`, `Vector2` and
  `button.MouseButton1Click`.
- They work in multiplayer, and in Roblox files (import and export).

**Character physics**
- The character's hitbox is now shaped like Roblox's R6 body (2 x 1 studs,
  turning with you), and each body part fires `Touched` on its own.
- The walk animation speeds up and slows down with how fast you really move.
- Loose parts knock you back; spinning platforms carry and turn you; jumping
  off a moving platform keeps its speed.
- Scripts: `math.atan2`, `math.pow` and `math.log10` work like in Roblox.
- The camera follows your head. Scroll all the way in for first person: your
  character fades out as the camera gets close, and the mouse looks around.

**In-game menu and Shift Lock, like Roblox**
- Esc opens a new menu with Players, Settings and Help tabs, and Reset / Leave
  / Resume buttons along the bottom (R and L keys work too, and ask first).
- New settings: Shift Lock Switch, camera sensitivity, invert camera, volume
  and fullscreen, next to graphics quality, FPS, gore and touch controls.
- Shift Lock: press Shift and the camera sits over your right shoulder while
  your character faces wherever you look. Works in Studio's play test too.

**Player collisions**
- Players bump into each other (and can stand on each other's heads). Games
  can turn it off in Game Settings or with `workspace.PlayerCollisions`.

**Creating and moderation**
- Hats are for Verified creators; shirts and pants can be made by anyone
  signed up (guests can only play).
- Decals and audio are always free, so anyone can use them in their games.
- Staff pick a reason when banning (sexual content, violent extremism,
  harassment, scams, exploiting and more), with an optional note. The banned
  player sees why.

**Phones**
- Popups in the app close when you tap outside them (phones have no Esc key).
- The old server picker popup (which couldn't be closed on phones) is gone.

**3D catalog**
- Hats, shirts and pants in the catalog are shown in 3D, worn by a grey
  mannequin, and an item's page has a 3D view you can drag to turn.

**The Library**
- Studio: **File > Publish Selection to Library** shares objects as a model,
  **public** or **private**. Verified creators can make as many public as they
  like; everyone else 5 a week. Studio takes a picture of it automatically.
- Studio's **Toolbox** now starts with the **Library**: everyone's public
  models, decals and audio, with pictures. Click one to insert it.
- The website's Create page has a **Library** tab (all public models, decals,
  audio and plugins) and a **Models** tab to switch yours public or private.

**A Toolbox like Roblox's**
- Library / Inventory / Recent tabs, a category menu with a search box, and
  a grid of pictures with blue names.
- Every model and asset has a picture. Studio photographs objects on their own
  (on a plain background) when you publish them, and takes one itself for any
  older model that has none. Parts and ready-made objects have pictures too.
- A gold **official** badge marks things made by Guts&Bolts staff (in Studio
  and on the website), so you know they're safe to use.

**Accessories and faces**
- New accessory types: **hair**, **face**, **neck**, **shoulder** and
  **waist**, next to hats. They have their own tabs in the catalog.
- Studio's new **AVATAR > Accessories** window (Verified creators): put it on a
  mannequin, move it into place, **Save position**, and **Upload** straight to
  the catalog with a picture.
- **Faces**: pictures drawn on the front of the head. Only Guts can make them
  (website: Create > Faces).
- Only Guts' own accessories and faces can be made **Limited**; shirts, pants,
  audio, decals and models can't.
- The Guts account can change its join date in Settings.

**Limiteds, resale and trading**
- Item creators (and staff) get an **Edit item** page: name, description,
  price, colour, hat shape or clothing picture.
- Only **Guts** can make an item **Limited**, with a fixed stock. Copies are
  numbered (#1, #2, ...); when it sells out, owners can **resell** their copy
  (they get 70%) and **trade** Limiteds with each other (the new Trades page).

**Username changes**
- Change your username for 1,000 Bolts on the website's Settings page.
- Old usernames show on your profile under "Past usernames" and stay yours:
  nobody else can take them, and you can switch back.

**Clothing templates**
- The character now has a proper clothing layout: shirts and pants can have a
  **picture** painted on the new 585 x 559 templates (`assets/templates/`, also
  downloadable from the website's Create page and saveable from the app).
- See-through parts of the picture show the clothing's colour underneath.
- Worn clothing pictures show in games and in multiplayer.

**Finding games**
- Games can pick up to 3 **genres** (Obby, Horror, Tycoon, Roleplay, Racing
  and more) and a **server size** on their Configure page.
- The Games page has genre buttons, searches descriptions and genres too, and
  sorts by Most played, Playing now, Top rated, Newest or Recently updated.
- Game pages show genres, 👍/👎 votes (after you've played), a like bar, and a
  stats table: playing now, visits, created, updated, server size.

**Game badges**
- Two kinds of badges now: **Guts&Bolts badges** (like Verified, given only
  by staff) and **game badges** that game creators make themselves.
- Make badges on your game's **Configure** page (name, description, colour),
  then award them from a script:
  `game:GetService("BadgeService"):AwardBadge(player, "badge-id")`.
  Everyone in the server sees "X earned the badge ...!" in chat.
- Profiles show both sections; game pages list their badges and how many
  times each has been won.

**Server cards on game pages**
- Every game page lists its running servers like Roblox: the faces of who's
  in each one, "7 of 12 people max", **Join** (that exact server) and
  **Share** (a link to it), with pages when there are lots.

**Always online**
- The Player always connects to the main Guts&Bolts server: no more offline
  or local-network play. Without internet it shows *Can't reach Guts&Bolts*
  with a **Try again** button. Studio's "Play in Guts&BoltsPlayer" puts you in
  an online server too.

**Assistant and MCP (AI in Studio)**
- New Assistant tab: chat with Claude (your own API key) and it builds,
  scripts, playtests and takes screenshots in your game. Everything is undoable.
- MCP support: let Claude Code, Claude Desktop and other AI tools on your
  computer work in Studio (HTTP at 127.0.0.1:44755/mcp, or `GutsAndBolts --mcp`).

**Damage and blood settings**
- Game Settings has a new Damage & Blood tab: fall damage on/off, safe fall
  speed, fall damage strength, gore, and blood color, amount and how long it stays.
- Blood is now liquid: streaking drops, pools that spread and merge, drips down walls.
- Scripts: `workspace.BloodColor`, `BloodAmount`, `SafeFallSpeed`,
  `FallDamageScale`, and `Effects.Blood(pos, amount, direction)`.

**Smooth multiplayer**
- Other players move smoothly instead of jittering: their movement is shown a
  tenth of a second behind, gliding between updates, and it adapts to bumpy
  connections.

**Names and whispers**
- Players' (and NPCs') names float above their heads, with a health bar when hurt.
- Private chat: `/w PlayerName message` or `/whisper PlayerName message`.

**Real water**
- Water now has moving waves and ripples that bounce off the pool's sides.
- Loose parts float or sink by material (wood floats, metal sinks), tip over
  realistically and bob on the waves.
- Splashes with spray and a sound when things fall in; swimmers leave a wake.
- New water attributes: `Waves` (ocean swell) and `Flow` (currents that carry
  you and floating things along).
- New **WaterSource** (Insert menu): pours water that flows downhill, fills
  pits and pools, and spills over walls. Swim in it and float things on it.

**NPCs and zombies**
- Any character-shaped Model is now an NPC: it walks, jumps, and dies like a
  player. Scripts steer it with `humanoid:MoveTo()`, `MoveToFinished`,
  `humanoid:Move()` and `humanoid.Jump`.
- New **PathfindingService**: works out a route around walls and up ledges.
- New ready-made **Zombie** in the Toolbox: it chases the nearest player and bites.
- Studio's Command Bar runs inside the game while you're playing, and can use `wait()`.

**Climbing and swimming**
- Walk into a **TrussPart** or a ladder to climb it. Parts called **Water** are
  swimmable. Both are in the Insert menu.

**The website looks like it's from 2011**
- A dark blue top bar with the logo, a game search box and your account. Below it
  is a shiny tab row and a gray row with Avatar, Friends, Groups and Bolts.
- The page is white on a gray background. Boxes have title bars, and the
  buttons are glossy.
- New home page: your 3D avatar, Bolts and online friends on the left, and
  "Best of Guts&Bolts" plus new catalog items on the right.
- The front page matches.

**The website is open to everyone**
- Visitors can see every page. When they try to buy something, claim Bolts,
  save an avatar, add a friend or upload, a "You need to log in" popup appears.
- **Play** on a game's page opens the Guts&Bolts app on that game. Visitors
  first pick **Play As Boy** (black cap) or **Play As Girl** and play as a guest.
- Game cards show visits and how many people are playing right now.

**Configure your games on the website**
- Create > My Games > **Configure**: name, description, who can play
  (**Public**, **Friends only** or **Private**), a thumbnail, a square icon, and
  uploading a new version.

**Email, forgot password and two-step verification**
- **Settings** on the website: add an email, change your password, and turn on
  two-step verification (logging in on a new device also needs a code from your
  email).
- **Forgot your password?** emails you a code to set a new one.
- The server needs an email service switched on for this; see the README.

**Fixed:** logging in as an account that never set a password (like Guts) said
"no account with that username". Now it explains how to set one, and the app's
home page reminds you.

**Choose Your Character (guests)**
- "Play as Guest" now asks you to pick **Play As Boy** (black cap) or **Play As
  Girl** (pink ponytail). The "Have an Account?" link takes you to Log In.
- New hairstyle, **Ponytail**, on the Avatar page (app and website).

**A new face**
- Everyone has the classic smile now: two small oval eyes and a round smile.
  Characters saved with the old block face get the new one automatically.

**Connecting to server screen**
- Pressing Play shows the game's icon and name (and who made it), a spinning
  circle with "Connecting to server...", and the Guts&Bolts logo under it,
  until the game is ready. Then it fades away.

## Fixed (Android)
- The app now sizes things using Android's own screen density, so buttons are
  the size you'd expect.
- On a phone held sideways, the header is slimmer and the margins are thinner,
  so there's more room for the page.
- Upright phones: long text wraps. The search box, catalog tabs and the Display
  name box fit on the screen.
- Settings fits on the screen (it scrolls) and matches the site's look.
- Messages in games wrap instead of running off the screen.
- The version number in Settings was stuck on 0.4.0.

# Guts&Bolts 0.5.2: animations, guests and 3D avatars

**Fixed:** "couldn't make a secure connection ... (SSL - Internal error)" when
connecting to the online server. The apps now connect with TLS 1.2, which every
server supports.

## New

**Animations, like Roblox**
- **AVATAR tab > Rig Builder** adds a dummy character. **Animation Editor**
  opens a timeline: move the playhead, then click a body part and turn it with
  the Rotate tool (it bends at the shoulder, hip or neck). That makes a keyframe.
- Drag keyframes to change their timing. Right-click one to change its easing
  (Linear, Constant, Cubic, Elastic, Bounce), give it a name, copy it or delete
  it. You can also loop an animation and set its priority.
- Scripts play them: `humanoid:LoadAnimation(anim):Play()`, or
  `model:LoadAnimation(anim)` for any rig. Tracks have Stop, speed, weight,
  and the Stopped, DidLoop and KeyframeReached events. Animations play on top
  of walking.

**Studio**
- **F** zooms the camera to whatever you've selected (parts, models, tools).
- **Snap to Grid**: move in steps of any number of studs (the grid follows),
  and turn in steps of any number of degrees. Each can be on or off.
- **Collisions**: parts you move stop flush against other parts instead of
  going through them.

**Guests**
- No account? Press **Play as Guest** and play any game, alone or with others.
  Guests can't chat ("Sign up to chat with other users!"), make friends or get
  Bolts.

**Avatars and profiles**
- The website shows your avatar in **3D** (drag to turn), on the Avatar page
  and on profiles.
- Profiles, on the website and in the app, look like a classic Roblox
  profile:
  - the 3D avatar and whether they're online;
  - **Currently Wearing**;
  - statistics (joined, friends, place visits);
  - badges, friends, games and groups.
- **Footstep sounds** while you run.

# Guts&Bolts 0.5.1

**Fixed:** the apps sometimes couldn't connect to the online server, with "SSL -
Internal error". When the app sent several requests at once, their secure
connections could get in each other's way. Now they take turns.

# Guts&Bolts 0.5.0: always online

Guts&Bolts now has its own **online server in the cloud**, so your account,
Bolts, friends, groups and published games are there any time, even when
nobody's computer is on. The apps connect to it by themselves: sign up and
press Play.

There's also a **website**, <https://project-guts-and-bolts.pizzadoe173.workers.dev>,
where you can sign in and do everything the Player's site pages do.

## Downloads

| File | What it is |
| --- | --- |
| `GutsAndBoltsPlayer-Android.apk` | The Player for Android phones and tablets (Android 7.0 or newer) |
| `GutsAndBolts-Windows.zip` | Studio + Player for Windows |
| `GutsAndBolts-macOS.zip` | Studio + Player for Mac |
| `GutsAndBolts-Linux.tar.gz` | Studio + Player for Linux |

To install the APK, open it on your phone and allow installing from your
browser or files app. It's signed with a test key, so Android may warn that it's
from an unknown developer.

## New

**Online**
- **The official server runs on Cloudflare.**
  - The apps talk to it over secure connections (HTTPS and secure WebSockets),
    and check its certificate.
  - The server window has an **Official server** button, and you can still use
    your own server.
- **Sign up and log in** with a username and password. Your password never
  leaves your device.
- **Friends, people and groups:**
  - profiles, friend requests, and seeing who's online and what they're playing;
  - groups with a wall, shouts and member roles.
- **Multiplayer:** public servers (Play finds one), and private servers for
  friends or anyone with the code. Nobody ever sees anyone else's address.
- **Bolts:** daily Bolts, Bolts for playing, codes, and buying from the catalog.
  Verified creators can sell what they make.

**The website**
- Games (and who's playing), the catalog, people, friends, groups and Bolts.
- **Create:** upload decals, audio, clothes, plugins and games, and rename your
  games.
- Works on phones too.

**Making games**
- **Tools:** swords, bats, anything a character can hold, with a hotbar (keys
  1-9).
- **Leaderboard** (`leaderstats`), **checkpoints**, and **saved player data**
  (`DataStoreService`).
- **Decals:** pictures stuck on any side of a part, from your computer or
  uploaded on the Create page.
- The site's new **Create page**:
  - **My Games**, with Edit name, Open in Studio and Play;
  - upload tabs with a Browse button and previews.
- New jump, spawn and respawn sounds.

**The dev log**
- Updates and releases can post to a Discord channel. See
  `.github/workflows/devlog.yml` for the one-time setup.

## Known limits

- Accounts from home-made servers aren't copied to the official server. Sign up
  again there.
- Cloudflare's free plan gives each request very little computer time, so
  uploading very big games or long songs can fail. Smaller uploads are fine.
- The Android app is tested in "phone mode" on Linux and built for real phones,
  but hasn't been tried on many devices yet. If something looks wrong, please
  open an issue.
