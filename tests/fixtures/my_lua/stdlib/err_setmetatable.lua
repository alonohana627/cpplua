-- An uncaught error (stage 9): setmetatable only accepts tables.
local function attach(value) return setmetatable(value, {}) end
print(type(attach({})))
attach(1)
