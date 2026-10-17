-- table.pack and table.unpack (stage 9): pack stores n (counting trailing
-- nils), unpack with default and explicit ranges, nils inside the range,
-- empty and reversed ranges, negative indices, __index on the source, and
-- the "too many results" error for huge ranges.

local p = table.pack(1, nil, 3, nil)
print(p.n, p[1], p[2], p[3], p[4], select("#", table.unpack(p, 1, p.n)))
print(table.pack().n, next(table.pack(), nil) == "n", table.pack(nil).n)
local q = table.pack("a", "b")
print(q.n, q[1], q[2], q[3])

print(table.unpack({1, 2, 3}))
print(table.unpack({1, 2, 3}, 2))
print(table.unpack({1, 2, 3}, 2, 3))
print(table.unpack({1, 2, 3}, 3, 5))
print(table.unpack({1, 2, 3}, -1, 1))
print(select("#", table.unpack({1, 2, 3}, 3, 2)), select("#", table.unpack({})))
print(table.unpack({[0] = "zero", "one"}, 0))
print(table.unpack({1, 2, 3}, 2.0, 3.0))
print(select("#", table.unpack({}, 1, 5)))
print(table.unpack({"x"}, math.maxinteger, math.maxinteger))
print(select("#", table.unpack({}, math.mininteger, math.mininteger + 2)))

-- unpack honours __index (and the default end uses __len).
local virtual = setmetatable({}, {
  __index = function(_, i) return i * i end,
  __len = function() return 4 end,
})
print(table.unpack(virtual))
print(table.unpack(virtual, 5, 6))

-- pack and unpack round trip through a function call.
local function pass(...) return table.unpack(table.pack(...), 1, select("#", ...)) end
print(pass(nil, false, nil))
print(select("#", pass(nil, nil, nil)))

-- There is no global unpack (it moved to the table library in 5.2).
print(unpack, type(table.unpack))

print(pcall(table.unpack, {}, 1, 1e8))
print(pcall(table.unpack, {}, math.mininteger, math.maxinteger))
print(pcall(table.unpack, {}, 1.5))
print(pcall(table.unpack, nil))
print(pcall(table.unpack))

return table.unpack({"r1", "r2"})
