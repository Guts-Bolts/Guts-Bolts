#!/usr/bin/env python3
"""Scout: find, score, deduplicate, and queue a small number of G&B tasks."""
import json
import os
import re
import subprocess
import time
import urllib.error
import urllib.request

API = os.getenv("OLLAMA_URL", "http://127.0.0.1:11434/api/chat").rstrip("/")
MODEL = os.getenv("OLLAMA_MODEL") or "qwen3:8b"
MINIMUM_SCORE = 55
MAX_RETRIES = 2


def gh(*args):
    return subprocess.check_output(["gh", *args], text=True, stderr=subprocess.STDOUT)


def ask_model(messages):
    payload = json.dumps({
        "model": MODEL,
        "messages": messages,
        "stream": False,
        "format": "json",
        "options": {"temperature": 0.3},
    }).encode()
    request = urllib.request.Request(
        API,
        data=payload,
        headers={"Content-Type": "application/json"},
    )

    for attempt in range(MAX_RETRIES + 1):
        try:
            with urllib.request.urlopen(request, timeout=300) as response:
                result = json.loads(response.read())
                return result["message"]["content"]
        except urllib.error.HTTPError as error:
            details = error.read().decode("utf-8", errors="replace")
            print(f"Ollama returned HTTP {error.code}: {details[:2000]}")
            if error.code in (429, 503) and attempt < MAX_RETRIES:
                delay = 5 * (attempt + 1)
                print(f"Retrying local Ollama request in {delay}s ({attempt + 1}/{MAX_RETRIES}).")
                time.sleep(delay)
                continue
            raise RuntimeError(f"Ollama request failed (HTTP {error.code}).") from error
        except (urllib.error.URLError, TimeoutError) as error:
            print(f"Cannot reach local Ollama at {API}: {error}")
            raise RuntimeError("Ensure Ollama is running and the configured model is available.") from error

    raise RuntimeError("Ollama request failed after bounded retries.")


def safe_run(command):
    try:
        return subprocess.check_output(
            command, text=True, stderr=subprocess.STDOUT, timeout=20
        )[:12000]
    except Exception as error:
        return f"(signal unavailable: {error})"


def score_idea(idea):
    """Weighted impact score with explicit effort and risk penalties."""
    dimensions = ("impact", "strategic", "confidence", "urgency", "leverage")
    values = {}
    for dimension in dimensions:
        try:
            values[dimension] = max(1, min(5, int(idea.get("scores", {}).get(dimension, 3))))
        except (TypeError, ValueError):
            values[dimension] = 3
    try:
        effort = max(1, min(5, int(idea.get("effort", 3))))
    except (TypeError, ValueError):
        effort = 3
    try:
        risk = max(1, min(5, int(idea.get("risk", 2))))
    except (TypeError, ValueError):
        risk = 2

    benefit = (
        values["impact"] * 0.25
        + values["strategic"] * 0.25
        + values["confidence"] * 0.20
        + values["urgency"] * 0.15
        + values["leverage"] * 0.15
    ) * 20
    final_score = max(0, min(100, round(benefit - (effort - 1) * 4 - (risk - 1) * 5)))
    return final_score, values, effort, risk


def main():
    readme = safe_run(["git", "show", "HEAD:README.md"])
    markers = safe_run(["git", "grep", "-n", "-E", "TODO|FIXME|XXX", "--", "src", "tests", "tools"])
    existing = json.loads(gh(
        "issue", "list", "--state", "open", "--limit", "100",
        "--json", "number,title,body,labels",
    ))
    existing_titles = {str(issue.get("title", "")).lower().strip() for issue in existing}

    prompt = f"""You are the Guts & Bolts product and engineering research agent.
Use only the supplied repository signals. Propose up to 3 distinct, evidence-backed,
small improvements suitable for one focused PR. Prefer bug fixes, tests, documentation,
developer experience, maintainability, and measurable performance improvements.
Do not invent bugs or capabilities. Do not propose large rewrites, auth/security,
payments, database migrations, deployment, secrets, or changes to production data.
Avoid anything already represented in the open issues below.
For each idea provide: title, body with acceptance criteria, evidence, priority,
scores (impact, strategic, confidence, urgency, leverage each integer 1-5),
effort and risk (integer 1-5; 5 is highest effort/risk).
Return JSON only:
{{"ideas":[{{"title":"...","body":"...","evidence":"...","priority":"low|medium|high",
"scores":{{"impact":3,"strategic":3,"confidence":3,"urgency":3,"leverage":3}},
"effort":3,"risk":2}}]}}
README excerpt:
{readme[:10000]}
TODO/FIXME scan:
{markers}
Open issues to avoid duplicating:
{json.dumps(existing)[:12000]}"""

    response = json.loads(ask_model([
        {"role": "system", "content": "You are a cautious product scout. Return valid JSON only."},
        {"role": "user", "content": prompt},
    ]))

    candidates = []
    for idea in response.get("ideas", [])[:3]:
        title = re.sub(r"\s+", " ", str(idea.get("title", "")).strip())[:100]
        body = str(idea.get("body", "")).strip()
        if not title or not body or title.lower() in existing_titles:
            continue
        score, dimensions, effort, risk = score_idea(idea)
        if score < MINIMUM_SCORE:
            print(f"Rejected low-scoring idea ({score}): {title}")
            continue
        candidates.append((score, title, body, str(idea.get("evidence", "")).strip(),
                           str(idea.get("priority", "medium")), dimensions, effort, risk))

    candidates.sort(key=lambda item: item[0], reverse=True)
    created = []
    for score, title, body, evidence, priority, dimensions, effort, risk in candidates:
        details = (
            body
            + "\n\n**Scout evidence:** " + (evidence or "No specific evidence supplied.")
            + f"\n\n**Autonomous priority score:** {score}/100"
            + f"\n**Priority:** {priority}"
            + f"\n**Effort estimate:** {effort}/5"
            + f"\n**Risk estimate:** {risk}/5"
            + f"\n**Scoring dimensions:** {json.dumps(dimensions, sort_keys=True)}"
        )
        url = gh(
            "issue", "create", "--title", title, "--body", details,
            "--label", "autonomous-ready", "--label", "ai-generated",
        ).strip()
        match = re.search(r"/issues/(\d+)$", url)
        if not match:
            raise RuntimeError(f"Could not parse created issue URL: {url}")
        created.append((score, int(match.group(1)), title))
        print(f"Created issue #{match.group(1)} (score {score}): {title}")

    selected = created[0] if created else None
    issue_number = str(selected[1]) if selected else ""
    output_path = os.getenv("GITHUB_OUTPUT")
    if output_path:
        with open(output_path, "a", encoding="utf-8") as output:
            output.write(f"issue_number={issue_number}\n")
    print(f"Scout finished. Created {len(created)} issue(s); selected issue: {issue_number or 'none'}.")


if __name__ == "__main__":
    main()
