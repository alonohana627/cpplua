-- rawequal(v1, v2): equality without calling __eq.
local mt = { __eq = function() return true end }
local a, b = setmetatable({}, mt), setmetatable({}, mt)
print(a == b)                                 --> true
print(rawequal(a, b))                         --> false
print(rawequal(a, a))                         --> true
