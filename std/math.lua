---@meta

-- The math library's functions are implemented in C++ in lib/std/math.cpp.
-- This file is a reference only (for readers and lua-language-server); lua2 never runs it.

---The math library. Functions marked "integer/float" keep the integer subtype when they can.
---@class mathlib
math = {}

---The value of pi.
---@type number
math.pi = 3.1415926535898

---The float value HUGE_VAL, greater than any other numeric value (positive infinity).
---@type number
math.huge = 1/0

---The maximum value of an integer: 2^63 - 1.
---@type integer
math.maxinteger = 9223372036854775807

---The minimum value of an integer: -2^63.
---@type integer
math.mininteger = -9223372036854775807 - 1

---Returns the absolute value of `x` (integer/float).
---@generic T: number
---@param x T
---@return T
function math.abs(x) end

---Returns the arc cosine of `x`, in radians.
---@param x number
---@return number
function math.acos(x) end

---Returns the arc sine of `x`, in radians.
---@param x number
---@return number
function math.asin(x) end

---Returns the arc tangent of `y/x` in radians, using both signs to find the quadrant.
---`x` defaults to 1, so `math.atan(y)` is the plain arc tangent.
---@param y number
---@param x? number
---@return number
function math.atan(y, x) end

---Returns the smallest integral value greater than or equal to `x` (an integer if it fits).
---@param x number
---@return integer
function math.ceil(x) end

---Returns the cosine of `x` (in radians).
---@param x number
---@return number
function math.cos(x) end

---Converts the angle `x` from radians to degrees.
---@param x number
---@return number
function math.deg(x) end

---Returns e raised to the power `x`.
---@param x number
---@return number
function math.exp(x) end

---Returns `x` converted to an integer if it has an exact integer value; otherwise nil
---(also nil for non-numbers).
---@param x any
---@return integer?
function math.tointeger(x) end

---Returns the largest integral value less than or equal to `x` (an integer if it fits).
---@param x number
---@return integer
function math.floor(x) end

---Returns the remainder of the division of `x` by `y` that rounds the quotient towards zero
---(integer/float). An integer division by zero is an error.
---@param x number
---@param y number
---@return number
function math.fmod(x, y) end

---Returns whether integer `m` is below integer `n` when both are compared as unsigned integers.
---@param m integer
---@param n integer
---@return boolean
function math.ult(m, n) end

---Returns the logarithm of `x` in the given base (default e, the natural logarithm).
---@param x number
---@param base? number
---@return number
function math.log(x, base) end

---Returns the argument with the maximum value, according to the operator `<`.
---@generic T: number
---@param x T
---@param ... T
---@return T
function math.max(x, ...) end

---Returns the argument with the minimum value, according to the operator `<`.
---@generic T: number
---@param x T
---@param ... T
---@return T
function math.min(x, ...) end

---Returns the integral part of `x` and its fractional part. The second result is always a float.
---@param x number
---@return number
---@return number
function math.modf(x) end

---Converts the angle `x` from degrees to radians.
---@param x number
---@return number
function math.rad(x) end

---Returns the sine of `x` (in radians).
---@param x number
---@return number
function math.sin(x) end

---Returns the square root of `x`.
---@param x number
---@return number
function math.sqrt(x) end

---Returns the tangent of `x` (in radians).
---@param x number
---@return number
function math.tan(x) end

---Returns "integer" or "float" for a number, or nil (fail) for anything else.
---@param x any
---@return "integer"|"float"|nil
function math.type(x) end

---Without arguments, returns a float in [0, 1). With `m`, an integer in [1, m]; with `m`
---and `n`, an integer in [m, n]. `math.random(0)` returns an integer with all bits random.
---Uses the xoshiro256** generator, as the reference Lua does.
---@overload fun(): number
---@overload fun(m: integer): integer
---@param m integer
---@param n integer
---@return integer
function math.random(m, n) end

---Seeds the pseudo-random generator. Without arguments, uses a seed that varies between runs;
---otherwise the integers `x` and `y` (default 0) form the seed. Returns the two seed components.
---@param x? integer
---@param y? integer
---@return integer
---@return integer
function math.randomseed(x, y) end

---Lua 5.3 compatibility: the same as `math.atan(y, x)`.
---@deprecated
---@param y number
---@param x number
---@return number
function math.atan2(y, x) end

---Lua 5.3 compatibility: returns the hyperbolic cosine of `x`.
---@deprecated
---@param x number
---@return number
function math.cosh(x) end

---Lua 5.3 compatibility: returns the hyperbolic sine of `x`.
---@deprecated
---@param x number
---@return number
function math.sinh(x) end

---Lua 5.3 compatibility: returns the hyperbolic tangent of `x`.
---@deprecated
---@param x number
---@return number
function math.tanh(x) end

---Lua 5.3 compatibility: returns `x` raised to the power `y` (use `x ^ y` instead).
---@deprecated
---@param x number
---@param y number
---@return number
function math.pow(x, y) end

---Lua 5.3 compatibility: returns `m` and `e` such that `x = m * 2^e`, where `|m|` is in
---[0.5, 1) (or zero) and `e` is an integer.
---@deprecated
---@param x number
---@return number m
---@return integer e
function math.frexp(x) end

---Lua 5.3 compatibility: returns `m * 2^e`.
---@deprecated
---@param m number
---@param e integer
---@return number
function math.ldexp(m, e) end

---Lua 5.3 compatibility: returns the base-10 logarithm of `x` (use `math.log(x, 10)` instead).
---@deprecated
---@param x number
---@return number
function math.log10(x) end
