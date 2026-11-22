# Guts & Bolts Autonomous Development (Local Ollama)

A zero-API-cost autonomous development workflow. GitHub Actions coordinates the work, but Scout and Builder run on your Windows PC using a local Ollama model.

## Architecture

Every six hours (or on manual dispatch), the workflow:

1. Scans repository README signals, TODO/FIXME markers, and open issues.
2. Uses local Ollama to propose up to three evidence-backed improvements.
3. Scores ideas (impact 25%, strategic value 25%, confidence 20%, urgency 15%, leverage 15%) and applies effort/risk penalties.
4. Creates qualifying labeled issues and selects the highest-ranked newly created issue.
5. Builder uses the same local model to inspect permitted project files, implement one focused change, run approved checks, and open a **draft PR**.

No paid model API or API key is used. The PC must be powered on, awake, connected to the Internet, with the GitHub runner service online and Ollama running. If the runner is offline, the scheduled job waits until a runner is available.

## Windows host setup

### 1. Install prerequisites

Install and keep available to the runner account:
- Git for Windows
- Python 3.12
- GitHub CLI (`gh`)
- CMake (for CMake validation)
- Ollama for Windows

Ollama must be running and reachable at `http://127.0.0.1:11434`. GitHub runner service should run as a **dedicated, non-administrator Windows account**, not your everyday account if practical.

### 2. Pull the local model (one-time)

Default model:
```powershell
ollama pull qwen3:8b
ollama show qwen3:8b
```

Qwen3 8B is the default starting point for the RTX 5070 12 GB system. You can change it later using the repository Actions variable `AUTODEV_OLLAMA_MODEL`, but test tool calling before switching models.

### 3. Install GitHub self-hosted runner

In the repository, open **Settings → Actions → Runners → New self-hosted runner → Windows x64** and follow GitHub's displayed download/configuration commands on the Windows PC.

During configuration:
- Use the labels `self-hosted`, `windows`, `x64`, and add the custom label `gb-autodev`.
- Configure it as a Windows service if supported by the runner installer.
- Use the dedicated runner account with only the local permissions needed for build/test.
- Do not paste the runner registration token into ChatGPT or commit it to the repository.

### 4. Install GitHub CLI and ensure tools are on PATH

If GitHub CLI is missing, install it:
```powershell
winget install --id GitHub.cli --exact
```

Make sure the runner's service account can execute `gh`, `git`, `python`, `cmake`, and `ollama`. If tools are installed after runner configuration, restart the runner service so it sees the updated PATH.

### 5. Optional repository variables

Repository → Settings → Secrets and variables → Actions → Variables:

| Variable | Default | Purpose |
|---|---|---|
| `AUTODEV_OLLAMA_MODEL` | `qwen3:8b` | Local Ollama model name |
| `AUTODEV_OLLAMA_URL` | `http://127.0.0.1:11434/api/chat` | Ollama chat endpoint |

Leave these unset to use defaults. **No API secrets are required for model inference.** The GitHub Actions token is short-lived and permission-limited by this workflow.

## Manual runs

Workflow: **G&B Autonomous Development**
- `scout`: Scout ideas, score them, and automatically try to implement the highest-ranked new issue.
- `builder`: Implement a specific issue number. That issue must have the `autonomous-ready` label.

## Guardrails

- Only scheduled or manual workflow dispatches from `main` run on the self-hosted machine.
- There is **no pull-request trigger** on the self-hosted workflow. PR validation stays on GitHub-hosted runners.
- Scout creates issues, not code.
- Builder only writes text files in `src/`, `tests/`, `docs/`, `resources/`, `games/`, or root `README.md`.
- Builder is capped at 8 changed files and 18 model/tool turns.
- Build/test child processes receive no GitHub credentials.
- Only four allowlisted validation commands are available.
- Builder creates draft PRs only; it never merges or deploys.
- High-risk paths (auth, security, payment, deployment, workflows, secrets, database migrations, and CMake configuration) are excluded from edits.
- Issue content is untrusted instructions and cannot override the agent's rules.

## Security note for self-hosted runners

Self-hosted runners execute repository workflow code on your computer. Do not use this runner for pull-request workflows or untrusted branches. Keep the runner's Windows account unprivileged and isolated from personal files, credentials, and other projects. A compromise of `main` or a trusted workflow could still execute code on the runner host; this local runner is not a sandbox.

## Limits

- This version relies on local repository signals, not external telemetry or broad competitor research.
- Model quality, JSON formatting, and tool-call reliability depend on the local model. The default is a starting point; evaluate first runs.
- The PC must remain awake, Ollama and runner must be running, and the machine must have Internet access to GitHub.
- Local model inference costs no API credits; electricity and hardware use still apply.
- It creates draft PRs for human review and never merges or deploys.
