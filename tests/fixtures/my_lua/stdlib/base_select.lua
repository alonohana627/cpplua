-- select, rawequal, rawlen, rawget, rawset, type (stage 9): select with
-- positive and negative indices and "#", rawequal ignoring __eq, rawlen
-- ignoring __len, rawget/rawset ignoring __index/__newindex, and their errors.

print(select("#"), select("#", nil), select("#", nil, nil), select("#", 1, nil, 3))
print(select(1, "a", "b", "c"))
print(select(2, "a", "b", "c"))
print(select(3, "a", "b", "c"))
print(select(4, "a", "b", "c"))
print(select(-1, "a", "b", "c"))
print(select(-3, "a", "b", "c"))
print(select(2.0, "a", "b"))
print(pcall(select, -4, "a", "b", "c"))
print(pcall(select, 0, "a"))
print(pcall(select, "x"))
print(pcall(select, 1.5, "a"))
print(pcall(select))

-- A vararg helper that counts with select.
local function count(...) return select("#", ...) end
print(count(), count(nil), count(1, 2, nil), count(table.unpack({1, nil, 3}, 1, 3)))

-- rawequal.
local eqmt = {__eq = function() return true end}
local a, b = setmetatable({}, eqmt), setmetatable({}, eqmt)
print(a == b, rawequal(a, b), rawequal(a, a), rawequal(1, 1.0), rawequal("x", "x"))
print(rawequal(nil, nil), rawequal(1, "1"), pcall(rawequal, 1))

-- rawlen.
local lenmt = {__len = function() return 99 end}
local t = setmetatable({1, 2, 3}, lenmt)
print(#t, rawlen(t), rawlen("hello"), rawlen(""))
print(pcall(rawlen, 5))
print(pcall(rawlen))

-- rawget and rawset.
local proxy = setmetatable({}, {
  __index = function(_, k) return "default " .. tostring(k) end,
  __newindex = function() error("read only", 0) end,
})
print(proxy.x, rawget(proxy, "x"), pcall(function() proxy.y = 1 end))
print(rawset(proxy, "y", 2) == proxy, proxy.y, rawget(proxy, "y"))
print(pcall(rawset, proxy, nil, 1))
print(pcall(rawset, proxy, 0 / 0, 1))
print(pcall(rawget, "s", 1))
print(pcall(rawset, {}, 1))

-- type.
print(type(nil), type(true), type(1), type(1.5), type("s"), type({}), type(print))
print(type(type), type(io.stdout), pcall(type))

return select("#", select(2, 1, 2, 3)), select(-2, "x", "y", "z")
