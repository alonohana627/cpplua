-- package.loadlib(libname, funcname): dynamically links a C library (when supported).
local f, err, where = package.loadlib("no_such_library", "luaopen_x")
print(f, type(err), where)                    --> nil	string	open
-- (cpplua has no dynamic C libraries, so it answers "absent" instead of "open".)
