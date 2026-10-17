-- Arithmetic on an upvalue (E8.2).
local up
local function f()
  return up * 2
end
return f()
