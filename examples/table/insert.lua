-- table.insert(list, [pos,] value): appends, or inserts at pos shifting the rest up.
local t = { "a", "c" }
table.insert(t, "d")                          -- append
table.insert(t, 2, "b")                       -- insert at position 2
print(table.concat(t, " "))                   --> a b c d
