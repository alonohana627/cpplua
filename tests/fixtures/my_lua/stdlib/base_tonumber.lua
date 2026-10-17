-- tonumber (stage 9): strings in decimal and hexadecimal, with spaces and
-- signs, integer versus float results, explicit bases 2..36 (letters in either
-- case, invalid digits give nil, no fractions or exponents), and the errors
-- for a bad base or a non-string with a base.

-- Without a base: numbers pass through, strings are converted like the lexer.
print(tonumber(10), tonumber(1.5), tonumber("10"), tonumber("  10  "))
print(tonumber("0x10"), tonumber("0X1p4"), tonumber("1e2"), tonumber(".5"), tonumber("5."))
print(tonumber("-7"), tonumber("+7"), tonumber("- 7"), tonumber("-0x10"))
print(math.type(tonumber("3")), math.type(tonumber("3.0")), math.type(tonumber("3e0")))
print(tonumber(""), tonumber(" "), tonumber("abc"), tonumber("1 2"), tonumber("1e"))
print(tonumber("0x"), tonumber("1..2"), tonumber("inf"), tonumber("nan"))
print(tonumber("9223372036854775807"), tonumber("9223372036854775808"))
print(tonumber("0xffffffffffffffff"), tonumber("0x10000000000000000"))
print(tonumber(nil), tonumber(true), tonumber({}) == nil)
print(tonumber("10\0"), tonumber("\t\n10\v\f\r"))

-- With a base.
print(tonumber("10", 2), tonumber("11111111", 2), tonumber("777", 8))
print(tonumber("ff", 16), tonumber("FF", 16), tonumber("zz", 36), tonumber("ZZ", 36))
print(tonumber("  101  ", 2), tonumber("-101", 2), tonumber("- 1", 2))
print(tonumber("2", 2), tonumber("8", 8), tonumber("g", 16), tonumber("1.5", 10))
print(tonumber("1e2", 10), tonumber("", 10), tonumber("0x10", 16), tonumber("10", 36))
print(math.type(tonumber("10", 10)), tonumber("7fffffffffffffff", 16))
print(tonumber("8000000000000000", 16), tonumber("10000000000000000", 16))
print(tonumber(" -ff ", 16), tonumber("+z", 36))

-- Every base from 2 to 36 reads "10" as the base itself.
local bases = {}
for b = 2, 36 do bases[#bases + 1] = tonumber("10", b) end
print(table.concat(bases, " "))

-- Errors.
print(pcall(tonumber, "10", 1))
print(pcall(tonumber, "10", 37))
print(pcall(tonumber, 10, 16))
print(pcall(tonumber, "10", "x"))
print(pcall(tonumber))

return tonumber("z", 36), tonumber("0.1")
