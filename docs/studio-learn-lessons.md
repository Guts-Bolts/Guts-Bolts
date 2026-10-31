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
