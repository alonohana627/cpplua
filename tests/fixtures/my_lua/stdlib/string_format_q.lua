-- string.format %q and format errors (stage 9): %q quotes strings so that
-- Lua reads them back (escaping quotes, backslashes, newlines, control
-- characters and zeros, with three digits before a digit), writes integers
-- (mininteger as 0x8000000000000000) and floats (in hexadecimal, or 1e9999,
-- -1e9999 for infinities), nil and booleans; then the errors for invalid
-- conversions, missing arguments, non-integer values and too long formats.

print(string.format("%q", "plain"))
print(string.format("%q", 'say "hi"'))
print(string.format("%q", "back\\slash"))
print(string.format("%q", "line1\nline2"))
print(string.format("%q", "tab\there"))
print(string.format("%q", "\0"), string.format("%q", "\0001"), string.format("%q", "\1a"))
print(string.format("%q", "\r\n"), string.format("%q", "\127"), string.format("%q", "\31x"))
print(string.format("%q", "\200\255") == '"\200\255"', string.format("%q", ""))
print(string.format("%q", 42), string.format("%q", -7), string.format("%q", 0))
print(string.format("%q", math.maxinteger), string.format("%q", math.mininteger))
print(string.format("%q", 1.5), string.format("%q", 0.1), string.format("%q", -2.0))
print(string.format("%q", 1.0), string.format("%q", 1e100))
print(string.format("%q", math.huge), string.format("%q", -math.huge))
print(string.format("%q", nil), string.format("%q", true), string.format("%q", false))

-- %q round trips through load.
local tricky = "quote\" back\\ nl\n zero\0 one\1 digit\0009 high\250"
local loaded = load("return " .. string.format("%q", tricky))()
print(loaded == tricky, #loaded)
for _, v in ipairs({0.1, 1 / 3, -1e-300, 2 ^ 63, math.mininteger, 12345}) do
  local back = load("return " .. string.format("%q", v))()
  io.write(tostring(back == v and math.type(back) == math.type(v)), " ")
end
print()

-- Errors.
print(pcall(string.format, "%q", {}))
print(pcall(string.format, "%q", print))
print(pcall(string.format, "%10q", "x"))
print(pcall(string.format, "%d", 1.5))
print(pcall(string.format, "%d", "1.5"))
print(pcall(string.format, "%d", "x"))
print(pcall(string.format, "%x", 2 ^ 63))
print(pcall(string.format, "%d"))
print(pcall(string.format, "%s"))
print(pcall(string.format, "%y", 1))
print(pcall(string.format, "%", 1))
print(pcall(string.format, "%10.123f", 1))
print(pcall(string.format, "%123d", 1))
print(pcall(string.format, "%#d", 1))
print(pcall(string.format, "%.3c", 65))
print(pcall(string.format, "%c", "x"))
print(pcall(string.format, "%f", "x"))
print(pcall(string.format))
print(pcall(string.format, {}))

return string.format("%q", "a\nb")
