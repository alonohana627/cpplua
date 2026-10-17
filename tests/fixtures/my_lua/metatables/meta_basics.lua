-- setmetatable and getmetatable (stage 7): setmetatable returns its table,
-- getmetatable gives the metatable or nil, nil removes it, and a __metatable
-- field protects it: getmetatable returns that field instead and
-- setmetatable fails.

local t = {}
local mt = {}
print(getmetatable(t))
print(setmetatable(t, mt) == t, getmetatable(t) == mt)

-- Removing the metatable.
print(setmetatable(t, nil) == t, getmetatable(t))

-- One metatable shared by several tables.
local a = setmetatable({}, mt)
local b = setmetatable({}, mt)
print(getmetatable(a) == getmetatable(b))

-- Replacing the metatable.
local mt2 = {}
setmetatable(a, mt2)
print(getmetatable(a) == mt2, getmetatable(b) == mt)

-- Non-table values: strings have a metatable only with the string library,
-- which lua2 does not have, so only check numbers, booleans and nil.
print(getmetatable(1), getmetatable(true), getmetatable(nil))

-- __metatable protects the metatable.
local protected = setmetatable({}, {__metatable = "locked"})
print(getmetatable(protected))
print(pcall(setmetatable, protected, {}))
print(pcall(setmetatable, protected, nil))
print(getmetatable(protected))

-- __metatable can be any value, even false.
local hidden = setmetatable({}, {__metatable = false})
print(getmetatable(hidden))
print(pcall(setmetatable, hidden, {}))

-- Bad arguments.
print(pcall(setmetatable, 1, {}))
print(pcall(setmetatable, {}, 2))
print(pcall(setmetatable, {}))
print(pcall(getmetatable))

-- A metatable can be its own metatable.
local self_mt = {}
setmetatable(self_mt, self_mt)
print(getmetatable(self_mt) == self_mt)

-- Fields of the metatable are not fields of the table.
local plain = setmetatable({}, {x = 1, __index = nil})
print(plain.x, getmetatable(plain).x)

return getmetatable(protected), getmetatable(b) == mt
