-- error(message [, level]): raises an error; level picks the position prefix.
local function check(n)
  if type(n) ~= "number" then
    error("expected a number", 2)             -- level 2 blames the caller
  end
  return n
end

print(pcall(check, "x"))                      --> false	expected a number (called through pcall: no position)
print(pcall(function() check("x") end))       --> false	...error.lua:10: expected a number
print(pcall(error, "no position", 0))         --> false	no position

-- Error objects need not be strings.
local _, e = pcall(error, { code = 7 })
print(e.code)                                 --> 7
