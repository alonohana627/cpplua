-- debug.traceback([message [, level]]): message followed by a stack traceback.
local function inner() return debug.traceback("where am I?") end
local function outer() return inner() end
local tb = outer()
print(tb:match("^[^\n]*"))                    --> where am I?
print(tb:find("stack traceback:", 1, true) ~= nil)   --> true
print(debug.traceback(42))                    --> 42 (non-string messages come back untouched)
