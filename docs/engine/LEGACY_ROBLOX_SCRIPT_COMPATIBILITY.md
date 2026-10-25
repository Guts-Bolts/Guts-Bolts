# Legacy Roblox Script Compatibility

Guts & Bolts already runs Lua 5.4 with a Luau source-rewrite pass and exposes many Roblox-style objects and APIs. Compatibility is an engine API project, not just a language parser feature.

## Target: Nebbys Star Glitcher (legacy R6 script)

The uploaded script is a stress test because it combines old client-side APIs, R6 body-part names, mesh and joint construction, body movers, cursor targeting, raycasts, sounds, particles, and long-running input loops.

### Support already present in the engine

- Luau conveniences such as compound assignment, `continue`, type annotation stripping, and interpolated strings.
- R6-style character parts, including `HumanoidRootPart`, `Torso`, `Head`, arms, and legs.
- `Vector3`, `CFrame`, `Color3`, legacy joints/welds, body movers, particles, trails, sounds, `Debris:AddItem`, and `workspace:FindPartOnRay`.
- The basic `Players`, `RunService`, and `UserInputService` services.

### Compatibility added in this PR

- `Enum.Material:GetEnumItems()`, `Enum.MeshType:GetEnumItems()`, and `Enum.Font:GetEnumItems()`.
- A legacy `Player:GetMouse()` object exposing cursor coordinates, `UnitRay`, `Hit`, `Target`, `TargetFilter`, and common key/mouse button signals. Rays are generated through the active camera and use the engine's existing raycast path.
- A limited `LoadLibrary("RbxUtility").Create(className)(properties)` shim for old object-construction helpers.
- Built-in `SpecialMesh` primitive rendering for `Head` / `Sphere`, `Cylinder`, `Brick` / `Torso`, and approximate `Wedge`, including `Scale` and `Offset`. `FileMesh` IDs still fall back to the host part geometry until a compatible asset importer exists.
- The legacy `game:service("Workspace")` lowercase alias.
- A `PlayerScripts.ControlScript` compatibility object whose `Disabled` / `Enabled` state toggles native keyboard/gamepad movement. Script-driven `Humanoid:Move()` / `Humanoid.Jump` remain available, and control state resets when Play stops or restarts.
- Regression checks in `tools/test_script_fixes.lua` for enum enumeration, mouse API shape/ray direction, the legacy helper, the lowercase service alias, and the controller placeholder.

Mouse coordinates are currently scaled from the application viewport to the active camera viewport. Embedded/docked editor viewports may require an explicit viewport-origin offset, so the API must be tested in both the standalone player and Studio's embedded play view.

### Remaining blockers before the script can run end-to-end

This PR is a compatibility increment, not a claim that the entire script now works.

1. **Character controller assumptions** — `ControlScript.Disabled` now suppresses native keyboard/gamepad movement while preserving script-driven movement. Verify expected legacy scripts can disable and restore controls during real play, including stop/restart.
2. **Legacy asset classes and properties** — built-in `SpecialMesh` primitives now render approximately, but `MeshType.FileMesh` and imported mesh identifiers still need a compatible asset importer. Mesh rendering also needs validation in all render passes (shadows, highlights, water interactions) and the full Star Glitcher fixture.
3. **Rig mutation and animation conflicts** — the script edits R6 shoulder/hip/neck joints while the built-in character animator is active. Motor6D C0/C1 semantics and script-owned joint pose precedence still need end-to-end implementation and play testing.
4. **Legacy event details** — verify key and button events fire once with the correct legacy arguments, including focus changes and script shutdown. Input-signal connection cleanup on script stop still needs an explicit lifecycle test.
5. **Execution lifecycle** — long-lived `BindableEvent` loops, input callbacks, and `Debris` cleanup need to remain safe through character respawn, script stop, and game exit.
6. **Asset and property gaps** — old BrickColor names, surface values, mesh URLs, animation properties, and sound assets need end-to-end coverage where used by the script.

## Acceptance test strategy

Use the script as an opt-in local compatibility fixture. Test the APIs in layers before attempting the complete effect-heavy script:

1. Enum enumeration, Luau parsing, and `RbxUtility.Create`.
2. `GetMouse()`, cursor hit testing, and legacy raycasts in standalone and embedded play views.
3. R6 rig lookup, joint creation, and joint property updates.
4. Mesh, sound, trail, particle, and light creation.
5. Character respawn and cleanup without dangling callbacks or crashes.

Do not report the full script as supported until these layers pass in an actual Guts & Bolts play test.
