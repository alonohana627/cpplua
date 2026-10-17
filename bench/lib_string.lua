-- The string library: find, match, gsub, gmatch, format, rep, sub, byte,
-- upper, and method calls on strings through their metatable.
local text = string.rep("the quick brown fox jumps over the lazy dog 12345 ", 40)
local total = 0
for i = 1, 300 do
  for word in text:gmatch("%a+") do total = total + #word end
  local s, n = text:gsub("o", "0")
  total = total + n + #s
  total = total + (text:find("lazy", i % 50 + 1, true) or 0)
  local a, b = text:match("(%a+) (%d+)")
  total = total + #a + tonumber(b)
  total = total + #string.format("%d:%5.2f:%s:%x", i, i / 7, "item", i * 31)
  total = total + text:sub(i % 100, i % 100 + 20):upper():byte(1)
end
return total
