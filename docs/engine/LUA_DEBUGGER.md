# Studio Lua Debugger

## Goal

Add an interactive debugger for scripts running in Studio Play/Simulate sessions. Creators should be able to pause at a breakpoint, inspect the call stack and locals, step through code, and resume execution without relying on print statements alone.

This is a design and implementation checklist. It does not claim that the current runtime exposes all required debug hooks.

## Required runtime capability audit

Before building the UI, confirm the scripting runtime can support each operation safely:

- Install and remove line breakpoints per script and line.
- Pause execution at a safe interpreter boundary.
- Resume, step into, step over, and step out without corrupting the VM.
- Enumerate stack frames and local/upvalue values without invoking user code or metamethods.
- Map runtime source locations back to Studio script IDs and line numbers.
- Handle multiple script coroutines and identify which coroutine is paused.
- Stop or detach debugging when Play ends, a script is destroyed, or a project is reloaded.

If the embedded Lua/Luau runtime lacks a capability, expose it as unavailable and track the runtime work separately. Do not simulate stepping by rerunning scripts.

## First release workflow

1. Add gutter breakpoint toggles to the script editor with clear enabled/disabled/verified states.
2. Add a Debug toolbar with Start Debugging, Continue, Pause (where safe), Step Over, Step Into, Step Out, and Stop.
3. Show a visible paused-execution banner with the active script, line, coroutine, and reason for pausing.
4. Highlight the current execution line separately from ordinary selection and diagnostic underlines.
5. Add a call-stack pane; selecting a frame navigates to its script and line.
6. Add Locals and Watch panes with safe, read-only value inspection.
7. Add a debug console that distinguishes debugger control messages from normal script output and runtime errors.

## Breakpoint model

A breakpoint should have a stable identity based on script ID and source line, plus enabled state and optional condition. For the initial version, support unconditional line breakpoints first. Conditional breakpoints and hit counts should follow only after safe expression evaluation is defined.

- Persist breakpoint metadata separately from script source unless the project format already has a standard mechanism for editor-only metadata.
- Revalidate breakpoints after edits and show when a line moved or no longer maps to executable code.
- Do not insert print statements or mutate user source to implement breakpoints.
- Avoid silently binding a breakpoint to a different script when IDs change.

## Value inspection and safety

- Read values directly from debugger/runtime metadata; do not evaluate arbitrary expressions by default.
- Use bounded rendering for tables, recursive values, and long strings.
- Detect cycles and limit nesting, item counts, and string lengths.
- Never call arbitrary __index, __tostring, or other user-defined metamethods just to display a value.
- Make inaccessible values explicit rather than showing misleading placeholders as actual nil.
- Avoid exposing secrets from unrelated processes or host environment; inspect only values belonging to the paused script VM.

## Runtime and multiplayer behavior

The first version should target a local Play/Simulate session. A breakpoint should pause only the selected local runtime session and must not freeze the Studio UI or unrelated Player processes.

For multiplayer testing, explicitly distinguish server and each Player process. Remote debugging can be a later milestone with opt-in target selection and clear pause scope. Never let a client-side breakpoint silently pause the authoritative server.

## Error and lifecycle handling

- Runtime errors should link to script, line, and stack frame when source metadata is available.
- Script reload, deletion, or hot edit should invalidate stale stack frames safely.
- Stop debugging on Play shutdown and release all hooks and references.
- If a script is waiting on an event or native call, explain why instruction stepping cannot currently advance it.
- Debugger failure must not leave the runtime permanently paused.

## Tests and acceptance criteria

- Breakpoint hit at the requested line, including multiple breakpoints in one script.
- Continue and each stepping mode follow the expected call/return behavior.
- Multiple coroutines pause and resume independently or report a documented runtime limitation.
- Call-stack frames and locals map to the correct script and source lines.
- Table inspection is bounded, cycle-safe, and does not invoke user metamethods.
- Editing, deleting, or reloading a paused script cannot crash the runtime or leave stale references.
- Stop/Play shutdown always removes debugger hooks and unblocks execution.
- UI remains responsive while the runtime is paused.
- Tests cover scripts with no debug symbols/source mapping and clearly display unavailable information.

## Implementation order

1. Audit the embedded Lua/Luau VM for debug hooks, source metadata, stack inspection, and safe pause/resume.
2. Add a minimal runtime debug-session API with lifecycle tests before adding editor UI.
3. Implement unconditional breakpoints and Continue/Stop.
4. Add current-line highlighting and call stack.
5. Add safe locals inspection.
6. Add Step Over/Into/Out and multiple coroutine handling.
7. Add watches, conditional breakpoints, hit counts, and remote multiplayer debugging only after core semantics are reliable.

## Non-goals for the first version

- Rewriting scripts to inject print statements.
- Evaluating arbitrary expressions against a paused runtime by default.
- Claiming source-level stepping when the VM cannot provide reliable line metadata.
- Pausing unrelated Player/server processes without explicit target selection.
- Treating the debugger as a substitute for static Script Analysis.

The debugger is ready for wider use only when pause/resume and stepping are reliable under errors, script edits, coroutine activity, and Play-session shutdown, with tests proving that debugger cleanup cannot strand a runtime.
