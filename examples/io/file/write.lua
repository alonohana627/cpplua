-- file:write(...): writes strings and numbers; returns the file, so calls chain.
local f = assert(io.tmpfile())
f:write("x = ", 42, "\n"):write("y = ", 1.5, "\n")
f:seek("set")
print(f:read("a"))
--> x = 42
--> y = 1.5
f:close()
