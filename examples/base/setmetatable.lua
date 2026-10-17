-- setmetatable(table, metatable): sets (or with nil, removes) a metatable; returns table.
local Vec = {}
Vec.__index = Vec
Vec.__add = function(a, b) return Vec.new(a.x + b.x, a.y + b.y) end
Vec.__tostring = function(v) return "(" .. v.x .. ", " .. v.y .. ")" end
function Vec.new(x, y) return setmetatable({ x = x, y = y }, Vec) end

print(Vec.new(1, 2) + Vec.new(3, 4))          --> (4, 6)

local t = setmetatable({}, Vec)
setmetatable(t, nil)
print(getmetatable(t))                        --> nil
