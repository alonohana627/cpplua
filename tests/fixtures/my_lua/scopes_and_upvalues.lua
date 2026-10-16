local counter = 0
local function increment(step)
  counter = counter + step
  return function() return counter, step, print end
end
for i = 1, 2 do
  local x = i
  increment(x)
end
local _ENV = {}
y = 1
