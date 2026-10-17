-- Yields inside arithmetic and bitwise metamethods, and __unm.
local Y = coroutine.yield
local mt = {}
local function V(n) return setmetatable({n = n}, mt) end
local function val(x) return type(x) == "table" and x.n or x end
for _, ev in ipairs({"add", "sub", "mul", "div", "mod", "pow", "idiv",
                     "band", "bor", "bxor", "shl", "shr"}) do
  mt["__" .. ev] = function(a, b)
    local r = Y(ev, val(a), val(b))
    return r
  end
end
mt.__unm = function(a) return Y("unm", a.n) end
mt.__bnot = function(a) return Y("bnot", a.n) end
local ops = {
  add = function(a, b) return a + b end, sub = function(a, b) return a - b end,
  mul = function(a, b) return a * b end, div = function(a, b) return a / b end,
  mod = function(a, b) return a % b end, pow = function(a, b) return a ^ b end,
  idiv = function(a, b) return a // b end, band = function(a, b) return a & b end,
  bor = function(a, b) return a | b end, bxor = function(a, b) return a ~ b end,
  shl = function(a, b) return a << b end, shr = function(a, b) return a >> b end,
  unm = function(a) return -a end, bnot = function(a) return ~a end,
}
local co = coroutine.create(function()
  local a, b = V(12), V(5)
  print(a + b, a - b, a * b, a / b)
  print(a % b, a ^ 2, a // b, 7 // a)
  print(a & b, a | 3, 1 ~ b, a << 1, a >> b)
  print(-a, ~b)
  -- in a larger expression: (a + 1) * 2 - -b
  local e = (a + 1) * 2 - -b
  print("e", e)
  -- as a for loop limit and in a condition
  local count = 0
  for i = 1, a + 0 do count = count + 1 end
  if a - 2 == 10 then print("cond ok", count) end
  return "end"
end)
local ok, r1, r2, r3 = coroutine.resume(co)
local steps = 0
while coroutine.status(co) == "suspended" do
  steps = steps + 1
  ok, r1, r2, r3 = coroutine.resume(co, ops[r1](r2, r3))
end
return ok, r1, steps
