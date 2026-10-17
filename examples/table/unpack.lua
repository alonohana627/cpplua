-- table.unpack(list [, i [, j]]): returns list[i..j] as multiple values.
print(table.unpack({ 1, 2, 3 }))              --> 1	2	3
print(table.unpack({ "a", "b", "c", "d" }, 2, 3))  --> b	c
print(math.max(table.unpack({ 4, 9, 2 })))    --> 9
