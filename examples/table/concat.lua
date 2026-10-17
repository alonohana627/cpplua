-- table.concat(list [, sep [, i [, j]]]): joins list[i..j] (strings or numbers) into one string.
local t = { "a", "b", "c", 4 }
print(table.concat(t))                        --> abc4
print(table.concat(t, ", "))                  --> a, b, c, 4
print(table.concat(t, "-", 2, 3))             --> b-c
print(table.concat({}, ",") == "")            --> true
