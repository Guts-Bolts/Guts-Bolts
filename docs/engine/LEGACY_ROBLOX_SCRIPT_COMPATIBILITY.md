# Legacy Roblox Script Compatibility

Guts & Bolts already runs Lua 5.4 with a Luau source-rewrite pass and exposes many Roblox-style objects and APIs. Compatibility still has to be treated as an engine API project, not just a language parser feature.

## Target: Nebbys Star Glitcher (legacy R6 script)

The uploaded script is a useful stress test because it combines old client-side Roblox APIs, R6 body-part names, mesh and joint construction, body movers, cursor targeting, raycasts, sounds, particles, and long-running input loops.

### Support already present in the engine

- Luau conveniences such as compound assignment, `continue`, type annotation stripping, and interpolated strings.
- R6-style character parts, including `HumanoidRootPart`, `Torso`, `Head`, arms, and legs.
- `Vector3`, `CFrame`, `Color3`, legacy joints/welds, body movers, particles, trails, sounds, `Debris:AddItem`, and `workspace:FindPartOnRay`.
- The basic `Players`, `RunService`, and `UserInputService` services.

### Compatibility gap addressed here

Legacy scripts commonly call `Enum.Material:GetEnumItems()` and choose a random result. Enum categories previously returned names such as `Enum.Material.Neon`, but did not implement `GetEnumItems()`. That caused the script's material-selection helper to fail before it reached most of its gameplay logic.

This change adds `GetEnumItems()` for the material, mesh-type, and font categories and adds a play-test regression check.

### Remaining blockers before this script can run end-to-end

This PR is a first compatibility increment, not a claim that the entire script now works.

1. **Legacy `Player:GetMouse()`** — the script expects a live Mouse object with cursor-relative `Hit` and `Target`. The engine needs a real pointer-to-world ray implementation; a camera-center placeholder would be incorrect.
2. **Input and character controller assumptions** — it looks for `PlayerScripts.ControlScript` and expects Roblox-style local character control. These need explicit compatibility behavior or clear errors.
3. **Legacy asset classes and properties** — `SpecialMesh`, `MeshType.FileMesh`, old surface values, and imported mesh identifiers must be mapped to actual renderable assets, not only accepted as attributes.
4. **Rig mutation and animation conflicts** — the script edits R6 shoulder/hip/neck joints while the built-in character animator is active. A compatibility test must verify that script-driven joints can be controlled without being overwritten each frame.
5. **Execution lifecycle** — long-lived `BindableEvent` loops, input callbacks, and `Debris` cleanup need to remain safe through character respawn, script stop, and game exit.

## Acceptance test strategy

Use the script as an opt-in local compatibility fixture. Test the APIs in layers before attempting the complete effect-heavy script:

1. Enum enumeration and Luau parsing.
2. `GetMouse()`, cursor hit testing, and legacy raycasts.
3. R6 rig lookup, joint creation, and joint property updates.
4. Mesh, sound, trail, particle, and light creation.
5. Character respawn and cleanup without dangling callbacks or crashes.

Do not silently report the script as supported until these layers pass in an actual Guts & Bolts play test.
