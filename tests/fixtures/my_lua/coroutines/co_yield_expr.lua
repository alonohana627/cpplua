-- yield in the middle of expressions, argument lists, constructors, tail calls and varargs.
local Y = coroutine.yield
local co = coroutine.create(function(...)
  local t = {Y(1), Y(2), Y(3)}
  print("ctor", t[1], t[2], t[3], t[4], t[5], t[6])
  local function f(...) return select("#", ...), ... end
  print("args", f(Y("a"), Y("b")))
  print("arith", Y("x") * 10 + Y("y"))
  print("concat", "<" .. Y("s1") .. Y("s2") .. ">")
  print("and/or", Y("p") and Y("q") or Y("r"))
  print("varargs", ...)
  local function tail(n) if n == 0 then return Y("tail") end return tail(n - 1) end
  print("tail", tail(100))
  local a, b, c = Y("multi")
  print("multi", a, b, c)
  return Y("last")
end)
local feed = {
  [1] = {"one", "extra"}, [2] = {}, [3] = {"three", "x", "y"},
  a = {"A1", "A2"}, b = {"B1", "B2"}, x = {4}, y = {2}, s1 = {"S1"}, s2 = {"S2"},
  p = {false}, q = {"Q"}, r = {"R"}, tail = {"T", "U"}, multi = {"m1", nil, "m3"}, last = {"L1", "L2"},
}
local ok, v = coroutine.resume(co, "v1", nil, "v3")
while coroutine.status(co) == "suspended" do
  local reply = feed[v]
  ok, v = coroutine.resume(co, table.unpack(reply, 1, 3))
end
print(ok, v)
local co2 = coroutine.wrap(function() return Y("last") end)
co2()
return co2("L1", "L2")
