-- getmetatable(object): returns the metatable, or its __metatable field.
local mt = {}
local t = setmetatable({}, mt)
print(getmetatable(t) == mt)                  --> true
print(getmetatable({}))                       --> nil
print(getmetatable("abc").__index == string)  --> true

local locked = setmetatable({}, { __metatable = "locked" })
print(getmetatable(locked))                   --> locked
