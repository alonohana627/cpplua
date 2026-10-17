-- coroutine.wrap: generators, and errors propagated with a position.
local function range(n)
  return coroutine.wrap(function()
    for i = 1, n do coroutine.yield(i) end
  end)
end
local gen = range(3)
print(gen(), gen(), gen())
print(select("#", gen()))
print(pcall(gen))
print(pcall(gen))
-- an error inside: the message gets the position of the call to the wrapper
local w = coroutine.wrap(function(x)
  coroutine.yield(x)
  error("boom")
end)
print(w(10))
print(pcall(w))
print(pcall(w))
-- a non-string error object passes unchanged
local t = {}
local wt = coroutine.wrap(function() error(t) end)
local ok, e = pcall(wt)
print(ok, e == t)
local wn = coroutine.wrap(function() error(nil) end)
print(pcall(wn))
local w2 = coroutine.wrap(function() error(42) end)
print(pcall(w2))
local w3 = coroutine.wrap(function() error("no position", 0) end)
print(pcall(w3))
local w4 = coroutine.wrap(function() local x = nil; return x.field end)
print(pcall(w4))
-- wrap inside wrap: each level adds its position
local outer = coroutine.wrap(function()
  local inner = coroutine.wrap(function() error("deep") end)
  inner()
end)
print(pcall(outer))
-- values through a wrapper
local echo = coroutine.wrap(function(...)
  local args = table.pack(...)
  while true do
    args = table.pack(coroutine.yield(args.n, table.unpack(args, 1, args.n)))
  end
end)
print(echo(1, nil, 3))
print(echo())
print(echo(nil))
return echo("a", "b")
