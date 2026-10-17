-- package.searchpath(name, path [, sep [, rep]]): finds a file for name on a path.
local base = os.tmpname()
assert(io.open(base .. "_found.lua", "w")):close()

print(package.searchpath("x", base .. "_found.lua") == base .. "_found.lua")   --> true
local found, err = package.searchpath("a.b", "./?.lua;./?/init.lua", ".", "/")
print(found, (err:gsub("%s+", " ")))          --> nil	no file './a/b.lua' no file './a/b/init.lua'
os.remove(base .. "_found.lua")
os.remove(base)
