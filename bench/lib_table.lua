-- The table library: sort (with and without an order function), insert,
-- remove, concat, unpack and move.
math.randomseed(42)
local total = 0
for round = 1, 12 do
  local a = {}
  for i = 1, 20000 do a[i] = math.random(1, 1000000) end
  table.sort(a)
  total = total + a[1] + a[#a]
  table.sort(a, function(x, y) return x > y end)
  total = total + a[1]
  local q = {}
  for i = 1, 5000 do table.insert(q, i) end
  for i = 1, 500 do table.insert(q, 1, i) end
  for i = 1, 500 do total = total + table.remove(q, 1) end
  for i = 1, 2000 do total = total + table.remove(q) end
  local words = {}
  for i = 1, 2000 do words[i] = tostring(i) end
  total = total + #table.concat(words, ",")
  total = total + select("#", table.unpack(a, 1, 200))
  total = total + #table.move(a, 1, 1000, 1, {})
end
return total
