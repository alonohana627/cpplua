-- string.unpack(fmt, s [, pos]): reads values packed by string.pack; the last result is the next position.
local data = string.pack("i4 d s1", 7, 2.5, "name")
local n, x, str, next_pos = string.unpack("i4 d s1", data)
print(n, x, str, next_pos == #data + 1)       --> 7	2.5	name	true

-- Reading one value at a time from a position.
local a, pos = string.unpack("B", "\5\6\7", 2)
print(a, pos)                                 --> 6	3
