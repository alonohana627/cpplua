-- Float arithmetic in a loop.
local x, y = 0.0, 1.0
for i = 1, 50000000 do
  x = x + y * 0.5
  y = y * 0.999999 + 0.25 / (i + 1.0)
end
return x + y