-- rawset(table, index, value): writes a field without calling __newindex.
local log = {}
local t = setmetatable({}, { __newindex = function(_, k) log[#log + 1] = k end })
t.a = 1
rawset(t, "b", 2)
print(rawget(t, "a"), t.b)                    --> nil	2
print(table.concat(log, ","))                 --> a
