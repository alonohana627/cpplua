-- Arithmetic metamethods (stage 7): __add __sub __mul __div __mod __pow
-- __unm __idiv, with the table on the left, on the right and on both sides,
-- with numbers and numeric strings on the other side. Each metamethod gets
-- the two operands in their original order; __unm gets its operand twice.

local function name(x)
  if type(x) == "table" then return x.name end
  return type(x) .. " " .. tostring(x)
end

local mt = {}
local function V(n) return setmetatable({name = n}, mt) end
for _, event in ipairs({"add", "sub", "mul", "div", "mod", "pow", "idiv"}) do
  mt["__" .. event] = function(a, b) return event .. "(" .. name(a) .. ", " .. name(b) .. ")" end
end
mt.__unm = function(a, b) return "unm(" .. name(a) .. ", same: " .. tostring(a == b) .. ")" end

local a, b = V("a"), V("b")
print(a + b, a - b, a * b)
print(a / b, a % b, a ^ b, a // b)
print(-a, -b)

-- A number on either side.
print(a + 1, 1 + a, a - 2.5, 2.5 - a)
print(a * 3, 3 // a, a % 4, 4 ^ a)

-- A numeric string on either side: no conversion happens, the string is passed.
print(a + "10", "10" + a, "2" * b, b / "x")

-- Only the right operand has the metamethod.
local plain = {name = "plain"}
print(pcall(function() return plain + a end))
print(plain * b)

-- The left operand's metamethod wins when both have one.
local other = setmetatable({name = "other"}, {__add = function() return "other's add" end})
print(a + other, other + a)

-- Numbers and strings that convert still use plain arithmetic.
print(1 + 2, "3" + 4, 10 // "3", "2" ^ 2)

-- A vector class returning new vectors.
local Vec = {}
Vec.__index = Vec
local function vec(x, y) return setmetatable({x = x, y = y}, Vec) end
Vec.__add = function(p, q) return vec(p.x + q.x, p.y + q.y) end
Vec.__sub = function(p, q) return vec(p.x - q.x, p.y - q.y) end
Vec.__mul = function(p, q)
  if type(p) == "number" then return vec(p * q.x, p * q.y) end
  if type(q) == "number" then return vec(p.x * q, p.y * q) end
  return p.x * q.x + p.y * q.y
end
Vec.__unm = function(p) return vec(-p.x, -p.y) end
Vec.__div = function(p, k) return vec(p.x / k, p.y / k) end
Vec.__idiv = function(p, k) return vec(p.x // k, p.y // k) end
Vec.__mod = function(p, k) return vec(p.x % k, p.y % k) end
Vec.__pow = function(p, k) return vec(p.x ^ k, p.y ^ k) end
function Vec:str() return "(" .. self.x .. ", " .. self.y .. ")" end

local u, v = vec(1, 2), vec(3, 4)
print((u + v):str(), (u - v):str(), u * v, (2 * u):str(), (v * 3):str())
print((-u):str(), (v / 2):str(), (v // 2):str(), (v % 3):str(), (u ^ 2):str())
print((u + v + u + v):str(), (u * 2 + v * -1):str())

-- Metamethods in compound expressions and with precedence.
print((-u + v * 2):str(), u * v + 1)

-- The result of a metamethod can be any value, also multiple values (only
-- the first is used) and nil.
local odd = setmetatable({}, {
  __add = function() return 1, 2, 3 end,
  __sub = function() return nil end,
  __mul = function() return false end,
})
print(odd + 1, odd - 1, odd * 1)

-- Errors when nobody has a metamethod.
print(pcall(function() return {} + 1 end))
print(pcall(function() local t = {} return 1 - t end))
print(pcall(function() local t = {} return -t end))

return a + b, (u + v).x
