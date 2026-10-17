-- math.sqrt, exp, log, sin, cos, tan, asin, acos, atan, deg, rad, pi and
-- huge (stage 9). Results that are not exact are printed with %.6f or %.6g
-- so the last digits of the C library do not matter; exact results are
-- printed directly.

local function f6(x) return string.format("%.6f", x) end
local function g6(x) return string.format("%.6g", x) end

print(math.sqrt(16), math.sqrt(2) ^ 2 == 2, f6(math.sqrt(2)), math.sqrt(0), math.sqrt(-0.0))
print(math.sqrt(math.huge), math.type(math.sqrt(4)), math.sqrt("9"))
print(math.sqrt(-1) ~= math.sqrt(-1))

print(math.exp(0), f6(math.exp(1)), g6(math.exp(10)), math.exp(-math.huge), math.exp(math.huge))
print(math.log(1), f6(math.log(math.exp(2))), math.log(0), math.log(8, 2), math.log(1024, 2))
print(math.log(100, 10), math.log(1000, 10), math.log(1e15, 10), f6(math.log(27, 3)))
print(math.log(2 ^ 52, 2), math.log(1, 10), g6(math.log(2, 10)), f6(math.log(10, math.exp(1))))
print(math.log(math.huge), math.log(-1) ~= math.log(-1), math.type(math.log(8, 2)))

print(f6(math.pi), math.pi == 3.141592653589793, math.huge, -math.huge, math.huge > math.maxinteger)
print(math.sin(0), f6(math.sin(math.pi / 2)), f6(math.sin(math.pi / 6)), f6(math.sin(-1)))
print(math.cos(0), f6(math.cos(math.pi)), f6(math.cos(math.pi / 3)), f6(math.cos(2)))
print(math.tan(0), f6(math.tan(math.pi / 4)), f6(math.tan(1)), f6(math.tan(-0.5)))
print(math.asin(0), f6(math.asin(1)), f6(math.asin(-0.5)), math.asin(2) ~= math.asin(2))
print(f6(math.acos(1)), f6(math.acos(0)), f6(math.acos(-1)), f6(math.acos(0.5)))
print(math.atan(0), f6(math.atan(1)), f6(math.atan(-1)), f6(math.atan(math.huge)))
print(f6(math.atan(1, 1)), f6(math.atan(1, -1)), f6(math.atan(-1, -1)), f6(math.atan(0, -1)))
print(math.atan(0, 1), f6(math.atan(1, 0)), f6(math.atan(-1, 0)), f6(math.atan(5)))

-- Degrees and radians by hand (math.deg / math.rad).
print(math.deg(math.pi), math.rad(180) == math.pi, f6(math.deg(1)), f6(math.rad(1)))
print(math.deg(0), math.rad(0), math.type(math.deg(1)))

-- Integer arguments are converted to floats.
print(math.type(math.sin(0)), math.type(math.exp(0)), math.type(math.sqrt(9)))

-- Errors.
print(pcall(math.sqrt))
print(pcall(math.sin, "x"))
print(pcall(math.log, 8, "two"))
print(pcall(math.atan))
print(pcall(math.exp, {}))

return f6(math.pi), math.sqrt(144)
