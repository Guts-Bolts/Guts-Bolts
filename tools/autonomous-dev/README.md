# Guts & Bolts Autonomous Development

A guarded GitHub Actions workflow for finding, ranking, and implementing small G&B improvements.

## Operating loop

On its six-hour schedule, the workflow:

1. Scans the repository README, TODO/FIXME markers, and open issues.
2. Uses an OpenAI model to propose up to three evidence-backed development tasks.
3. Scores proposals using impact (25%), strategic value (25%), confidence (20%), urgency (15%), and technical leverage (15%), with effort and risk penalties.
4. Creates issues for qualifying ideas, then selects the highest-scoring newly created task.
5. Runs Builder on that task in an isolated branch.
6. Builder inspects allowed source areas, makes a bounded change, runs suitable checks, commits, and opens a **draft pull request**.

Only one implementation task is attempted per scheduled run. If Scout creates no qualifying idea, Builder is skipped.

## Setup

1. Add the repository Actions secret `OPENAI_API_KEY`.
2. Optionally set the repository variable `AUTODEV_MODEL`. If unset, the scripts use `gpt-4.1-mini`.
3. Ensure GitHub Actions is enabled and workflow permissions permit contents, issues, and pull-request writes.
4. Run **G&B Autonomous Development** manually in `scout` mode for the first check.
5. Review the generated issue and draft PR.

## Manual operation

In GitHub Actions, select **G&B Autonomous Development**:
- `scout`: generate and rank ideas, then automatically attempt the best new task.
- `builder`: implement a specific issue number, which must carry the `autonomous-ready` label.

## Safety boundaries

- Runs on ephemeral GitHub-hosted runners, not production servers.
- Scout creates labeled issues; it does not edit application code.
- Builder only writes text files under `src/`, `tests/`, `docs/`, `resources/`, `games/`, or the root `README.md`.
- Builder is limited to 8 changed files and 18 model/tool turns by default.
- Build/test subprocesses do not receive `OPENAI_API_KEY`, `GH_TOKEN`, or `GITHUB_TOKEN`.
- Builder opens draft PRs only. It never merges or deploys.
- High-risk areas such as authentication, authorization, payments, secrets, deployment, workflows, CMake configuration, and database migrations are outside its write permissions.
- The issue body is treated as untrusted task data.
- The agent may not change its own controller, workflow, or safety rules.

## Configuration

Repository Actions secret:
- `OPENAI_API_KEY`: an OpenAI API key with API billing/usage enabled.

Optional repository variable:
- `AUTODEV_MODEL`: a model available to the API project. Defaults to `gpt-4.1-mini`.

Workflow defaults:
- Scout schedule: every 6 hours.
- Maximum proposals per run: 3.
- Minimum idea score: 55/100.
- Maximum changed files per Builder run: 8.
- Maximum model/tool turns: 18.

## Current limitations

The first operating version uses repository-local signals rather than community-wide telemetry or a full external competitor research system. It creates draft pull requests, but does not autonomously approve, merge, or deploy. Review CI results and generated diffs before merging.
