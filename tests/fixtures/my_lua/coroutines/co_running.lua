-- coroutine.running and coroutine.isyieldable, in the main thread and in coroutines.
local main, ismain = coroutine.running()
print(type(main), ismain)
print(coroutine.isyieldable())
print(coroutine.isyieldable(main))
local co
co = coroutine.create(function()
  local me, m = coroutine.running()
  print("inside", type(me), m, me == co, me == main)
  print("yieldable", coroutine.isyieldable(), coroutine.isyieldable(main), coroutine.isyieldable(co))
  print("in pcall", pcall(coroutine.isyieldable))
  table.sort({3, 2, 1}, function(a, b)
    if a == 3 then print("in sort", coroutine.isyieldable()) end
    return a < b
  end)
  return me
end)
print(coroutine.isyieldable(co))
local ok, me = coroutine.resume(co)
print(ok, me == co)
print(coroutine.isyieldable(co))
-- wrap: running gives the hidden coroutine
local w = coroutine.wrap(function()
  local me, m = coroutine.running()
  coroutine.yield(type(me), m, coroutine.status(me))
  return me == main
end)
print(w())
print(w())
return type(main), select("#", coroutine.running())
