-- io.close([file]): closes file, or the default output file without an argument.
local name = os.tmpname()
local f = assert(io.open(name, "w"))
print(io.close(f))                            --> true
print(io.type(f))                             --> closed file
os.remove(name)
