-- An uncaught failed assert (stage 8): "assertion failed!" with the position
-- of the assert call.
local function check(n) assert(n > 0) return n end
print(check(1))
check(-1)
