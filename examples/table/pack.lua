-- table.pack(...): packs its arguments into a table with an "n" field holding their count.
local t = table.pack("x", nil, "z")
print(t.n, t[1], t[2], t[3])                  --> 3	x	nil	z
