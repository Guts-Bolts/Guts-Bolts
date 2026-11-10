#!/usr/bin/env python3
"""Builds the sample games in games/ (run from the repository root):
    python3 tools/make_sample_games.py              every game
    python3 tools/make_sample_games.py waterslide   just one (obby, demolition, plaza, waterslide)

The scripts inside are taken from the editor's ready-made objects
(src/editor/Premades.cpp), so the samples always match the Toolbox.
"""
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PREMADES = (ROOT / "src/editor/Premades.cpp").read_text()


def premade_script(name):
    return re.search(r"const char\* " + name + r' = R"\((.*?)\)";', PREMADES, re.S).group(1)


class Builder:
    def __init__(self):
        self.next_id = 100

    def node(self, name, kind="Part", shape="Cube", pos=(0, 0, 0), size=(1, 1, 1), rot=(0, 0, 0),
             color=(0.64, 0.64, 0.66), material="Plastic", anchored=True, collide=True,
             transparency=0.0, children=(), **extra):
        self.next_id += 1
        n = {"id": self.next_id, "name": name, "kind": kind, "pos": list(pos), "rot": list(rot),
             "size": list(size)}
        if kind == "Part":
            n.update(shape=shape, color=list(color), material=material, anchored=anchored,
                     canCollide=collide, transparency=transparency, castShadow=True)
        n.update(extra)
        if children:
            n["children"] = list(children)
        return n

    def script(self, source, name="Script"):
        return self.node(name, kind="Script", source=source, enabled=True)

    def light(self, color=(1, 0.85, 0.6), brightness=3, rng=16, kind="Point", angle=60, pos=(0, 0, 0), rot=(0, 0, 0)):
        return self.node("SpotLight" if kind == "Spot" else "PointLight", kind="Light", pos=pos, rot=rot,
                         lightType=kind, color=list(color), brightness=brightness, range=rng,
                         spotAngle=angle, enabled=True)


def save(path, info, world, env, children):
    doc = {
        "format": "GutsAndBolts", "version": 2,
        "info": info, "world": world, "environment": env,
        "workspace": {"id": 1, "name": "Workspace", "kind": "Model", "pos": [0, 0, 0], "rot": [0, 0, 0],
                      "size": [1, 1, 1], "children": children},
    }
    path.write_text(json.dumps(doc, indent=2))
    print("wrote", path.relative_to(ROOT))


GORE_WORLD = {"deathStyle": 1, "gore": 2, "dismemberment": True, "fallDamage": True, "fallDamageSpeed": 20}
CLASSIC_WORLD = {"deathStyle": 0, "gore": 0, "dismemberment": False, "fallDamage": False}

# ---------------------------------------------------------------------------
# 1. Obby of Doom
# ---------------------------------------------------------------------------

