-- Unbounded recursion (E6.7).
local function down(n)
  return 1 + down(n + 1)
end
return down(1)
