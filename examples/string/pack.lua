-- string.pack(fmt, v1, v2, ...): packs values into a binary string.
local packed = string.pack("<i4 i2 z", 100000, -2, "hi")
print(#packed)                                --> 9 (4 + 2 + "hi\0")
print(string.unpack("<i4 i2 z", packed))      --> 100000	-2	hi	10
print(string.byte(string.pack(">I2", 258), 1, -1))   --> 1	2 (big endian)