def obby():
    b = Builder()
    kids = []
    kill = premade_script("kKillBrick")
    kids.append(b.node("Lava", pos=(0, -0.5, -40), size=(160, 1, 160), color=(1.0, 0.3, 0.05),
                       material="Neon", children=[b.script(kill)]))
    kids.append(b.node("StartPlatform", pos=(0, 3.5, 0), size=(10, 1, 10), color=(0.5, 0.52, 0.56), material="Concrete"))
    kids.append(b.node("SpawnLocation", pos=(0, 4.1, 0), size=(3, 0.2, 3), color=(0.3, 0.8, 0.4), material="Neon"))

    grey = (0.75, 0.77, 0.8)
    for i, z in enumerate((-8, -13, -18)):
        kids.append(b.node(f"Step{i + 1}", pos=(0, 3.5, z), size=(3, 1, 3), color=grey))
        kids.append(b.node("Coin", shape="Cylinder", pos=(0, 5.2, z), size=(0.9, 0.12, 0.9), rot=(90, 0, 0),
                           color=(1, 0.8, 0.15), material="Metal", collide=False,
                           children=[b.script(premade_script("kCoin"))]))

    kids.append(b.node("MovingPlatform", pos=(0, 3.5, -25), size=(4, 0.6, 4), color=(0.6, 0.45, 0.95),
                       children=[b.script(premade_script("kMoving"))]))
    for i, z in enumerate((-31, -36)):
        kids.append(b.node(f"FadingPlatform{i + 1}", pos=(0, 3.5, z), size=(3.5, 0.6, 3.5), color=(0.3, 0.75, 0.95),
                           children=[b.script(premade_script("kDisappearing"))]))
    kids.append(b.node("PadPlatform", pos=(0, 3.5, -42), size=(5, 1, 5), color=grey))
    kids.append(b.node("JumpPad", shape="Cylinder", pos=(0, 4.1, -42), size=(2.5, 0.2, 2.5), color=(0.2, 0.9, 0.3),
                       material="Neon", children=[b.script(premade_script("kJumpPad"))]))

    # High walkway with saw blades to dodge.
    kids.append(b.node("SawWalk", pos=(0, 9.5, -60), size=(5, 1, 22), color=(0.35, 0.36, 0.4), material="Metal"))
    for i, (x, z) in enumerate(((-1.2, -54), (1.2, -60), (-1.2, -66))):
        kids.append(b.node(f"SawBlade{i + 1}", shape="Cylinder", pos=(x, 11.4, z), size=(2.6, 0.12, 2.6),
                           rot=(90, 0, 0), color=(0.8, 0.82, 0.85), material="Metal",
                           children=[b.script(premade_script("kSawBlade"))]))
    kids.append(b.node("Coin", shape="Cylinder", pos=(1.5, 11.2, -57), size=(0.9, 0.12, 0.9), rot=(90, 0, 0),
                       color=(1, 0.8, 0.15), material="Metal", collide=False,
                       children=[b.script(premade_script("kCoin"))]))

    finish_src = '''-- Finish line: touch it to win!
local finish = script.Parent
local won = false

finish.Touched:Connect(function(hit)
    if won or not hit.Parent:FindFirstChild("Humanoid") then return end
    won = true
    Gui.Message("YOU WIN! Time: " .. string.format("%.1f", time()) .. "s", 6)
    Sounds.Play("win")
    for i = 1, 6 do
        Effects.Sparks(finish.Position + Vector3.new(math.random(-3, 3), 1, math.random(-3, 3)), 40)
        wait(0.2)
    end
end)
'''
    kids.append(b.node("Finish", pos=(0, 9.5, -76), size=(8, 1, 8), color=(1.0, 0.8, 0.1), material="Neon",
                       children=[b.script(finish_src)]))
    kids.append(b.node("FinishLight", kind="Light", pos=(0, 13, -76), lightType="Point", color=[1, 0.85, 0.3],
                       brightness=5, range=18, enabled=True))

    timer_src = '''-- Shows a timer while you play.
Gui.Message("Reach the golden platform! Don't touch the lava.", 4)
game:GetService("RunService").Heartbeat:Connect(function()
    Gui.Label("Timer", "Time: " .. string.format("%.1f", time()))
end)
'''
    kids.append(b.script(timer_src, "GameScript"))

    env = {"clockTime": 17.5, "sunAzimuth": 150, "sunElevation": 18, "sunColor": [1, 0.72, 0.45],
           "sunIntensity": 1.8, "skyZenith": [0.2, 0.28, 0.55], "skyHorizon": [0.95, 0.62, 0.4],
           "fogColor": [0.9, 0.6, 0.45], "fogDensity": 0.008, "bloomIntensity": 0.8, "cloudCover": 0.5}
    save(ROOT / "games/Obby of Doom.gbscene",
         {"title": "Obby of Doom", "author": "Guts and Bolts",
          "description": "Jump, dodge the saw blades and don't touch the lava. How fast can you reach the golden platform?"},
         GORE_WORLD, env, kids)

# ---------------------------------------------------------------------------
# 2. Demolition Yard
# ---------------------------------------------------------------------------

