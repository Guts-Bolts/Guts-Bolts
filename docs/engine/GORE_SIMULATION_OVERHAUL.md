# Gore & Dismemberment Simulation Overhaul

## Goal

Extend the existing R6 ragdoll and gore systems into a configurable, performance-conscious engine feature. The repository already has ragdoll stick-cutting/dismemberment, gore toggles, and body-part blood smearing; this proposal targets the next layer of physical credibility instead of duplicating those systems.

## Scope

### 1. Blood behavior and surface interaction

- Add a dedicated blood simulation layer that consumes impact, wound, and body-contact events from the existing ragdoll system.
- Support gravity-driven wall streaks, droplets, splashes, and floor pooling.
- Use a size spectrum for airborne drops, with stronger drag on fine mist than heavy beads.
- Shape high-energy impacts into velocity-oriented, elongated splashes with irregular directional spines.
- Preserve the existing age-driven wet-to-dry PBR response; extend surface projection and material-aware flow as later phases.
- Accumulate and merge puddles using a bounded representation rather than spawning an unbounded number of particles.
- Make spread, absorption, streaking, and lifetime depend on surface material.
- Keep decals and droplets persistent for a configurable lifetime, with a global budget and distance-based cleanup.
- Reuse the existing gore setting and expose quality controls so creators can disable gore or lower simulation cost.

### 2. Dismemberment and skeletal presentation

- Preserve existing joint/stick-cutting behavior and extend it through a data-driven body-region definition.
- Keep detached segments attached to the correct bone/segment transform before release, then hand them to the existing ragdoll physics.
- Allow optional skeletal/bone meshes or stylized exposed-bone effects to be supplied by the game; do not assume one anatomical asset fits every avatar.
- Make detached-part count, lifetime, collision detail, and replication budget configurable.
- Ensure respawn and character reset restore original visibility, transforms, accessories, and state.

### 3. Layered wound effects

The first implementation slice adds stylized rounded organ-like fragments and short intestine-like strand segments to the existing flesh/bone gib system. Torso, upper-body, lower-body, leg, thigh, and hip part names trigger a bounded coil when those parts burst. These are procedural visual cues rather than anatomically accurate organ models; creators can later replace them with authored assets. The initial blood spray is also region-aware: torso bursts emit a fuller spray, head/neck bursts favor a faster and tighter spray, and leg/hip bursts use a slightly reduced count and speed. These multipliers only shape cosmetic particles; they do not alter damage, hitboxes, or authority.

- Define wound profiles by body region, damage source, impact energy, and creator-selected gore preset.
- Support optional skin/muscle/bone/organ visual layers as assets or material effects, with non-graphic defaults and a creator-controlled intensity setting.
- On torso or leg destruction, show a short, bounded intestine-like coil at the wound, with strand segments using existing particle physics, cleanup, and budgets.
- Keep visual layers separate from gameplay damage rules so effects do not accidentally change health, hitboxes, or network authority.

### 4. Blood trails and pooling

- Use collision/contact points and surface normals to place effects accurately on walls, floors, and props.
- Convert fast impacts into directional splashes; use slower contact and movement to produce smears and trails.
- Accumulate pooled blood in a bounded surface-space representation; merge nearby pools and cap per-scene decals.
- Prevent blood from appearing through walls, floating above surfaces, or sticking to moving characters after they leave the surface.

### 5. Multiplayer and performance

- The server remains authoritative for gameplay events; clients may render cosmetic detail locally from compact, replicated event data.
- Avoid replicating every particle or droplet. Replicate event seed, position, normal, intensity, surface type, and lifetime instead.
- Apply budgets for active emitters, decals, detached parts, and persistent pools; degrade gracefully at lower quality settings.
- Ensure gore-disabled clients do not receive or render detailed gore assets unnecessarily where the current replication architecture permits it.

## Suggested implementation sequence

1. Audit and document current ragdoll, gore settings, contact smearing, and cleanup paths.
2. Add a size spectrum and size-dependent air drag for airborne blood.
3. Shape high-energy floor and wall impacts into velocity-oriented directional splashes.
4. Introduce a small blood-event/data model and surface-material response table.
5. Improve surface projection and material-aware flow while keeping pooling bounded.
6. Add creator settings, quality budgets, and multiplayer event replication.
7. Add optional skeletal and layered wound assets through data-driven presets.
8. Add deterministic tests for lifetime cleanup, pool budgets, respawn restoration, and gore-disabled behavior, followed by in-engine visual tests.

## Acceptance criteria

