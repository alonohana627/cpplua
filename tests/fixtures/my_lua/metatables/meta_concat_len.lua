-- __concat and __len (stage 7). __concat is called when either operand is
-- not a string or number, with the operands in their original order; in a
-- chain a .. b .. c (right associative) the plain string parts are joined
-- first. __len replaces the length of a table, with any result type.

local function name(x)
  if type(x) == "table" then return x.name end
  return tostring(x)
end
local mt = {__concat = function(a, b) return "[" .. name(a) .. "+" .. name(b) .. "]" end}
local function V(n) return setmetatable({name = n}, mt) end
local a, b = V("a"), V("b")

print(a .. b)
print(a .. "s", "s" .. a, a .. 1, 2 .. a, a .. 1.5)
print("x" .. "y" .. a)       -- "y" .. a first, then "x" .. result
print(a .. "x" .. "y")       -- "x" .. "y" first, then a .. "xy"
print("x" .. a .. "y")
print(a .. b .. a)
print("p" .. "q" .. a .. "r" .. "s")
print(1 .. 2 .. a .. 3 .. 4)

-- The metamethod's result can be a table that has __concat too.
local Rope = {}
local function rope(s) return setmetatable({s = s}, Rope) end
local function str(x) if type(x) == "table" then return x.s end return tostring(x) end
Rope.__concat = function(p, q) return rope(str(p) .. str(q)) end
local r = rope("ab") .. "cd" .. rope("ef") .. 7
print(getmetatable(r) == Rope, r.s)

-- Errors without a metamethod.
print(pcall(function() return "a" .. {} end))
print(pcall(function() local t = {} return t .. "a" end))
print(pcall(function() return "a" .. nil end))
print(pcall(function() local f = true return 1 .. f end))

-- __len.
local sized = setmetatable({1, 2, 3}, {__len = function(t) return 42 end})
print(#sized, rawlen(sized))
local stringy = setmetatable({}, {__len = function() return "long" end})
print(#stringy)
local multi = setmetatable({}, {__len = function() return 1, 2 end})
print(#multi)
-- __len receives the table (and the table again as a second operand).
local got = setmetatable({}, {__len = function(t, u) return rawequal(t, u) end})
print(#got)
-- A proxy forwarding the length of another table.
local data = {10, 20, 30, 40}
local proxy = setmetatable({}, {__index = data, __len = function() return #data end})
local s = 0
for i = 1, #proxy do s = s + proxy[i] end
print(#proxy, s)

-- rawlen on tables and strings.
print(rawlen({1, 2}), rawlen("abc"), pcall(rawlen, 5))

return #sized, a .. "end"
