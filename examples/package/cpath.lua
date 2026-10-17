-- package.cpath: the search path require uses for C libraries.
print(type(package.cpath))                    --> string
print(package.cpath:find("?", 1, true) ~= nil)   --> true
