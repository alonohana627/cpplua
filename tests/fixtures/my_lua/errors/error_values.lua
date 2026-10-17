-- Error objects of every type (stage 8): pcall returns exactly the value
-- given to error, tables by identity, numbers, booleans and nil unchanged
-- (no position is added). A table with __tostring can be printed through
-- tostring. Messages built with tostring of nil, booleans and numbers.

local function catch(v, level)
  return select(2, pcall(error, v, level))
end

print(catch(42), catch(-1.5), catch(2^63))
print(catch(true), catch(false), catch(nil))
print(type(catch(42)), type(catch(nil)), type(catch("s")))

-- Tables keep their identity and fields.
local err = {code = 404, reason = "not found"}
local got = catch(err)
print(got == err, got.code, got.reason)

-- An error class with __tostring.
local Error = {}
Error.__index = Error
Error.__tostring = function(e) return "Error[" .. e.code .. "]: " .. e.msg end
local function throw(code, msg) error(setmetatable({code = code, msg = msg}, Error)) end
local ok, e = pcall(throw, 500, "server failure")
print(ok, tostring(e), e.code, getmetatable(e) == Error)
print(e)  -- print uses __tostring too

-- Rethrowing keeps the same object.
local ok2, e2 = pcall(function()
  local _, inner = pcall(throw, 1, "first")
  error(inner)
end)
print(ok2, e2.msg, tostring(e2))

-- Dispatching on the error type.
local function handle(f)
  local ok3, x = pcall(f)
  if ok3 then return "ok" end
  if type(x) == "table" and getmetatable(x) == Error then return "Error " .. x.code end
  if type(x) == "string" then return "string: " .. x end
  return "other: " .. tostring(x)
end
print(handle(function() end))
print(handle(function() throw(7, "seven") end))
print(handle(function() error("plain", 0) end))
print(handle(function() error(3.25) end))
print(handle(function() error() end))
print(handle(function() local n = nil; return n.x end))

-- Messages built with tostring and concatenation.
local function describe(v) error("bad value: " .. tostring(v), 0) end
print(pcall(describe, nil))
print(pcall(describe, true))
print(pcall(describe, 10))
print(pcall(describe, 10.0))
print(pcall(describe, -0.0))
print(pcall(describe, 1e308 * 10))
print(pcall(describe, -(1e308 * 10)))
print(pcall(describe, 2^-1074))
print(pcall(describe, 9007199254740993))
print(pcall(describe, "text"))
print(pcall(function() error("count: " .. 3 .. ", ratio: " .. 0.5, 0) end))

-- The error value in a to-be-closed variable's close function and in
-- pcall is the same object.
local seen
local ok4, e4 = pcall(function()
  local c <close> = setmetatable({}, {__close = function(_, x) seen = x end})
  error(err)
end)
print(ok4, e4 == err, seen == err)

return catch(nil), catch(0)
