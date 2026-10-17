-- An error raised by a __close function at the end of a block is not caught
-- (stage 7): it stops the program with its own message and position.
local resource = setmetatable({}, {__close = function()
  error("could not release the resource")
end})
do
  local r <close> = resource
  print("using the resource")
end
print("not reached")
