-- dofile(filename): runs a Lua file and returns everything it returns.
local name = os.tmpname()
local f = assert(io.open(name, "w"))
f:write("print('running the chunk')\nreturn 1, 2, 3\n")
f:close()

print(dofile(name))
--> running the chunk
--> 1	2	3
os.remove(name)
