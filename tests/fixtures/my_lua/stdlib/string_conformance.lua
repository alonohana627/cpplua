-- Small differences from Lua found in review (issue #9): pack formats end
-- at a NUL, the 'X' error comes first, %a checks its specification first,
-- utf8.codes converts its control value, unpack's "too many results" is a
-- stack overflow, and a NUL in a format specification ends its text.
print(string.packsize("i\0i"), #string.pack("i\0i", 1), string.unpack("i\0i", string.pack("i", 7)))
print(pcall(string.pack, "!16Xc3i9", 1))
print(pcall(string.format, "%100a", "x"))
print(pcall(string.format, "%\0d", 1))
local f = utf8.codes("abc")
print(f("abc", 1.0), f("abc", "1"))
local ok, message = pcall(string.unpack, string.rep("b", 1e6), string.rep("\0", 1e6))
print(ok, message)
