#!/usr/bin/env python3
"""Checks Studio's "Test with N players" with no screen (Linux, needs xvfb-run).
Run from the repository root after building:

    python3 tools/test_multiplayer.py            (build/ is where the programs are)
    python3 tools/test_multiplayer.py --build out/Release --players 2 --seconds 30

It makes a small game: a server Script that prints when each player joins and
leaves and answers them through a RemoteEvent, and a LocalScript that says hi
to the server and prints what comes back. Then Studio is started with
--test-multiplayer, which drives the multiplayer_test AI tool: start with N
players, add one more halfway (joining late), print the test's output and stop.
Halfway through the second half this script closes Player2's window (leaving).

Passes if every player joined, every player's LocalScript got the server's
answer, Player2's leaving was seen by the server, and nothing is left running.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SERVER_SCRIPT = """
local hello = game.ReplicatedStorage:WaitForChild("Hello")
game.Players.PlayerAdded:Connect(function(player)
    print("SERVER: " .. player.Name .. " joined")
    hello:FireAllClients("everyone: " .. player.Name .. " is here")
end)
game.Players.PlayerRemoving:Connect(function(player)
    print("SERVER: " .. player.Name .. " left")
end)
hello.OnServerEvent:Connect(function(player, message)
    print("SERVER got from " .. player.Name .. ": " .. tostring(message))
    hello:FireClient(player, "welcome " .. player.Name)
end)
"""

LOCAL_SCRIPT = """
local hello = game.ReplicatedStorage:WaitForChild("Hello")
local me = game.Players.LocalPlayer
hello.OnClientEvent:Connect(function(message)
    print("CLIENT " .. me.Name .. " got: " .. tostring(message))
end)
hello:FireServer("hi from " .. me.Name)
"""


def make_place(path):
    next_id = [100]

    def node(name, kind, **extra):
        next_id[0] += 1
        n = {"id": next_id[0], "name": name, "kind": kind, "pos": [0, 0, 0], "rot": [0, 0, 0], "size": [1, 1, 1]}
        n.update(extra)
        return n

    def part(name, pos, size, color):
        return node(name, "Part", pos=pos, size=size, shape="Cube", color=color, material="Plastic",
                    anchored=True, canCollide=True, transparency=0.0, castShadow=True)

    children = [
        part("Baseplate", [0, -0.5, 0], [60, 1, 60], [0.35, 0.6, 0.35]),
        part("SpawnLocation", [0, 0.1, 0], [3, 0.2, 3], [0.25, 0.6, 1.0]),
        node("ReplicatedStorage", "Model", children=[node("Hello", "Remote")]),
        node("JoinScript", "Script", source=SERVER_SCRIPT, enabled=True),
        node("HelloClient", "Script", source=LOCAL_SCRIPT, enabled=True, local=True),
    ]
    doc = {"format": "GutsAndBolts", "version": 2,
           "info": {"title": "Multiplayer Test"}, "world": {}, "environment": {},
           "workspace": {"id": 1, "name": "Workspace", "kind": "Model", "pos": [0, 0, 0], "rot": [0, 0, 0],
                         "size": [1, 1, 1], "children": children}}
    path.write_text(json.dumps(doc, indent=2))


def test_processes():
    """Server and player programs a multiplayer test started (Linux /proc)."""
    found = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            cmd = (Path("/proc") / pid / "cmdline").read_bytes().split(b"\0")
        except OSError:
            continue
        args = [a.decode(errors="replace") for a in cmd if a]
        if "--studio-test" in args or ("--place" in args and any("gutsandbolts-multiplayer-test" in a for a in args)):
            found.append((int(pid), args))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=str(ROOT / "build"))
    ap.add_argument("--players", type=int, default=2)
    ap.add_argument("--seconds", type=float, default=30.0)
    opts = ap.parse_args()
    studio = Path(opts.build) / "GutsAndBolts"
    if not studio.exists():
        sys.exit(f"No Studio at {studio} (build first)")

    work = Path(tempfile.mkdtemp(prefix="gb-multitest-"))
    place = work / "Multiplayer Test.gbscene"
    make_place(place)
    cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24", str(studio), "--open", str(place),
           "--test-multiplayer", f"{opts.players}:{opts.seconds}"]
    print("running:", " ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=opts.build)

    # Player2 leaves three quarters of the way through.
    closed = []

    def close_player2():
        time.sleep(opts.seconds * 0.75 + 2)
        for pid, args in test_processes():
            if "Player2" in args:
                os.kill(pid, 15)
                closed.append(pid)

    threading.Thread(target=close_player2, daemon=True).start()
    try:
        out, _ = proc.communicate(timeout=opts.seconds + 90)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate()
        print(out)
        sys.exit("FAIL: Studio didn't finish")

    results = {}
    for line in out.splitlines():
        if line.startswith("MULTITEST "):
            _, label, data = line.split(" ", 2)
            results[label] = json.loads(data)
    lines = results.get("output", {}).get("output", [])
    for l in lines:
        print(f"  {l['time']} [{l['from']}] {l['level']}: {l['text']}")

    total = opts.players + 1   # (one more joined halfway)
    names = [f"Player{i}" for i in range(1, total + 1)]
    texts = [(l["from"], l["text"]) for l in lines]
    problems = []
    if not results.get("start", {}).get("success"):
        problems.append("multiplayer_test start failed: " + json.dumps(results.get("start")))
    for n in names:
        if ("Server", f"SERVER: {n} joined") not in texts:
            problems.append(f"the server's Script didn't see {n} join")
        if ("Server", f"SERVER got from {n}: hi from {n}") not in texts:
            problems.append(f"the server didn't get {n}'s RemoteEvent")
        if (n, f"CLIENT {n} got: welcome {n}") not in texts:
            problems.append(f"{n}'s LocalScript didn't get the server's answer")
    if not closed:
        problems.append("couldn't find Player2's window to close")
    elif ("Server", "SERVER: Player2 left") not in texts:
        problems.append("the server's Script didn't see Player2 leave")
    stop = results.get("stop", {})
    if not stop.get("success") or stop.get("running"):
        problems.append("multiplayer_test stop didn't stop it: " + json.dumps(stop))
    time.sleep(1)
    left = test_processes()
    if left:
        problems.append(f"still running after Studio closed: {left}")

    if problems:
        print("\n".join("FAIL: " + p for p in problems))
        sys.exit(1)
    print(f"PASS: {total} players joined (one late), RemoteEvents went both ways, Player2 left, everything closed.")


if __name__ == "__main__":
    main()
