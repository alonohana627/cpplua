-- file:read(...): reads using formats "l", "L", "n", "a" or a byte count.
local f = assert(io.tmpfile())
f:write("line one\n0x10 rest")
f:seek("set")
print(f:read("l"))                            --> line one
print(f:read("n"))                            --> 16
print(f:read(5))                              -->  rest
print(f:read("a") == "", f:read("l"))         --> true	nil
f:close()
