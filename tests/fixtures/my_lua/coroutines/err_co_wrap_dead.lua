-- Calling the wrapper of a dead coroutine.
local gen = coroutine.wrap(function() return "only" end)
print(gen())
print(gen())
