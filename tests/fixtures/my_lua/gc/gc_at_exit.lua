-- Finalizers of objects still alive when the program ends (stage 7): they
-- run when the state is closed, after the program's results are printed,
-- in reverse order of their setmetatable calls; garbage that was never
-- collected is finalized then too. The collector is stopped, so nothing is
-- finalized before the end.

collectgarbage("stop")

local function finalizer(name)
  return {__gc = function() print("finalizing " .. name) end}
end

local first = setmetatable({}, finalizer("first (alive)"))
setmetatable({}, finalizer("garbage, never collected"))
local second = setmetatable({}, finalizer("second (alive)"))
_ENV.global_obj = setmetatable({}, finalizer("global (alive)"))

-- A finalizer at the end can still use the global environment and tables.
local data = {1, 2, 3}
local summer = setmetatable({}, {__gc = function()
  local s = 0
  for _, v in ipairs(data) do s = s + v end
  print("sum at exit", s)
end})

print("program body done")
return "returned", first ~= second
