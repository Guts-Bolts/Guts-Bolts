# Studio Learn: interactive lessons

## Goal

Add a **Learn** area to **Create** that teaches people how to use Guts & Bolts Studio by doing the work inside Studio. This is not a collection of videos or static help articles. Each lesson is a short, guided, hands-on activity in a real Studio session.

## Intended player journey

1. A creator opens **Create > Learn** on the website.
2. They browse lessons grouped by skill level and topic, with a short description, estimated time, and completion state.
3. Selecting **Start lesson** opens Guts & Bolts Studio through the existing Guts & Bolts app launch flow and starts the selected lesson.
4. Studio opens a guided lesson panel. It explains one task at a time, highlights the relevant UI where practical, and gives the learner room to use the real editor.
5. The lesson advances when the required action is detected. A learner can also open a hint, repeat the explanation, go back, pause, or leave the lesson.
6. Progress is saved locally first and synchronized to the signed-in account when online. A lesson can be resumed from either Create > Learn or Studio.

## Website: Create > Learn

Add a **Learn** tab to the existing Create tab strip. The page should include:

- A clear introduction: learn Studio by building something, one step at a time.
- Lesson cards with title, skill level, estimated duration, topic, short outcome, and progress.
- Filters for Beginner / Intermediate / Advanced and topic (Building, Physics, Scripting, UI, Modeling, Publishing).
- A **Continue learning** area for an in-progress lesson.
- A **Start lesson** action that launches the desktop app with a validated lesson identifier. If the app is not installed, show the existing download path and explain that lessons run in desktop Studio, not the mobile Player.
- A completion state and a link back to the lesson catalogue.

The page must work for signed-out visitors too. They can browse and start beginner lessons; signing in is only required for cross-device progress sync.

## Studio: guided lesson mode

Create a reusable lesson runner rather than hard-coding a separate tutorial window for every lesson. A lesson is data-driven and consists of:

- Stable lesson ID, title, summary, level, topic, duration, and optional prerequisite IDs.
- Ordered steps with concise instructions, optional hints, and a completion condition.
- Optional UI target metadata (menu, panel, or control to point out) and optional visual callout.
- Optional step action for safe setup, such as creating a clean starter scene. Never silently overwrite an existing project.

The lesson panel should provide:

- Step count and progress indicator.
- Current instruction and a short explanation of why the task matters.
- **Hint**, **Repeat**, **Back**, **Next**, **Pause**, and **Exit lesson** controls as applicable.
- Clear feedback when a step is complete.
- A way to dismiss the panel and reopen it without losing progress.
- Keyboard and mouse accessibility, readable text, and a layout that does not permanently cover the viewport or essential panels.

A step must not be marked complete just because the learner pressed Next. Prefer observing actual editor state (for example, a Part was inserted, a property was changed, a Script was created, or Play mode was entered). If a step cannot be reliably observed, explain the action and require an explicit **I did this** confirmation.

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

The first release should prioritize **Your First Build**, **Make It Move**, and **Your First Script**. Keep lesson content accurate to the current Studio UI and use the same labels as the menus and panels.

## Progress and data

- Store lesson progress by stable lesson ID and completed step IDs, not by display title or array position.
- Persist progress locally so offline learners do not lose it.
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
- [ ] At least three beginner lessons are complete and tested end-to-end.
- [ ] At least one lesson detects a real editor action rather than relying only on Next.
- [ ] Hints, repeat, back, pause, resume, and exit behave consistently.
- [ ] Existing unsaved work is protected when a lesson starts.
- [ ] Progress survives app restarts and works offline.
- [ ] Signed-in progress can sync across devices without storing project contents.
- [ ] Tests cover lesson definitions, progress persistence, and the first lesson flows.
- [ ] The website's Create page remains usable on desktop and mobile; the lessons themselves clearly require desktop Studio.
