#!/usr/bin/env python3
"""Guarded task implementer. Opens a draft PR; does not merge or deploy."""
import json, os, subprocess, sys, urllib.request, pathlib, shlex

API="https://api.openai.com/v1/chat/completions"
MODEL=os.getenv("OPENAI_MODEL","gpt-4.1-mini")
TOKEN=os.environ["OPENAI_API_KEY"]
MAX_CALLS=int(os.getenv("AUTODEV_MAX_TOOL_CALLS","18"))
MAX_FILES=int(os.getenv("AUTODEV_MAX_FILES","8"))
ROOT=pathlib.Path.cwd().resolve()
CHANGED=set()
ALLOWED_ROOTS=("src","tests","docs","resources","games")
ALLOWED_ROOT_FILES=("README.md",)

def git(*a): return subprocess.check_output(["git",*a],text=True,stderr=subprocess.STDOUT)
def gh(*a): return subprocess.check_output(["gh",*a],text=True,stderr=subprocess.STDOUT)
def allowed_path(p):
    path=(ROOT/p).resolve()
    if not (path==ROOT or ROOT in path.parents): return False
    rel=path.relative_to(ROOT).as_posix()
    if rel in ALLOWED_ROOT_FILES: return True
    return rel.startswith(tuple(x+"/" for x in ALLOWED_ROOTS)) and not any(part in {".git",".github"} for part in path.parts) and path.suffix.lower() in {".cpp",".c",".h",".hpp",".md",".txt",".json",".yaml",".yml",".toml",".py",".cmake",".xml"}
def run_tool(name,args):
    if name=="read_file":
        p=args["path"]
        if not allowed_path(p): return "ERROR: path blocked or outside allowed project areas"
        try: return (ROOT/p).resolve().read_text(encoding="utf-8")[:20000]
        except Exception as e: return f"ERROR: {e}"
    if name=="list_files":
        return git("ls-files",*ALLOWED_ROOTS,*ALLOWED_ROOT_FILES)[:16000]
    if name=="write_file":
        p=args["path"]
        if not allowed_path(p): return "ERROR: path blocked or outside allowed project areas"
        if p not in CHANGED and len(CHANGED)>=MAX_FILES: return "ERROR: file-change limit reached"
        target=(ROOT/p).resolve()
        if ROOT not in target.parents: return "ERROR: symlink/path traversal blocked"
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(args["content"],encoding="utf-8"); CHANGED.add(target.relative_to(ROOT).as_posix())
        return f"Written {p} ({len(args['content'])} chars)"
    if name=="run_check":
        cmd=args["command"]
        checks={"cmake-configure":"cmake -S . -B build-autodev -DCMAKE_BUILD_TYPE=Debug",
                "cmake-build":"cmake --build build-autodev --parallel 2",
                "python-tests":"python -m pytest -q",
                "git-diff-check":"git diff --check"}
        if cmd not in checks: return "ERROR: unsupported validation command"
        try: return subprocess.check_output(shlex.split(checks[cmd]),text=True,stderr=subprocess.STDOUT,timeout=240)[:12000]
        except subprocess.CalledProcessError as e: return f"CHECK FAILED (exit {e.returncode):\n{e.output[-12000:]}"
        except Exception as e: return f"CHECK ERROR: {e}"
    return "ERROR: unknown tool"

TOOLS=[
{"type":"function","function":{"name":"list_files","description":"List tracked files in allowed project areas","parameters":{"type":"object","properties":{},"required":[]}}},
{"type":"function","function":{"name":"read_file","description":"Read a UTF-8 file in allowed project areas","parameters":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"]}}},
{"type":"function","function":{"name":"write_file","description":"Create or replace a UTF-8 text file only within src, tests, docs, resources, games, or README.md; excludes sensitive/control areas","parameters":{"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}},"required":["path","content"]}}},
{"type":"function","function":{"name":"run_check","description":"Run one approved check: cmake-configure, cmake-build, python-tests, or git-diff-check","parameters":{"type":"object","properties":{"command":{"type":"string","enum":["cmake-configure","cmake-build","python-tests","git-diff-check"]}},"required":["command"]}}]

def call(messages):
    req=urllib.request.Request(API,data=json.dumps({"model":MODEL,"messages":messages,"tools":TOOLS,"tool_choice":"auto"}).encode(),headers={"Authorization":"Bearer "+TOKEN,"Content-Type":"application/json"})
    with urllib.request.urlopen(req,timeout=120) as r: return json.loads(r.read())["choices"][0]["message"]

def main():
    if len(sys.argv)!=2 or not sys.argv[1].isdigit():
        raise SystemExit("Usage: builder.py ISSUE_NUMBER")
    issue_num=sys.argv[1]
    task=json.loads(gh("issue","view",issue_num,"--json","title,body,labels"))
    labels=[x["name"] for x in task.get("labels",[])]
    if "autonomous-ready" not in labels: raise SystemExit("Issue lacks autonomous-ready label; refusing.")
    branch=f"autodev/issue-{issue_num}"
    git("checkout","-b",branch)
    system=f"""You are an implementation agent for Guts & Bolts. Implement ONLY this issue: #{issue_num} {task['title']}\n{task['body']}\nRules: inspect before editing; keep scope tight; never touch secrets, credentials, build artifacts, deployment, auth/security, payment, data migrations, workflows, CMake build configuration, or files outside allowed areas. You can only write files in src/, tests/, docs/, resources/, games/, or README.md, with text extensions. Limit changed files to {MAX_FILES}. Use approved validation commands only. Run git-diff-check before finishing. Do not claim tests passed unless run. Finish with concise summary and checks. If unable, explain blocker and avoid speculative changes."""
    messages=[{"role":"system","content":system},{"role":"user","content":"Inspect the allowed repository files, then implement this issue. Run suitable approved validation checks before reporting completion."}]
    for i in range(MAX_CALLS):
        msg=call(messages); messages.append(msg)
        if msg.get("tool_calls"):
            for tc in msg["tool_calls"]:
                fn=tc["function"]["name"]; args=json.loads(tc["function"]["arguments"])
                result=run_tool(fn,args)
                messages.append({"role":"tool","tool_call_id":tc["id"],"content":str(result)})
            continue
        if msg.get("content"): print(msg["content"])
        break
    else: print("Stopped: maximum model/tool loop reached.")
    if not CHANGED: raise SystemExit("No repository changes made; draft PR not created.")
    check=run_tool("run_check",{"command":"git-diff-check"})
    if "CHECK FAILED" in check or "CHECK ERROR" in check: raise SystemExit("Diff validation failed: "+check)
    git("add",*sorted(CHANGED))
    git("commit","-m",f"Autonomous implementation for issue #{issue_num}")
    diff=git("show","--stat","--oneline","HEAD")
    gh("push","--set-upstream","origin",branch)
    gh("pr","create","--draft","--title",f"[Autonomous] {task['title']}","--body",f"Implements issue #{issue_num}.\n\nGenerated by guarded G&B autonomous-dev Builder.\n\nChanged files: {', '.join(sorted(CHANGED))}\n\nCommit summary:\n{diff}\n\nReview and run CI before merge.","--base","main","--head",branch)
    print("Draft PR opened. This agent does not merge or deploy.")
if __name__=="__main__": main()
