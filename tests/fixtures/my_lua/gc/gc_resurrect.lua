-- Resurrection (stage 7): a finalizer can store its object somewhere
-- reachable, bringing it back. The object is then alive again with its
-- fields, but its finalizer does not run a second time unless setmetatable
-- registers it again. The collector is stopped; collection happens only at
-- the explicit collectgarbage() calls.

collectgarbage("stop")

local saved = {}
local runs = 0
local mt = {__gc = function(o)
  runs = runs + 1
  saved[#saved + 1] = o
end}

local function create()
  local o = setmetatable({name = "phoenix", data = {10, 20, 30}}, mt)
  o.data.back = o
end
create()
print(#saved, runs)
collectgarbage()
print(#saved, runs)

-- The object is back, with everything it referred to.
local o = saved[1]
print(o.name, o.data[3], o.data.back == o, getmetatable(o) == mt)

-- Dropping it again: no second finalization.
saved[1] = nil
o = nil
collectgarbage()
collectgarbage()
print(#saved, runs)

-- Registering it again with setmetatable allows another finalization.
local second = 0
local function create_again()
  local obj = setmetatable({}, mt)
  saved[1] = obj
end
create_again()
saved[1] = nil
collectgarbage()           -- finalized, resurrected into saved[1]
print(#saved, runs)
local function register_again()
  local back = saved[1]
  saved[1] = nil
  setmetatable(back, {__gc = function() second = second + 1 end})
end
register_again()
collectgarbage()
print(runs, second)

-- A finalizer that resurrects other objects it refers to.
local rescued
local function holder()
  local child = {value = "child value"}
  setmetatable({child = child}, {__gc = function(h) rescued = h.child end})
end
holder()
collectgarbage()
print(rescued and rescued.value)

-- A finalizer that creates new objects with finalizers: those run in a
-- later collection, not in the same one.
local order = {}
local function spawner()
  setmetatable({}, {__gc = function()
    order[#order + 1] = "parent"
    setmetatable({}, {__gc = function() order[#order + 1] = "child" end})
  end})
end
spawner()
collectgarbage()
print(#order, order[1], order[2])
collectgarbage()
print(#order, order[1], order[2])

return runs
