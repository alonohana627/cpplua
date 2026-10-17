-- tostring(v): converts any value to a readable string, honouring __tostring.
print(tostring(10), tostring(10.0), tostring(-0.0))   --> 10	10.0	-0.0
print(tostring(nil), tostring(true))                  --> nil	true
print(tostring(1e100), tostring(2^63))                --> 1e+100	9.2233720368548e+18
local p = setmetatable({}, { __tostring = function() return "a point" end })
print(tostring(p))                                    --> a point
print(tostring({}):match("^table: ") ~= nil)          --> true
