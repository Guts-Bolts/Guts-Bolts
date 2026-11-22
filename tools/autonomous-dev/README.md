# Guts & Bolts Autonomous Development (Foundation)

This is a guarded, GitHub Actions-based autonomous development foundation. Scout researches the repository and proposes deduplicated tasks as GitHub Issues. Builder is an isolated workflow that can be started manually after the first foundation is reviewed.

## Initial safety model
- Runs on a GitHub-hosted runner, not on production servers.
- Scout may create labeled issues only.
- Builder works in a new branch and opens a **draft PR**; it never merges or deploys.
- API key is stored in Actions secret `OPENAI_API_KEY`.
- Builder is intentionally not enabled on issue-open events in this foundation version.
- No secrets, credentials, environment files, or production data are exposed to the model tools.

## Setup
1. Add `OPENAI_API_KEY` as a repository Actions secret.
2. Ensure GitHub Actions is enabled and has permission to create issues and pull requests (workflow permissions are declared in the YAML).
3. Review the workflow and Python scripts before enabling scheduled runs.
4. Run the Scout workflow manually once. It creates issues tagged `autonomous-ready`.
5. Run Builder manually with an issue number to generate a draft PR.
6. After observing safe, reliable results, consider a future staged upgrade to issue-triggered runs, independent review, and gated auto-merge.

## How Scout works
- Reads the README and bounded repository signals (TODO/FIXME markers, open issues).
- Uses the configured OpenAI model to propose actionable, small improvements.
- Deduplicates against open issues by normalized title.
- Creates up to 3 new issues per run; labels: `autonomous-ready`, `ai-generated`.

## Builder
Builder takes a selected issue number, reads the task and repository context, and uses a constrained tool loop to make changes. It can read files, write files, and run a short allowlist of validation commands. It creates a branch and a draft PR with test results. It does not merge or deploy.

## Configuration
- `OPENAI_MODEL` (optional, default `gpt-4.1-mini`)
- `AUTODEV_MAX_FILES` (optional, default 8)
- `AUTODEV_MAX_TOOL_CALLS` (optional, default 18)

The first release is deliberately review-first. Do not enable unattended merge or production deployment until the pipeline has proven stable.
