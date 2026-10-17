-- tostring (stage 9): integers, floats (always with a ".0" or an exponent so
-- they read back as floats, %.14g precision), infinities, -0.0, the extreme
-- integers, booleans, nil, strings, and __tostring / __name metamethods.

print(tostring(0), tostring(-0), tostring(42), tostring(-42))
print(tostring(0.0), tostring(-0.0), tostring(1.0), tostring(-1.5))
print(tostring(1e100), tostring(-1e100), tostring(1e15), tostring(1e16))
print(tostring(123456789012345.0), tostring(1234567890123456.0))
print(tostring(0.1), tostring(1 / 3), tostring(2 ^ 53), tostring(2 ^ 63))
print(tostring(1e-5), tostring(5e-324), tostring(1.7976931348623157e308))
print(tostring(math.huge), tostring(-math.huge), tostring(1 / 0), tostring(-1 / 0))
print(tostring(math.maxinteger), tostring(math.mininteger))
print(tostring(math.maxinteger + 0.0), tostring(math.mininteger + 0.0))
print(tostring(3 / 1), tostring(7 // 2), tostring(7.0 // 2), tostring(2 ^ 2))
print(tostring(true), tostring(false), tostring(nil))
print(tostring("text"), tostring(""), #tostring(12345))

-- tostring of a table or function starts with its type name.
print(tostring({}):sub(1, 7), tostring(print):sub(1, 10), tostring(function() end):sub(1, 10))

-- __tostring and __name.
local mt = {__tostring = function(self) return "point(" .. self.x .. ")" end}
local p = setmetatable({x = 3}, mt)
print(tostring(p), p)
local named = setmetatable({}, {__name = "MyType"})
print(tostring(named):sub(1, 8))
print(pcall(tostring, setmetatable({}, {__tostring = function() return 1 end})))
print(pcall(tostring))

-- Concatenation converts numbers the same way.
print(1 .. "", 1.0 .. "", -0.0 .. "", 2 ^ 63 .. "", 10 // 3 .. "|" .. 10 / 2)

return tostring(1e300 * 1e10), tostring(255 // 1.0)
