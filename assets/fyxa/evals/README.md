# Fyxa knowledge-quality evaluation set

This directory contains source-grounded evaluation cases for Fyxa's Guts & Bolts knowledge and Studio workflows.

**Purpose: evaluation, not model training.** These cases are a regression benchmark. They must not be mixed into a fine-tuning dataset or used as evidence that a model has been trained.

## Data rules

- Every expected fact must be supported by the listed repository source.
- Prefer executable engine help, implementation, and current engine documentation over assumptions based on Roblox.
- Treat compatibility as granular: supported, partial, missing, or unverified. Never infer full parity from a familiar API name.
- Keep the expected answer focused on what the source proves. The model may add clearly labelled inference, but must not present inference as engine behavior.
- Include negative constraints to catch common hallucinations, especially invented APIs, client-authoritative game state, and claims of successful tool execution without a tool result.
- Do not use private messages, private projects, user memory, credentials, or arbitrary creator uploads as general-purpose training examples.
- Do not include personal or sensitive information in fixtures.

## Case format

Each line in `engine-core.jsonl` is one JSON object:

- `id`: stable benchmark identifier.
- `category`: skill area being tested.
- `prompt`: realistic user request.
- `source_paths`: repository sources that support the answer.
- `expected_facts`: facts a correct answer should preserve.
- `must_not_claim`: unsupported or unsafe claims the answer must avoid.
- `tool_intent`: whether the request is answer-only or requires Studio inspection/action. This is a benchmark label, not an authorization grant.

## Running and interpreting evaluations

A benchmark runner should retrieve the listed sources (or their indexed chunks), ask the target Fyxa provider the prompt, and score:

1. **Retrieval recall:** were the relevant source passages retrieved?
2. **Grounded correctness:** are all required facts present and accurate?
3. **Unsupported-claim rate:** did the answer assert anything in `must_not_claim`?
4. **Tool behavior:** when `tool_intent` is `studio_action`, did Fyxa call an authorized tool and wait for its result before claiming success?
5. **Abstention:** when sources do not establish a fact, does Fyxa say so and identify what would verify it?

Do not use a single aggregate score to hide critical failures. Report each metric by category and provider/model version. Keep a fixed holdout set so prompt or retrieval changes cannot simply overfit the visible examples.

## Data provenance and updates

The benchmark is version-controlled with the engine. When an API or behavior changes, update the relevant source and benchmark case in the same change. Reviewers must verify each expected fact against the cited source paths. Record the tested repository commit and model/provider version with every benchmark run.

The initial set is deliberately small. Expand it from real, anonymized failure modes and documented engine behavior, not synthetic volume. Every new case requires a source review.

## Validation

Run `python tools/validate_fyxa_eval.py` from the repository root. The validator checks JSONL syntax, required fields, unique IDs, allowed tool-intent labels, and that every cited source path exists.
