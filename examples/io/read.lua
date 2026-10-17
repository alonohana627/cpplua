-- io.read(...): reads from the default input file using formats "l", "L", "n", "a" or a count.
local name = os.tmpname()
local f = assert(io.open(name, "w"))
f:write("42 3.5\nrest of line\ntail")
f:close()

io.input(name)
print(io.read("n", "n"))                      --> 42	3.5
print(io.read("L"))                           --> (the newline left after 3.5, kept by "L")
print(io.read(4))                             --> rest
print(io.read("l"))                           -->  of line
print(io.read("a"))                           --> tail
print(io.read("l"))                           --> nil (end of file)
io.input():close()
io.input(io.stdin)
os.remove(name)
