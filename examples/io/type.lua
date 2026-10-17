-- io.type(obj): "file", "closed file", or fail (nil) when obj is not a file handle.
local f = assert(io.tmpfile())
print(io.type(f))                             --> file
f:close()
print(io.type(f))                             --> closed file
print(io.type("not a file"))                  --> nil
