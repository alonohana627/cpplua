-- Bitwise metamethods (stage 7): __band __bor __bxor __shl __shr __bnot, with
-- the table on either side and numbers or strings on the other; also for
-- floats without an integer value, which have no plain bitwise meaning.

local function name(x)
  if type(x) == "table" then return x.name end
  return type(x) .. " " .. tostring(x)
end

local mt = {}
local function V(n) return setmetatable({name = n}, mt) end
for _, event in ipairs({"band", "bor", "bxor", "shl", "shr"}) do
  mt["__" .. event] = function(a, b) return event .. "(" .. name(a) .. ", " .. name(b) .. ")" end
end
mt.__bnot = function(a, b) return "bnot(" .. name(a) .. ", same: " .. tostring(a == b) .. ")" end

local a, b = V("a"), V("b")
print(a & b, a | b, a ~ b)
print(a << b, a >> b, ~a)
print(a & 1, 1 | a, a ~ 0xff, 2 << a, a >> 3)
print(a & "7", "7" | a, a ~ 1.5, 1.5 << a)

-- Plain bitwise operations still work, including on floats with integer
-- values (strings are not converted for bitwise operators in 5.4).
print(6 & 3, 6 | 3, 6 ~ 3, 1 << 4, 256 >> 4, ~0, 3.0 | 0)
print(pcall(function() return "12" & 4 end))

-- A bit set class.
local Bits = {}
Bits.__index = Bits
local function bits(n) return setmetatable({n = n}, Bits) end
local function raw(x) if type(x) == "table" then return x.n end return x end
Bits.__band = function(p, q) return bits(raw(p) & raw(q)) end
Bits.__bor = function(p, q) return bits(raw(p) | raw(q)) end
Bits.__bxor = function(p, q) return bits(raw(p) ~ raw(q)) end
Bits.__shl = function(p, q) return bits(raw(p) << raw(q)) end
Bits.__shr = function(p, q) return bits(raw(p) >> raw(q)) end
Bits.__bnot = function(p) return bits(~raw(p)) end
local x, y = bits(12), bits(10)
print((x & y).n, (x | y).n, (x ~ y).n, (x << 2).n, (x >> 2).n, (~x).n)
print((1 | x).n, (0xf0 & bits(0xff)).n, ((x | y) & ~bits(2)).n)

-- Errors without metamethods.
print(pcall(function() return {} & 1 end))
print(pcall(function() local t = {} return 1 << t end))
print(pcall(function() local t = {} return ~t end))
print(pcall(function() return 1.5 | 1 end))

return (x & y).n, a | b