def demolition():
    b = Builder()
    kids = [b.node("Baseplate", pos=(0, -0.5, 0), size=(80, 1, 80), color=(0.45, 0.43, 0.4), material="Concrete"),
            b.node("SpawnLocation", pos=(0, 0.1, 12), size=(3, 0.2, 3), color=(0.55, 0.57, 0.6))]
    wood = (0.62, 0.42, 0.22)
    # Crate towers.
    for tx, tz in ((-8, -6), (6, -10), (-3, -16)):
        for y in range(4):
            for dx in (0, 1.05):
                kids.append(b.node("Crate", pos=(tx + dx, 0.5 + y * 1.02, tz), size=(1, 1, 1), color=wood,
                                   material="Wood", anchored=False))
    for x, z in ((-4, -4), (3, -4), (8, -2), (-10, -12), (1, -12), (10, -14)):
        kids.append(b.node("ExplodingBarrel", shape="Cylinder", pos=(x, 0.75, z), size=(1, 1.5, 1),
                           color=(0.8, 0.12, 0.08), material="Metal", anchored=False,
                           children=[b.script(premade_script("kBarrel"))]))
    for x, z in ((0, 4), (-6, 2), (5, 2), (-2, -8), (4, -7)):
        kids.append(b.node("Landmine", shape="Cylinder", pos=(x, 0.05, z), size=(0.9, 0.1, 0.9),
                           color=(0.3, 0.05, 0.05), material="Neon", collide=False,
                           children=[b.script(premade_script("kLandmine"))]))
    kids.append(b.node("JumpPad", shape="Cylinder", pos=(8, 0.1, 8), size=(2.5, 0.2, 2.5), color=(0.2, 0.9, 0.3),
                       material="Neon", children=[b.script(premade_script("kJumpPad").replace("launchSpeed = 42", "launchSpeed = 72"))]))
    kids.append(b.node("Tower", pos=(-14, 6, 6), size=(4, 12, 4), color=(0.4, 0.4, 0.45), material="Concrete"))
    kids.append(b.node("TowerStairs1", pos=(-11.5, 0.5, 6), size=(1, 1, 2), color=(0.4, 0.4, 0.45)))

    nuke_src = '''-- The big red button. Click it...
local button = script.Parent
local used = false

button.Clicked:Connect(function()
    if used then return end
    used = true
    Gui.Message("NUKE INCOMING!", 3)
    for i = 3, 1, -1 do
        Gui.Label("Countdown", "Boom in " .. i .. "...")
        wait(1)
    end
    Gui.Label("Countdown", nil)
    for i = 1, 12 do
        Explode(Vector3.new(math.random(-15, 15), 1, math.random(-18, 8)), 7)
        wait(0.15)
    end
    wait(5)
    used = false
end)
'''
    kids.append(b.node("NukeButton", pos=(0, 0.5, 18), size=(1.5, 1, 1.5), color=(0.9, 0.1, 0.1), material="Neon",
                       children=[b.script(nuke_src)]))
    kids.append(b.script('Gui.Message("Welcome to the Demolition Yard. Click the red button if you dare.", 5)', "Welcome"))
    save(ROOT / "games/Demolition Yard.gbscene",
         {"title": "Demolition Yard", "author": "Guts and Bolts",
          "description": "Exploding barrels, landmines and a very big red button. Ragdoll physics and gore turned all the way up."},
         GORE_WORLD, {"clockTime": 13, "sunAzimuth": 150, "sunElevation": 50, "cloudCover": 0.6}, kids)

# ---------------------------------------------------------------------------
# 3. Night Plaza
# ---------------------------------------------------------------------------

