-- Yields in __index and __newindex of _ENV: reading and writing globals yields.
local Y, print, rawget, rawset, setmetatable = coroutine.yield, print, rawget, rawset, setmetatable
local co = coroutine.wrap(function()
  local _ENV = setmetatable({}, {
    __index = function(t, k) return Y("get", k) end,
    __newindex = function(t, k, v) Y("set", k, v); rawset(t, k, v * 10) end,
  })
  answer = 4
  print("raw", rawget(_ENV, "answer"), answer)
  local s = alpha + beta
  print("sum", s)
  gamma = alpha
  print("gamma", gamma)
  -- a global function call: the function itself comes from __index
  return fn(1, 2)
end)
local r = {co()}
while true do
  print("  ", table.unpack(r))
  if r[1] == "get" then
    if r[2] == "fn" then r = {co(function(a, b) return "fn", a + b end)}
    else r = {co(#r[2])} end
  elseif r[1] == "set" then r = {co()}
  else break end
end
return "end"
