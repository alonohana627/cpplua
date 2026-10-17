-- os.rename(oldname, newname): renames a file or directory.
local old, new = os.tmpname(), os.tmpname()
os.remove(new)
local f = assert(io.open(old, "w"))
f:write("moved")
f:close()

print(os.rename(old, new))                    --> true
print(io.open(old), (io.open(new):read("a"))) --> nil	...	moved
os.remove(new)
