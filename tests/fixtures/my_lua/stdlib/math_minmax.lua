-- math.max, math.min, math.ult and the integer limits (stage 9): max and min
-- keep the type of the winning argument (integer or float), work with one
-- argument and with mixed integers and floats, and fail without arguments
-- (they do not check types, see below);
-- ult compares as unsigned; maxinteger and mininteger wrap around.

local function show(v) return tostring(v) .. ":" .. math.type(v) end

print(show(math.max(1, 2, 3)), show(math.max(3, 2, 1)), show(math.max(1, 2.5)), show(math.max(2.0, 2)))
print(show(math.max(2, 2.0)), show(math.min(2, 2.0)), show(math.min(2.0, 2)))
print(show(math.min(1, 2, 3)), show(math.min(-1.5, -1)), show(math.max(7)), show(math.min(7.0)))
print(show(math.max(-math.huge, math.mininteger)), show(math.min(math.huge, math.maxinteger)))
print(show(math.max(math.maxinteger, 2 ^ 63)), show(math.min(-0.0, 0)), show(math.max(0, -0.0)))
-- max and min do not check the types of their arguments: they compare them
-- with < (strings as strings, mixed types fail) and return the winner as is.
print(pcall(math.max, "10", 9))
print(pcall(math.min, 1, "0.5"))
print(math.max("10"), type(math.max("10")), math.max("10", "9"), math.min("10", "9"))
print(math.max("a", "b", "ab"), type(math.min({})), math.max(true))
print(pcall(math.max))
print(pcall(math.min))
print(pcall(math.max, 1, "x"))
print(pcall(math.min, nil, 1))

-- Integer limits.
print(math.maxinteger, math.mininteger, math.maxinteger + 1 == math.mininteger)
print(math.mininteger - 1 == math.maxinteger, -math.mininteger == math.mininteger)
print(math.maxinteger * 2, math.mininteger // -1, math.mininteger % -1)
print(math.maxinteger + 0.0 == 2 ^ 63, math.maxinteger // 1 == math.maxinteger)
print(math.type(math.maxinteger), math.type(math.huge), math.huge // 1, -math.huge // 1)

-- ult: unsigned less than.
print(math.ult(1, 2), math.ult(2, 1), math.ult(-1, 1), math.ult(1, -1), math.ult(0, math.mininteger))
print(math.ult(math.maxinteger, math.mininteger), math.ult(5, 5), math.ult(-2, -1))
print(pcall(math.ult, 1.5, 2))
print(pcall(math.ult, 1))
print(math.ult(1.0, 2))

return math.max(1, 5, 3), math.min(4.5, 9)
