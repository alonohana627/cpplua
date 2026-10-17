-- Integer keys around the array/hash boundary (stage 6): a table filled
-- backwards, sparse keys, zero and negative keys, very large integers, and
-- keys moving between the parts as the table grows (rehashing). Whatever the
-- layout, every key must read back its value.

-- Filled backwards: the high keys arrive first.
local back = {}
for i = 100, 1, -1 do back[i] = i * 2 end
local ok = true
for i = 1, 100 do if back[i] ~= i * 2 then ok = false end end
print(#back, ok, back[0], back[101])

-- Sparse keys.
local sparse = {}
for i = 1, 20 do sparse[i * i] = i end
local found, sum = 0, 0
for k, v in pairs(sparse) do found = found + 1 sum = sum + k end
print(found, sum, sparse[1], sparse[4], sparse[400], sparse[2], sparse[399])

-- Zero and negative keys live with the other keys.
local neg = {}
for i = -10, 10 do neg[i] = -i end
local nsum = 0
for i = -10, 10 do nsum = nsum + neg[i] * i end
print(neg[-10], neg[0], neg[10], nsum)

-- Very large integers and their neighbours.
local huge = {}
local maxi = 9223372036854775807
local mini = -9223372036854775807 - 1
huge[maxi] = "max"
huge[mini] = "min"
huge[maxi - 1] = "max-1"
huge[1 << 40] = "2^40"
print(huge[maxi], huge[mini], huge[maxi - 1], huge[1 << 40], huge[(1 << 40) + 1], huge[2 ^ 40])
print(huge[math_missing or maxi + 1], huge[mini - 1])  -- integers wrap around

-- An array that grows a little at a time, interleaved with hash keys, so the
-- table is resized many times.
local grow = {}
for i = 1, 300 do
  grow[i] = i
  grow["k" .. i] = -i
  if i % 7 == 0 then grow[i + 0.5] = true end
end
local isum, ssum, fcount = 0, 0, 0
for k, v in pairs(grow) do
  if type(k) == "number" and v == true then fcount = fcount + 1
  elseif type(k) == "number" then isum = isum + v
  else ssum = ssum + v end
end
print(#grow, isum, ssum, fcount, grow[150], grow.k150, grow[7.5], grow[8.5])

-- Removing keys again: the remaining ones are still found.
for i = 1, 300, 2 do grow[i] = nil grow["k" .. i] = nil end
local left = 0
for _ in pairs(grow) do left = left + 1 end
print(left, grow[1], grow[2], grow.k1, grow.k2, grow[300])

-- An array part with a gap that then gets filled.
local gap = {1, 2, 3}
gap[5] = 5
gap[4] = 4
print(#gap, gap[5])
gap[6] = 6
print(#gap)

-- Float keys that are integers go to the same place as integer keys.
local mixkeys = {}
for i = 1, 10 do mixkeys[i + 0.0] = i end
print(#mixkeys, mixkeys[10], mixkeys[10.0])

return #back, found, left
