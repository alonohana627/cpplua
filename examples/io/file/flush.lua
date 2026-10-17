-- file:flush(): saves any buffered writes of this file.
local name = os.tmpname()
local w = assert(io.open(name, "w"))
w:write("flushed")
w:flush()                                     -- the data is on disk before the handle closes

local r = assert(io.open(name))
print(r:read("a"))                            --> flushed
r:close()
w:close()
os.remove(name)
