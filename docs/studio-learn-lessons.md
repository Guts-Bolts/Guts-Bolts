# Studio Learn: interactive lessons

## Goal

Add a **Learn** area to **Create** that teaches people how to use Guts & Bolts Studio by doing the work inside Studio. This is not a collection of videos or static help articles. Each lesson is a short, hands-on activity with two deliberate phases: **watch and learn**, followed by **try it yourself**.

## Intended player journey

1. A creator opens **Create > Learn** on the website.
2. They browse lessons grouped by skill level and topic, with a short description, estimated time, and completion state.
3. Selecting **Start lesson** opens Guts & Bolts Studio through the existing Guts & Bolts app launch flow and starts the selected lesson.
4. Studio opens the lesson panel and begins with a guided demonstration. The lesson explains the task and shows the learner exactly where to click, using a visual highlight on the relevant Studio control.
5. Once the learner has seen the technique, the lesson switches to a practice challenge. The learner performs the same kind of task independently, without the answer permanently highlighted.
6. A small **lightbulb button** is available during practice. Clicking it reveals a contextual hint by highlighting the relevant menu, panel, button, or property. The hint points the learner in the right direction; it does not click the control or complete the task for them.
7. Studio detects the learner's actual action where possible, confirms success, and moves to the next lesson step. Progress is saved locally first and synchronized to the signed-in account when online.

## Website: Create > Learn

Add a **Learn** tab to the existing Create tab strip. The page should include:

- A clear introduction: learn Studio by building something, one step at a time.
- Lesson cards with title, skill level, estimated duration, topic, short outcome, and progress.
- Filters for Beginner / Intermediate / Advanced and topic (Building, Physics, Scripting, UI, Modeling, Publishing).
- A **Continue learning** area for an in-progress lesson.
- A **Start lesson** action that launches the desktop app with a validated lesson identifier. If the app is not installed, show the existing download path and explain that lessons run in desktop Studio, not the mobile Player.
- A completion state and a link back to the lesson catalogue.

The page must work for signed-out visitors too. They can browse and start beginner lessons; signing in is only required for cross-device progress sync.

## Studio: reusable lesson runner

Create a reusable, data-driven lesson runner rather than hard-coding a separate tutorial window for every lesson. A lesson definition includes:

- Stable lesson ID, title, summary, level, topic, duration, and optional prerequisite IDs.
- Ordered steps, each with a **demonstration** and a **practice challenge**.
- Concise instructions, optional explanation, and optional hint text for each phase.
- UI target metadata identifying the relevant menu, panel, control, or property to highlight.
- A completion condition that can observe the real editor state where possible.
- Optional safe setup action, such as creating a clean starter scene. Never silently overwrite an existing project.

### Phase 1: guided demonstration

- Explain what the learner is about to do and why it matters.
- Walk through the action in small steps, one control at a time.
- Highlight the exact control the learner should click using a clear outline, glow, spotlight, or pointer callout. Keep the rest of the interface visible.
- Use short labels such as **Click Insert**, **Select Move**, or **Change Anchored** rather than long paragraphs.
- Wait for the expected interaction before advancing. Do not auto-click controls on the learner's behalf.
- Allow **Repeat** to replay the current explanation.

The demonstration should feel like someone showing the learner around Studio, not a video playing over an inactive editor. The learner should make the clicks and see the real editor respond.

### Phase 2: try it yourself

After the demonstration, present a small challenge that asks the learner to repeat or apply the skill without step-by-step highlighting. For example, after demonstrating how to insert and resize a Part, ask the learner to create another Part and make it a different size.

- Keep the challenge instruction visible, but remove the always-on target highlight.
- Provide a small, persistent **lightbulb icon button** near the lesson instructions.
- Clicking the lightbulb briefly reveals a visual highlight around the relevant UI target and displays a short contextual hint.
- The hint can identify the correct menu, button, panel, or property, but must not perform the action or mark the challenge complete.
- Allow the learner to dismiss the hint and try again. The hint should be optional and should not block editor input.
- If a challenge has multiple actions, hints should reveal the next useful target rather than exposing the entire solution at once.
- Do not penalize learners for using hints. Completing a challenge with hints still counts as completing the lesson.

### Lesson panel controls and feedback

The panel should provide:

- Step count and progress indicator, including whether the learner is in demonstration or practice.
- Current instruction and a short explanation of why the task matters.
- **Lightbulb hint**, **Repeat**, **Back**, **Pause**, and **Exit lesson** controls as applicable.
- Clear feedback when an action is detected and when a challenge is complete.
- A way to dismiss the panel and reopen it without losing progress.
- Keyboard and mouse accessibility, readable text, and a layout that does not permanently cover the viewport or essential panels.

A step must not be marked complete just because the learner pressed Next. Prefer observing actual editor state (for example, a Part was inserted, moved, or resized; a property was changed; a Script was created; or Play mode was entered). If an action cannot be reliably observed, explain it and require an explicit **I did this** confirmation.

### Visual highlighting behavior

