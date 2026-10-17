-- Integer arithmetic in a numeric for loop: the VM's core instructions
-- (FORLOOP, arithmetic with immediates and constants, comparisons).
local s = 0
for i = 1, 100000000 do
  local t = i * 3 + 1
  if t % 7 < 3 then s = s + t else s = s - 1 end
end
return s