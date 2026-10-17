-- To-be-closed variables (stage 7): local x <close> calls x's __close when
-- the variable goes out of scope, with the value and nil, in reverse order
-- of declaration; on normal block end, break, return (after the return
-- values are computed), goto out of the block, and in loops every
-- iteration. nil and false are allowed and ignored.

local log = {}
local function say(s) log[#log + 1] = s end
local function closer(name)
  return setmetatable({name = name}, {__close = function(self, err)
    say("close " .. self.name .. " err=" .. tostring(err))
  end})
end
local function flush()
  for i, s in ipairs(log) do print(i, s) end
  log = {}
end

-- Block end, reverse order.
do
  local a <close> = closer("a")
  local b <close> = closer("b")
  say("body")
end
say("after")
flush()

-- The close function gets the variable's value.
do
  local obj = closer("obj")
  local got
  getmetatable(obj).__close = function(v) got = v end
  do local x <close> = obj end
  print(got == obj)
end

-- break out of a loop: closed at every iteration and at the break.
for i = 1, 5 do
  local c <close> = closer("loop" .. i)
  if i == 3 then break end
  say("iteration " .. i)
end
flush()

-- while loop with close in its body.
local n = 0
while n < 2 do
  n = n + 1
  local w <close> = closer("while" .. n)
end
flush()

-- return: values are computed before closing.
local function f()
  local r <close> = closer("r")
  say("returning")
  return (function() say("computing result") return "result" end)()
end
print(f())
flush()

-- return of several values and a tail-call-looking return (not a tail call
-- when there is a to-be-closed variable).
local function g(x)
  local q <close> = closer("q")
  return x, x * 2, f()
end
print(g(21))
flush()

-- goto out of a block.
do
  local i = 1
  ::again::
  do
    local gt <close> = closer("goto" .. i)
    if i < 3 then
      i = i + 1
      goto again
    end
  end
end
flush()

-- nil and false are allowed and do nothing.
do
  local none <close> = nil
  local no <close> = false
  local yes <close> = closer("yes")
  say("nil and false ok")
end
flush()

-- Nested functions: each closes its own variables.
local function inner() local i <close> = closer("inner") say("in inner") end
local function outer() local o <close> = closer("outer") inner() say("back in outer") end
outer()
flush()

-- A const-like use: the variable cannot be assigned, but its fields can.
do
  local res <close> = closer("res")
  res.name = "renamed"
end
flush()

-- The close value can be any value with __close, also a callable object.
local closed_count = 0
local counter_mt = {__close = setmetatable({}, {__call = function() closed_count = closed_count + 1 end})}
for _ = 1, 4 do local c <close> = setmetatable({}, counter_mt) end
print(closed_count)

return #log
