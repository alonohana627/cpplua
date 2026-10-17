-- table.move compares the two tables (calling __eq) only when the ranges
-- overlap, as Lua does (issue #9).
local calls = 0
local mt = {__eq = function() calls = calls + 1 error("no __eq here") end}
local a = setmetatable({1, 2, 3}, mt)
local b = setmetatable({}, mt)
table.move(a, 1, 3, 5, b) -- t > e: no compare
table.move(a, 2, 3, 1, b) -- t <= f: no compare
print(calls, b[1], b[2], b[5], b[7])
print(pcall(table.move, a, 1, 3, 2, b)) -- overlapping: compared
