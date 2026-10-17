-- Records: small tables made by constructors, fields read and written by
-- name, and the garbage they leave.
local count = 0
local last
for i = 1, 1000000 do
  local p = {x = i, y = i * 2, name = "point"}
  p.x = p.x + p.y
  p.z = p.x - i
  if p.z > 0 then
    count = count + 1
  end
  last = p
end
local keys = {}
for i = 1, 200000 do
  keys["k" .. (i % 1000)] = i
end
local sum = 0
for k, v in pairs(keys) do
  sum = sum + v
end
return count, last.z, sum
