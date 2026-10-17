-- next, pairs, ipairs and select (stage 6). The order next and pairs visit
-- non-sequence keys in is not defined, so this program only counts, sums and
-- checks membership for those; ipairs order is defined.

-- next on an empty table, and next(t) on a one-entry table.
print(next({}))
local one = {only = 42}
local k, v = next(one)
print(k, v, next(one, k))

-- pairs visits every entry exactly once.
local t = {10, 20, 30, x = 1, y = 2, z = 3, [2.5] = 4, [true] = 5}
local count, vsum, seen = 0, 0, {}
for key, val in pairs(t) do
  count = count + 1
  vsum = vsum + val
  seen[key] = (seen[key] or 0) + 1
end
print(count, vsum, seen[1], seen[2], seen[3], seen.x, seen.y, seen.z, seen[2.5], seen[true])

-- The same with a manual next loop.
local n2, key = 0, nil
repeat
  key = next(t, key)
  if key ~= nil then n2 = n2 + 1 end
until key == nil
print(n2)

-- pairs returns next, the table and nil.
local f, s, init = pairs(t)
print(f == next, s == t, init)

-- Assigning nil to existing fields during traversal is allowed.
local clear = {a = 1, b = 2, c = 3, d = 4, 5, 6}
for key in pairs(clear) do clear[key] = nil end
print(next(clear))

-- Changing values of existing fields during traversal is allowed too.
local dbl = {a = 1, b = 2, c = 3}
for key, val in pairs(dbl) do dbl[key] = val * 2 end
print(dbl.a, dbl.b, dbl.c)

-- ipairs goes 1, 2, ... and stops at the first nil.
local seq = {"a", "b", "c", nil, "e"}
for i, val in ipairs(seq) do print(i, val) end
local f2, s2, i2 = ipairs(seq)
print(s2 == seq, i2, f2(seq, 0))
print(f2(seq, 3))
local hits = 0
for _ in ipairs({}) do hits = hits + 1 end
for _ in ipairs({nil, 2}) do hits = hits + 1 end
print(hits)
for i, val in ipairs({false, false}) do print(i, val) end

-- select.
print(select("#"))
print(select("#", nil, nil))
print(select("#", 1, nil, 3, nil))
print(select(1, "a", "b", "c"))
print(select(2, "a", "b", "c"))
print(select(3, "a", "b", "c"))
print(select(4, "a", "b", "c"))
print(select(-1, "a", "b", "c"))
print(select(-2, "a", "b", "c"))
print(select(-3, "a", "b", "c"))
print(select(2.0, "x", "y"))
print(pcall(select, 0, "a"))
print(pcall(select, -4, "a", "b", "c"))

local function count_args(...) return select("#", ...) end
local function last_arg(...) return (select(-1, ...)) end
print(count_args(), count_args(nil), count_args(1, 2, 3), last_arg(1, 2, 3))

-- A vararg sum through select.
local function sum(...)
  local total = 0
  for i = 1, select("#", ...) do total = total + (select(i, ...)) end
  return total
end
print(sum(), sum(1), sum(1, 2, 3, 4, 5))

return select("#", 1, 2), select(2, "p", "q")
