-- An algorithm on arrays: quicksort written in Lua, with a seeded random
-- generator (no standard library).
local seed = 42
local function random(n)
  seed = (seed * 1103515245 + 12345) % 2147483648
  return seed % n
end

local function quicksort(a, lo, hi)
  while lo < hi do
    local p = a[(lo + hi) // 2]
    local i, j = lo, hi
    while i <= j do
      while a[i] < p do i = i + 1 end
      while a[j] > p do j = j - 1 end
      if i <= j then
        a[i], a[j] = a[j], a[i]
        i, j = i + 1, j - 1
      end
    end
    if j - lo < hi - i then
      quicksort(a, lo, j)
      lo = i
    else
      quicksort(a, i, hi)
      hi = j
    end
  end
end

local checksum = 0
for round = 1, 5 do
  local a = {}
  for i = 1, 100000 do
    a[i] = random(1000000)
  end
  quicksort(a, 1, #a)
  for i = 2, #a do
    if a[i - 1] > a[i] then error("not sorted") end
  end
  checksum = checksum + a[1] + a[#a] + a[50000]
end
return checksum
