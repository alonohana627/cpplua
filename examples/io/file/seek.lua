-- file:seek([whence [, offset]]): gets or sets the position; whence is "set", "cur" or "end".
local f = assert(io.tmpfile())
f:write("0123456789")
print(f:seek())                               --> 10 (the current position)
print(f:seek("set", 3), f:read(2))            --> 3	34
print(f:seek("cur", 1), f:read(1))            --> 6	6
print(f:seek("end"))                          --> 10 (also the file's size)
f:close()
