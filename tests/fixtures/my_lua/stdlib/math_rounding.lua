-- math.abs, ceil, floor, fmod, modf, tointeger, type (stage 9): integer
-- versus float results (shown with math.type), values beyond the integer
-- range staying floats, -0.0, infinities, fmod by zero for integers and
-- floats, and argument errors.

local function show(...)
  local out = {}
  for i = 1, select("#", ...) do
    local v = select(i, ...)
    out[i] = tostring(v) .. ":" .. (math.type(v) or type(v))
  end
  return table.concat(out, " ")
end

print(show(math.abs(-5), math.abs(5), math.abs(-5.5), math.abs(-0.0), math.abs(math.mininteger)))
print(show(math.abs(-math.huge), math.abs("-3")))

print(show(math.floor(3.7), math.floor(-3.7), math.floor(5), math.floor(-0.0), math.floor(2 ^ 62)))
print(show(math.floor(2 ^ 63), math.floor(-2 ^ 63), math.floor(1e100), math.floor(math.huge)))
print(show(math.ceil(3.2), math.ceil(-3.2), math.ceil(5), math.ceil(-0.5), math.ceil(1e100)))
print(show(math.floor("3.5"), math.ceil("2")))
print(pcall(math.floor, "a"))
print(pcall(math.ceil))

-- fmod: integers keep integers, the result has the sign of the dividend.
print(show(math.fmod(7, 3), math.fmod(-7, 3), math.fmod(7, -3), math.fmod(-7, -3)))
print(show(math.fmod(7.5, 2), math.fmod(-7.5, 2), math.fmod(7, 2.5), math.fmod(1, math.huge)))
print(show(math.fmod(math.mininteger, -1), math.fmod(5, math.mininteger), math.fmod(0, 5)))
print(show(math.fmod(5.0, 0) ~= math.fmod(5.0, 0)), show(math.fmod(-6, 4.0)))
print(pcall(math.fmod, 5, 0))
print(pcall(math.fmod, 5))
print(-7 % 3, math.fmod(-7, 3), -7.5 % 2, math.fmod(-7.5, 2))

-- modf: the integral part becomes an integer when it fits, the fraction is a float.
print(show(math.modf(3.7)))
print(show(math.modf(-3.7)))
print(show(math.modf(5)))
print(show(math.modf(-0.0)))
print(show(math.modf(math.huge)))
print(show(math.modf(-math.huge)))
print(show(math.modf(2 ^ 70)))

-- tointeger: only exact integer values convert.
print(show(math.tointeger(3.0), math.tointeger(3.5), math.tointeger(-0.0), math.tointeger(7)))
print(show(math.tointeger(2 ^ 63), math.tointeger(-2 ^ 63), math.tointeger(2 ^ 53)))
print(show(math.tointeger("8"), math.tointeger({}), math.tointeger(math.huge)))
print(pcall(math.tointeger))

-- type.
print(math.type(1), math.type(1.0), math.type("1"), math.type(nil), math.type({}))
print(pcall(math.type))

return math.floor(-0.5), math.ceil(-0.5)
