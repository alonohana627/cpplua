-- Coroutines are collected: weak tables lose them, and what only their stacks reach.
-- Everything is made inside functions, so no dead register of the main chunk keeps it.
collectgarbage("stop")
local weak = setmetatable({}, {__mode = "v"})
local function make_garbage()
  -- a suspended coroutine nobody references
  local co = coroutine.create(function(x)
    local local_table = {}
    weak.inner = local_table
    coroutine.yield(x)
  end)
  coroutine.resume(co, 1)
  weak.suspended = co
  -- a dead coroutine
  local dead = coroutine.create(function() return 1 end)
  coroutine.resume(dead)
  weak.dead = dead
  -- a coroutine never started, and the function it holds
  local f = function() end
  weak.fn = f
  weak.fresh = coroutine.create(f)
  -- a wrapped generator that keeps a table on its stack
  local t = {}
  weak.gen_table = t
  local gen = coroutine.wrap(function() local keep = t; coroutine.yield(1); coroutine.yield(2) end)
  gen()
end
make_garbage()
-- a coroutine that is still referenced
local kept = coroutine.create(function() coroutine.yield() end)
coroutine.resume(kept)
weak.kept = kept
local names = {"suspended", "inner", "dead", "fn", "fresh", "gen_table", "kept"}
local function show(when)
  local parts = {}
  for _, k in ipairs(names) do parts[#parts + 1] = k .. "=" .. (weak[k] and "yes" or "no") end
  print(when, table.concat(parts, " "))
end
show("before")
collectgarbage()
show("after")
-- a suspended coroutine referenced only from another suspended coroutine's stack
local holder = {}
local function make_chain()
  local inner = coroutine.create(function() coroutine.yield() end)
  coroutine.resume(inner)
  weak.chained = inner
  local outer = coroutine.create(function() local i = inner; coroutine.yield() end)
  coroutine.resume(outer)
  weak.outer = outer
  holder.outer = outer
end
make_chain()
names = {"chained", "outer", "kept"}
collectgarbage()
show("held")
holder.outer = nil
collectgarbage()
show("released")
-- a finalizer of an object on a collected coroutine's stack runs; its __close does not
local function make_closing()
  local co = coroutine.create(function()
    local x <close> = setmetatable({}, {
      __close = function() print("__close runs") end,
      __gc = function() print("__gc runs") end})
    coroutine.yield()
  end)
  coroutine.resume(co)
end
make_closing()
collectgarbage()
print("after finalizer")
-- weak keys: coroutines as keys
local wk = setmetatable({}, {__mode = "k"})
local alive = coroutine.create(print)
wk[alive] = "alive"
local function add_key() wk[coroutine.create(print)] = "garbage" end
add_key()
collectgarbage()
local count = 0
for k, v in pairs(wk) do count = count + 1; print(type(k), v) end
return count
