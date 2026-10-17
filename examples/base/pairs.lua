-- pairs(t): iterates all key/value pairs (order unspecified), honouring __pairs.
local t = { x = 1, y = 2, z = 3 }
local items = {}
for k, v in pairs(t) do items[#items + 1] = k .. "=" .. v end
table.sort(items)
print(table.concat(items, " "))               --> x=1 y=2 z=3

local proxy = setmetatable({}, {
  __pairs = function() return next, { a = "from __pairs" }, nil end,
})
for k, v in pairs(proxy) do print(k, v) end   --> a	from __pairs
