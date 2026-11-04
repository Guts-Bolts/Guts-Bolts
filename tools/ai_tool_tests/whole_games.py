import time

TITLE = "Making whole games (build, terrain, create_ui, add_game_system, set_atmosphere)"

# (Named so it runs after the other files: its round system moves and respawns
# characters during playtests. It switches its scripts off again at the end.)


def lua(m, code):
    d = m.call("run_lua", code=code)
    return (d.get("output") or [""])[-1] if d.get("success") else "LUA ERROR: " + str(d.get("error"))


def made_paths(d):
    return [x.get("path", "") for x in d.get("made", [])]


def run(m, t):
    # ---- build: many parts in one call, checked first
    d = m.call("build", model="GmHouse", parts=[
        {"name": "GmFloor", "position": [300, 0.25, 0], "size": [12, 0.5, 12], "color": "tan", "material": "wood"},
        {"name": "GmStep", "position": [300, 0.25, 8], "size": [4, 0.5, 1.5], "repeat": {"count": 5, "step": [0, 0.5, 1.5]}},
        {"name": "GmCrate", "position": [300, 8, 0], "size": [1, 1, 1], "anchored": False, "shape": "Block"},
        {"name": "GmCopy", "position": [300, 0.25, 0], "size": [12, 0.5, 12]},
    ])
    t.check(t.ok(d) and d["count"] == 8 and d["path"] == "Workspace.GmHouse", "build makes 8 parts (repeat makes 5 steps) in one Model", d)
    warn = " ".join(d.get("warnings", []))
    t.check("inside each other" in warn and "Loose part" in warn, "build warns about doubled parts and a loose part with nothing under it", d)
    out = lua(m, "local s = workspace.GmHouse.GmStep3 print(s.Position.Y, s.Position.Z, workspace.GmHouse.GmFloor.Material)")
    t.check(out.startswith("1.25 11") and out.endswith("Wood"), "repeated parts step along and materials are set", out)
    d = m.call("build", parts=[{"position": [0, 0, 0]}, {"position": [0, 0, 0], "material": "Lava"}])
    t.check(t.code(d) == "INVALID_ENUM_VALUE" and d["error"]["part_index"] == 1 and "Wood" in t.choices(d),
            "a bad material is refused with the choices, and says which part", d)
    gone = lua(m, "print(#workspace:GetChildren())")
    d2 = m.call("build", parts=[{"position": [0, 0, 0], "color": "bluish"}])
    t.check(t.code(d2) == "INVALID_INPUT" and lua(m, "print(#workspace:GetChildren())") == gone, "a bad part builds nothing at all", d2)

    # ---- terrain
    d = m.call("terrain", action="generate", seed=4, hills=0.5, size=256)
    t.check(t.ok(d) and d["exists"] and d["size"] == 256 and d["highest"] > d["lowest"], "terrain generate makes hills", d)
    d = m.call("terrain", action="height_at", points=[[10, 10], [5000, 0]])
    h = d.get("heights", [{}, {}])
    t.check(t.ok(d) and isinstance(h[0].get("height"), float) and h[1].get("height") is None,
            "height_at reads the ground (and null off the edge)", d)
    d = m.call("terrain", action="brush", brush="Flatten", position=[10, 3, 10], radius=12, strength=1)
    after = m.call("terrain", action="height_at", points=[[10, 10]])
    t.check(t.ok(d) and abs(after["heights"][0]["height"] - 3) < 0.6, "the Flatten brush levels a spot for building", after)
    d = m.call("terrain", action="brush", brush="Paint", material="Sand", points=[[-60, -60], [60, 60]], radius=6)
    t.check(t.ok(d) and d["dabs"] > 10 and d["materials_percent"].get("Sand", 0) > 0, "a brush runs along a line of points (a sand path)", d)
    d = m.call("terrain", action="water", level=2)
    t.check(t.ok(d) and d.get("object_id") and abs(d["water"]["top"] - 2) < 0.01, "terrain water adds a Water part with its top at the level", d)
    d = m.call("terrain", action="fill_ball", position=[0, 0, 0], radius=5, material="Lava")
    t.check(t.code(d) == "INVALID_ENUM_VALUE" and "Rock" in t.choices(d), "an unknown terrain material lists the real ones", d)

    # ---- create_ui
    d = m.call("create_ui", ui={"class": "ScreenGui", "name": "GmHud", "children": [
        {"class": "TextLabel", "name": "GmCoins", "props": {"Size": [0, 200, 0, 50], "Text": "Coins: 0", "TextScaled": True, "Colour": [1, 0, 0]}},
        {"class": "Fram", "name": "GmBad"}]})
    probs = d.get("problems", [])
    t.check(t.ok(d) and "StarterGui.GmHud.GmCoins" in made_paths(d), "create_ui makes a ScreenGui with a label inside, in StarterGui", d)
    t.check(any(p.get("property") == "Colour" and "TextColor3" in p.get("valid_choices", []) for p in probs) and
            any("Frame" in p.get("did_you_mean", []) for p in probs), "bad properties and classes are listed in problems, the rest is made", d)
    out = lua(m, "print(game.StarterGui.GmHud.GmCoins.Text, game.StarterGui.GmHud.GmCoins.Size.X.Offset)")
    t.check(out.startswith("Coins: 0 200"), "the label's properties are really set", out)

    # ---- set_atmosphere
    d = m.call("set_atmosphere", preset="sunset", weather="Snow")
    li = d.get("lighting", {})
    t.check(t.ok(d) and li.get("ClockTime") == 18 and li.get("FogEnabled") is True and li.get("Weather") == "Snow" and li.get("WeatherIntensity", 0) > 0.1,
            "set_atmosphere sets a preset, and a value given wins over it", d)
    d = m.call("set_atmosphere", preset="disco")
    t.check(t.code(d) == "INVALID_ENUM_VALUE" and "spooky" in t.choices(d), "an unknown preset lists the real ones", d)

    # ---- add_game_system
    d = m.call("add_game_system", system="leaderboard", stats=["GmCoins", "GmWins"])
    t.check(t.ok(d) and "ServerScriptService.Leaderboard" in made_paths(d), "leaderboard adds a Leaderboard script", d)
    d = m.call("add_game_system", system="leaderboard", stats=["GmCoins", "GmWins", "GmKills"])
    t.check(t.ok(d) and d["replaced"] == ["Leaderboard"] and
            lua(m, "local n = 0 for _, c in ipairs(game.ServerScriptService:GetChildren()) do if c.Name == 'Leaderboard' then n = n + 1 end end print(n)") == "1",
            "calling it again replaces the old one (no copies)", d)
    m.call("build", model="GmGems", parts=[{"name": "GmGem", "position": [320, 1, 0], "size": [1, 1, 1], "color": "cyan", "can_collide": False}])
    d = m.call("add_game_system", system="collectibles", name="GmGem", stat="GmCoins", amount=5, respawn=2)
    t.check(t.ok(d) and d["settings"]["found"] == 1 and "ServerScriptService.Collectibles" in made_paths(d), "collectibles finds the gems", d)
    d = m.call("build", model="GmArena", parent="ServerStorage", parts=[
        {"name": "GmArenaFloor", "position": [0, 60, 300], "size": [30, 1, 30]},
        {"name": "ArenaSpawn", "position": [0, 61, 300], "size": [2, 0.2, 2], "transparency": 1, "can_collide": False}])
    arena = d.get("object_id")
    d = m.call("add_game_system", system="rounds", map=arena, intermission=2, round_time=3, stat="GmWins")
    t.check(t.ok(d) and {"ReplicatedStorage.RoundStatus", "ServerScriptService.RoundSystem", "StarterGui.RoundStatusGui.Status"} <= set(made_paths(d)),
            "rounds adds the round script, its status value and the status bar", d)
    d = m.call("add_game_system", system="shop", currency="GmCoins",
               items=[{"name": "GmSword", "price": 5, "item": "Classic Sword"}, {"name": "GmCoil", "price": 500, "item": "speed coil"}])
    t.check(t.ok(d) and "StarterGui.ShopGui.Window.List.GmSword" in made_paths(d) and not d.get("problems"), "shop makes the window with a button per item", d)
    out = lua(m, "local f = game.ServerStorage.ShopItems print(f.GmSword.ClassName, f.GmCoil.ClassName, workspace:FindFirstChild('GmSword') == nil)")
    t.check(out == "Tool Tool true", "the items are Tools kept in ServerStorage.ShopItems (not lying in the Workspace)", out)
    d = m.call("add_game_system", system="shop", items=[{"name": "GmX", "price": 1, "item": "Banana Gun"}])
    t.check(t.code(d) == "UNKNOWN_CLASS", "a gear that doesn't exist is refused", d)
    d = m.call("add_game_system", system="teams", teams=[{"name": "GmRed", "color": "red"}, {"name": "GmBlue", "color": "blue"}], at=[0, 0, -300])
    t.check(t.ok(d) and {"Teams.GmRed", "Workspace.GmRedSpawn"} <= set(made_paths(d)), "teams makes the teams and a coloured spawn for each", d)
    d = m.call("add_game_system", system="teams", teams=[{"name": "A", "color": "red"}, {"name": "B", "color": "red"}])
    t.check(t.code(d) == "INVALID_INPUT", "two teams with the same colour are refused", d)
    d = m.call("add_game_system", system="checkpoints", name="GmCheck", stat="GmStage")
    t.check(t.ok(d) and "ServerScriptService.Checkpoints" in made_paths(d), "checkpoints adds its script", d)
    d = m.call("add_game_system", system="minigolf")
    t.check(t.code(d) == "INVALID_ENUM_VALUE" and "rounds" in t.choices(d), "an unknown system lists the real ones", d)

    # ---- play it: everything runs together without errors
    m.call("playtest", action="start")
    time.sleep(3)
    # (Studio is shared with the other test files, so there may be other teams too.)
    out = lua(m, "local p = game.Players:GetPlayers()[1] print(p.Team ~= nil, p.leaderstats:FindFirstChild('GmKills') ~= nil)")
    t.check(out == "true true", "in play the player is on a team and has the leaderboard numbers", out)
    lua(m, "local p = game.Players:GetPlayers()[1] p.Character:PivotTo(CFrame.new(workspace.GmGems.GmGem.Position))")
    time.sleep(1)
    out = lua(m, "print(game.Players:GetPlayers()[1].leaderstats.GmCoins.Value)")
    t.check(out == "5", "touching a gem gives 5 GmCoins", out)
    lua(m, "game.ReplicatedStorage.ShopBuy:FireServer('GmSword') game.ReplicatedStorage.ShopBuy:FireServer('GmCoil')")
    time.sleep(1)
    out = lua(m, "local p = game.Players:GetPlayers()[1] print(p.leaderstats.GmCoins.Value, p.Backpack:FindFirstChild('GmSword') ~= nil, p.Backpack:FindFirstChild('GmCoil') == nil)")
    t.check(out == "0 true true", "the shop sells what you can pay for (and not what you can't)", out)
    deadline = time.time() + 20
    text = ""
    while time.time() < deadline and "Round over" not in text:
        time.sleep(1)
        text = " ".join(x.get("text", "") for x in m.call("get_output", run="latest").get("lines", []))
    t.check("Round over." in text, "a whole round runs: intermission, round, the end", text[-400:])
    # (Survival: the player wins if they're still alive. Things other test files left
    # in this shared Studio can knock them out, so a win is only checked when there was one.)
    if "Round over. Winner:" in text:
        out = lua(m, "print(game.Players:GetPlayers()[1].leaderstats.GmWins.Value >= 1)")
        t.check(out == "true", "the winner gets a win", out)
    deadline = time.time() + 10   # (the winner shows for 5 seconds, then the arena goes and the next intermission starts)
    while time.time() < deadline and not lua(m, "print(game.ReplicatedStorage.RoundStatus.Value)").startswith("Intermission"):
        time.sleep(0.5)
    out = lua(m, "print(workspace:FindFirstChild('GmArena') == nil)")
    t.check(out == "true", "after the round the arena copy is taken away", out)
    e = m.call("get_errors", run="latest")
    ours = ("Leaderboard", "RoundSystem", "ShowStatus", "Collectibles", "Checkpoints", "Shop")
    mine = [x for x in e.get("errors", []) if any(n in x.get("text", "") for n in ours)]
    t.check(t.ok(e) and not mine, "no errors from the game systems' scripts in the playtest", mine)
    m.call("playtest", action="stop")

    # ---- tidy up for anything that runs after: switch the systems off, flat lighting, no terrain
    for system in ("leaderboard", "rounds", "collectibles", "checkpoints", "shop"):
        for o in m.call("find_objects", tag="GameSystem:" + system).get("matches", []):
            if o.get("class") in ("Script", "LocalScript"):
                m.call("set_property", object=o["object_id"], property="Enabled", value=False)
    for team in ("Teams.GmRed", "Teams.GmBlue"):
        m.call("run_lua", code=f"game.{team}:SetAttribute('AutoAssignable', false)")
    m.call("set_atmosphere", preset="day")
    m.call("terrain", action="clear")
