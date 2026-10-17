-- Yields inside __eq, __lt and __le, in expressions, conditions and loops.
local Y = coroutine.yield
local mt = {
  __eq = function(a, b) return Y("eq", a.v, b.v) end,
  __lt = function(a, b) return Y("lt", a.v, b.v) end,
  __le = function(a, b) return Y("le", a.v, b.v) end,
}
local function O(v) return setmetatable({v = v}, mt) end
local co = coroutine.create(function()
  local one, two, other = O(1), O(2), O(1)
  print("eq", one == other, one ~= other, one == two)
  print("lt", one < two, two < one, one > two)
  print("le", one <= two, two >= one, two <= one)
  print("not", not (one < two), not (one == other))
  if one < two and two > one then print("if ok") end
  local n = 0
  local i = O(0)
  while i < O(3) do n = n + 1; i = O(i.v + 1) end
  print("while", n)
  local t = {}
  repeat t[#t + 1] = #t until O(#t) >= O(2)
  print("repeat", #t)
  -- results are converted to booleans
  return one == other, one < two
end)
local answers = {eq = function(a, b) return a == b and "yes" or nil end,
                 lt = function(a, b) return a < b and 1 or false end,
                 le = function(a, b) return a <= b end}
local ok, op, a, b = coroutine.resume(co)
local count = 0
while coroutine.status(co) == "suspended" do
  count = count + 1
  ok, op, a, b = coroutine.resume(co, answers[op](a, b))
end
return ok, op, a, count
