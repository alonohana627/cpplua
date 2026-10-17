-- string.format (stage 9): %d %i %u %c %x %X %o with widths, precisions and
-- the flags - + space # 0; %e %E %f %g %G with precisions; %s with widths and
-- precisions (and __tostring); %a for exactly representable values; %%; and
-- float arguments with integer values accepted by integer conversions.

print(string.format("%d|%5d|%-5d|%05d|%+d|% d", 42, 42, 42, 42, 42, 42))
print(string.format("%i|%.3d|%8.3d|%-8.3d|%+.0d|%.0d", -7, 7, 7, 7, 0, 0))
print(string.format("%d %d", math.maxinteger, math.mininteger))
print(string.format("%d %d %d", 3.0, -0.0, 2 ^ 53))
print(string.format("%u", 42))
print(string.format("%c%c%c", 76, 117, 97), string.format("[%5c]", 65), string.format("[%-3c]", 66))
print(string.format("%x %X %#x %#X %08x %-6x|", 255, 255, 255, 255, 255, 255))
print(string.format("%x", -1), string.format("%X", math.mininteger))
print(string.format("%o %#o %5o", 8, 8, 64))

-- Floats.
print(string.format("%f|%.2f|%10.3f|%-10.1f|%+.1f|%010.2f", 3.14159, 3.14159, 3.14159, 3.14159, 3.14159, -3.14159))
print(string.format("%.0f %.0f %.0f %#.0f", 0.5, 1.5, 2.5, 3.0))
print(string.format("%e|%.2e|%E|%12.3e|%-12.1E|", 12345.678, 12345.678, 0.000123, -1.5, 1e100))
print(string.format("%g|%g|%g|%g|%g|%g", 1.0, 0.1, 1e20, 1e-5, 123456789, 100000))
print(string.format("%.3g|%.10g|%G|%G|%#g|%g", 3.14159, 1 / 3, 1e-10, 1e30, 1.0, 0.0001))
print(string.format("%f %e %g", 5, 5, 5))
print(string.format("%f %g %.1f", math.huge, -math.huge, 1 / 0))
print(string.format("%.3f", 2 ^ 63), string.format("%.1f", -0.0))
print(string.format("%5.1f|%-8.3e|", 9.99, 9.9995))

-- %a for values that are exactly representable.
print(string.format("%a %a %a %a", 1.0, 0.5, 2.0, -0.75))
print(string.format("%A %a", 255.5, 1e-3))

-- Strings.
print(string.format("%s|%10s|%-10s|%.2s|%5.1s|", "abc", "abc", "abc", "abc", "abc"))
print(string.format("%s %s %s %s", 1, 1.5, true, nil))
print(string.format("%s", setmetatable({}, {__tostring = function() return "custom" end})))
print(string.format("%s and %s", "a\0b", "c") == "a\0b and c")
print(string.format("%.20s", "a long string that will be cut"))
print(string.format("%5s|%-5s|", "", ""))

-- Literal text and %%.
print(string.format("100%%"), string.format("no specifiers"), string.format("%%d"))
print(string.format("%d%%", 50), string.format(""))

-- Extra arguments are ignored.
print(string.format("%d", 1, 2, 3))

-- Numeric strings are converted.
print(string.format("%d %x %.1f", "10", "255", "2.25"))

return string.format("%5.2f|%-5d|%s", 1 / 3, 7, "end")
