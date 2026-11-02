#!/usr/bin/env python3
"""End-to-end server-authoritative vehicle spawning test.

Run after building GutsAndBolts:
    python3 tools/test_vehicle_spawning.py
    python3 tools/test_vehicle_spawning.py --build out/Release --players 3

The generated place contains a server Script, a LocalScript in StarterPlayerScripts,
a RemoteEvent, a ServerStorage car template, and a Workspace spawn marker. Every
client asks to spawn; the server clones and positions the car. The test checks the
server's spawn log and each player's acknowledgement.
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SERVER_SCRIPT = """
print("CAR_TEST_SERVER_START")
local Players = game:GetService("Players")
local ServerStorage = game:GetService("ServerStorage")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
print("CAR_TEST_SERVER_SERVICES_READY")
local templates = ServerStorage:FindFirstChild("CarTemplates")
local points = workspace:FindFirstChild("CarSpawnPoints")
local request = ReplicatedStorage:FindFirstChild("SpawnCar")
assert(templates, "ServerStorage.CarTemplates is missing")
assert(points, "Workspace.CarSpawnPoints is missing")
assert(request, "ReplicatedStorage.SpawnCar is missing")
print("CAR_TEST_SERVER_READY")
local active = {}
local last = {}

request.OnServerEvent:Connect(function(player)
    print("CAR_TEST_SERVER_REQUEST|" .. player.Name)
    local now = os.clock()
    if last[player.UserId] and now - last[player.UserId] < 5 then
        request:FireClient(player, false, "cooldown")
        return
    end
    local template = templates:FindFirstChild("BasicCar")
    local point = points:FindFirstChild("Spawn1")
    assert(template and point, "test fixture is missing the car or spawn point")
    local car = template:Clone()
    local chassis = car:FindFirstChild("Chassis")
    assert(chassis and chassis:IsA("BasePart"), "car has no Chassis BasePart")
    car.PrimaryPart = chassis
    if active[player.UserId] then active[player.UserId]:Destroy() end
    car.Name = player.Name .. "'s Car"
    car:SetAttribute("OwnerUserId", player.UserId)
    car.Parent = workspace
    car:PivotTo(point.CFrame * CFrame.new(0, 2, 0))
    active[player.UserId] = car
    last[player.UserId] = now
    local p = car.PrimaryPart.Position
    print(string.format("CAR_SPAWNED|%s|%.2f,%.2f,%.2f", player.Name, p.X, p.Y, p.Z))
    request:FireClient(player, true, car.Name)
end)

