-- A size no memory can hold is the catchable error "not enough memory",
-- never a C++ exception that ends the program (issues #5 and #7): a huge
-- count, and a negative one, which reads as a huge size.
local f = io.tmpfile()
f:write("abc")
f:seek("set")
print(pcall(f.read, f, 1 << 50))
print(pcall(f.read, f, -1))
print(pcall(function() for _ in f:lines(-1) do end end))
print(f:read("a")) -- the file still works
f:close()
print(pcall(string.rep, "x", 1 << 40))
