-- io.open(filename [, mode]): opens a file; on failure returns fail, a message and an error code.
local name = os.tmpname()
local f = assert(io.open(name, "w"))
f:write("hello")
f:close()

f = assert(io.open(name, "r"))
print(f:read("a"))                            --> hello
f:close()

local missing, err = io.open("no/such/dir/file.txt")
print(missing, err:match("No such file") ~= nil)   --> nil	true
os.remove(name)
