-- collectgarbage options (stage 7): "collect" (the default) returns 0,
-- "count" a positive number of kilobytes, "step" a boolean, "stop",
-- "restart" and "isrunning" control and report the automatic collector,
-- "incremental" and "generational" switch modes and return the previous
-- mode. Only things that do not depend on memory sizes or timing are
-- printed. Collected tables stay collected whichever mode is used.

collectgarbage("generational")      -- the starting mode is not printed
print(collectgarbage("incremental"))
print(collectgarbage("incremental"))
print(collectgarbage("generational"))
print(collectgarbage("generational"))
print(collectgarbage("incremental"))

print(collectgarbage(), collectgarbage("collect"))
print(type(collectgarbage("count")), collectgarbage("count") > 0)
print(type(collectgarbage("step")), type(collectgarbage("step", 100)))

print(collectgarbage("isrunning"))
collectgarbage("stop")
print(collectgarbage("isrunning"))
collectgarbage("restart")
print(collectgarbage("isrunning"))

-- Memory freed by a collection: count goes down after dropping a lot.
collectgarbage("stop")
local function allocate()
  local t = {}
  for i = 1, 20000 do t[i] = {i} end
  return t
end
local big = allocate()
local with_big = collectgarbage("count")
big = nil
collectgarbage()
print(collectgarbage("count") < with_big)
collectgarbage("restart")

-- Weak entries are cleared in both modes.
local function weak_survivors(mode)
  collectgarbage(mode)
  collectgarbage("stop")
  local w = setmetatable({}, {__mode = "v"})
  local function fill() for i = 1, 100 do w[i] = {} end end
  fill()
  local keep = {}
  w[101] = keep
  collectgarbage()
  local n = 0
  for _ in pairs(w) do n = n + 1 end
  collectgarbage("restart")
  return n, w[101] == keep
end
print(weak_survivors("incremental"))
print(weak_survivors("generational"))
print(weak_survivors("incremental"))

-- Invalid options.
print(pcall(collectgarbage, "nonsense"))
print(pcall(collectgarbage, 42))

return collectgarbage("isrunning")
