-- A coroutine resuming another: yields go to the nearest resumer.
local inner = coroutine.create(function(a)
  print("inner start", a)
  local b = coroutine.yield("inner y1", a * 2)
  print("inner got", b)
  local c = coroutine.yield("inner y2")
  return "inner done", c
end)
local outer = coroutine.create(function(x)
  print("outer start", x)
  print("resume inner", coroutine.resume(inner, x + 1))
  local y = coroutine.yield("outer y1")
  print("outer got", y)
  print("resume inner", coroutine.resume(inner, y))
  print("resume inner", coroutine.resume(inner, "last"))
  print("resume inner", coroutine.resume(inner))
  return "outer done"
end)
print("main", coroutine.resume(outer, 10))
print("main", coroutine.resume(outer, "Y"))
print("main", coroutine.status(outer), coroutine.status(inner))
-- a chain of coroutines, each one resuming the next
local function chain(n)
  if n == 0 then
    return coroutine.create(function(v) coroutine.yield(v + 1); return v + 100 end)
  end
  local next = chain(n - 1)
  return coroutine.create(function(v)
    local ok, r = coroutine.resume(next, v * 2)
    coroutine.yield(r)
    local ok2, r2 = coroutine.resume(next)
    return r2 + n
  end)
end
local top = chain(5)
print(coroutine.resume(top, 1))
print(coroutine.resume(top))
-- an error in the inner one does not kill the outer one
local outer2 = coroutine.wrap(function()
  local bad = coroutine.create(function() coroutine.yield(1); error("inner failure") end)
  print(coroutine.resume(bad))
  print(coroutine.resume(bad))
  coroutine.yield("outer still alive")
  return coroutine.status(bad)
end)
print(outer2())
return outer2()
