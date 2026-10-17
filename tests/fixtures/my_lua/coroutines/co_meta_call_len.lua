-- Yields inside __len, __call and __close.
local Y = coroutine.yield
local callable = setmetatable({}, {__call = function(self, a, b)
  local r = Y("call", a, b)
  return r, r * 2
end})
local sized = setmetatable({}, {__len = function() return Y("len") end})
local co = coroutine.wrap(function()
  print("len", #sized, #sized + 1)
  print("call", callable(3, 4))
  local x, y = callable(5)
  print("call2", x, y)
  print("in args", select("#", callable(1, 2)), (callable(0)))
  -- __call in a tail call
  local function tail() return callable("t") end
  print("tail", tail())
  -- __call through a method
  local obj = {f = callable}
  print("method", obj:f(9))
  do
    local res <close> = setmetatable({}, {__close = function(_, e) Y("close", e) end})
    print("leaving block")
  end
  print("after close")
  return "done"
end)
local r = {co()}
while r[1] ~= "done" do
  for i = 1, #r do if type(r[i]) == "table" then r[i] = "a table" end end
  print("  yield", table.unpack(r))
  local k = r[1]
  if k == "len" then r = {co(7)}
  elseif k == "call" then r = {co(10)}
  else r = {co()} end
end
return r[1]
