-- loadfile([filename [, mode [, env]]]): like load, but reads the chunk from a file.
local name = os.tmpname()
local f = assert(io.open(name, "w"))
f:write("return greeting .. ', ' .. ...\n")
f:close()

local chunk = assert(loadfile(name, "t", { greeting = "hello" }))
print(chunk("world"))                         --> hello, world
os.remove(name)

local missing, err = loadfile("no/such/file.lua")
print(missing, err:match("^cannot open") ~= nil)   --> nil	true
