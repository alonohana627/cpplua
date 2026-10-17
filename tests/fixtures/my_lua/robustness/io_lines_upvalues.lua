-- debug.setupvalue can replace the upvalues of an io.lines iterator: the
-- format count and the file are checked, never trusted (issues #2 and #3).
local f = io.tmpfile()
f:write("a\nb\n")
f:seek("set")
local it = f:lines("l")
debug.setupvalue(it, 2, 50) -- more formats than the closure has
print(pcall(it))
debug.setupvalue(it, 2, "x") -- not a number: no formats
print(pcall(it))
debug.setupvalue(it, 2, 1)
print(pcall(it))
debug.setupvalue(it, 1, "not a file")
print(pcall(it))
f:close()
