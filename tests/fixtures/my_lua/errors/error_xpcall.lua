-- xpcall (stage 8): like pcall, but on an error the message handler is
-- called with the error object and its result becomes the error result.
-- Extra arguments go to the function. The handler runs at the point of the
-- error (before unwinding), receives non-string errors unchanged, and an
-- error inside the handler gives what real Lua gives (see the output).

local function add_context(msg) return "handled: " .. tostring(msg) end

print(xpcall(function() return "fine", 2 end, add_context))
print(xpcall(function() error("broken", 0) end, add_context))
print(xpcall(function() error("broken") end, add_context))
print(xpcall(function() local t = nil return t[1] end, add_context))

-- Extra arguments.
print(xpcall(function(a, b, ...) return a + b, select("#", ...) end, add_context, 1, 2, 3, 4))
print(xpcall(function(a) return a.x end, add_context, nil))

-- The handler can return a table (or anything else).
local ok, info = xpcall(function() error("with table", 0) end, function(m)
  return {message = m, handled = true}
end)
print(ok, type(info), info.message, info.handled)

-- Only the first result of the handler is kept.
print(xpcall(error, function(m) return m, "dropped" end, "first only", 0))

-- Non-string errors reach the handler unchanged.
local obj = {}
print(xpcall(function() error(obj) end, function(e) return e == obj end))
print(xpcall(function() error(12) end, function(e) return type(e) .. " " .. e end))
print(xpcall(function() error() end, function(e) return "got " .. tostring(e) end))
print(xpcall(function() error(false) end, function(e) return e == false end))

-- The handler runs before the stack unwinds: to-be-closed variables of the
-- failing function are closed after it.
local order = {}
print(xpcall(function()
  local c <close> = setmetatable({}, {__close = function() order[#order + 1] = "close" end})
  error("x", 0)
end, function(m) order[#order + 1] = "handler" return m end))
print(order[1], order[2])

-- A handler that counts how often it is called.
local calls = 0
local function counting(m) calls = calls + 1 return m end
for i = 1, 3 do xpcall(error, counting, i) end
xpcall(function() end, counting)
print(calls)

-- An error inside the handler.
print(xpcall(function() error("original", 0) end, function() error("in handler", 0) end))
print(xpcall(function() error("original", 0) end, function(m) local t = nil return t.x end))

-- A handler that is not a function is rejected before the call.
print(pcall(xpcall, function() error("e", 0) end, 42))
print(pcall(xpcall, function() return "no error" end, nil))
-- xpcall without a handler argument.
print(pcall(xpcall, print))

-- xpcall inside pcall and the other way around.
print(pcall(xpcall, function() error("inner", 0) end, add_context))
print(xpcall(function() return pcall(error, "deep", 0) end, add_context))
print(xpcall(function() pcall(error, "caught") error("escaped", 0) end, add_context))

return xpcall(function(...) return ... end, add_context, "r1", "r2")
