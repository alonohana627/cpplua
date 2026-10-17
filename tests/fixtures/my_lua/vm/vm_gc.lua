-- Garbage collection under a running program (stage 5): a structure kept
-- alive only through closures, garbage strings and closures made in loops,
-- and a deep recursion that grows the stack (and moves it, with open
-- upvalues pointing into it) and lets it shrink again. The results must not
-- depend on when the collector runs (E9.2).

-- A linked list made of closures: each node keeps its string and the rest.
local function cons(head, tail)
  return function(want_head)
    if want_head then return head end
    return tail
  end
end

local list = nil
for i = 1, 1000 do
  list = cons("item" .. i, list)
end

-- Garbage: strings and closures nobody keeps.
local junk = 0
for i = 1, 5000 do
  local s = "temporary " .. i .. " " .. i * 2
  local f = function() return s end
  junk = junk + #f()
end

-- Walk the list: every node and string must still be there.
local count, total = 0, 0
local node = list
while node do
  count = count + 1
  total = total + #node(true)
  node = node(false)
end

-- Deep recursion with a closure over a local at every level: the stack
-- grows (and moves) while upvalues are open, and each closure must still
-- see its own level's value.
local function deep(n)
  local mine = n * 3
  local function get() return mine end
  if n == 0 then return get() end
  local below = deep(n - 1)
  return below + get() - mine * 2 -- the same as below - mine: get() still sees mine
end

-- A long string built in steps, each step garbage.
local s = ""
for i = 1, 300 do s = s .. (i % 10) end

return count, total, junk, deep(3000), #s, list(true), list(false)(false)(true)
