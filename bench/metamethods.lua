-- Operators on objects: __add, __mul, __eq, __len and __index functions.
local V = {}
V.__index = V
local function vec(x, y) return setmetatable({x = x, y = y}, V) end
V.__add = function(a, b) return vec(a.x + b.x, a.y + b.y) end
V.__mul = function(a, k) return vec(a.x * k, a.y * k) end
V.__eq = function(a, b) return a.x == b.x and a.y == b.y end
V.__len = function(a) return a.x + a.y end

local defaults = setmetatable({}, {__index = function(t, k) return k * 2 end})

local acc = vec(0, 0)
local same = 0
local d = 0
for i = 1, 200000 do
  acc = acc + vec(1, 2) * 2
  if vec(i, i) == vec(i, i) then
    same = same + 1
  end
  d = d + defaults[i % 100]
end
return #acc, same, d
