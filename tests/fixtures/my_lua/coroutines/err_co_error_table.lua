-- An uncaught table error from a wrapped coroutine reaches the top unchanged.
local gen = coroutine.wrap(function()
  coroutine.yield("first")
  error({code = 1})
end)
print(gen())
gen()
