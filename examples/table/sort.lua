-- table.sort(list [, comp]): sorts in place, with < or a custom comparison.
local nums = { 5, 2, 8, 1, 9 }
table.sort(nums)
print(table.concat(nums, " "))                --> 1 2 5 8 9

table.sort(nums, function(a, b) return a > b end)
print(table.concat(nums, " "))                --> 9 8 5 2 1

local people = { { name = "Bo", age = 40 }, { name = "Al", age = 25 }, { name = "Cy", age = 33 } }
table.sort(people, function(a, b) return a.age < b.age end)
for _, p in ipairs(people) do io.write(p.name, " ") end
print()                                       --> Al Cy Bo
