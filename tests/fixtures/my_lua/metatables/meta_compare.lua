-- Comparison metamethods (stage 7). __eq is only tried for two different
-- tables (not for identical ones, not for rawequal, not against other types)
-- and its result becomes a boolean; ~= is its negation. __lt and __le get
-- the operands in order; a > b is b < a and a >= b is b <= a. The reference
-- Lua here is built with LUA_COMPAT_5_3, so a missing __le falls back to
-- not (b < a) using __lt. Mixed types without metamethods fail.

local eq_calls = 0
local mt = {}
mt.__eq = function(x, y)
  eq_calls = eq_calls + 1
  return x.v == y.v and "yes" or nil  -- any value: converted to a boolean
end
local function V(v) return setmetatable({v = v}, mt) end
local a, b, c = V(1), V(1), V(2)

print(a == b, a == c, a ~= b, a ~= c, eq_calls)
print(a == a, rawequal(a, b), rawequal(a, a), eq_calls)  -- no calls
print(a == 1, 1 == a, a == "1", a == nil, eq_calls)      -- no calls
-- Only one operand needs __eq; the left one is tried first.
local plain = {v = 1}
print(a == plain, plain == a, eq_calls)
local other_mt = {__eq = function() return true end}
local o = setmetatable({}, other_mt)
print(o == a, a == o)

-- Tables in a list compared with ==.
local list = {V(5), V(6), V(7)}
local function index_of(t, x)
  for i, e in ipairs(t) do if e == x then return i end end
  return nil
end
print(index_of(list, V(6)), index_of(list, V(9)))

-- __lt and __le.
local log = {}
local ord = {}
ord.__lt = function(x, y) log[#log + 1] = "lt" return x.v < y.v end
ord.__le = function(x, y) log[#log + 1] = "le" return x.v <= y.v end
local function N(v) return setmetatable({v = v}, ord) end
local one, two, two2 = N(1), N(2), N(2)
print(one < two, two < one, one <= two, two <= two2, two > one, one >= two)
print(#log, log[1], log[2], log[3], log[4], log[5], log[6])

-- With a number on one side, the metamethod still gets both operands.
local mixed = setmetatable({}, {
  __lt = function(x, y) return type(x) .. "<" .. type(y) end,
  __le = function(x, y) return false end,
})
print(mixed < 1, 1 < mixed, mixed <= 1, 1 >= mixed)

-- A missing __le falls back to not (y < x) with __lt (LUA_COMPAT_LT_LE).
local only_lt = {__lt = function(x, y) return x.v < y.v end}
local p, q = setmetatable({v = 1}, only_lt), setmetatable({v = 2}, only_lt)
print(pcall(function() return p <= q, q <= p, p >= q end))

-- Without __le or __lt the comparison fails.
local nothing = setmetatable({}, {__eq = function() return true end})
print(pcall(function() return nothing < nothing end))
print(pcall(function() return nothing <= {} end))

-- Mixed types without metamethods.
print(pcall(function() return 1 < "2" end))
print(pcall(function() return "a" <= 1 end))
print(pcall(function() return {} < 1 end))
print(pcall(function() local t = {} return t > t end))
print(pcall(function() return nil < 1 end))
print(pcall(function() return true <= false end))

-- Sorting with a hand-written insertion sort that uses <.
local items = {N(5), N(3), N(9), N(1), N(7)}
for i = 2, #items do
  local x, j = items[i], i - 1
  while j >= 1 and x < items[j] do items[j + 1] = items[j] j = j - 1 end
  items[j + 1] = x
end
local out = ""
for _, it in ipairs(items) do out = out .. it.v .. " " end
print(out)

return a == b, one < two
