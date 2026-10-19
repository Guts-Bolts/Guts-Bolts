-- Checks MessagingService in a real play test (Studio's play test keeps its messages on this
-- computer, so every local server of the same game hears them). Run it like the other tests:
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_messaging.lua)" \
--       --screenshot /tmp/messaging.png --frames 1500
--
-- To check two servers talking, start a second copy at the same time that only sends
-- (put `MESSAGING_PARTNER = true` in front of the script for it):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "MESSAGING_PARTNER = true $(cat tools/test_messaging.lua)" \
--       --screenshot /tmp/partner.png --frames 900 &
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "MESSAGING TEST DONE".
local MessagingService = game:GetService("MessagingService")

if MESSAGING_PARTNER then
    -- The other server: say hello now and then, and answer pings.
    task.spawn(function()
        MessagingService:SubscribeAsync("PartnerPing", function(msg)
            MessagingService:PublishAsync("PartnerPong", { echo = msg.Data, from = "partner" })
        end)
        for i = 1, 40 do
            MessagingService:PublishAsync("PartnerHello", { n = i, from = "partner" })
            task.wait(0.25)
        end
        print("MESSAGING PARTNER DONE")
    end)
    return
end

task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function waitFor(test, seconds)
        local t = 0
        while not test() and t < (seconds or 3) do t = t + task.wait() end
        return test()
    end
    local function fails_with(pattern, fn, ...)
        local ok, err = pcall(fn, ...)
        return not ok and tostring(err):find(pattern) ~= nil, err
    end

    check(MessagingService ~= nil and MessagingService.ClassName == "MessagingService", "game:GetService(\"MessagingService\")")
    check(game.MessagingService == MessagingService, "game.MessagingService is the same service")

    -- A message reaches this server too, as {Data, Sent}.
    local got = {}
    local conn = MessagingService:SubscribeAsync("Shout", function(msg) table.insert(got, msg) end)
    check(type(conn) == "table" and conn.Connected == true and type(conn.Disconnect) == "function", "SubscribeAsync returns a connection")
    local before = tick()
    MessagingService:PublishAsync("Shout", { text = "hi", list = { 1, 2, 3 }, yes = true })
    check(waitFor(function() return #got >= 1 end), "the sender gets its own message")
    local m = got[1]
    check(m and type(m.Data) == "table" and m.Data.text == "hi" and m.Data.yes == true and #m.Data.list == 3,
          "Data is the table that was sent")
    check(m and type(m.Sent) == "number" and math.abs(m.Sent - before) < 5, "Sent is the time it was sent (unix seconds)", m and m.Sent)

    -- Plain values, and every subscriber of a topic (only that topic).
    local second, other = {}, {}
    local conn2 = MessagingService:SubscribeAsync("Shout", function(msg) table.insert(second, msg.Data) end)
    local conn3 = MessagingService:SubscribeAsync("Other", function(msg) table.insert(other, msg.Data) end)
    MessagingService:PublishAsync("Shout", "just text")
    MessagingService:PublishAsync("Shout", 42)
    check(waitFor(function() return #got >= 3 and #second >= 2 end), "two subscribers both get it")
    check(got[2] and got[2].Data == "just text" and got[3] and got[3].Data == 42, "text and numbers arrive as they were, in order")
    check(#other == 0, "other topics don't get it")

    -- Disconnect: no more messages for that callback (the other one still gets them).
    conn:Disconnect()
    check(conn.Connected == false, "Disconnect sets Connected to false")
    conn:Disconnect()   -- (twice is fine)
    MessagingService:PublishAsync("Shout", "after")
    check(waitFor(function() return #second >= 3 end), "the other subscriber still gets messages")
    task.wait(0.3)
    check(#got == 3, "a disconnected callback gets nothing", #got)
    conn2:Disconnect()
    conn3:Disconnect()

    -- Limits, with clear errors.
    check(fails_with("1%-80 letters", function() MessagingService:PublishAsync(string.rep("x", 81), 1) end), "topics over 80 letters are refused")
    check(fails_with("1%-80 letters", function() MessagingService:PublishAsync("", 1) end), "empty topics are refused")
    check(fails_with("1%-80 letters", function() MessagingService:SubscribeAsync(string.rep("y", 81), function() end) end),
          "subscribing to a too-long topic is refused")
    local ok80 = pcall(function() MessagingService:PublishAsync(string.rep("x", 80), 1) end)
    check(ok80, "an 80-letter topic is fine")
    local okBig = pcall(function() MessagingService:PublishAsync("Big", string.rep("a", 1022)) end)
    check(okBig, "a message of exactly 1 KB once encoded is fine")
    local tooBig, err = fails_with("1 KB", function() MessagingService:PublishAsync("Big", string.rep("a", 1023)) end)
    check(tooBig, "a message over 1 KB is refused", err)
    check(fails_with("numbers, text", function() MessagingService:PublishAsync("Bad", { f = function() end }) end),
          "functions can't be sent")
    check(fails_with("must be a function", function() MessagingService:SubscribeAsync("NoFn", 5) end),
          "SubscribeAsync needs a callback")

    -- Subscriptions: 20 + 8 per player (1 player here: 28).
    local conns = {}
    local made = 0
    for i = 1, 40 do
        local okSub, c = pcall(function() return MessagingService:SubscribeAsync("Many" .. i, function() end) end)
        if not okSub then
            check(tostring(c):find("subscriptions") ~= nil, "too many subscriptions gives a clear error", c)
            break
        end
        made = made + 1
        table.insert(conns, c)
    end
    check(made == 28, "a server with 1 player can hold 28 subscriptions", made)
    for _, c in ipairs(conns) do c:Disconnect() end
    local okAgain = pcall(function() MessagingService:SubscribeAsync("Many1", function() end):Disconnect() end)
    check(okAgain, "disconnecting frees subscriptions again")

    -- Sending: 150 + 60 per player a minute (210 here; a few were sent above already).
    local sent, limited, why = 0, false, nil
    for i = 1, 260 do
        local okSend, e = pcall(function() MessagingService:PublishAsync("Flood", i) end)
        if not okSend then limited, why = true, e break end
        sent = sent + 1
    end
    check(limited and tostring(why):find("too many messages") ~= nil, "sending too fast gives a clear error", why)
    check(sent > 150 and sent <= 210, "up to 210 a minute can be sent with 1 player", sent)

    -- Another server of the same game on this computer (if one was started alongside, see above).
    local fromPartner = {}
    local pc = MessagingService:SubscribeAsync("PartnerHello", function(msg) table.insert(fromPartner, msg.Data) end)
    if waitFor(function() return #fromPartner > 0 end, 6) then
        check(fromPartner[1].from == "partner", "a message from another running server arrives")
    else
        print("SKIP another server (start a partner copy to check this, see the top of the file)")
    end
    pc:Disconnect()

    print("MESSAGING TEST DONE (" .. fails .. " failed)")
end)
