-- Coroutines (stage 10): values passed both ways through resume and yield.
local co = coroutine.create(function(a, b, ...)
  print("start", a, b, select("#", ...), ...)
  local x, y, z = coroutine.yield(a + b, nil, "two")
  print("got", x, y, z)
  local n = select("#", coroutine.yield())
  print("count", n)
  local t = table.pack(coroutine.yield(nil, nil, nil))
  print("packed", t.n, t[1], t[2], t[3], t[4])
  return "done", nil, 3
end)
print(type(co))
print(coroutine.resume(co, 1, 2, nil, "v", nil))
print(coroutine.resume(co, "x", nil, "z", "extra"))
print(coroutine.resume(co))
print(coroutine.resume(co, nil, false, nil, nil))
print(coroutine.status(co))
print(select("#", coroutine.resume(coroutine.create(function() end))))
print(select("#", coroutine.resume(coroutine.create(function() return nil, nil end))))
print(coroutine.resume(coroutine.create(function(...) return ... end), 1, nil, 3, nil))
print(coroutine.resume(coroutine.create(function(...) return select("#", ...) end)))
-- many values both ways
local big = coroutine.create(function(...)
  local args = table.pack(...)
  local got = table.pack(coroutine.yield(table.unpack(args, 1, args.n)))
  return got.n, got[1], got[got.n]
end)
local vals = {}
for i = 1, 200 do vals[i] = i * 2 end
local res = table.pack(coroutine.resume(big, table.unpack(vals)))
print(res.n, res[1], res[2], res[201])
print(coroutine.resume(big, table.unpack(vals, 1, 150)))
return coroutine.status(big)
