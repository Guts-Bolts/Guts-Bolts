# Studio Lua Debugger

## Goal

Add an interactive debugger for scripts running in Studio Play/Simulate sessions. Creators should be able to pause at a breakpoint, inspect the call stack and locals, step through code, and resume execution without relying on print statements alone.

This document is the implementation contract and follow-up checklist. The current PR adds local Play/Simulate breakpoints, pause/resume, step into/over/out, a call stack, bounded read-only local-variable inspection, and an editor debugger window. It deliberately does not claim support for gutter breakpoints, conditional expressions, watch evaluation, remote multiplayer debugging, or persisted breakpoint metadata.

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

## Implemented in this PR

- A Studio View > Lua Debugger window can add and remove line breakpoints for a selected/open script during Play or Simulate.
- Lua line hooks pause the actual suspended coroutine at a matching source line; execution is resumed from that coroutine rather than rerunning the script.
- Continue and step into/over/out controls are exposed while paused.
- The window shows the call stack and local variables for the top frame. Values are read without evaluating expressions; tables are not expanded.
- The debugger preserves the existing instruction-count timeout hook so debugging does not disable infinite-loop protection.
- Paused coroutine registry references are retained until resume or the session's Lua VM is closed.

## Added in the follow-up (lua-debugger-fixes)

- Clicking a line number in the Script Editor toggles a breakpoint (red dot); the paused line gets a yellow arrow and highlight, and the editor jumps to it when a script pauses.
- The whole play session waits while a script is paused (Editor::update skips the session, like F6).
- Breakpoints can be set before pressing Play; they stay set between playtests (not saved with the project).
- Continue no longer skips the next hit of the same breakpoint. Lua 5.4 doesn't call the line hook again after a hook yield (CIST_HOOKYIELD), so the old "skip once" ate the next loop iteration.
- Steps follow only the paused coroutine, and end when it finishes.
- Only coroutines resumed by ScriptEngine::resume pause (a coroutine the script runs with coroutine.resume would hand the yield to the script). Code Lua can't yield from (table.sort comparators, metamethods) keeps running with a one-time warning.
- A paused script that is stopped or disabled is dropped cleanly.
- AI tool `lua_debugger` (status, add/remove/clear breakpoints, continue, step over/into/out), checked by tools/test_ai_tools.py.

## Follow-up workflow and capability gaps

1. ~~Add gutter breakpoint toggles to the script editor~~ (done; no enabled/disabled/verified states yet).
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

## Remaining implementation order

1. More runtime tests: error paths and scripts destroyed while paused (breakpoints, stepping and Play shutdown are covered by tools/test_ai_tools.py).
2. ~~Add editor gutter markers and current-line navigation/highlighting.~~ (done)
3. Persist breakpoint metadata safely across Play sessions and project reloads.
4. Add multiple-coroutine selection and explicit server/client target selection.
5. Add watch expressions only with bounded evaluation and clear side-effect safeguards.
6. Consider conditional breakpoints and hit counts after expression evaluation is safely specified.

## Non-goals for the first version

- Rewriting scripts to inject print statements.
- Evaluating arbitrary expressions against a paused runtime by default.
- Claiming source-level stepping when the VM cannot provide reliable line metadata.
- Pausing unrelated Player/server processes without explicit target selection.
- Treating the debugger as a substitute for static Script Analysis.

The debugger is ready for wider use only when pause/resume and stepping are reliable under errors, script edits, coroutine activity, and Play-session shutdown, with tests proving that debugger cleanup cannot strand a runtime.
