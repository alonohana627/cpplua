-- table.sort (stage 9): numbers (integers and floats mixed), strings,
-- descending order through a comparator, sorting records by a unique key,
-- already sorted, reversed and small tables, __lt on the elements, __index
-- and __len on the table, and the errors for values that cannot be compared.
-- Every sorted result here is unique, so the output does not depend on the
-- (unstable) algorithm.

local function show(t) local out = {} for i = 1, #t do out[i] = tostring(t[i]) end return table.concat(out, " ") end

local nums = {5, 2, 8, 1, 9, 3, 7, 4, 6, 10}
table.sort(nums)
print(show(nums))
local mixed = {3.5, 1, -2, 2.25, 0, -0.5, 100, 1e10, -1e10}
table.sort(mixed)
print(show(mixed))
local words = {"pear", "apple", "fig", "banana", "Cherry", "apricot", ""}
table.sort(words)
print(show(words))
table.sort(words, function(a, b) return a > b end)
print(show(words))
table.sort(words, function(a, b) return #a < #b or (#a == #b and a < b) end)
print(show(words))

-- Records by a unique key.
local people = {{name = "Ann", age = 31}, {name = "Bob", age = 25}, {name = "Cid", age = 40}, {name = "Dee", age = 19}}
table.sort(people, function(a, b) return a.age < b.age end)
local names = {}
for i, person in ipairs(people) do names[i] = person.name .. person.age end
print(table.concat(names, " "))

-- Edge sizes and orders.
local one, none = {42}, {}
table.sort(one) table.sort(none)
print(show(one), #none)
local sorted, reversed = {}, {}
for i = 1, 50 do sorted[i] = i; reversed[i] = 51 - i end
table.sort(sorted) table.sort(reversed)
print(show(sorted) == show(reversed), sorted[1], sorted[50], reversed[25])
local big = {}
for i = 1, 200 do big[i] = (i * 7919) % 1009 end
table.sort(big)
local ok = true
for i = 2, #big do if big[i - 1] >= big[i] then ok = false end end
print(ok, big[1], big[100], big[200])
table.sort(big, function(a, b) return a > b end)
print(big[1], big[200])

-- Elements with __lt.
local mt = {__lt = function(a, b) return a.v < b.v end}
local objs = {}
for i, v in ipairs({30, 10, 20}) do objs[i] = setmetatable({v = v}, mt) end
table.sort(objs)
print(objs[1].v, objs[2].v, objs[3].v)

-- A table with __index and __len.
local backing = {"c", "a", "b"}
local proxy = setmetatable({}, {
  __index = backing,
  __newindex = backing,
  __len = function() return #backing end,
})
table.sort(proxy)
print(show(backing))

-- Errors.
print(pcall(table.sort, {true, false}))
print(pcall(table.sort, {{}, {}}))
print(pcall(table.sort, {1, 2}, 3))
print(pcall(table.sort, "abc"))
print(pcall(table.sort, {3, 1, 2}, function(a, b) error("cmp failed", 0) end))

return show(nums)