- Blood can travel down vertical surfaces and form bounded pools on horizontal surfaces.
- Repeated events merge or cap instead of creating unlimited scene objects.
- Surface type and impact direction visibly affect the result.
- Dismembered parts remain physically simulated and are cleaned up or restored safely.
- Respawn cannot leave hidden body parts, orphaned effects, or stale gore state.
- Gore can be disabled and quality budgets are respected.
- Multiplayer clients see consistent major events without per-particle network spam.
- Existing ragdoll, accessories, physics, and character-reset behavior remain intact.

## Risks and constraints

This is an engine-system expansion, not just a particle tuning pass. The current physics layer uses simplified collision shapes, so accurate surface adhesion and blood accumulation must be designed around those limits. Organic deformation and fully simulated fluid dynamics are likely too expensive and unnecessary for the first iteration; convincing results should come from event-driven effects, surface-aware decals, gravity, and pooling. Exact realism also depends on the game's character assets and material setup.


## High-end blood simulation blueprint (complete target scope)

This section records the full hybrid-rendering target. The items below are not all implemented by the current PR; each phase is deliberately separated so future commits can be tested without pretending a CPU particle approximation is a full fluid solver.

### A. Airborne liquid and particle simulation

**Target behavior**
- Use asymmetric, velocity-aligned droplets rather than perfect spheres. Stretch the rendered shape along the velocity vector and vary the stretch by speed and droplet size.
- Emit a size distribution from one wound event: a small number of large beads/chunks, a larger group of medium droplets, and fine mist.
- Apply gravity to all airborne drops. Apply stronger air resistance to small droplets and mist; preserve more momentum for larger droplets.
- Allow a wound profile to vary count, size distribution, speed, spread, and mist fraction by event intensity.
- Keep a hard particle budget and degrade in stages: remove mist first, reduce secondary droplets next, preserve primary impact cues last.

**Current PR implementation**
- Blood spray now uses a three-band size distribution.
- Small wet droplets receive stronger size-dependent drag.
- The renderer already aligns and stretches airborne drops along their velocity vector.
- This is still CPU simulation; GPU compute simulation is a future optimization gated by profiling and platform support.

### B. Impact detection, splashes, and decal projection

**Target behavior**
- Resolve impact position, surface normal, incoming velocity, droplet size, and impact energy from the collision event.
- Near-normal impacts should produce compact, roughly circular splashes. Shallow-angle impacts should produce elongated ovals and directional spines aligned with travel.
- Choose splash shape from impact energy and size, not a purely random texture choice: fine high-energy mist, irregular high-energy spines, medium droplets, and low-energy rounded impacts should remain visually distinct.
- Project stains onto the contacted surface using a stable tangent basis derived from the surface normal and impact direction.
- Add a depth-buffer/screen-space decal path for complex props and uneven terrain to reduce clipping and z-fighting. Clamp projection depth and reject pixels behind the receiver surface so blood does not bleed through walls.
- Keep a fallback surface-aligned decal path for hardware or render paths that do not support screen-space decals.

**Current PR implementation**
- High-energy floor and wall impacts can select a procedural irregular-spine shape.
- Impact speed changes the splash stretch; impact direction informs the orientation.
- Fast wall impacts also spawn one to three bounded, surface-aligned satellite stains offset along the projected impact direction, creating a small directional splatter field without extra airborne particle bursts.
- Current decals are surface-aligned meshes. The depth-buffer/screen-space projection path is still future work.

### C. Surface flow, pooling, and wetting

**Target behavior**
- Continue gravity-driven wall streaks and droplets; use contact normals and collision checks to keep streaks attached to the actual surface and stop them at edges.
- Let fresh blood creep along sloped surfaces and gather in crevices using surface normals and a bounded flow field. A screen-space fluid approximation or per-surface flow map is a later option; a full Navier–Stokes solver is not required.
- Grow floor pools over time with a bounded 2D surface grid or compute pass. Spread should be faster on nearly level surfaces, slower on steep slopes, and blocked by vertical barriers. Nearby pools should merge; the combined area must remain capped.
- Let a pool reaching an edge emit a limited number of drips, which can fall and create a new impact event below.
- Add material response profiles for concrete/stone, wood, cloth, metal, and porous surfaces. Profiles control spread rate, wet lifetime, roughness, absorption/darkening, streak persistence, and whether pooling is allowed. Cloth/porous surfaces should absorb and dull sooner; sealed hard surfaces should retain a more reflective surface film.
- Apply a bounded wetting mask to contacted surfaces to darken the underlying diffuse/albedo response and lower roughness while wet. The mask must fade or be removed when the stain dries or is cleaned.
- Keep the existing smear, footprint, body-stain, and blood-transfer behaviors integrated with the same event and lifetime rules.

