-- rawget(table, index): reads a field without calling __index.
local t = setmetatable({ real = 1 }, { __index = function() return "default" end })
print(t.missing)                              --> default
print(rawget(t, "missing"))                   --> nil
print(rawget(t, "real"))                      --> 1
