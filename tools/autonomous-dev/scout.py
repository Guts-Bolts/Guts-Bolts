#!/usr/bin/env python3
"""Scout: turn repository signals into a small, deduplicated GitHub issue backlog."""
import json, os, re, subprocess, urllib.request

API = "https://api.openai.com/v1/chat/completions"
MODEL = os.getenv("OPENAI_MODEL", "gpt-4.1-mini")
TOKEN = os.environ["OPENAI_API_KEY"]

def gh(*args):
    return subprocess.check_output(["gh", *args], text=True, stderr=subprocess.STDOUT)

def api(messages):
    req = urllib.request.Request(API, data=json.dumps({
        "model": MODEL, "messages": messages, "temperature": 0.3,
        "response_format": {"type":"json_object"}
    }).encode(), headers={"Authorization":"Bearer "+TOKEN,"Content-Type":"application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())["choices"][0]["message"]["content"]

def safe_run(cmd):
    try: return subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT, timeout=20)[:12000]
    except Exception as e: return f"(signal unavailable: {e})"

def main():
    readme = safe_run(["git","show","HEAD:README.md"])
    markers = safe_run(["git","grep","-n","-E","TODO|FIXME|XXX","--","src","tests","tools"])
    issues_text = gh("issue","list","--state","open","--limit","100","--json","title,body,labels")
    issues = json.loads(issues_text)
    existing_titles = {str(x.get("title","")).lower().strip() for x in issues}
    prompt = f"""You are the Guts & Bolts product research agent. Use only repository signals supplied below.
Generate up to 3 distinct, realistic, small engineering/product tasks for this game creation engine.
Prefer concrete fixes, developer experience, tests, docs, performance wins, or useful features supported by evidence.
Do not invent existing bugs. Do not suggest large rewrites, security-sensitive changes, payments, auth, database migrations, or production deployment.
Each task must be self-contained, testable, and suitable for one focused PR.
Return JSON: {{\"ideas\":[{{\"title\":string,\"body\":string,\"priority\":\"low|medium|high\",\"evidence\":string}}]}}
README excerpt:
{readme[:10000]}
TODO/FIXME scan:
{markers}
Existing open issues (avoid duplicates):
{json.dumps(issues)[:12000]}"""
    data = json.loads(api([{"role":"system","content":"You are a cautious product scout. Output valid JSON only."},{"role":"user","content":prompt}]))
    created = 0
    for idea in data.get("ideas", [])[:3]:
        title = re.sub(r"\s+", " ", str(idea.get("title","")).strip())[:100]
        body = str(idea.get("body","")).strip()
        evidence = str(idea.get("evidence","")).strip()
        if not title or not body or title.lower() in existing_titles: continue
        full_body = body + "\n\n**Scout evidence:** " + (evidence or "No specific evidence supplied.") + "\n\n**Scout priority:** " + str(idea.get("priority","medium"))
        gh("issue","create","--title",title,"--body",full_body,"--label","autonomous-ready","--label","ai-generated")
        existing_titles.add(title.lower()); created += 1
    print(f"Scout completed: {created} new idea issue(s).")
if __name__ == "__main__": main()
