-- Errors raised inside metamethods (stage 8) propagate to the nearest
-- pcall like any other error: from __index and __newindex functions,
-- arithmetic, comparison, concatenation, __len, __call, __eq, __close and
-- __tostring. error(msg, 2) inside a metamethod points at the line that
-- triggered it.

local strict = setmetatable({}, {
  __index = function(_, k) error("undefined field '" .. tostring(k) .. "'", 2) end,
  __newindex = function(_, k) error("cannot add field '" .. tostring(k) .. "'", 2) end,
})
print(pcall(function()
  return strict.missing      -- line 12
end))
print(pcall(function()
  strict.new = 1             -- line 15
end))
print(pcall(function() return strict[1] end))

-- Level 1 inside a metamethod points into the metamethod.
local m = setmetatable({}, {
  __add = function() error("no adding") end,          -- line 21
  __lt = function() error("no comparing", 0) end,
  __concat = function() error({reason = "concat"}) end,
  __len = function() error("no length", 0) end,
  __call = function(_, x) error("called with " .. tostring(x), 0) end,
  __eq = function() error("no equality", 0) end,
  __unm = function() local z = nil return z.field end, -- line 27
})
print(pcall(function() return m + 1 end))
print(pcall(function() return 1 + m end))
print(pcall(function() return m < m end))
print(pcall(function() return m > 1 end))
local ok, e = pcall(function() return "a" .. m end)
print(ok, type(e), e.reason)
print(pcall(function() return #m end))
print(pcall(m, 99))
print(pcall(function() return m == setmetatable({}, getmetatable(m)) end))
print(pcall(function() return m == m end))  -- same object: no call, no error
print(pcall(function() return -m end))

-- Errors in __tostring reach the caller of tostring or print.
local bad = setmetatable({}, {__tostring = function() error("cannot show", 0) end})
print(pcall(tostring, bad))
print(pcall(print, "before", bad))

-- An __index function that fails for some keys only.
local lazy = setmetatable({}, {__index = function(t, k)
  if type(k) ~= "number" then error("only numbers, got " .. type(k), 0) end
  return k * 2
end})
print(lazy[21], pcall(function() return lazy.name end))
print(pcall(function() return lazy[true] end))

-- An error in a metamethod leaves the objects usable.
local acc = setmetatable({total = 0}, {__add = function(a, n)
  if n < 0 then error("negative", 0) end
  a.total = a.total + n
  return a
end})
local _ = acc + 5
print(pcall(function() return acc + -1 end))
_ = acc + 10
print(acc.total)

-- A metamethod error inside nested pcalls is caught by the innermost.
print(pcall(function()
  local inner_ok, inner_e = pcall(function() return m + 1 end)
  return "outer fine", inner_ok, inner_e
end))

return acc.total
