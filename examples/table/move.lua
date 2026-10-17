-- table.move(a1, f, e, t [, a2]): copies a1[f..e] to a2[t..]; returns a2.
local src = { 1, 2, 3, 4, 5 }
local dst = table.move(src, 2, 4, 1, {})
print(table.concat(dst, " "))                 --> 2 3 4

table.move(src, 1, 3, 3)                      -- overlapping move within one table
print(table.concat(src, " "))                 --> 1 2 1 2 3
