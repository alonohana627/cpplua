-- io.output([file]): gets or sets the default output file (by name or by handle).
local name = os.tmpname()
io.output(name)                               -- io.write now goes to the file
io.write("captured text")
io.output():close()
io.output(io.stdout)                          -- restore the default

local f = assert(io.open(name))
print(f:read("a"))                            --> captured text
f:close()
os.remove(name)
