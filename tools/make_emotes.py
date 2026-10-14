#!/usr/bin/env python3
"""Builds assets/emotes/emotes.gbemotes: every emote and animation pack that comes
with Guts&Bolts, as real keyframes for the classic (R6) character. Run it from the
repository root after changing a move:

    python3 tools/make_emotes.py                  the keyframes
    python3 tools/make_emotes.py --pictures DIR   also pack the pictures in DIR
                                                  (made by: GutsAndBolts --emote-pictures DIR)

The file holds:
  builtin  the emotes everyone has ("/e dance", "/e wave" ...), played by the engine
  catalog  the starter Emote and Animation pack items the servers put in the catalog
           (worker/server.js and src/server both read this same file)

A pose is how far a part turns (degrees, around its own X / Y / Z) and moves, away
from how the character was built. The character faces +Z. Arms and legs turn at the
top (shoulder / hip), the head at the neck, the Torso in its middle (and the arms,
legs and head follow it).
  - Arms and legs: X -90 = straight out in front, X -180 = straight up.
    Z = out to the side: + for the Right Arm / Leg, - for the Left ones.
  - Head: X - = look up, Y = turn.  Torso: X + = bow, pos Y + = up (the whole body).
"""
import base64
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets/emotes/emotes.gbemotes"
PICTURES = ROOT / "assets/emotes/pictures.gbemotes"

LA, RA, LL, RL, HEAD, TORSO = "Left Arm", "Right Arm", "Left Leg", "Right Leg", "Head", "Torso"


def P(rx=0.0, ry=0.0, rz=0.0, y=0.0, ease="Cubic", dir="InOut"):
    """One part's pose."""
    p = {"pos": [0.0, round(y, 3), 0.0], "rot": [round(rx, 2), round(ry, 2), round(rz, 2)]}
    if ease != "Linear":
        p["ease"] = ease
    if dir != "In":
        p["dir"] = dir
    return p


def arms(l, r, ease="Cubic"):
    """Both arms: (pitch, out) each. Out is away from the body for either arm."""
    return {LA: P(l[0], 0, -l[1], ease=ease), RA: P(r[0], 0, r[1], ease=ease)}


def legs(l, r, ease="Cubic"):
    return {LL: P(l[0], 0, -l[1], ease=ease), RL: P(r[0], 0, r[1], ease=ease)}


REST = {LA: P(), RA: P(), LL: P(), RL: P(), HEAD: P(), TORSO: P()}


def clip(keys, loop=False, priority="Action"):
    """keys: [(time, {part: pose})]. Parts left out of a key just carry on to the next."""
    return {"loop": loop, "priority": priority,
            "keys": [{"t": round(t, 3), "poses": poses} for t, poses in keys]}


def merge(*dicts):
    out = {}
    for d in dicts:
        out.update(d)
    return out


# ---------------------------------------------------------------------------
# The built-in emotes (everyone has these: "/e dance" and friends)
# ---------------------------------------------------------------------------