**Current PR implementation**
- Existing code already has wall streaks, floor-pool spreading and merging, edge drips, smears, footprints, and age-driven drying.
- Material-specific absorption/flow maps and surface wetting masks remain future work; the current collision layer uses simplified solid queries rather than detailed triangle-level liquid simulation.

### D. Fresh-to-dry blood material

**Target behavior**
- Drive appearance from normalized stain age and wetness, rather than a single abrupt state change.
- Fresh blood target: vibrant crimson, low roughness (approximately 0.05–0.10), strong specular response, and smooth rounded highlights. Thin edges may use a restrained translucency/subsurface approximation where the renderer supports it.
- Dry blood target: darker brownish-maroon, medium-high roughness (approximately 0.60–0.80), reduced specular response, and a flatter, irregular/clotted surface.
- Dry from exposed edges inward. Use a procedural normal/height perturbation for small cracks and clots as stains age; avoid expensive full subsurface scattering if the engine's renderer cannot support it.
- Pool thickness and surface normal should affect highlights; thin rims can appear brighter than the center without making the whole pool emissive.
- Treat numeric material values above as authoring targets to tune against the engine's lighting, not guaranteed physical measurements.

**Current PR implementation**
- The existing landed-blood shader already interpolates color/roughness with age and adds a dried-edge/crack treatment.
- The PR does not add true volumetric translucency or physically based subsurface scattering; those remain optional renderer work.

### E. Dismemberment, wound layers, and anatomy assets

- Preserve current joint/stick dismemberment and ragdoll physics.
- Use body-region wound profiles for torso, upper/lower body, hips, thighs, and legs; choose visual layers based on region, event energy, and creator settings.
- Support creator-authored skin, muscle, bone, organ, and intestine assets/materials, with procedural fallback visuals when no asset is supplied.
- Keep the existing bounded intestine-like coils and organ-like fragments as initial placeholders, not anatomical simulation.
- Visual effects must not change damage, hitboxes, physics authority, or character reset rules. Respawn must restore original parts, accessories, visibility, and wound state.

### F. Multiplayer, quality controls, and validation

- Keep gameplay event authority on the server, but do not replicate individual droplets. Replicate compact cosmetic events containing a stable seed, position, normal, incoming velocity/direction, intensity, surface profile, and lifetime; let each client generate local detail.
- Respect gore-disabled settings on both event generation and rendering paths where the current network architecture allows it.
- Expose quality tiers for particle count, mist, decals, flow/pool resolution, persistence distance, detached pieces, and optional anatomy layers. Establish hard per-scene and per-event caps.
- Add deterministic tests for size/drag bounds, surface-normal placement, pool merging, edge drip limits, lifetime cleanup, disabled-gore behavior, respawn restoration, and event-seed consistency.
- Add visual regression scenes for perpendicular vs shallow-angle impacts, wall streaks, slopes, crevices, wood/cloth/concrete/metal, moving ragdolls, and low-quality settings.
- Profile CPU time, GPU time, memory, draw calls, and worst-case particle/decal counts before enabling compute-based systems by default.

### Implementation phases

1. **Phase 1 — particle and collision foundation:** finish velocity-aligned droplets, bounded size distribution, collision classification, and surface-aligned impact splats. (Partially implemented in this PR.)
2. **Phase 2 — material response:** complete age-driven PBR tuning and introduce surface profiles/wetting masks. (Existing drying shader; surface profiles and wetting masks remain.)
3. **Phase 3 — directional projection:** refine impact-angle classification, authored shape selection, and robust surface projection. (Procedural directional splines are implemented; depth-buffer/screen-space decals remain.)
4. **Phase 4 — surface flow and pooling:** implement bounded slope-aware flow, material absorption, and pool-grid/compute simulation if profiling supports it.
5. **Phase 5 — GPU path and production hardening:** prototype GPU particle simulation only after baseline profiling; add quality tiers, multiplayer event replication checks, and automated/runtime visual tests.

### Completion definition

The overall overhaul is complete only when the target behaviors above are implemented, documented, covered by deterministic tests, visually verified in-engine, and shown to respect the configured performance budgets. This PR currently implements the initial CPU particle and procedural-splatter improvements plus the earlier organ/intestine placeholders; the future phases are tracked here explicitly rather than represented as finished.
