-- The string metatable (stage 9): method calls on string values and
-- literals, the __index of strings being the string table (so adding a
-- function to string makes it a method), string arithmetic coercions (a
-- string that looks like a number takes part in arithmetic, keeping integer
-- or float), and the errors for strings that are not numbers.

local s = "hello"
print(s:len(), s:upper(), s:rep(2, "-"), s:sub(2, 3), s:byte(1), s:find("l"))
print(("x"):rep(3), ("%d-%s"):format(5, "five"), ("a,b"):gsub(",", ";"))
print(#"abc", #"", #s, -#s)
print(("abc"):reverse():upper(), s:len() + 1)

-- Strings index the string table.
local mt = getmetatable("")
print(type(mt), mt.__index == string, getmetatable(s) == mt)
function string.shout(str) return str:upper() .. "!" end
print(s:shout(), ("quiet"):shout())
string.shout = nil
print(pcall(function() return s:shout() end))
print(s.len == string.len, ("x").nothing, s[1])

-- Arithmetic coercion: integers stay integers, floats stay floats.
print("10" + 1, "10" * "2", "3.0" + 1, "0x10" + 0, "1e1" + 0)
print(math.type("10" + 1), math.type("10" + 1.0), math.type("10.0" + 1))
print("7" // "2", "7" % "3", "2" ^ "3", "7" / "2", -"3", -"3.5")
print(" 5 " + 0, "\t5\n" * 2, "-5" - "-5", "10" // "0.0")
print(10 .. 20, 1.5 .. "", "x" .. 1 .. 2)

-- Bitwise operators do not convert strings (the string metatable has only
-- the arithmetic metamethods).
print(pcall(function() return "3" | 0 end))
print(pcall(function() return 1 << "4" end))
print(pcall(function() return ~"0" end))

-- Comparison does not coerce.
print("10" == 10, pcall(function() return "10" < 5 end))
print("10" < "9", "a" < "b", "" < "a", "Z" < "a")

-- Strings that are not numbers.
print(pcall(function() return "abc" + 1 end))
print(pcall(function() return "1.5" | 0 end))
print(pcall(function() return "10x" * 2 end))
print(pcall(function() return {} .. "x" end))
print(pcall(function() return -"x" end))

-- The methods also work on numbers converted to strings explicitly.
print(tostring(12345):reverse(), (tostring(3.5)):len(), (2 ^ 10 .. ""):sub(1, 2))

return s:upper(), ("10" + 5) * 2
