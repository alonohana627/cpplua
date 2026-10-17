-- Errors inside coroutines: string and non-string error objects, levels, runtime errors.
local function run(f, ...)
  local co = coroutine.create(f)
  local r = table.pack(coroutine.resume(co, ...))
  return coroutine.status(co), r.n, table.unpack(r, 1, r.n)
end
print(run(function() error("plain") end))
print(run(function() error("no position", 0) end))
print(run(function() local function g() error("level 2", 2) end; g() end))
print(run(function() error() end))
print(run(function() error(nil) end))
print(run(function() error(false) end))
print(run(function() error(3.5) end))
local obj = setmetatable({}, {__tostring = function() return "custom error" end})
local status, n, ok, e = run(function() error(obj) end)
print(status, n, ok, e == obj, tostring(e))
print(run(function() local x = 1 + nil end))
print(run(function(t) return t.a.b end, {}))
print(run(function() return #5 end))
print(run(function() undefined_function() end))
print(run(function() return ("x"):bad() end))
print(run(function() return 1 < "2" end))
print(run(function() local t = setmetatable({}, {__index = function() error("from __index") end}); return t.k end))
-- an error after some yields
local co = coroutine.create(function()
  for i = 1, 3 do coroutine.yield(i) end
  error({msg = "table after yields"})
end)
for i = 1, 4 do
  local ok, v = coroutine.resume(co)
  print(ok, type(v) == "table" and v.msg or v)
end
print(coroutine.status(co))
-- an error in the error handler of an xpcall inside a coroutine
print(run(function() return xpcall(error, function() error("handler fails") end, "E") end))
-- error with a level that is not a number: an argument error
print(run(function(...) error(select(2, ...)) end, "a", "b", "c"))
return "end"
