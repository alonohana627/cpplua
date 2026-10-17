-- A new coroutine gets the hook mask and count of the thread that creates
-- it, but not its hook function (lua_newthread; issue #12).
local function h() end
debug.sethook(h, "l", 7)
local co = coroutine.create(function() end)
print(debug.gethook(co) == nil, select(2, debug.gethook(co)))
debug.sethook()
local co2 = coroutine.create(function() end)
print(debug.gethook(co2))
