-- select(index, ...): the arguments after index, or their count with "#".
print(select("#", "a", nil, "c"))             --> 3
print(select(2, "a", "b", "c"))               --> b	c
print(select(-1, "a", "b", "c"))              --> c

local function sum(...)
  local s = 0
  for i = 1, select("#", ...) do s = s + (select(i, ...)) end
  return s
end
print(sum(1, 2, 3, 4))                        --> 10