- Use a consistent highlight treatment for all lesson targets so players learn what it means.
- The highlight should be high-contrast, animated subtly (for example, a soft pulse), and visible without obscuring the control label.
- If the target is inside a closed menu or collapsed panel, guide the learner to open the parent first, then highlight the next target.
- If a panel is moved, resized, hidden, or unavailable, do not draw a misleading highlight at stale coordinates. Re-resolve the target or show a text hint explaining where to find it.
- Highlighting is an overlay only; it must never intercept clicks or change editor state.
- Respect reduced-motion settings where available.

## First lesson set

Ship a small, polished set first; add lessons incrementally using the same runner.

| Lesson | Level | Outcome |
|---|---|---|
| Your First Build | Beginner | Insert a Part, move it, resize it, anchor it, and save a simple scene |
| Make It Move | Beginner | Add a physics object, change its physical properties, and test it in Play mode |
| Build a Tiny Obby | Beginner | Create a short sequence of platforms and test the route |
| Your First Script | Beginner | Create a Script, add a simple Lua behavior, and run the game to see it work |
| Light Up the Scene | Beginner | Add a light and adjust its properties |
| Publish Your First Game | Beginner | Set a title and description, save, and publish a game |

The first release should prioritize **Your First Build**, **Make It Move**, and **Your First Script**. Each should include at least one demonstration followed by an independent practice challenge. Keep lesson content accurate to the current Studio UI and use the same labels as the menus and panels.

## Progress and data

- Store lesson progress by stable lesson ID and completed step IDs, not by display title or array position.
- Persist progress locally so offline learners do not lose it.
- Track demonstration and practice completion separately so a learner can resume the correct phase.
- Persist whether a challenge was completed with or without hints only if useful for optional learning analytics; never make hint use affect completion or rewards.
- Sync signed-in progress through a dedicated server operation with validation and reasonable size limits.
- Do not store scene contents or personal data as part of lesson progress.
- Make lesson completion idempotent and safe to retry.
- Changing lesson content must not reset already completed lessons; use a lesson content version only when a deliberate progress reset is required.

## Safety and project handling

- Starting a lesson must never overwrite an existing scene without an explicit confirmation.
- Prefer a new untitled scene for lessons that need a clean workspace. If the current scene has unsaved changes, offer save, discard, or cancel.
- Lessons must not execute downloaded Lua or arbitrary remote content. Lesson definitions should be shipped with the app.
- Publishing lessons must use the existing publish confirmation and permissions.

## Acceptance criteria

- [ ] Create has a Learn tab and a responsive lesson catalogue.
- [ ] Each lesson card clearly communicates what the learner will make or learn.
- [ ] Starting a lesson opens Studio and selects the requested lesson.
- [ ] Studio displays a reusable, dismissible, step-by-step lesson panel.
- [ ] Each initial lesson teaches a skill in a guided demonstration, then asks the learner to apply it independently.
- [ ] Demonstration targets are visually highlighted at the correct point in the walkthrough.
- [ ] Practice mode removes the always-on highlight and provides an optional lightbulb hint button.
- [ ] Clicking the lightbulb highlights the relevant UI target and explains what to look for, without clicking it or completing the task.
- [ ] Hints are contextual, dismissible, non-blocking, and can reveal multi-action solutions progressively.
- [ ] At least three beginner lessons are complete and tested end-to-end.
- [ ] At least one lesson detects a real editor action rather than relying only on Next.
- [ ] Repeat, back, pause, resume, and exit behave consistently.
- [ ] Existing unsaved work is protected when a lesson starts.
- [ ] Progress survives app restarts and works offline.
- [ ] Signed-in progress can sync across devices without storing project contents.
- [ ] Tests cover lesson definitions, highlight target resolution, hint behavior, progress persistence, and the first lesson flows.
- [ ] The website's Create page remains usable on desktop and mobile; the lessons themselves clearly require desktop Studio.

## Lesson quality standards

Every lesson should teach one transferable skill, not just walk through a sequence of clicks. A learner should leave knowing what they changed, why it mattered, and how to repeat it in a different project.

### A consistent lesson shape

Use this structure for each lesson:

1. **Outcome:** State the thing the learner will make or the skill they will gain in one sentence.
2. **Why it matters:** Give one short, practical reason to use the skill.
3. **Guided demonstration:** Break the task into small actions. Each action has one instruction, one UI target, and one observable result.
4. **Try it yourself:** Give the learner a similar but not identical task. Do not simply ask them to repeat the exact clicks with the answer still visible.
5. **Check your work:** Explain what success looks like and validate the actual result when the editor can observe it.
6. **Take it further:** Offer one optional variation that encourages experimentation without being required for completion.

Keep each instruction focused on one action. Prefer "Select the Part, then change its Size" over a paragraph containing several unrelated directions. Define unfamiliar terms the first time they appear, in plain language, without hiding the real Studio terminology.

### First-release lesson blueprints

These blueprints define the teaching experience and expected outcomes. Before implementation, verify every target name and property against the current Studio UI. Do not invent menu names or rely on fixed screen coordinates.

#### Your First Build

**Outcome:** Make and save a small platform using a Part.

