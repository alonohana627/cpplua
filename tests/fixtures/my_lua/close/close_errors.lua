-- To-be-closed variables and errors (stage 7): when an error passes through
-- a block, its __close functions run with the error object as the second
-- argument, innermost first, and the error goes on to pcall. An error inside
-- a __close replaces the original one. A value without __close is an error
-- at the declaration.

local log = {}
local function say(s) log[#log + 1] = s end
local function closer(name)
  return setmetatable({}, {__close = function(_, err)
    say(name .. " got " .. tostring(err))
  end})
end
local function flush()
  for i, s in ipairs(log) do print(i, s) end
  log = {}
end

-- A string error passes through two variables.
print(pcall(function()
  local a <close> = closer("a")
  local b <close> = closer("b")
  error("boom", 0)
end))
flush()

-- A runtime error with a position.
print(pcall(function()
  local c <close> = closer("c")
  local t = nil
  return t.x
end))
flush()

-- A table error object reaches the close function and pcall unchanged.
local err_obj = {code = 7}
local seen
local ok, e = pcall(function()
  local d <close> = setmetatable({}, {__close = function(_, err) seen = err end})
  error(err_obj)
end)
print(ok, e == err_obj, seen == err_obj, e.code)

-- Only the variables already declared are closed.
print(pcall(function()
  local first <close> = closer("first")
  error("early", 0)
  local second <close> = closer("second")
end))
flush()

-- Errors through nested functions close every level.
local function level3() local l3 <close> = closer("l3") error("deep", 0) end
local function level2() local l2 <close> = closer("l2") level3() end
local function level1() local l1 <close> = closer("l1") level2() end
print(pcall(level1))
flush()

-- An error in a __close replaces the error being propagated; the other
-- close functions see the new error.
print(pcall(function()
  local outer <close> = closer("outer")
  local bad <close> = setmetatable({}, {__close = function(_, err)
    say("bad got " .. tostring(err))
    error("from close", 0)
  end})
  error("original", 0)
end))
flush()

-- An error in a __close on a normal exit.
print(pcall(function()
  local after <close> = closer("after")
  local bad <close> = setmetatable({}, {__close = function() error("close failed", 0) end})
  return "not returned"
end))
flush()

-- A value without __close.
print(pcall(function() local x <close> = 42 end))
print(pcall(function() local y <close> = {} end))
print(pcall(function()
  local good <close> = closer("good")
  local z <close> = setmetatable({}, {})
end))
flush()

-- pcall inside a block with a to-be-closed variable: the inner error is
-- caught and the variable is closed normally later.
do
  local keep <close> = closer("keep")
  print(pcall(error, "inner", 0))
  say("still running")
end
flush()

return "done"
