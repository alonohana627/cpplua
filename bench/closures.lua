-- Making and calling closures, with upvalues read and written.
local function counter(step)
  local n = 0
  return function()
    n = n + step
    return n
  end
end
local total = 0
for i = 1, 2000000 do
  local c = counter(i)
  total = total + c() + c()
end
return total