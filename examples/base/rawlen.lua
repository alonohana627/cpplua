-- rawlen(v): length of a table or string without calling __len.
local t = setmetatable({ 1, 2, 3 }, { __len = function() return 99 end })
print(#t)                                     --> 99
print(rawlen(t))                              --> 3
print(rawlen("hello"))                        --> 5
