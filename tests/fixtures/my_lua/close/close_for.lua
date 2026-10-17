-- The generic for's closing value (stage 7): a fourth value from the
-- explist is a to-be-closed variable of the loop, closed when the loop ends
-- normally, by break, by return, by goto and by an error.

local log = {}
local function say(s) log[#log + 1] = s end
local function flush()
  for i, s in ipairs(log) do print(i, s) end
  log = {}
end

-- An iterator factory returning iterator, state, control and a closer.
local function counting(n, name)
  local closer = setmetatable({}, {__close = function(_, err)
    say("closed " .. name .. " err=" .. tostring(err))
  end})
  return function(_, i)
    if i < n then return i + 1 end
  end, nil, 0, closer
end

for i in counting(3, "normal") do say("i=" .. i) end
flush()

for i in counting(10, "break") do
  if i == 2 then break end
  say("i=" .. i)
end
flush()

local function find(target)
  for i in counting(10, "return") do
    if i == target then return "found " .. i end
  end
  return "not found"
end
print(find(4))
print(find(40))
flush()

do
  for i in counting(10, "goto") do
    if i == 2 then goto out end
  end
  ::out::
  say("after goto")
end
flush()

print(pcall(function()
  for i in counting(10, "error") do
    if i == 3 then error("stop at 3", 0) end
  end
end))
flush()

-- Nested loops, each with its own closing value.
for i in counting(2, "outer") do
  for j in counting(2, "inner" .. i) do say(i .. "," .. j) end
end
flush()

-- A nil closing value is fine, and pairs/ipairs give none.
for _ in function(_, i) if not i then return 1 end end, nil, nil, nil do say("one pass") end
for _, v in ipairs({"x"}) do say(v) end
flush()

-- A non-closable fourth value is an error when the loop starts.
print(pcall(function() for _ in next, {}, nil, 42 do end end))

return "done"
