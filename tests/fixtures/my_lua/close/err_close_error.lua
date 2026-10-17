-- An error passing through a block whose __close also fails (stage 7): the
-- close function's error replaces the original one. (The standalone lua adds a
-- traceback to the original message before closing, so it is not printed.)
local function cleanup()
  return setmetatable({}, {__close = function(_, err)
    print("closing after an error of type " .. type(err))
    error("cleanup failed", 0)
  end})
end
local function work()
  local c <close> = cleanup()
  error("work failed")
end
work()
