-- table.remove(list [, pos]): removes and returns list[pos] (default: the last element).
local t = { "a", "b", "c", "d" }
print(table.remove(t))                        --> d
print(table.remove(t, 1))                     --> a
print(table.concat(t, " "), #t)               --> b c	2
