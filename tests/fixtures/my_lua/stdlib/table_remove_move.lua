-- table.remove and table.move (stage 9): remove from the end, the middle,
-- the front, an empty table (also at positions 0 and #t+1), and its bounds
-- error; move within a table in both overlapping directions, into another
-- table, with empty ranges, returning the destination, and its errors.

local function show(t, n) local out = {} for i = 1, n or #t do out[i] = tostring(t[i]) end return table.concat(out, " ") end

local t = {"a", "b", "c", "d", "e"}
print(table.remove(t), show(t))
print(table.remove(t, 2), show(t))
print(table.remove(t, 1), show(t))
print(table.remove(t, #t), show(t))
print(table.remove(t, #t + 1), show(t))
print(table.remove(t), show(t), #t)
print(table.remove(t), #t)

-- Removing from an empty table returns nil (positions 0 and #t+1 are allowed).
local empty = {}
print(table.remove(empty), table.remove(empty, 0), table.remove(empty, 1), #empty)
print(select("#", table.remove({})))
local zero = {[0] = "z"}
print(table.remove(zero, 0), zero[0])
print(pcall(table.remove, {1, 2, 3}, 5))
print(pcall(table.remove, {1, 2, 3}, -1))
print(pcall(table.remove, {}, 2))
print(pcall(table.remove, {1}, "x"))
print(pcall(table.remove))

-- Removing in a loop empties the table.
local stack = {1, 2, 3, 4}
local popped = {}
while #stack > 0 do popped[#popped + 1] = table.remove(stack) end
print(show(popped), #stack)

-- move within one table, shifting right (overlapping).
local m = {1, 2, 3, 4, 5}
print(table.move(m, 1, 3, 3) == m, show(m))
-- shifting left (overlapping).
m = {1, 2, 3, 4, 5}
table.move(m, 3, 5, 1)
print(show(m))
-- into another table.
local src, dst = {"x", "y", "z"}, {"1", "2", "3", "4"}
print(table.move(src, 1, 3, 2, dst) == dst, show(dst), show(src))
-- empty range: nothing happens.
print(show(table.move({1, 2}, 2, 1, 1, {9, 9})))
-- moving to negative and zero positions.
local neg = table.move({"a", "b"}, 1, 2, -1, {})
print(neg[-1], neg[0], neg[1])
-- the source range can include nils.
local holes = table.move({1, nil, 3}, 1, 3, 1, {})
print(holes[1], holes[2], holes[3])
-- move uses __index and __newindex.
local writes = {}
local proxy = setmetatable({}, {__newindex = function(_, k, v) writes[#writes + 1] = k .. ":" .. v end})
table.move(setmetatable({}, {__index = function(_, k) return k * 10 end}), 1, 3, 5, proxy)
print(table.concat(writes, " "))

print(pcall(table.move, {}, 1, 2))
print(pcall(table.move, {}, 1, 2, 1, "x"))
print(pcall(table.move, {}, 1, math.maxinteger, 2))
print(pcall(table.move, {}, -1, math.maxinteger, 1))
print((pcall(table.move, {1}, 1, 1, math.maxinteger)))
print(pcall(table.move, 1, 1, 1, 1))

return show(m), show(dst)
