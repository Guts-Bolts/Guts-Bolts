# Fyxa Tasks and Skills Implementation Plan

> REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Give Fyxa persistent, inspectable task tracking and safe, on-demand skill loading inside its existing Studio assistant.

**Architecture:** Add two small C++ components with injectable filesystem paths so they can be tested without launching Studio. Task state is stored as JSON in the app's writable data directory; skills are Markdown files discovered from a bundled directory and a user-writable directory. Expose both through provider-neutral Fyxa tools translated into each model provider's existing tool format and routed locally, never through external MCP.

**Tech Stack:** C++20, nlohmann::json, std::filesystem, existing CMake/CTest test pattern.

**Spec:** docs/superpowers/specs/2026-11-08-fyxa-claw-code-adaptations.md and docs/superpowers/specs/2026-11-08-fyxa-platform-agent-design.md.

## Global Constraints

- Fyxa is the Guts & Bolts-wide agent; Studio is one capability surface.
- Model-generated tool requests never grant permissions or authority.
- Skill content is untrusted instruction material and cannot grant tool permissions.
- Task completion is recorded only when Fyxa has evidence from the operation it reports.
- No new third-party dependencies.
- Persistent files stay in the app's writable data directory; tests use temporary directories.
- Skill names are restricted to lowercase letters, digits, and hyphens; files are capped at 64 KiB.
- Task titles are capped at 160 bytes; descriptions and notes are capped at 4 KiB.
- Task statuses are limited to pending, in_progress, blocked, and completed.

## Review Focus

- Malformed or truncated task JSON must not crash Fyxa or silently overwrite a valid file.
- A failed write must not report a task as created or updated.
- Skill names containing traversal, separators, or unexpected characters must be rejected.
- Oversized or unreadable skill files must not be loaded into model context.
- A model must not be able to mark a task complete by inventing evidence; the system prompt requires real operation results.

---

### Task 1: Persistent task store

**Files:**
- Create: src/editor/FyxaTaskStore.h
- Create: src/editor/FyxaTaskStore.cpp
- Create: tests/FyxaTaskStoreTests.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- FyxaTaskStore(const std::filesystem::path& file)
- nlohmann::json list() const
- nlohmann::json create(const std::string& title, const std::string& description, std::string& error)
- bool update(const std::string& id, const std::string& status, const std::string& note, std::string& error)

- [ ] Add a minimal stub implementation and tests for create/list, status validation, unknown IDs, and persistence across instances.
- [ ] Run the task-store test and verify assertion failures occur for the unimplemented behavior.
- [ ] Implement bounded validation, generated IDs, JSON persistence, and safe temporary-file replacement.
- [ ] Run the task-store test and full configured CTest suite.
- [ ] Add the component to editor sources and the standalone CTest target.

### Task 2: Safe Markdown skill loader

**Files:**
- Create: src/editor/FyxaSkills.h
- Create: src/editor/FyxaSkills.cpp
- Create: tests/FyxaSkillsTests.cpp
- Create: assets/fyxa/skills/studio-basics.md
- Create: assets/fyxa/skills/lua-scripting.md
- Create: assets/fyxa/skills/debugging.md
- Modify: CMakeLists.txt

**Interfaces:**
- FyxaSkills(const std::filesystem::path& bundledDir, const std::filesystem::path& userDir)
- nlohmann::json list() const
- bool load(const std::string& name, std::string& content, std::string& error) const

- [ ] Add tests for listing, loading bundled and user skills, traversal rejection, and size limits.
- [ ] Run tests to verify the missing loader behavior fails.
- [ ] Implement filename validation, bounded reads, and user-skill precedence over bundled names.
- [ ] Run the skill tests and full configured CTest suite.

### Task 3: Expose tasks and skills to both model providers

**Files:**
- Modify: src/editor/panels/AssistantPanel.h
- Modify: src/editor/panels/AssistantPanel.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- Local tools: fyxa_task_list, fyxa_task_create, fyxa_task_update, fyxa_skill_list, fyxa_skill_load.
- Tool schemas must be added to both Ollama and Claude request formats.
- Tool results must be handled locally before the existing external MCP dispatch.
- User skill files are instruction content only; they do not alter tool authorization.

- [ ] Add provider-neutral local tool definitions and execution helper.
- [ ] Add failing tests for tool schemas, invalid arguments, and skill/tool dispatch if the existing test structure permits.
- [ ] Wire tools into Ollama and Claude and return structured tool results.
- [ ] Extend Fyxa's system guide to use tasks for multi-step work and load only relevant skills on demand.
- [ ] Verify CMake builds and all CTest tests pass; inspect CI results for Windows and Linux.

### Task 4: Documentation and PR status

**Files:**
- Modify: docs/superpowers/specs/2026-11-08-fyxa-claw-code-adaptations.md
- Modify: PR #283 description.

- [ ] Mark task tracking and skills as implemented only after tests and builds pass.
- [ ] Record platform limitations and outstanding work accurately.
- [ ] Keep PR #283 draft and unmerged.


## Execution status

Implementation files, standalone test targets, provider tool wiring, Studio task/skill panels, and the three bundled skill packs have been added. Verification remains incomplete: the available GitHub Actions run was created for an earlier commit and its native build/test jobs were cancelled as newer commits arrived. No completed build or test result for the current head is available, so this plan and PR remain unverified and draft.
