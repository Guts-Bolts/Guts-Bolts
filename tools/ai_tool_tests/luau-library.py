TITLE = 'The rest of the Luau library works in run_lua (bit32, buffer, table.freeze, debug.traceback)'


def last(d, t):
    return d.get("output", [""])[-1].split() if t.ok(d) else []


def run(m, t):
    d = m.call("run_lua", code='print(bit32.band(0xFF, 0x0F), bit32.lshift(1, 4))')
    t.check(last(d, t) == ["15", "16"], "bit32 works", d)

    d = m.call("run_lua", code='local b = buffer.create(4) buffer.writeu32(b, 0, 123456) print(buffer.readu32(b, 0), buffer.len(b))')
    t.check(last(d, t) == ["123456", "4"], "a buffer keeps a number", d)

    d = m.call("run_lua", code='local f = table.freeze({1}) print(table.isfrozen(f), (pcall(function() f[2] = 2 end)), #table.clone({1, 2}))')
    t.check(last(d, t) == ["true", "false", "2"], "table.freeze and table.clone", d)

    d = m.call("run_lua", code='local ok, msg = xpcall(function() error("boom") end, debug.traceback) print(ok, msg:find("traceback") ~= nil)')
    t.check(last(d, t) == ["false", "true"], "xpcall with debug.traceback", d)

    d = m.call("run_lua", code='print(10 / 2)')
    t.check(last(d, t) == ["5"], "10 / 2 prints as 5", d)
