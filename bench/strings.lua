-- Strings: short concatenations (interned), conversions of numbers, and the
-- garbage they leave behind.
local n = 0
for i = 1, 3000000 do
  local s = "item" .. i .. ":" .. (i % 100)
  n = n + #s
end
return n