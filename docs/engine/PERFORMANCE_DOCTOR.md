# Studio Performance Doctor

## Goal

Give creators an actionable explanation of why a place may run slowly, using evidence from the current project and optional live profiling. The tool should recommend specific fixes rather than merely display a large collection of counters.

This document defines the first implementation boundary. It is a design plan, not a claim that profiling instrumentation already exists.

## Product principles

- **Evidence before advice.** Every finding identifies the measurement or project structure that triggered it.
- **Separate static checks from runtime measurements.** A project scan can estimate risk, but cannot claim an observed frame-time or memory problem.
- **No automatic destructive edits.** Findings may navigate to an object or script; suggested fixes remain user-controlled.
- **Useful without a running game.** Static project checks should work before Play. Runtime measurements appear only when instrumentation is available.
- **Bounded overhead.** Sampling must be lightweight, rate-limited, and disabled outside an active profiling session.
- **Clear confidence.** Label results as measured, statically inferred, or heuristic.

## Proposed Studio workflow

Add a **Performance Doctor** panel under Studio's analysis/tooling area. It should support:

1. **Scan Project** — run static checks against the current scene and scripts.
2. **Profile Play Session** — collect short-window runtime samples while a local Play session is active.
3. **Compare Runs** — compare two saved summaries captured with the same build and scene revision.
4. **Export Report** — save a portable JSON report containing findings, measurements, scene identifiers, and tool version.

The panel should show an overall summary, severity filters, categories, a finding list, and a details view with evidence plus a **Select Object / Open Script** action where a stable target exists.

## Static checks for the first useful release

### Scene and rendering risks

- Count visible renderable objects and summarize their mesh/material distribution.
- Flag unusually high numbers of transparent objects, particle emitters, lights, shadow-casting lights, and high-cost effects.
- Identify repeated mesh/material references that may be candidates for batching or shared-resource reuse.
- Flag very high-poly imported meshes only when reliable mesh statistics are available.
- Detect large numbers of individually moving objects as a heuristic, never as proof of a bottleneck.

### Script risks

- Reuse the Project Dependency Map's script inventory and relationship graph.
- Flag obvious unbounded loops without a yield only when the parser can establish the pattern reliably.
- Flag repeated scene-wide searches or expensive descendant scans inside frequently invoked callbacks when static analysis can identify them.
- Identify duplicate update loops and repeated polling patterns as review suggestions, not guaranteed performance defects.
- Link each finding to the script and, where possible, a source range.

### Physics and simulation risks

- Count active rigid bodies, joints/constraints, collision-enabled objects, and high-cost fluid/particle systems when those properties are exposed by the scene model.
- Flag dense clusters of dynamic objects and large constraint counts as risk indicators.
- Never infer measured physics cost from object count alone.

## Runtime profiler requirements

Runtime measurements should be a separate layer and should only be shown if the engine exposes trustworthy counters.

- Frame time: median, p95, and worst observed frame over the selected window.
- CPU and GPU frame time when independently measured; do not label total frame time as GPU time.
- Physics/script/render phase timings only when the engine has explicit instrumentation for those phases.
- Entity, draw-call, triangle, particle, and memory counters only when sourced from real engine counters.
- Capture duration, sample count, scene/build identity, and dropped-sample count.
- Show a warning when a metric is unavailable instead of filling it with zero or an estimate.

A short profiling session should be user-started, capped in duration/sample count, and stop automatically when Play ends. Avoid writing one log row per frame indefinitely.

## Finding format

Each finding should include:

- Stable rule ID and rule version.
- Severity: info, low, medium, high.
- Category: rendering, scripts, physics, assets, memory, or runtime.
- Evidence and the exact metric or static pattern used.
- Confidence: measured, inferred, or heuristic.
- Target object/script IDs and source range when available.
- Suggested next action and why it may help.
- Explicit limitations and a way to dismiss a non-actionable suggestion.

## Report format and reproducibility

Use a versioned JSON schema. Include project/build identifiers, capture timestamps, settings relevant to profiling, measurement windows, and findings. Do not include account tokens, private credentials, or unrelated user data. Keep reports portable and avoid requiring network access.

Run comparisons should warn if scene revision, build, quality settings, viewport size, or capture duration differ. Do not imply a regression solely because incomparable runs have different numbers.

## Testing and acceptance criteria

- Unit-test each static rule with positive, negative, and boundary fixtures.
- Verify missing/unsupported properties produce an unavailable result, not a fabricated value.
- Test large projects to ensure scans are bounded and do not mutate the scene.
- Test that navigation targets still resolve before selecting an object or opening a script.
- Test profiling start/stop, Play-session termination, sample caps, and report serialization.
- Validate that profiler overhead is measured and disclosed.
- Manually profile a small scene and a deliberately heavy scene; compare the report against actual observed behavior before presenting severity labels as trustworthy.

## Suggested implementation sequence

1. Establish a typed finding model and versioned JSON report schema.
2. Implement static scene counts and a small set of high-confidence rules.
3. Add findings UI with object/script navigation.
4. Add runtime sampling only after identifying existing authoritative engine counters and timing boundaries.
5. Add report export and repeatable comparison.
6. Expand rules only when tests demonstrate that they catch real issues without excessive false positives.

## Non-goals for the initial version

- Automatically rewriting scripts or changing rendering/physics settings.
- Claiming that static estimates are measured performance.
- Guessing GPU utilization from CPU-side data.
- Downloading or uploading project data.
- Treating every high count as a bug.

The initial release is successful when a creator can scan a project, understand why each warning exists, jump to the relevant content, and distinguish confirmed runtime bottlenecks from static risk indicators.
