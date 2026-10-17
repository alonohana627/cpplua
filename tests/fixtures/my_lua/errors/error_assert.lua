-- assert (stage 8): returns all its arguments when the first is true (any
-- value but nil and false); otherwise raises its second argument as the
-- error object, or "assertion failed!" when there is none. It raises it the
-- way error(msg) does at level 1: a string message gets the position of the
-- Lua code that called assert (none when assert is called by pcall itself),
-- other values are kept unchanged.

print(assert(1, "unused", 3))
print(assert(true))
print(assert(0, nil, nil))
print(assert("", {}) == "")
print(select("#", assert(1, nil, nil, nil)))

print(pcall(assert, false))
print(pcall(assert, nil))
print(pcall(assert, false, "custom message"))
print(pcall(assert, nil, "custom"))
print(pcall(assert))
print(pcall(function() assert(false) end))
print(pcall(function() assert(1 == 2, "math is broken") end))
print(pcall(function() assert(nil, 42) end))

-- The message can be any value, kept as is.
local obj = {}
local ok, e = pcall(assert, false, obj)
print(ok, e == obj)
local ok2, e2 = pcall(function() assert(false, setmetatable({}, {__tostring = function() return "custom object" end})) end)
print(ok2, tostring(e2))
print(pcall(assert, false, nil))
print(pcall(assert, false, false))

-- The usual idiom: assert on a function's results.
local function lookup(t, k)
  local v = t[k]
  if v == nil then return nil, "no key " .. tostring(k) end
  return v
end
local t = {a = 1}
print(assert(lookup(t, "a")))
print(pcall(function() return assert(lookup(t, "b")) end))

-- assert in an expression.
local n = assert(tonumber_missing == nil and 5)
print(n * 2)

return assert("returned", "unused")
