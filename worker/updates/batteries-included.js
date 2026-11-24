export default {
  id: 'batteries-included', name: 'Batteries Included', time: 1795498930, tag: 'Engine',
  summary: 'Scripts written for Roblox now find the whole Luau library: bit32, buffer, table.freeze, debug.traceback and more.',
  items: [
    'bit32, buffer and vector work, so scripts that pack data into bytes or do bit maths no longer stop with "attempt to index a nil value"',
    'table.clone, table.create, table.freeze and table.isfrozen, math.sign and math.map, utf8.graphemes and string.split are there',
    'xpcall(fn, debug.traceback) works and shows where an error came from; debug.info and debug.profilebegin work too',
    'task.spawn and task.delay give back the thread, and task.cancel stops it',
    'Numbers print like Roblox: 10 / 2 shows as 5, not 5.0, so "Coins: 5" comes out right',
    'Vector3, Vector2, CFrame and Color3 got their missing methods (Abs, Floor, Max, FuzzyEq, Angle, ToHSV, ToHex, CFrame.lookAlong and more)',
    'New small types: Axes, Faces, Rect, Region3, Font, PathWaypoint, Vector3int16 and DateTime formatting like "LLL"',
    'Parallel Luau scripts run: Actors, SendMessage / BindToMessage, ConnectParallel and SharedTable (one at a time, same results)',
    'Old globals scripts still use are back: unpack, getfenv, loadstring, newproxy, gcinfo, shared, settings() and UserSettings()',
  ],
};
