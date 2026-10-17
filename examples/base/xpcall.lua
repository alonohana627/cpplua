-- xpcall(f, msgh, ...): like pcall, but msgh can rewrite the error first.
local function handler(err)
  return "handled: " .. tostring(err)
end
print(xpcall(function(x) return x * 2 end, handler, 21))   --> true	42
print(xpcall(function() error("bad", 0) end, handler))     --> false	handled: bad

-- A common use: attach a traceback to the message.
local ok, msg = xpcall(function() error("deep") end, debug.traceback)
print(ok, msg:match("stack traceback") ~= nil)             --> false	true
