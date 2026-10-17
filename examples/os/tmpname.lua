-- os.tmpname(): a name usable for a temporary file (the program must remove it itself).
local name = os.tmpname()
print(type(name), #name > 0)                  --> string	true
local f = assert(io.open(name, "w"))
f:write("temporary")
f:close()
os.remove(name)
