-- An uncaught error from inside a wrapped coroutine.
local gen = coroutine.wrap(function()
  coroutine.yield(1)
  local t = nil
  return t.x
end)
print(gen())
print(gen())
