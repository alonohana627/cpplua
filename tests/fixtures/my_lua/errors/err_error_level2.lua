-- An uncaught error at level 2 reports the line of the call to the function
-- that raised it (stage 8).
local function need_string(s)
  if type(s) ~= "string" then error("need a string, got " .. type(s), 2) end
  return s
end
need_string("fine")
local x = need_string(42)
print(x)
