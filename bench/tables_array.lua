-- Arrays: filling, reading and summing integer keys, # and ipairs.
local n = 200000
local total = 0
for round = 1, 10 do
  local t = {}
  for i = 1, n do
    t[i] = i * round
  end
  local sum = 0
  for i = 1, #t do
    sum = sum + t[i]
  end
  for _, v in ipairs(t) do
    sum = sum - v
  end
  total = total + sum + #t
end
return total
