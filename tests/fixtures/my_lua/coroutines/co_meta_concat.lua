-- Yields inside __concat, including chains a..b..c that the VM concatenates in one go.
local Y = coroutine.yield
local mt = {}
mt.__concat = function(a, b)
  local sa = type(a) == "table" and "<" .. a.s .. ">" or tostring(a)
  local sb = type(b) == "table" and "<" .. b.s .. ">" or tostring(b)
  local r = Y(sa, sb)
  return r
end
local function S(s) return setmetatable({s = s}, mt) end
local co = coroutine.wrap(function()
  local a, b, c = S("a"), S("b"), S("c")
  print("1:", a .. "x")
  print("2:", "x" .. a)
  print("3:", "p" .. "q" .. a .. "r" .. "s")
  print("4:", a .. b .. c)
  print("5:", 1 .. 2 .. a .. 3)
  local n = 10
  print("6:", "n=" .. n .. a .. b .. "!")
  return "fin"
end)
local r1, r2 = co()
local steps = 0
while r1 ~= "fin" do
  steps = steps + 1
  print("  concat", r1, r2)
  r1, r2 = co(r1 .. "+" .. r2)
end
return steps
