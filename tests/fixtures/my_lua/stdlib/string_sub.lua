-- string.sub (stage 9): positive and negative indices, defaults, indices out
-- of range on either side, empty results, float indices with integer values,
-- and argument errors.

local s = "Hello, Lua"
print(string.sub(s, 1, 5), string.sub(s, 8), string.sub(s, -3), string.sub(s, -3, -2))
print(string.sub(s, 0), string.sub(s, 1, 0) == "", string.sub(s, 5, 3) == "")
print(string.sub(s, -100, 2), string.sub(s, 8, 100), string.sub(s, 11) == "")
print(string.sub(s, -1, -1), string.sub(s, 10, 10), string.sub(s, 0, 0) == "")
print(string.sub(s, math.mininteger, math.maxinteger) == s)
print(string.sub(s, math.maxinteger) == "", string.sub(s, 2, math.mininteger) == "")
print(string.sub(s, 2.0, 3.0), string.sub(s, "2", "4"))
print(string.sub("", 1, 1) == "", string.sub("", -1) == "", string.sub("x", 1, 1))
print(string.sub(12345, 2, 4), string.sub(1.25, 2))
print(pcall(string.sub, s))
print(pcall(string.sub, s, 1.5))
print(pcall(string.sub, s, 1, "z"))
print(pcall(string.sub, nil, 1))

-- Every suffix through negative indices.
local suffixes = {}
for i = -1, -4, -1 do suffixes[#suffixes + 1] = ("abcd"):sub(i) end
print(table.concat(suffixes, " "))

-- Every prefix through positive end indices.
local prefixes = {}
for j = 0, 4 do prefixes[#prefixes + 1] = "[" .. ("abcd"):sub(1, j) .. "]" end
print(table.concat(prefixes, " "))

return s:sub(-3), s:sub(1, -6)