def plaza():
    b = Builder()
    kids = [b.node("Baseplate", pos=(0, -0.5, 0), size=(80, 1, 80), color=(0.28, 0.3, 0.33), material="Concrete"),
            b.node("SpawnLocation", pos=(0, 0.1, 10), size=(3, 0.2, 3), color=(0.55, 0.57, 0.6))]
    # Lamp posts around the plaza.
    for x, z in ((-8, -8), (8, -8), (-8, 8), (8, 8), (0, -14)):
        bulb = b.node("Bulb", shape="Sphere", pos=(0, 0.56, 0), size=(2.4, 0.15, 2.4), color=(1, 0.85, 0.55),
                      material="Neon", children=[b.light(pos=(0, -1, 0))])
        bulb["castShadow"] = False
        kids.append(b.node("LampPost", shape="Cylinder", pos=(x, 2, z), size=(0.25, 4, 0.25), color=(0.15, 0.15, 0.17),
                           material="Metal", children=[bulb]))
    # Disco floor in the middle.
    tiles = [b.node("Tile", pos=(x * 2 - 3, 0.05, z * 2 - 3), size=(1.9, 0.1, 1.9), color=(0.5, 0.2, 0.9), material="Neon")
             for x in range(4) for z in range(4)]
    tiles.append(b.light(color=(1, 0.3, 0.8), brightness=4, rng=20, pos=(0, 5, 0)))
    tiles[-1]["name"] = "PartyLight"
    tiles.append(b.script(premade_script("kDisco")))
    kids.append(b.node("DiscoFloor", kind="Model", children=tiles))
    # A glass shop with a spotlight.
    kids.append(b.node("ShopBack", pos=(0, 2, -20), size=(10, 4, 0.5), color=(0.7, 0.3, 0.3), material="Wood"))
    kids.append(b.node("ShopWindow", pos=(0, 2, -16), size=(10, 4, 0.2), color=(0.6, 0.8, 1.0), material="Glass",
                       transparency=0.7))
    kids.append(b.node("ShopRoof", pos=(0, 4.25, -18), size=(10.5, 0.5, 4.5), color=(0.2, 0.2, 0.22)))
    kids.append(b.light(color=(0.6, 0.8, 1.0), brightness=6, rng=14, kind="Spot", angle=70, pos=(0, 3.8, -18)))
    kids.append(b.node("Sign", pos=(0, 5, -18), size=(6, 1, 0.3), color=(0.2, 1.0, 0.8), material="Neon"))
    for x, z in ((-5, 0), (5, 0), (0, -9)):
        kids.append(b.node("Coin", shape="Cylinder", pos=(x, 1.2, z), size=(0.9, 0.12, 0.9), rot=(90, 0, 0),
                           color=(1, 0.8, 0.15), material="Metal", collide=False,
                           children=[b.script(premade_script("kCoin"))]))
    env = {"clockTime": 22, "sunAzimuth": 205, "sunElevation": 35, "sunColor": [0.55, 0.65, 0.95], "sunIntensity": 0.45,
           "sunSize": 0.7, "ambientColor": [0.16, 0.21, 0.38], "groundAmbient": [0.05, 0.05, 0.08],
           "ambientIntensity": 0.35, "reflections": 0.6, "skyZenith": [0.01, 0.015, 0.05],
           "skyHorizon": [0.05, 0.08, 0.17], "skyGround": [0.02, 0.02, 0.04], "cloudCover": 0.3,
           "cloudColor": [0.35, 0.4, 0.55], "fogColor": [0.05, 0.08, 0.16], "fogDensity": 0.02,
           "exposure": 1.25, "bloomIntensity": 0.9, "vignette": 0.4}
    save(ROOT / "games/Night Plaza.gbscene",
         {"title": "Night Plaza", "author": "Guts and Bolts",
          "description": "A chill night out: street lamps, a disco floor and a few hidden coins."},
         CLASSIC_WORLD, env, kids)



# ---------------------------------------------------------------------------
# 4. Mega Water Slide
# ---------------------------------------------------------------------------

def vadd(a, b): return tuple(x + y for x, y in zip(a, b))
def vsub(a, b): return tuple(x - y for x, y in zip(a, b))
def vmul(a, k): return tuple(x * k for x in a)
def vlen(a): return math.sqrt(sum(x * x for x in a))


def flow_attr(v):
    return {"n": "Flow", "t": 3, "v": [round(x, 3) for x in v]}


def water(b, name, pos, size, color=(0.2, 0.55, 0.85), transparency=0.35, rot=(0, 0, 0), attrs=None):
    n = b.node(name, pos=pos, size=size, rot=rot, color=color, material="Glass", collide=False,
               transparency=transparency)
    n["castShadow"] = False
    if attrs:
        n["attrs"] = attrs
    return n


