-- os.remove(filename): deletes a file (or empty directory); on failure returns fail and a message.
local name = os.tmpname()
assert(io.open(name, "w")):close()
print(os.remove(name))                        --> true
local ok, err = os.remove(name)
print(ok, err ~= nil)                         --> nil	true