def dance():
    # The classic: arms pumping up and down in turn, a kick on each beat, head bobbing.
    up, mid, down = (-165, 15), (-90, 15), (-15, 15)
    k = []
    for i, t in enumerate([0.0, 0.26, 0.52, 0.78, 1.04, 1.30, 1.56, 1.82, 2.08]):
        phase = i % 4
        a = {0: arms(mid, mid), 1: arms(down, up), 2: arms(mid, mid), 3: arms(up, down)}[phase]
        l = {0: legs((0, 0), (0, 0)), 1: legs((-28, 0), (0, 0)), 2: legs((0, 0), (0, 0)), 3: legs((0, 0), (-28, 0))}[phase]
        turn = [0, 12, 0, -12][(i // 2) % 4] if i % 2 == 0 else [6, 6, -6, -6][(i // 2) % 4]
        bob = -0.05 if phase in (1, 3) else 0.0
        k.append((t, merge(a, l, {HEAD: P(0, turn), TORSO: P(0, 0, 0, bob)})))
    return clip(k, loop=True)


def dance2():
    # Arms out to the sides, flapping; little side steps; head nodding to the beat.
    k = []
    for i in range(5):
        t = i * 0.25
        flap = [0, 1, 0, -1, 0][i]
        k.append((t, merge(arms((-10, 75 + 30 * flap), (-10, 75 - 30 * flap)),
                           legs((0, 12 if flap > 0 else 0), (0, 12 if flap < 0 else 0)),
                           {HEAD: P(6 if i % 2 else -4), TORSO: P(0, 0, -6 * flap)})))
    return clip(k, loop=True)


def dance3():
    # Disco: one arm points up at the ceiling, then the other, hips swinging.
    upR, downL = (-168, 25), (-20, 10)
    k = []
    for half, side in ((0.0, 1), (0.5, -1)):
        r, l = (upR, downL) if side > 0 else (downL, upR)
        pose = merge(arms(l, r), legs((-20 if side > 0 else 0, 0), (0 if side > 0 else -20, 0)),
                     {HEAD: P(-8, 15 * side), TORSO: P(0, 10 * side, 5 * side)})
        k.append((half, pose))
        k.append((half + 0.4, pose))
    k.append((1.0, k[0][1]))
    return clip(k, loop=True)


def wave():
    # Right hand up by the head, waving side to side, then back down.
    k = [(0.0, REST), (0.3, merge(arms((0, 0), (-160, 30)), {HEAD: P(-4, 0, 6)}))]
    t, out = 0.3, 30
    for i in range(6):
        t += 0.22
        out = 60 if i % 2 == 0 else 15
        k.append((t, {RA: P(-160, 0, out)}))
    k.append((t + 0.25, {RA: P(-160, 0, 30), HEAD: P(-4, 0, 6)}))
    k.append((t + 0.55, REST))
    return clip(k)


def point():
    # Right arm straight out in front, looking where it points.
    aim = merge(arms((0, 0), (-90, 0)), {HEAD: P(0, 6), TORSO: P(0, -10)})
    return clip([(0.0, REST), (0.2, aim), (1.7, aim), (2.0, REST)])


def cheer():
    # Both arms up, pumping, with little hops.
    k = [(0.0, REST)]
    t = 0.0
    for i in range(8):
        t += 0.22
        high = i % 2 == 0
        k.append((t, merge(arms((-170 if high else -145, 18), (-170 if high else -145, 18)),
                           {HEAD: P(-12), TORSO: P(0, 0, 0, 0.18 if high else 0.0)})))
    k.append((t + 0.3, REST))
    return clip(k)


def laugh():
    # Holding your belly, leaning back and shaking with laughter.
    k = [(0.0, REST)]
    t = 0.0
    for i in range(14):
        t += 0.13
        s = 1 if i % 2 == 0 else -1
        k.append((t, merge(arms((-35 + 4 * s, -18), (-35 - 4 * s, -18)),
                           {HEAD: P(-18 + 6 * s), TORSO: P(-6 + 2 * s)})))
    k.append((t + 0.3, REST))
    return clip(k)


BUILTIN = [
    ("dance", "Dance", dance(), 0.26),
    ("dance2", "Dance 2", dance2(), 0.25),
    ("dance3", "Dance 3", dance3(), 0.2),
    ("wave", "Wave", wave(), 0.52),
    ("point", "Point", point(), 1.0),
    ("cheer", "Cheer", cheer(), 0.22),
    ("laugh", "Laugh", laugh(), 0.4),
]


# ---------------------------------------------------------------------------
# Starter catalog emotes
# ---------------------------------------------------------------------------

def salute():
    hand = merge(arms((0, 0), (-140, -38)), {HEAD: P(-4), TORSO: P(-3)})
    return clip([(0.0, REST), (0.25, hand), (1.6, hand), (1.85, REST)])


def shrug():
    up = merge(arms((-28, 38), (-28, 38)), {HEAD: P(0, 0, 14), TORSO: P(0, 0, 0, 0.05)})
    return clip([(0.0, REST), (0.3, up), (1.1, up), (1.5, REST)])


def robot():
    # Stiff and jerky: every move snaps (Constant easing).
    def snap(pose):
        return {name: dict(p, ease="Constant") for name, p in pose.items()}
    k = []
    steps = [((-90, 0), (-90, 0), 0, 0), ((-110, 0), (-70, 0), 30, 15), ((-70, 0), (-110, 0), 30, 15),
             ((-90, 0), (-90, 0), 0, 0), ((-70, 0), (-110, 0), -30, -15), ((-110, 0), (-70, 0), -30, -15)]
    for i, (l, r, head, torso) in enumerate(steps):
        k.append((i * 0.35, snap(merge(arms(l, r), legs((0, 0), (0, 0)), {HEAD: P(0, head), TORSO: P(0, torso)}))))
    k.append((len(steps) * 0.35, k[0][1]))
    return clip(k, loop=True)


def jumping_jacks():
    together = merge(arms((0, 5), (0, 5)), legs((0, 0), (0, 0)), {TORSO: P(0, 0, 0, 0.0)})
    air = merge(arms((0, 95), (0, 95)), legs((0, 9), (0, 9)), {TORSO: P(0, 0, 0, 0.35)})
    spread = merge(arms((0, 165), (0, 165)), legs((0, 18), (0, 18)), {TORSO: P(0, 0, 0, 0.0)})
    return clip([(0.0, together), (0.2, air), (0.4, spread), (0.6, air), (0.8, together)], loop=True)


def sprinkler():
    # Left hand behind the head, right arm out in front, the body clicking round
    # in little steps, then sweeping smoothly back.
    def body(turn, ease="Constant"):
        return merge(arms((-165, -50), (-90, 0)), {TORSO: P(0, turn, ease=ease), HEAD: P(0, 0, ease=ease)})
    k = []
    for i in range(7):
        k.append((i * 0.22, body(-45 + 15 * i)))
    k.append((1.6, body(45, "Cubic")))
    k.append((2.4, body(-45)))
    return clip(k, loop=True)


# ---------------------------------------------------------------------------
# Starter animation packs (idle, walk, run, jump, fall)
# ---------------------------------------------------------------------------

def bouncy_pack():
    idle = clip([(0.0, merge(arms((4, 6), (4, 6)), {TORSO: P(0, 0, 0, 0.0)})),
                 (0.6, merge(arms((-4, 10), (-4, 10)), {TORSO: P(0, 0, 0, -0.06)})),
                 (1.2, merge(arms((4, 6), (4, 6)), {TORSO: P(0, 0, 0, 0.0)}))], loop=True, priority="Idle")

    def stride(swing, arm, bounce, length):
        a = merge(arms((swing * arm, 10), (-swing * arm, 10)), legs((-swing, 0), (swing, 0)), {TORSO: P(4, 0, 0, 0.0)})
        b = merge(arms((0, 12), (0, 12)), legs((0, 0), (0, 0)), {TORSO: P(4, 0, 0, bounce)})
        c = merge(arms((-swing * arm, 10), (swing * arm, 10)), legs((swing, 0), (-swing, 0)), {TORSO: P(4, 0, 0, 0.0)})
        q = length / 4
        return clip([(0, a), (q, b), (2 * q, c), (3 * q, b), (length, a)], loop=True, priority="Movement")

    jump = clip([(0.0, merge(arms((-150, 40), (-150, 40)), legs((-50, 0), (-20, 0)), {HEAD: P(-10)}))], priority="Movement")
    fall = clip([(0.0, merge(arms((-120, 70), (-60, 70)), legs((-10, 10), (10, 10)))),
                 (0.2, merge(arms((-60, 70), (-120, 70)), legs((10, 10), (-10, 10)))),
                 (0.4, merge(arms((-120, 70), (-60, 70)), legs((-10, 10), (10, 10))))], loop=True, priority="Movement")
    return {"idle": idle, "walk": stride(35, 1.1, 0.12, 0.7), "run": stride(55, 1.2, 0.2, 0.5), "jump": jump, "fall": fall}


def robot_pack():
    def snap(pose):
        return {name: dict(p, ease="Constant") for name, p in pose.items()}
    idle = clip([(0.0, snap(merge(arms((0, 2), (0, 2)), {HEAD: P(0, 0)}))),
                 (1.6, snap({HEAD: P(0, 25)})), (2.2, snap({HEAD: P(0, -25)})), (2.8, snap({HEAD: P(0, 0)})),
                 (4.0, snap({HEAD: P(0, 0)}))], loop=True, priority="Idle")

    def march(swing, length):
        a = merge(arms((-swing * 0.6, 4), (swing * 0.6, 4), "Linear"), legs((-swing, 0), (swing, 0), "Linear"), {TORSO: P(0, 0, 0, 0.0)})
        c = merge(arms((swing * 0.6, 4), (-swing * 0.6, 4), "Linear"), legs((swing, 0), (-swing, 0), "Linear"), {TORSO: P(0, 0, 0, 0.0)})
        return clip([(0, a), (length / 2, c), (length, a)], loop=True, priority="Movement")

    jump = clip([(0.0, snap(merge(arms((-180, 0), (-180, 0)), legs((0, 0), (0, 0)))))], priority="Movement")
    fall = clip([(0.0, snap(merge(arms((0, 90), (0, 90)), legs((0, 8), (0, 8)))))], priority="Movement")
    return {"idle": idle, "walk": march(28, 0.8), "run": march(45, 0.55), "jump": jump, "fall": fall}


CATALOG = [
    {"id": "emote-salute", "kind": "emote", "name": "Salute", "price": 0, "picture": 1.0,
     "description": "Stand up straight and salute. Free for everyone.", "clip": salute()},
    {"id": "emote-shrug", "kind": "emote", "name": "Shrug", "price": 0, "picture": 0.8,
     "description": "Who knows? Not you. Free for everyone.", "clip": shrug()},
    {"id": "emote-robot", "kind": "emote", "name": "Robot", "price": 0, "picture": 0.4,
     "description": "Beep boop. Stiff, jerky moves that go on until you walk away. Free for everyone.", "clip": robot()},
    {"id": "emote-jumping-jacks", "kind": "emote", "name": "Jumping Jacks", "price": 15, "picture": 0.4,
     "description": "Get your exercise in between rounds. Keeps going until you move.", "clip": jumping_jacks()},
    {"id": "emote-sprinkler", "kind": "emote", "name": "Sprinkler", "price": 25, "picture": 0.66,
     "description": "The classic garden sprinkler dance. Keeps going until you move.", "clip": sprinkler()},
    {"id": "pack-bouncy", "kind": "animpack", "name": "Bouncy Animation Pack", "price": 0, "picture": ["walk", 0.0],
     "description": "A springy idle, a bouncy walk and run, and arms flailing when you fall. Free for everyone.",
     "clips": bouncy_pack()},
    {"id": "pack-robot", "kind": "animpack", "name": "Robot Animation Pack", "price": 40, "picture": ["fall", 0.0],
     "description": "Walk, run, jump and fall like a robot: stiff arms, marching legs, head turning in little clicks.",
     "clips": robot_pack()},
]


def main():
    data = {
        "version": 1,
        "builtin": [{"name": n, "title": t, "picture": pic, "clip": c} for n, t, c, pic in BUILTIN],
        "catalog": CATALOG,
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(data, separators=(",", ":")) + "\n")
    print("wrote", OUT.relative_to(ROOT), OUT.stat().st_size, "bytes")
    if "--pictures" in sys.argv:
        folder = Path(sys.argv[sys.argv.index("--pictures") + 1])
        pics = {}
        for item in [b["name"] for b in data["builtin"]] + [c["id"] for c in CATALOG]:
            f = folder / (item + ".png")
            if f.exists():
                pics[item] = base64.b64encode(f.read_bytes()).decode()
            else:
                print("no picture for", item)
        PICTURES.write_text(json.dumps(pics, separators=(",", ":"), sort_keys=True) + "\n")
        print("wrote", PICTURES.relative_to(ROOT), len(pics), "pictures,", PICTURES.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
