-- io.input([file]): gets or sets the default input file (by name or by handle).
local name = os.tmpname()
local f = assert(io.open(name, "w"))
f:write("first line\nsecond line\n")
f:close()

io.input(name)                                -- from now on io.read reads this file
print(io.read())                              --> first line
print(io.input():read())                      --> second line
io.input():close()
io.input(io.stdin)                            -- restore the default
os.remove(name)
