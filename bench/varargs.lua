-- Calls with varargs and multiple results.
local function pass(...) return ... end
local function sum3(a, b, c) return a + b + c end
local total = 0
for i = 1, 10000000 do
  total = total + sum3(pass(i, 1, 2))
end
return total