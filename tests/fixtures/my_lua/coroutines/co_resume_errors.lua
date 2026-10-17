-- Resuming coroutines that cannot be resumed, and argument errors.
local co = coroutine.create(function() return 1 end)
print(coroutine.resume(co))
print(coroutine.resume(co))
print(coroutine.resume(co, "again"))
local self
self = coroutine.create(function()
  print("self", coroutine.resume(self))
  return "ok"
end)
print(coroutine.resume(self))
local outer, inner
inner = coroutine.create(function()
  print("normal", coroutine.resume(outer))
end)
outer = coroutine.create(function() coroutine.resume(inner) end)
print(coroutine.resume(outer))
print(coroutine.resume(coroutine.running()))
-- argument errors
print(pcall(coroutine.create))
print(pcall(coroutine.create, 1))
print(type(coroutine.create(print)))
print(pcall(coroutine.resume))
print(pcall(coroutine.resume, {}))
print(pcall(coroutine.resume, function() end))
print(pcall(coroutine.status, nil))
print(pcall(coroutine.status, "co"))
print(pcall(coroutine.wrap, 42))
print(pcall(coroutine.close, 1))
print(pcall(coroutine.isyieldable, 1))
print(pcall(coroutine.yield, 1))
-- a coroutine of a C function
local cf = coroutine.create(print)
print(coroutine.resume(cf, "from", "print"))
print(coroutine.status(cf))
local sel = coroutine.wrap(select)
print(sel(2, "a", "b", "c"))
print(pcall(sel, 1))
return "end"