def flume(b, a, c, color, water_speed):
    """One straight piece of slide from a to c (points on the middle of its floor):
    a floor and two walls. The water in it is real liquid poured in at the top."""
    d = vsub(c, a)
    length = vlen(d)
    horiz = math.hypot(d[0], d[2])
    yaw = math.atan2(d[0], d[2])
    pitch = math.atan2(-d[1], horiz)          # positive = going down
    sy, cy, sp, cp = math.sin(yaw), math.cos(yaw), math.sin(pitch), math.cos(pitch)
    fwd = (sy * cp, -sp, cy * cp)
    up = (sy * sp, cp, cy * sp)
    right = (cy, 0.0, -sy)
    rot = (math.degrees(pitch), math.degrees(yaw), 0)
    mid = vmul(vadd(a, c), 0.5)
    over = 1.6   # pieces overlap so the bends have no gaps for water to leak through
    parts = [b.node("SlideFloor", pos=vadd(mid, vmul(up, -0.3)), size=(5.4, 0.6, length + over), rot=rot,
                    color=(0.95, 0.96, 0.98), material="Plastic", tags=["Slippery"])]
    # Walls, and a see-through roof making it a tube slide: the water stays in at the
    # bends. Tall enough inside (4.2) for a rider, with room for the water to go over them.
    for side in (-1, 1):
        parts.append(b.node("SlideWall", pos=vadd(vadd(mid, vmul(right, side * 2.9)), vmul(up, 1.95)),
                            size=(0.6, 4.5, length + over), rot=rot, color=color, material="Plastic"))
    parts.append(b.node("SlideTube", pos=vadd(mid, vmul(up, 4.4)), size=(6.4, 0.4, length + over), rot=rot,
                        color=(0.8, 0.92, 1.0), material="Glass", transparency=0.75))
    return parts, mid, fwd, up


def liquid_source(b, pos, direction, size, speed, rate=None):
    """A FluidSource part pouring real liquid out of its front (its LookVector) along `direction`."""
    dx, dy, dz = direction
    horiz = math.hypot(dx, dz)
    yaw = math.degrees(math.atan2(dx, dz)) + 180.0 if horiz > 1e-6 else 0.0
    pitch = -math.degrees(math.atan2(-dy, horiz))
    src = b.node("FluidSource", pos=pos, size=size, rot=(pitch, yaw, 0), color=(0.35, 0.75, 1.0), material="Neon",
                 collide=False, transparency=0.3)
    src["attrs"] = [{"n": "Speed", "t": 1, "v": speed}] + ([{"n": "Rate", "t": 1, "v": rate}] if rate else [])
    return src


def pool_walls(b, center, sx, sz, height, color=(0.85, 0.87, 0.9)):
    x, _, z = center
    t = 0.8
    return [b.node("PoolWall", pos=(x, height / 2, z - sz / 2 - t / 2), size=(sx + 2 * t, height, t), color=color, material="Concrete"),
            b.node("PoolWall", pos=(x, height / 2, z + sz / 2 + t / 2), size=(sx + 2 * t, height, t), color=color, material="Concrete"),
            b.node("PoolWall", pos=(x - sx / 2 - t / 2, height / 2, z), size=(t, height, sz), color=color, material="Concrete"),
            b.node("PoolWall", pos=(x + sx / 2 + t / 2, height / 2, z), size=(t, height, sz), color=color, material="Concrete")]


RIDE_TIMER = '''-- Times your ride from the top to the splash.
local startLine = script.Parent:FindFirstChild("StartLine")
local finishLine = script.Parent:FindFirstChild("FinishLine")
local started = nil

startLine.Touched:Connect(function(hit)
    if not hit.Parent:FindFirstChild("Humanoid") then return end
    if started and time() - started < 3 then return end
    started = time()
    Gui.Message("WHEEEE!", 2)
end)

finishLine.Touched:Connect(function(hit)
    if not started or not hit.Parent:FindFirstChild("Humanoid") then return end
    local ride = time() - started
    started = nil
    Gui.Label("RideTime", nil)
    Gui.Message("SPLASH! Your ride took " .. string.format("%.1f", ride) .. " seconds.", 5)
    Sounds.Play("win")
end)

game:GetService("RunService").Heartbeat:Connect(function()
    if started then Gui.Label("RideTime", "Ride: " .. string.format("%.1f", time() - started) .. "s") end
end)
'''

TELEPORT = '''-- Step on it to go straight to the top of the Mega Slide.
local pad = script.Parent
local last = -10

pad.Touched:Connect(function(hit)
    local char = hit.Parent
    local root = char and char:FindFirstChild("HumanoidRootPart")
    if not root or not char:FindFirstChild("Humanoid") then return end
    if time() - last < 1.5 then return end
    last = time()
    root.Position = Vector3.new(-2, TOP, 0)
    Gui.Message("Welcome to the top! Walk into the slide and let the water take you.", 4)
end)
'''


