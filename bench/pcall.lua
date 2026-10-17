-- Protected calls: pcall that succeeds, pcall that catches error(), and
-- to-be-closed variables.
local function ok(x) return x + 1 end
local function fails(x) error({code = x}) end
local closer = setmetatable({}, {__close = function() end})

local sum, caught = 0, 0
for i = 1, 200000 do
  local good, v = pcall(ok, i)
  if good then sum = sum + v end
  local bad, e = pcall(fails, i)
  if bad then error("expected an error") end
  caught = caught + e.code % 7
  do
    local c <close> = closer
  end
end
return sum, caught
