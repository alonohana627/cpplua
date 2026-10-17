-- error levels (stage 8): level 1 (the default) adds the position where
-- error was called, level 2 the position of the call to the function that
-- called error, level 3 one further up; level 0 adds nothing. A level that
-- reaches a C function (pcall) or goes past the main chunk adds nothing.
-- Only string messages get a position.

local function check(x)
  if type(x) ~= "number" then
    error("expected a number, got " .. type(x), 2)
  end
  return x
end

local function caller()
  return check("oops")   -- a tail call: caller's frame is gone, so level 2
                         -- is pcall and there is no position
end
print(pcall(caller))

local function wrapper()
  local v = check({})    -- line 21: level 2 points here
  return v
end
print(pcall(wrapper))

-- Level 3 from a helper two calls deep.
local function validate(v) if not v then error("invalid", 3) end end
local function api(v) validate(v) end
local function user_code()
  api(false)             -- line 30: level 3 points here
end
print(pcall(user_code))

-- Levels 1 and 2 from the same function.
local function lvl(n) error("level " .. n, n) end
local function call_lvl(n)
  lvl(n)                 -- line 37
end
print(pcall(call_lvl, 1))
print(pcall(call_lvl, 2))
print(pcall(call_lvl, 0))

-- Level 2 when the caller is pcall itself (a C function): no position.
print(pcall(lvl, 2))
-- A level far beyond the stack: no position.
print(pcall(call_lvl, 50))

-- Non-string messages are never changed, whatever the level.
print(pcall(error, 42, 1))
print(pcall(function() error(42) end))
print(pcall(function() error(true, 2) end))
local t = {}
local ok, e = pcall(function() error(t, 1) end)
print(ok, e == t)

-- A numeric string is a string, so it gets a position.
print(pcall(function() error("42") end))

-- Positions inside methods and in a function defined on one line.
local obj = {}
function obj:fail() error("method failed") end
print(pcall(obj.fail, obj))
print(pcall(function() obj:fail() end))

-- Errors from a multi-line expression point at the line of the operator.
print(pcall(function()
  local a = {}
  local b = 1
  return b
    +
    a
end))

return "done"
