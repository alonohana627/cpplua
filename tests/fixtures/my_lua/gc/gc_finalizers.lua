-- __gc finalizers (stage 7). The automatic collector is stopped so that
-- collection happens only at the explicit collectgarbage() calls. Finalizers
-- run for unreachable objects at such a call, in reverse order of their
-- setmetatable calls, once each; a __gc added to the metatable after
-- setmetatable does not count; reachable objects are not finalized.

collectgarbage("stop")
print(collectgarbage("isrunning"))

local log = {}
local function make(name)
  return setmetatable({name = name}, {__gc = function(o) log[#log + 1] = o.name end})
end
local function flush(label)
  local s = label .. ":"
  for _, name in ipairs(log) do s = s .. " " .. name end
  print(s)
  log = {}
end

-- Five garbage objects, finalized newest first.
local function garbage(prefix, n)
  for i = 1, n do make(prefix .. i) end
end
garbage("g", 5)
flush("before collect")
collectgarbage()
flush("after collect")
collectgarbage()
flush("again (nothing)")

-- Reachable objects are kept and not finalized.
local keep = {make("kept1"), make("kept2")}
local single = make("single")
garbage("x", 2)
collectgarbage()
flush("with live objects")
single = nil
collectgarbage()
flush("single dropped")
keep[1] = nil
collectgarbage()
flush("kept1 dropped")

-- The finalizer receives the object itself, with its fields intact.
local seen_name, seen_extra
do
  local function f()
    local o = setmetatable({name = "self", extra = {1, 2, 3}}, {__gc = function(o)
      seen_name, seen_extra = o.name, #o.extra
    end})
  end
  f()
end
collectgarbage()
print(seen_name, seen_extra)

-- A __gc field added after setmetatable is not seen: no finalizer.
local late_called = false
local function late()
  local mt = {}
  local o = setmetatable({}, mt)
  mt.__gc = function() late_called = true end
end
late()
collectgarbage()
print("late __gc called:", late_called)

-- Setting the metatable again after adding __gc registers the object.
local again_called = false
local function again()
  local mt = {}
  local o = setmetatable({}, mt)
  mt.__gc = function() again_called = true end
  setmetatable(o, mt)
end
again()
collectgarbage()
print("re-set __gc called:", again_called)

-- A __gc present at setmetatable time may be replaced later: the current
-- one runs.
local function replaced()
  local mt = {__gc = function() log[#log + 1] = "old" end}
  local o = setmetatable({}, mt)
  mt.__gc = function() log[#log + 1] = "new" end
end
replaced()
collectgarbage()
flush("replaced")

-- An object referenced by another garbage object is finalized in the same
-- collection; both finalizers run.
local function pair()
  local a = make("pa")
  local b = make("pb")
  a.other, b.other = b, a
end
pair()
collectgarbage()
flush("cycle")

-- Errors in finalizers do not stop the program (they become warnings,
-- which are off by default).
local function failing() setmetatable({}, {__gc = function() error("in gc") end}) end
failing()
collectgarbage()
print("still running")

-- Many finalized objects at once.
local count = 0
local function many()
  for _ = 1, 200 do setmetatable({}, {__gc = function() count = count + 1 end}) end
end
many()
collectgarbage()
print("finalized", count)

print(collectgarbage("isrunning"), collectgarbage("count") > 0)
collectgarbage("restart")
print(collectgarbage("isrunning"))
collectgarbage("stop")
print(collectgarbage("isrunning"))

return keep[2].name
