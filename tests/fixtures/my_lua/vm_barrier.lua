-- The write barrier on upvalues (stage 5, E9.2): new strings are stored
-- into a closed upvalue while the collector is marking (incremental mode)
-- or once the upvalue is old (generational mode). Without the barrier the
-- collector would free a string the program still uses.
local function box()
  local v = "init"
  return function(x) v = x end, function() return v end
end

local set, get = box()
local total = 0
for i = 1, 2000 do
  set("value " .. i)          -- a new (white, young) string into the upvalue
  local junk = "junk " .. i   -- more allocation, so the collector keeps running
  total = total + #get() + #junk
end

-- Many boxes, each updated in turn, kept alive through a chain of closures.
local function chain(n)
  if n == 0 then return function() return 0 end end
  local s, g = box()
  local rest = chain(n - 1)
  return function(i)
    s("v" .. i .. "." .. n)
    return #g() + rest(i)
  end
end
local all = chain(50)
local sum = 0
for i = 1, 200 do sum = sum + all(i) end

return total, get(), sum
