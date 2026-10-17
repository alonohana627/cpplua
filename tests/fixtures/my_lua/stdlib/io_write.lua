-- io.write and io.type (stage 9): io.write writes strings and numbers
-- (integers with %d, floats with plain %.14g, so 3.0 and -0.0 come out as
-- "3" and "-0", unlike tostring) without separators or newlines, returns the
-- file so calls chain, and rejects other values; io.type tells files from
-- other values; io.stdout:write is the same. Nothing is read and no file is
-- opened.

io.write("plain ", 42, " ", 1.5, " ", 3.0, " ", -0.0, " ", 1e100, " ", 2 ^ 63, " ", 10 // 3, "\n")
io.write()
io.write("")
io.write("no newline yet; ")
io.write("now\n")

-- io.write returns the file it wrote to.
local f = io.write("chained ")
print(io.type(f), f == io.stdout, io.type(io.stdout), io.type(io.stderr), io.type(io.stdin))
io.write("a"):write("b"):write("c", "\n")
io.stdout:write("through io.stdout ", 7, "\n")

-- io.type of other values.
print(io.type(nil), io.type(42), io.type("file"), io.type({}), io.type(print))
print(pcall(io.type))

-- Numbers keep their Lua formatting.
io.write(math.maxinteger, " ", math.mininteger, " ", 0.1, " ", 1 / 3, " ", math.huge, " ", -math.huge, "\n")
io.write(string.format("%5.2f|%-4d|%x\n", 3.14159, 7, 255))

-- Invalid values.
print(pcall(io.write, {}))
print(pcall(io.write, "ok ", nil))
print(pcall(io.write, true))
io.write("\n")
print(pcall(io.stdout.write, io.stdout, print))
print(pcall(io.stdout.write, 42))

-- The file metatable.
print(tostring(io.stdout):sub(1, 6), type(getmetatable(io.stdout)), getmetatable(io.stdout).__name)
print(type(io.stdout.write), io.stdout.write == io.stderr.write)

io.write("last line\n")
return io.type(io.stdout)
