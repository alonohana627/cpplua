-- pcall (stage 8): true plus all the results on success; false plus the
-- error object on failure, for runtime errors (with "file:line:"), for
-- error() with a string, and for errors raised by library functions (with
-- the position of the Lua code that called them). pcall itself needs a
-- value to call; calling a non-function fails inside the protected call.

print(pcall(function() return 1, 2, 3 end))
print(pcall(function() end))
print(select("#", pcall(function() return nil, nil end)))
print(pcall(function(...) return select("#", ...), ... end, "a", nil, "c"))

-- Runtime errors.
print(pcall(function() local x = nil return x.y end))
print(pcall(function() return 1 + {} end))
print(pcall(function() return #5 end))
print(pcall(function() return 1 < "x" end))
print(pcall(function() return ("x")() end))
print(pcall(function() return 1 // 0 end))
print(pcall(function() return 2^53 | 0, 1.5 | 0 end))

-- error with a string: level 1 adds the position of the error call.
print(pcall(error, "direct"))  -- called from pcall (C): no position
print(pcall(function() error("level one") end))
print(pcall(function() error("level one", 1) end))
print(pcall(function() error("no position", 0) end))
print(pcall(function() error() end))

-- Library errors from Lua code get the position of the call.
print(pcall(function() return setmetatable(1, {}) end))
print(pcall(function() return select(0) end))
print(pcall(setmetatable, 1, {}))

-- Results of pcall used in expressions.
local ok, msg = pcall(error, "x", 0)
print(ok, msg, type(ok), type(msg))
local ok2 = pcall(function() end)
print(ok2)

-- pcall of non-functions.
print(pcall(42))
print(pcall(nil))
print(pcall({}))
print(pcall("string"))
print(pcall(pcall))
print(pcall(pcall, pcall))
-- pcall with no arguments is an error raised by pcall itself.
print(pcall(function() return pcall() end))

-- Nested pcalls: each level catches its own error.
print(pcall(function()
  local inner_ok, inner_msg = pcall(error, "inner", 0)
  print("inside", inner_ok, inner_msg)
  error("outer", 0)
end))
print(pcall(pcall, error, "twice"))
print(pcall(pcall, error))

-- Errors in deeply nested calls unwind to the nearest pcall.
local depth = 0
local function dive(n)
  depth = depth + 1
  if n == 0 then error("bottom", 0) end
  return dive(n - 1) + 1
end
print(pcall(dive, 50), depth)

-- The program goes on normally after a caught error, also with locals and
-- upvalues of the failed function.
local counter = 0
local function bump_and_fail() counter = counter + 1 error("fail " .. counter, 0) end
for _ = 1, 3 do pcall(bump_and_fail) end
print(counter, pcall(bump_and_fail))

return pcall(function() return "last" end)
