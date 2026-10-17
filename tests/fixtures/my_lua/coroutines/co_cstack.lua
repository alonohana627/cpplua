-- Resumes nested too deeply: "C stack overflow".
local depth = 0
local function nest()
  depth = depth + 1
  local co = coroutine.create(nest)
  local ok, e = coroutine.resume(co)
  if not ok then error(e, 0) end
  return e
end
local ok, e = pcall(nest)
print(ok, e)
print(depth > 150, depth < 250)
-- the same through coroutine.wrap: each level adds a position
local level = 0
local function wrapnest()
  level = level + 1
  return coroutine.wrap(wrapnest)()
end
local ok2, e2 = pcall(wrapnest)
print(ok2, (string.gsub(e2, "co_cstack.lua:%d+: ", "")))
print(level > 150, level < 250, depth == level)
-- after unwinding, coroutines work again
return coroutine.wrap(function() return "fine" end)()
