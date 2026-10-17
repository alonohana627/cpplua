-- Yields inside __index and __newindex called by the VM.
local Y = coroutine.yield
local log = {}
local proxy = setmetatable({}, {
  __index = function(t, k) local v = Y("index", k); return v end,
  __newindex = function(t, k, v) local ok = Y("newindex", k, v); rawset(t, k, ok and v or "refused") end,
})
local chain = setmetatable({}, {__index = setmetatable({}, {__index = function(_, k) return Y("deep", k) .. "!" end})})
local co = coroutine.wrap(function()
  local a = proxy.alpha
  print("a =", a)
  local b = proxy[1] + proxy[2]
  print("b =", b)
  proxy.x = "X"
  proxy.y = "Y"
  print("raw", rawget(proxy, "x"), rawget(proxy, "y"))
  print("x via table", proxy.x)
  local s = chain.key
  print("s =", s)
  -- in a call argument, a table constructor and a method call
  local t = {proxy.p, n = proxy.q}
  print("t", t[1], t.n)
  local obj = setmetatable({}, {__index = function(_, k)
    Y("method", k)
    return function(self, v) return k .. ":" .. v end
  end})
  print(obj:greet("bob"))
  -- self-index in a loop
  local sum = 0
  for i = 1, 3 do sum = sum + proxy[i * 10] end
  return "sum", sum
end)
local r = {co()}
local answers = {alpha = "A", [1] = 1, [2] = 2, x = true, y = false, key = "K",
                 p = "P", q = "Q", greet = nil, [10] = 10, [20] = 20, [30] = 30}
while r[1] ~= "sum" do
  print("yielded", table.unpack(r))
  if r[1] == "newindex" then r = {co(r[2] == "x")}
  else r = {co(answers[r[2]])} end
end
return table.unpack(r)
