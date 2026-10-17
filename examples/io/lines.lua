-- io.lines([filename, ...]): iterates over a file's lines (closing it at the end).
local name = os.tmpname()
local f = assert(io.open(name, "w"))
f:write("alpha\nbeta\n10 20\n")
f:close()

for line in io.lines(name) do
  print(line)
end
--> alpha
--> beta
--> 10 20

-- Formats work as in read: "L" keeps each line's newline.
for line in io.lines(name, "L") do
  io.write("> ", line)
end
--> > alpha
--> > beta
--> > 10 20
os.remove(name)
