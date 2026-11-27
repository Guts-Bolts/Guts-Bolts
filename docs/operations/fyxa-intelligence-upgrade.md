# Fyxa intelligence upgrade

This release groups the next agent improvements into one cohesive implementation: source-grounded retrieval, explicit build planning, iterative scene verification, project-scoped local memory, routing refinements, benchmark expansion, and training-governance checks.

## Source-grounded knowledge retrieval

- `assets/fyxa/knowledge/sources.json` is the explicit allowlist. Every entry is first-party and `reference_only`.
- `FyxaKnowledge` reads only allowlisted paths, rejects absolute/traversal paths and symlinks, caps the manifest and source sizes, splits source files into bounded chunks, and returns ranked excerpts with source IDs and paths.
- Studio exposes this through `fyxa_knowledge_search`. Retrieval is on-demand rather than sending the entire corpus into every prompt.
- Retrieved code/docs are reference data and may contain hostile or accidental instructions. Fyxa must not treat source text as policy, permission, or a prompt override.

## Build planning and verification

The system guide directs multi-object builds to use a task plan, inspect the current scene, create in bounded batches, verify results using scene tools and screenshots, correct failures, and only then report. The existing task store and tool-result evidence workflow remain the completion authority. Planning is not a substitute for authorization or execution.

## Local project memory

- Stored in `fyxa-project-memory.json` next to the Studio app data.
- Scoped by a project label, with bounded titles/content, maximum 500 records, and an atomic replacement write.
- Stored as local plaintext JSON; do not save secrets or sensitive personal information.
- Search/list/save/delete are local tools. Saving is permitted only when the user explicitly asks Fyxa to remember something; deletion is only on request.
- Memory is not sent to G&B servers or training through this implementation. Avoid credentials, sensitive personal information, and private content.

## Tool routing

The native tool selector gains a few intent aliases for map/city/terrain/building/road/layout terms so related scene and object tools are surfaced more consistently. Permission and approval checks are unchanged.

## Evaluation

- The source-grounded evaluation cases cover scripting, networking, UI, tool-use, build planning, verification, retrieval, memory, tool routing, and training-gate behavior.
- `python tools/validate_fyxa_eval.py` checks fixture and source manifest integrity.
- `python tools/evaluate_fyxa_retrieval.py` measures source-path recall for the fixed prompts against the local allowlisted corpus. This is a retrieval smoke test, not a model/provider answer-quality score.
- Provider/model answer evaluation still requires invoking actual model versions and reviewing the output; no live provider score is asserted.

## Training governance

The current screening function is intentionally a conservative scan only and does not mark a source eligible. The new manifest validator checks the required metadata gates (current/public/moderated/ownership, account opt-in, per-content inclusion, rights attestation, staff rights approval, age eligibility, and approved screening), source types, hashes, provenance history, and forbids raw prompts/answers/tool payloads/project files/screenshots/messages.

This is governance groundwork, not a dataset exporter or model-fine-tuning service. A clean automated scan is not approval; training remains blocked until a trusted server-side freeze/revalidation, approved content processing, and provider-specific training interface are implemented and reviewed. The evaluation benchmark remains separate from training data.

## Verification limitations

This workspace can create GitHub commits and run repository actions, but cannot clone the repository locally or execute its C++/Python CI suite directly. Pull-request CI is required to validate compilation and the standalone tests.