Players.PlayerRemoving:Connect(function(player)
    if active[player.UserId] then active[player.UserId]:Destroy() end
    active[player.UserId] = nil
    last[player.UserId] = nil
end)
"""

CLIENT_SCRIPT = """
print("CAR_TEST_CLIENT_START")
local Players = game:GetService("Players")
local replicated = game:GetService("ReplicatedStorage")
local request = replicated:FindFirstChild("SpawnCar")
assert(request, "client ReplicatedStorage.SpawnCar is missing")
local player = Players.LocalPlayer
print("CAR_TEST_CLIENT_READY|" .. tostring(player and player.Name))
request.OnClientEvent:Connect(function(ok, name)
    print("CAR_ACK|" .. player.Name .. "|" .. tostring(ok) .. "|" .. tostring(name))
end)
task.wait(1)
print("CAR_TEST_CLIENT_SEND|" .. player.Name)
request:FireServer()
"""

def make_place(path):
    next_id = [100]

    def node(name, kind, **extra):
        next_id[0] += 1
        value = {"id": next_id[0], "name": name, "kind": kind,
                 "pos": [0, 0, 0], "rot": [0, 0, 0], "size": [1, 1, 1]}
        value.update(extra)
        return value

    def part(name, pos, size, anchored=True):
        return node(name, "Part", pos=pos, size=size, shape="Cube",
                    color=[0.3, 0.5, 0.8], material="Plastic",
                    anchored=anchored, canCollide=True, transparency=0.0,
                    castShadow=True)

    chassis = part("Chassis", [0, 0, 0], [6, 2, 4], anchored=True)
    car = node("BasicCar", "Model", children=[chassis])
    storage = node("ServerStorage", "Model", children=[
        node("CarTemplates", "Model", children=[car])
    ])
    replicated = node("ReplicatedStorage", "Model", children=[
        node("SpawnCar", "Remote")
    ])
    spawn_points = node("CarSpawnPoints", "Model", children=[
        part("Spawn1", [20, 3, -10], [8, 1, 8], anchored=True)
    ])
    # The headless multiplayer harness starts scripts placed directly in the
    # generated Workspace. Keep the service containers above for the objects
    # they expose, but put the test drivers at the same level as the engine's
    # existing tools/test_multiplayer.py fixture.
    server_script = node("CarSpawner", "Script", source=SERVER_SCRIPT, enabled=True)
    client_script = node("CarSpawnerClient", "Script", source=CLIENT_SCRIPT,
                         enabled=True, local=True)
    world = {"id": 1, "name": "Workspace", "kind": "Model",
             "pos": [0, 0, 0], "rot": [0, 0, 0], "size": [1, 1, 1],
             "children": [
                 part("Baseplate", [0, -0.5, 0], [100, 1, 100]),
                 spawn_points, storage, replicated, server_script, client_script
             ]}
    doc = {"format": "GutsAndBolts", "version": 2, "info": {"title": "Vehicle Spawn Test"},
           "world": {}, "environment": {}, "workspace": world}
    path.write_text(json.dumps(doc, indent=2))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default=str(ROOT / "build"))
    parser.add_argument("--players", type=int, default=2)
    parser.add_argument("--seconds", type=float, default=18.0)
    opts = parser.parse_args()
    if opts.players < 2 or opts.players > 4:
        sys.exit("--players must be between 2 and 4")
    build_dir = Path(opts.build).resolve()
    studio = build_dir / "GutsAndBolts"
    if not studio.is_file():
        sys.exit(f"No Studio at {studio}; build the project first")
    work = Path(tempfile.mkdtemp(prefix="gb-vehicle-spawn-"))
    place = work / "Vehicle Spawn Test.gbscene"
    make_place(place)
    cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24",
           str(studio), "--open", str(place), "--test-multiplayer",
           f"{opts.players}:{opts.seconds}"]
    print("running:", " ".join(cmd))
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, cwd=build_dir, timeout=opts.seconds + 90)
    except subprocess.TimeoutExpired as exc:
        print(exc.stdout or "")
        sys.exit("FAIL: multiplayer vehicle test timed out")

    if proc.returncode != 0:
        print(proc.stdout)
        sys.exit(f"FAIL: Studio exited with status {proc.returncode}")

    results = {}
    for line in proc.stdout.splitlines():
        if line.startswith("MULTITEST "):
            _, label, data = line.split(" ", 2)
            results[label] = json.loads(data)
    lines = results.get("output", {}).get("output", [])
    for line in lines:
        print(f"  {line['time']} [{line['from']}] {line['level']}: {line['text']}")
    problems = []
    if not results.get("start", {}).get("success"):
        problems.append("multiplayer test did not start")
    names = [f"Player{i}" for i in range(1, opts.players + 2)]
    texts = [(line["from"], line["text"]) for line in lines]
    for name in names:
        spawned = [text for source, text in texts
                   if source == "Server" and text.startswith(f"CAR_SPAWNED|{name}|")]
        ack = (name, f"CAR_ACK|{name}|true|{name}'s Car")
        if not spawned:
            problems.append(f"server did not spawn a car for {name}")
        else:
            try:
                x, y, z = map(float, spawned[0].split("|", 2)[2].split(","))
                if abs(x - 20) > 0.05 or abs(y - 5) > 0.05 or abs(z + 10) > 0.05:
                    problems.append(f"{name}'s car was at the wrong position: {(x, y, z)}")
            except (ValueError, IndexError):
                problems.append(f"could not parse {name}'s spawn position")
        if ack not in texts:
            problems.append(f"{name} did not receive the server acknowledgement")
    if any(line["level"] == "error" for line in lines):
        problems.append("one or more scripts logged an error")
    stop = results.get("stop", {})
    if not stop.get("success") or stop.get("running"):
        problems.append("multiplayer_test stop did not stop the test: " + json.dumps(stop))
    if problems:
        print("\n".join("FAIL: " + problem for problem in problems))
        sys.exit(1)
    print(f"PASS: server spawned and positioned cars for {len(names)} players; each client received its acknowledgement")

if __name__ == "__main__":
    main()
