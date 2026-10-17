-- To-be-closed variables in coroutines: __close methods that yield, on normal exit, break, return and error.
local Y = coroutine.yield
local function yclose(name)
  return setmetatable({}, {__close = function(_, e)
    local r = Y("closing " .. name, e)
    print("closed", name, r)
  end})
end
local co = coroutine.create(function()
  do
    local a <close> = yclose("a")
    local b <close> = yclose("b")
    print("block body")
  end
  for i = 1, 3 do
    local c <close> = yclose("c" .. i)
    if i == 2 then break end
  end
  local function f()
    local d <close> = yclose("d")
    return "f result", 2
  end
  print("f returned", f())
  local ok, e = pcall(function()
    local g <close> = yclose("g")
    error("pcall error", 0)
  end)
  print("pcall", ok, e)
  local h <close> = yclose("h")
  return "body done"
end)
local n = 0
local ok, a, b = coroutine.resume(co)
while coroutine.status(co) == "suspended" do
  n = n + 1
  print("yielded", a, b)
  ok, a, b = coroutine.resume(co, n)
end
print(ok, a, b)
-- coroutine.close with a __close that yields: the yield is an error
local co2 = coroutine.create(function()
  local x <close> = setmetatable({}, {__close = function() Y("cannot") end})
  Y("first")
end)
print(coroutine.resume(co2))
print(coroutine.close(co2))
-- a __close that yields while the coroutine unwinds an error
local co3 = coroutine.create(function()
  local z <close> = yclose("z")
  error("unwinding", 0)
end)
print(coroutine.resume(co3))
print(coroutine.resume(co3, "resumed"))
print(coroutine.status(co3))
return n
