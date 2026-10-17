-- file:close(): closes the file; closing a popen'ed file returns the exit status.
local f = assert(io.tmpfile())
print(f:close())                              --> true
print(pcall(f.close, f))                      --> false	attempt to use a closed file
