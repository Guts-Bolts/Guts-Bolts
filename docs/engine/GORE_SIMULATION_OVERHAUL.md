# Gore & Dismemberment Simulation Overhaul

## Goal

Extend the existing R6 ragdoll and gore systems into a configurable, performance-conscious engine feature. The repository already has ragdoll stick-cutting/dismemberment, gore toggles, and body-part blood smearing; this proposal targets the next layer of physical credibility instead of duplicating those systems.

## Scope

### 1. Blood behavior and surface interaction

- Add a dedicated blood simulation layer that consumes impact, wound, and body-contact events from the existing ragdoll system.
- Support gravity-driven wall streaks, droplets, splashes, and floor pooling.
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

The first implementation slice adds stylized rounded organ-like fragments and short intestine-like strand segments to the existing flesh/bone gib system. Torso, upper-body, lower-body, leg, thigh, and hip part names trigger a bounded coil when those parts burst. These are procedural visual cues rather than anatomically accurate organ models; creators can later replace them with authored assets.

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
2. Introduce a small blood-event/data model and surface-material response table.
3. Implement gravity-driven streaks and bounded floor pooling.
4. Integrate event generation with existing hit, dismemberment, and ragdoll-contact paths.
5. Add creator settings, quality budgets, and multiplayer event replication.
6. Add optional skeletal and layered wound assets through data-driven presets.
7. Add deterministic tests for lifetime cleanup, pool budgets, respawn restoration, and gore-disabled behavior, followed by in-engine visual tests.

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