def waterslide():
    b = Builder()
    kids = [b.node("Baseplate", pos=(0, -0.5, 0), size=(240, 1, 240), color=(0.38, 0.66, 0.32), material="Plastic"),
            b.node("SpawnLocation", pos=(0, 0.1, 44), size=(4, 0.2, 4), color=(0.3, 0.75, 0.95), material="Neon")]

    # --- The tower and the slide spiralling around it ------------------------
    top = 80.0                    # height of the slide's start (steep enough for the water to get going)
    R = 24.0                      # radius of the spiral
    turns = 2.75                  # ends on the far side from spawn
    kids.append(b.node("Tower", pos=(0, top / 2, 0), size=(8, top, 8), color=(0.93, 0.62, 0.2), material="Concrete"))
    kids.append(b.node("TowerTop", pos=(0, top - 0.5, 0), size=(12, 1, 12), color=(0.95, 0.95, 0.97), material="Concrete"))
    # A low curb round the top (easy to step over, even off the ladder) so splashes
    # can't pour off the tower; the gap on the +x side is where the slide starts.
    curb = (0.85, 0.2, 0.2)
    for x, z, sx, sz in ((-5.8, 0, 0.4, 12), (0, -5.8, 12, 0.4), (0, 5.8, 12, 0.4),
                         (5.8, -4.4, 0.4, 3.2), (5.8, 4.4, 0.4, 3.2)):
        kids.append(b.node("TowerCurb", pos=(x, top + 0.25, z), size=(sx, 0.5, sz), color=curb))
    kids.append(b.node("TowerRoof", shape="Cylinder", pos=(0, top + 7, 0), size=(13, 0.6, 13), color=(0.9, 0.2, 0.2)))
    for x, z in ((-5.5, -5.5), (5.5, -5.5), (-5.5, 5.5), (5.5, 5.5)):
        kids.append(b.node("RoofPole", shape="Cylinder", pos=(x, top + 3.3, z), size=(0.4, 7, 0.4), color=(0.95, 0.95, 0.97)))
    # The ladder runs up outside the top's overhang, a little past the top so you can climb off.
    kids.append(b.node("TowerTruss", pos=(-6.7, (top + 1.5) / 2, 0), size=(1.2, top + 1.5, 2.4), color=(0.3, 0.3, 0.33),
                       material="Metal", tags=["Climbable"]))
    kids.append(b.light(color=(1, 0.9, 0.7), brightness=3, rng=20, pos=(0, top + 5, 0)))

    # Points along the middle of the slide floor: out from the tower top, then round and round.
    pts = []
    t, T = 0.0, turns * 2 * math.pi
    while t <= T + 1e-6:
        r = R - (R - 6.0) * math.exp(-t / 0.7)
        pts.append((r * math.cos(t), 0.0, r * math.sin(t)))
        dr = (R - 6.0) / 0.7 * math.exp(-t / 0.7)
        t += 6.0 / math.hypot(r, dr)
    # Height: steady, with a steeper plunge in the middle of the second turn.
    total = sum(vlen(vsub(pts[i + 1], pts[i])) for i in range(len(pts) - 1))
    print("  slide spiral: %.0f studs, %d pieces" % (total, len(pts) - 1))
    s, heights = 0.0, [top]
    for i in range(1, len(pts)):
        s += vlen(vsub(pts[i], pts[i - 1]))
        f = s / total
        plunge = 0.18 * min(1.0, max(0.0, (f - 0.42) / 0.08))   # an extra 18% of the drop, all at once
        heights.append(top - (top - 11.0) * (0.82 * f + plunge))
    pts = [(p[0], h, p[2]) for p, h in zip(pts, heights)]
    # Then straight out into the splash pool.
    last, prev = pts[-1], pts[-2]
    out = vsub(last, prev)
    out = vmul((out[0], 0, out[2]), 1.0 / math.hypot(out[0], out[2]))
    for k in range(1, 6):
        p = vadd(last, vmul(out, 6.0 * k))
        pts.append((p[0], last[1] - 1.5 * k, p[2]))

    colors = [(1.0, 0.8, 0.1), (0.15, 0.55, 1.0), (1.0, 0.3, 0.25)]
    slide = []
    for i in range(len(pts) - 1):
        color = colors[(i // 12) % len(colors)]
        parts, mid, fwd, up = flume(b, pts[i], pts[i + 1], color, water_speed=18.0)
        if i == 0:
            # The tap: real water pours out here and runs all the way down.
            slide.append(liquid_source(b, vadd(vadd(pts[0], vmul(up, 0.45)), vmul(fwd, 1.5)), fwd, (4.2, 0.5, 0.4), speed=10))
            # A lip across the back of the start (you step over it): water can only go down the slide.
            slide.append(b.node("SlideLip", pos=vadd(vadd(pts[0], vmul(up, 0.45)), vmul(fwd, -0.2)), size=(5.4, 0.9, 0.4),
                                rot=(0, math.degrees(math.atan2(fwd[0], fwd[2])), 0), color=color, material="Plastic"))
        slide += parts
        r = math.hypot(mid[0], mid[2])
        if i % 4 == 2 and i < len(pts) - 7 and r > 8:
            # A beam from the tower holds the slide up.
            ang = math.atan2(mid[2], mid[0])
            inner = 4.0
            ln = r - 2.8 - inner
            c = ((inner + ln / 2) * math.cos(ang), mid[1] - 0.9, (inner + ln / 2) * math.sin(ang))
            slide.append(b.node("SlideBeam", pos=c, size=(ln, 0.6, 0.6), rot=(0, -math.degrees(ang), 0),
                                color=(0.55, 0.57, 0.6), material="Metal"))
        if i >= len(pts) - 6:
            # The last straight run stands on posts.
            slide.append(b.node("SlidePost", shape="Cylinder", pos=(mid[0], (mid[1] - 0.6) / 2, mid[2]),
                                size=(0.9, mid[1] - 0.6, 0.9), color=(0.55, 0.57, 0.6), material="Metal"))
        if i % 9 == 5 and i < len(pts) - 7:
            slide.append(b.node("Coin", shape="Cylinder", pos=vadd(mid, vmul(up, 1.3)), size=(1.1, 0.14, 1.1),
                                rot=(90, 0, 0), color=(1, 0.8, 0.15), material="Metal", collide=False,
                                children=[b.script(premade_script("kCoin"))]))

    # Start and finish lines for the ride timer (invisible).
    start, end = pts[0], pts[-1]
    slide.append(b.node("StartLine", pos=(start[0] + 1.5, start[1] + 1.5, start[2]), size=(1, 3, 5), color=(1, 1, 1),
                        collide=False, transparency=1))
    slide.append(b.node("FinishLine", pos=(end[0] + out[0] * 3, 4.5, end[2] + out[2] * 3), size=(8, 6, 8),
                        color=(1, 1, 1), collide=False, transparency=1))
    slide.append(b.script(RIDE_TIMER, "RideTimer"))
    kids.append(b.node("MegaSlide", kind="Model", children=slide))

    # --- Splash pool where the slide ends, with things floating in it -------
    pc = vadd(end, vmul(out, 16))
    pc = (pc[0], 0, pc[2])
    pool = pool_walls(b, pc, 30, 30, 3.4)
    pool.append(water(b, "Water", (pc[0], 1.5, pc[2]), (30, 3, 30)))
    for k in range(6):
        a = k * math.pi / 3
        pool.append(b.node("BeachBall", shape="Sphere", pos=(pc[0] + 7 * math.cos(a), 4, pc[2] + 7 * math.sin(a)),
                           size=(1.6, 1.6, 1.6), color=[(1, 0.3, 0.3), (1, 0.85, 0.2), (0.3, 0.6, 1)][k % 3],
                           material="Plastic", anchored=False))
    for k in range(4):
        a = k * math.pi / 2 + 0.4
        pool.append(b.node("FloatingRing", shape="Cylinder", pos=(pc[0] + 11 * math.cos(a), 3.6, pc[2] + 11 * math.sin(a)),
                           size=(2.6, 0.5, 2.6), color=(0.62, 0.42, 0.22), material="Wood", anchored=False))
    kids.append(b.node("SplashPool", kind="Model", children=pool))

    # --- Wave pool: big rolling waves and inner tubes riding them ---------------
    wc = (-58, 0, 30)
    wave = pool_walls(b, wc, 34, 26, 4.2)
    wave.append(water(b, "Water", (wc[0], 1.8, wc[2]), (34, 3.6, 26), attrs=[{"n": "Waves", "t": 1, "v": 0.7}]))
    for k in range(8):
        wave.append(b.node("InnerTube", shape="Cylinder", pos=(wc[0] - 12 + (k % 4) * 8, 4.5, wc[2] - 6 + (k // 4) * 12),
                           size=(2.6, 0.7, 2.6), color=[(1, 0.45, 0.2), (0.2, 0.8, 0.5)][k % 2], material="Plastic",
                           anchored=False))
    wave.append(b.node("Sign", pos=(wc[0], 6, wc[2] - 14), size=(10, 2, 0.4), color=(0.2, 0.9, 1.0), material="Neon"))
    kids.append(b.node("WavePool", kind="Model", children=wave))

    # --- Fountain: jets of real liquid arcing up and splashing into its pool ------
    fc = (-55, 0, -45)
    fountain = pool_walls(b, fc, 18, 18, 1.6, color=(0.75, 0.72, 0.68))
    fountain.append(water(b, "Water", (fc[0], 0.65, fc[2]), (18, 1.3, 18)))
    fountain.append(b.node("FountainColumn", shape="Cylinder", pos=(fc[0], 2.5, fc[2]), size=(1.6, 5, 1.6),
                           color=(0.75, 0.72, 0.68), material="Concrete"))
    # One jet straight up from the top of the column, and four arcing out round it
    # (spaced apart and clear of the stone, so no two pour into the same spot).
    fountain.append(liquid_source(b, (fc[0], 5.6, fc[2]), (0, 1, 0), (0.6, 0.6, 0.3), speed=12, rate=120))
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        d = (0.45 * math.cos(a), 1.0, 0.45 * math.sin(a))
        fountain.append(liquid_source(b, (fc[0] + 1.3 * math.cos(a), 5.4, fc[2] + 1.3 * math.sin(a)), d,
                                      (0.5, 0.5, 0.3), speed=11, rate=100))
    kids.append(b.node("Fountain", kind="Model", children=fountain))

    # --- Getting to the top -------------------------------------------------
    kids.append(b.node("TeleportPad", shape="Cylinder", pos=(7, 0.1, 44), size=(4, 0.2, 4), color=(1, 0.8, 0.1),
                       material="Neon", children=[b.script(TELEPORT.replace("TOP", str(top + 3)))]))
    kids.append(b.node("TeleportSign", pos=(7, 4, 47), size=(6, 1.5, 0.3), color=(1, 0.85, 0.2), material="Neon"))
    kids.append(b.script('Gui.Message("Welcome to the Mega Water Slide! Step on the gold pad to go to the top.", 6)', "Welcome"))

    env = {"clockTime": 13, "sunAzimuth": 140, "sunElevation": 60, "cloudCover": 0.3, "bloomIntensity": 0.6,
           "fogDensity": 0.003,
           "skyZenith": [0.12, 0.35, 0.75], "skyHorizon": [0.62, 0.8, 0.95]}
    save(ROOT / "games/Mega Water Slide.gbscene",
         {"title": "Mega Water Slide", "author": "Guts and Bolts",
          "description": "A 590-stud water slide with real flowing water, spiralling around a giant tower. Plus a wave "
                         "pool, a splash pool full of floating toys and a fountain. Shows off the water physics."},
         CLASSIC_WORLD, env, kids)


GAMES = {"obby": obby, "demolition": demolition, "plaza": plaza, "waterslide": waterslide}

if __name__ == "__main__":
    # Build the games named on the command line (like `waterslide`), or all of them.
    # Careful: rebuilding a game replaces any changes made to it by hand in Studio.
    (ROOT / "games").mkdir(exist_ok=True)
    for name in sys.argv[1:] or GAMES:
        GAMES[name]()
