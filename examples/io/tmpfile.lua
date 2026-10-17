-- io.tmpfile(): a temporary file opened for update, removed when the program ends.
local f = assert(io.tmpfile())
f:write("scratch data")
f:seek("set")                                 -- rewind before reading back
print(f:read("a"))                            --> scratch data
f:close()
