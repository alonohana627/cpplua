-- Yielding from the main thread.
local function f() coroutine.yield(1) end
print("before")
f()
