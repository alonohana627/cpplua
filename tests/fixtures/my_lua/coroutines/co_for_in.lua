-- Yields inside for-in iterators, generators used as iterators, and yields in loop bodies.
local Y = coroutine.yield
-- an iterator function that yields to the resumer before each step
local function slow_ipairs(t)
  return function(s, i)
    i = i + 1
    local v = s[i]
    if v == nil then return nil end
    Y("step", i)
    return i, v
  end, t, 0
end
local co = coroutine.wrap(function()
  for i, v in slow_ipairs({"a", "b", "c"}) do
    print("body", i, v)
  end
  -- a generator as the iterator of a for-in loop
  local function gen(n)
    return coroutine.wrap(function() for i = 1, n do Y(i, i * i) end end)
  end
  for i, sq in gen(4) do
    Y("outer", i, sq)
  end
  -- the iterator is a callable table with a yielding __call
  local iter = setmetatable({}, {__call = function(_, s, c)
    if c >= 2 then return nil end
    Y("call-iter", c)
    return c + 1
  end})
  for k in iter, nil, 0 do print("k", k) end
  -- numeric for with a yield in its body
  local acc = 0
  for i = 10, 1, -3 do acc = acc + Y("num", i) end
  print("acc", acc)
  return "done"
end)
local r = {co()}
while r[1] ~= "done" do
  print(table.unpack(r))
  r = {co(r[2])}
end
-- string.gmatch and ipairs inside a coroutine, yielding in the body
local words = coroutine.wrap(function(s)
  for w in string.gmatch(s, "%a+") do Y(w:upper()) end
  for i, v in ipairs({7, 8}) do Y(i .. "=" .. v) end
end)
print(words("one two three"), words(), words(), words(), words())
return r[1]
