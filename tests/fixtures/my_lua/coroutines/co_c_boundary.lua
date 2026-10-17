-- Yielding through a C function that cannot be continued is an error.
local function try(f, ...)
  local co = coroutine.create(f)
  print(coroutine.resume(co, ...))
  print("", coroutine.status(co))
end
-- a table.sort comparator
try(function()
  local t = {3, 1, 2}
  table.sort(t, function(a, b) coroutine.yield("cmp"); return a < b end)
  return "sorted"
end)
-- a string.gsub callback
try(function()
  return (string.gsub("abc", "%w", function(c) coroutine.yield(c) end))
end)
-- a __tostring metamethod called by tostring
try(function()
  return tostring(setmetatable({}, {__tostring = function() coroutine.yield(); return "s" end}))
end)
-- a reader function of load
try(function()
  return load(function() coroutine.yield() end)
end)
-- the yield is caught by a pcall around the C function; the coroutine continues
try(function()
  local ok, e = pcall(table.sort, {2, 1}, function(a, b) coroutine.yield(); return a < b end)
  coroutine.yield("caught", ok, e)
  return "after"
end)
-- pairs calls __pairs with a continuation, so a yield there is fine
try(function()
  local t = setmetatable({}, {__pairs = function(t)
    coroutine.yield("in __pairs")
    return next, {10}, nil
  end})
  for k, v in pairs(t) do return k, v end
end)
-- string.rep, select and other C functions are fine when nothing yields inside them
try(function() return select("#", coroutine.yield(string.rep("ab", 2))) end)
-- the error message of a yield across string.gsub is catchable with its message
local co = coroutine.wrap(function()
  local ok, e = pcall(string.gsub, "x", "x", coroutine.yield)
  return ok, e
end)
return co()
