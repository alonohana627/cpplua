-- Yields inside pcall and xpcall, and errors after a resume caught by a pcall that yielded.
local Y = coroutine.yield
local co = coroutine.create(function()
  print("pcall1", pcall(function(a)
    local b = Y("in pcall", a)
    return a + b
  end, 1))
  print("pcall2", pcall(function()
    Y("before error")
    error("failed after yield")
  end))
  local ok, e = pcall(function()
    Y("before table error")
    error({code = 7})
  end)
  print("pcall3", ok, type(e), e.code)
  print("xpcall1", xpcall(function(...)
    local x = Y("in xpcall", ...)
    error("xp " .. x)
  end, function(m) return "handled: " .. m end, "p", "q"))
  print("xpcall2", xpcall(function()
    Y("again")
    local t = nil
    return t.k
  end, function(m) return (m:gsub("^.-:%d+: ", "")) end))
  -- nested pcalls with yields at every level
  print("nested", pcall(function()
    Y("level 1")
    local ok, e = pcall(function()
      Y("level 2")
      error("inner", 0)
    end)
    Y("level 1 again", ok, e)
    error("outer", 0)
  end))
  -- pcall of yield directly
  print("direct", pcall(Y, "direct"))
  print("status inside", coroutine.status(coroutine.running()))
  return "end"
end)
local ok, a, b, c = coroutine.resume(co)
while coroutine.status(co) == "suspended" do
  print("  yielded", a, b, c)
  ok, a, b, c = coroutine.resume(co, 41)
end
print(ok, a)
-- an error after the coroutine yielded inside a pcall, not caught: resume returns it
local co2 = coroutine.create(function()
  local ok, e = pcall(Y, 1)
  print("pcall returned", ok, e)
  error("after the pcall")
end)
print(coroutine.resume(co2))
print(coroutine.resume(co2, "v"))
print(coroutine.status(co2))
-- an xpcall handler gets an error raised after a yield
local co3 = coroutine.wrap(function()
  return xpcall(function() Y(); return 1 + {} end, function(m) return "H(" .. m .. ")" end)
end)
co3()
return co3()
