-- The math library: random, floor, sqrt, sin, max, abs, fmod, tointeger.
math.randomseed(7)
local total = 0.0
for i = 1, 400000 do
  local x = math.random()
  total = total + math.floor(x * 100) + math.sqrt(i) + math.sin(x)
  total = total + math.max(i % 7, i % 5, 3) + math.abs(-i) % 3 + math.fmod(i, 7)
  total = total + (math.tointeger(i * 1.0) or 0) % 2 + math.random(10)
end
return math.floor(total)
