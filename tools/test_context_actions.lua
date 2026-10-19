-- Checks ContextActionService (key bindings) in a real play test, pressing pretend keys,
-- clicks and touch buttons with __gb_testInput (only there when Studio runs a --test-command):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_context_actions.lua)" \
--       --screenshot /tmp/actions.png --frames 2400
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "ACTIONS TEST DONE".
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end
    local CAS = game:GetService("ContextActionService")
    local UIS = game:GetService("UserInputService")
    local press = __gb_testInput
    check(type(press) == "function", "the test input hook is there")
    check(CAS == ContextActionService and CAS.ClassName == "ContextActionService", "game:GetService finds it")
    local head = game.Players.LocalPlayer.Character.Head

    local function key(name, down) press("key", name, down) end
    local function tap(name) key(name, true) frames(4) key(name, false) frames(4) end
    -- Hold Space a moment; how high did the head go?
    local function jumpHeight()
        frames(40)
        local y0, top = head.Position.Y, head.Position.Y
        key("Space", true)
        for _ = 1, 15 do frames(1) top = math.max(top, head.Position.Y) end
        key("Space", false)
        for _ = 1, 40 do frames(1) top = math.max(top, head.Position.Y) end
        return top - y0
    end
    -- What each action heard: log[i] = "Name:State:KeyCode:UserInputType"
    local log = {}
    local function recorder(result)
        return function(name, state, input)
            log[#log + 1] = name .. ":" .. state .. ":" .. tostring(input.KeyCode) .. ":" .. tostring(input.UserInputType)
            return result
        end
    end
    local function heard() local s = table.concat(log, " ") log = {} return s end
    local lastProcessed
    UIS.InputBegan:Connect(function(input, gameProcessed)
        if input.KeyCode == Enum.KeyCode.Space then lastProcessed = gameProcessed end
    end)

    -- Nothing bound: Space jumps, and the game's own controls didn't process it.
    local h = jumpHeight()
    check(h > 1, "Space jumps when nothing is bound", h)
    check(lastProcessed == false, "gameProcessedEvent is false with nothing bound", lastProcessed)

    -- A Sink binding on Space: the action hears Begin and End, and the character stays down.
    CAS:BindAction("Hold", recorder(Enum.ContextActionResult.Sink), false, Enum.KeyCode.Space)
    h = jumpHeight()
    check(h < 0.3, "a Sink binding on Space stops the jump", h)
    check(heard() == "Hold:Begin:Space:Keyboard Hold:End:Space:Keyboard", "the action hears Begin then End")
    check(lastProcessed == true, "gameProcessedEvent is true when an action sank it", lastProcessed)

    -- Returning nothing sinks too (like Roblox).
    CAS:BindAction("Hold", function(name, state) log[#log + 1] = name .. ":" .. state end, false, Enum.KeyCode.Space)
    h = jumpHeight()
    check(h < 0.3, "returning nil sinks", h)
    check(heard() == "Hold:Begin Hold:End", "binding a name again replaces it")

    -- Priority: a lower one under a Sink hears nothing; a higher one that Passes hears it first.
    CAS:BindAction("Hold", recorder(Enum.ContextActionResult.Sink), false, Enum.KeyCode.Space)
    CAS:BindActionAtPriority("Low", recorder(Enum.ContextActionResult.Sink), false, Enum.ContextActionPriority.Low.Value, Enum.KeyCode.Space)
    CAS:BindActionAtPriority("Top", recorder(Enum.ContextActionResult.Pass), false, Enum.ContextActionPriority.High.Value, Enum.KeyCode.Space)
    tap("Space")
    check(heard() == "Top:Begin:Space:Keyboard Hold:Begin:Space:Keyboard Top:End:Space:Keyboard Hold:End:Space:Keyboard",
          "higher priority first, Pass goes on down, Sink stops it")
    -- The newest binding wins at the same priority.
    CAS:BindAction("Newer", recorder(Enum.ContextActionResult.Sink), false, Enum.KeyCode.Space)
    tap("Space")
    check(heard() == "Top:Begin:Space:Keyboard Newer:Begin:Space:Keyboard Top:End:Space:Keyboard Newer:End:Space:Keyboard",
          "the newest binding wins at the same priority")

    -- Cancel: a key held by an action that a new, higher binding takes over.
    CAS:UnbindAction("Top")
    key("Space", true) frames(4)
    CAS:BindActionAtPriority("Late", recorder(Enum.ContextActionResult.Sink), false, 3000, Enum.KeyCode.Space)
    key("Space", false) frames(4)
    check(heard() == "Newer:Begin:Space:Keyboard Newer:Cancel:Space:Keyboard",
          "the action holding a key gets Cancel when a higher binding takes it", table.concat(log, " "))
    heard()

    -- Info.
    local info = CAS:GetBoundActionInfo("Low")
    check(info.priorityLevel == 1000 and info.inputTypes[1] == Enum.KeyCode.Space and info.createTouchButton == false,
          "GetBoundActionInfo", info.priorityLevel)
    local all = CAS:GetAllBoundActionInfo()
    check(all.Hold and all.Low and all.Newer and all.Late and not all.Top, "GetAllBoundActionInfo lists what's bound")
    check(next(CAS:GetBoundActionInfo("Nope")) == nil, "an unbound action's info is empty")

    -- Unbinding: Space jumps again, and Pass lets the jump through.
    CAS:UnbindAllActions()
    check(next(CAS:GetAllBoundActionInfo()) == nil, "UnbindAllActions unbinds them all")
    CAS:BindAction("Watch", recorder(Enum.ContextActionResult.Pass), false, Enum.KeyCode.Space)
    h = jumpHeight()
    check(h > 1, "a Pass binding lets the jump happen", h)
    check(heard() == "Watch:Begin:Space:Keyboard Watch:End:Space:Keyboard", "and still hears it")
    CAS:UnbindAction("Watch")
    h = jumpHeight()
    check(h > 1 and heard() == "", "after UnbindAction it hears nothing", h)

    -- A whole UserInputType: any key, and a controller-style name too.
    CAS:BindAction("AnyKey", recorder(Enum.ContextActionResult.Sink), false, Enum.UserInputType.Keyboard)
    tap("E")
    check(heard() == "AnyKey:Begin:E:Keyboard AnyKey:End:E:Keyboard", "Enum.UserInputType.Keyboard hears every key")
    CAS:UnbindAction("AnyKey")
    CAS:BindAction("PadA", recorder(Enum.ContextActionResult.Sink), false, Enum.KeyCode.ButtonA, Enum.KeyCode.Q)
    tap("Q")
    check(heard() == "PadA:Begin:Q:Keyboard PadA:End:Q:Keyboard", "several inputs on one action")
    CAS:UnbindAction("PadA")

    -- The mouse: MouseButton1 in the game view.
    CAS:BindAction("Shoot", recorder(Enum.ContextActionResult.Sink), false, Enum.UserInputType.MouseButton1)
    press("click") frames(6)
    check(heard() == "Shoot:Begin:Unknown:MouseButton1 Shoot:End:Unknown:MouseButton1", "MouseButton1 Begin and End")
    CAS:UnbindAction("Shoot")

    -- Touch buttons.
    CAS:BindAction("Fire", recorder(Enum.ContextActionResult.Sink), true, Enum.KeyCode.F)
    local button = CAS:GetButton("Fire")
    check(button ~= nil and button:IsA("ImageButton"), "GetButton gives an ImageButton-like object")
    CAS:SetTitle("Fire", "FIRE")
    CAS:SetDescription("Fire", "Shoot a fireball")
    CAS:SetImage("Fire", "rbxassetid://1234")
    CAS:SetPosition("Fire", UDim2.new(0.5, -40, 0.2, 0))
    info = CAS:GetBoundActionInfo("Fire")
    check(info.title == "FIRE" and info.description == "Shoot a fireball" and info.image == "rbxassetid://1234" and info.createTouchButton,
          "SetTitle / SetDescription / SetImage show in the info")
    check(button.Image == "rbxassetid://1234" and button.ActionTitle.Text == "FIRE", "the button object reads them")
    check(button.Position == UDim2.new(0.5, -40, 0.2, 0), "SetPosition", button.Position)
    button.ActionTitle.Text = "Boom"
    check(CAS:GetBoundActionInfo("Fire").title == "Boom", "changing the button object changes the button")
    press("button", "Fire", true) frames(3)
    check(button.Pressed == true, "the button is pressed")
    press("button", "Fire", false) frames(3)
    check(heard() == "Fire:Begin:Unknown:Touch Fire:End:Unknown:Touch", "pressing the touch button runs the action with Touch")
    tap("F")
    check(heard() == "Fire:Begin:F:Keyboard Fire:End:F:Keyboard", "and its key still works")
    CAS:BindAction("NoButton", recorder(), false, Enum.KeyCode.G)
    check(CAS:GetButton("NoButton") == nil, "no touch button asked for: GetButton is nil")
    CAS:UnbindAction("Fire")
    check(CAS:GetButton("Fire") == nil, "an unbound action has no button")

    -- Touch fingers on the game view.
    CAS:BindAction("Finger", recorder(Enum.ContextActionResult.Sink), false, Enum.UserInputType.Touch)
    press("touch", "", true) frames(3) press("touch", "", false) frames(3)
    check(heard() == "Finger:Begin:Unknown:Touch Finger:End:Unknown:Touch", "Enum.UserInputType.Touch")
    CAS:UnbindAllActions()

    -- Mistakes say what's wrong.
    check(not pcall(function() CAS:BindAction(5, function() end, false, Enum.KeyCode.E) end), "a name that isn't a string is an error")
    check(not pcall(function() CAS:BindAction("X", "nope", false, Enum.KeyCode.E) end), "a callback that isn't a function is an error")
    check(not pcall(function() CAS:BindAction("X", function() end, false, 42) end), "an input that isn't an Enum is an error")
    check(not pcall(function() return Enum.ContextActionPriority.Huge end), "an unknown priority is an error")

    print(fails == 0 and "ACTIONS TEST DONE: all passed" or ("ACTIONS TEST DONE: " .. fails .. " failed"))
end)
