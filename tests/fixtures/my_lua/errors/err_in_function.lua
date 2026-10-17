-- A runtime error inside a function called from the main chunk (stage 8):
-- the message has the line inside the function.
local function average(list)
  local sum = 0
  for i = 1, #list do sum = sum + list[i] end
  return sum / #list
end
print(average({1, 2, 3}))
print(average({1, {}, 3}))