| Order | Guided demonstration | Independent practice | Success check |
|---|---|---|---|
| 1 | Find the control for inserting a basic Part and insert one. Explain that a Part is a basic 3D building block. | Insert a second Part. | The scene contains a newly created Part. |
| 2 | Select the Part and use the move controls to place it above the ground. | Move the second Part to a different position. | Its position differs from its starting position. |
| 3 | Change the Part's size using the relevant transform control or Properties field. | Make the second Part noticeably wider than it is tall. | Its dimensions reflect the requested shape. |
| 4 | Find the Anchored property and turn it on. Explain that anchoring prevents gravity and physics from moving the Part. | Anchor the second Part too. | The selected Part has Anchored enabled. |
| 5 | Save the project using the current save workflow. | Save the scene under a name the learner chooses. | The save operation reports success. |

**Hint ladder:** (1) where to create objects, (2) how to select and transform an object, (3) where object properties live, (4) which physics property keeps a Part in place, (5) where saving is located.

**Optional variation:** Build a two-step staircase using Parts of different heights.

#### Make It Move

**Outcome:** Understand the difference between a fixed object and an object that physics can move.

| Order | Guided demonstration | Independent practice | Success check |
|---|---|---|---|
| 1 | Insert a Part and position it above a surface. | Create a second object above the surface. | The practice object exists above the ground. |
| 2 | Show the relevant physics property and explain how Anchored changes whether physics can move the object. | Configure the practice object so physics can move it. | The object's anchored state is correct for the challenge. |
| 3 | Start Play mode and observe gravity acting on the unanchored object. | Run the scene and observe the second object. | The object moves under simulation; do not require an exact landing position. |
| 4 | Stop Play mode and explain that test changes may differ from the editable scene. | Return to editing and make one deliberate change before testing again. | The learner can return to editing and repeat the test. |

**Hint ladder:** locate the object, locate its physics properties, identify the anchored setting, then locate the Play control.

**Optional variation:** Compare two objects side by side, one anchored and one unanchored.

#### Your First Script

**Outcome:** Create a Script, make it run, and see its output.

| Order | Guided demonstration | Independent practice | Success check |
|---|---|---|---|
| 1 | Show where scripts are created and explain that a Script contains instructions the game can run. | Create a second Script in an appropriate location. | A Script exists in the intended location. |
| 2 | Open the script editor and identify the editable code area. | Open the practice Script. | The correct Script is open for editing. |
| 3 | Enter a tiny, safe example that prints a short message. Explain what the print call does. | Change the message to something of the learner's choice. | The script contains the learner's changed message. |
| 4 | Run the game and show where script output appears. | Run the game and find the practice message in the output. | The expected message appears in the output. |

**Hint ladder:** where scripts are added, how to open one, where code is edited, and where output is displayed.

**Optional variation:** Print a second message or combine two pieces of text. Keep the first lesson focused on running a script, not on teaching the entire language.

### Practice and hint behavior

- Practice tasks should change at least one detail from the demonstration (different object, position, size, name, or message) so the learner must apply the concept.
- Hints should be layered: first identify the relevant area, then narrow to the control, then explain the property or action. The learner chooses when to reveal each level.
- Hints must never reveal a destructive action without a clear warning. A hint must not trigger a click, edit a property, insert an object, execute code, or save a project.
- A learner can retry indefinitely. Do not grade speed, penalize undo, or require hint-free completion.
- If the expected state is not detected, show a useful recovery message (for example, "Select the Part first") rather than a generic failure.
- If the learner makes a different but valid choice, accept it when it satisfies the learning objective. Avoid brittle checks for exact coordinates, names, or cosmetic values unless the task explicitly requires them.
- Keep the optional "Take it further" activity separate from required completion. It must never block progress.

### Progress, resuming, and learner confidence

Represent each step with separate demonstration and practice states: not started, in progress, and complete. Save after meaningful transitions, not only when the whole lesson ends. Reopening a lesson should return to the current phase and step.

Show progress in human terms (for example, "Step 2 of 4 · Practice") rather than a percentage alone. On completion, summarize the skills demonstrated and offer a clear next action: repeat the lesson, return to Learn, or try the next recommended lesson.

Do not show a failing red state for an unfinished task. Use neutral guidance while the learner is working, clear success feedback when the condition is met, and actionable recovery guidance when it is not.

### Content and interaction QA

Before a lesson ships, test it with a clean project and with a project that already contains objects. Verify:

- Every instruction matches a control or label that exists in the current build.
- The highlight points to the correct visible control at common window sizes and after panels are rearranged.
- A closed menu or hidden panel is handled as a navigation step, not by drawing a highlight at an invisible location.
- The learner can complete the task using keyboard navigation where the control supports it.
- Undo, redo, accidental selection changes, and repeated clicks do not corrupt lesson state.
- A learner can pause, close the panel, restart Studio, and resume at the right step.
- An offline session can complete and retain progress.
- A failed or unavailable validation falls back to an explicit "I did this" confirmation with a clear explanation.
- The lesson never silently discards unsaved work or runs code without the learner choosing to start Play mode.

