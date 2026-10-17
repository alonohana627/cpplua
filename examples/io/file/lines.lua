-- file:lines(...): iterates over this file's lines (without closing it at the end).
local f = assert(io.tmpfile())
f:write("1 2\n3 4\n")
f:seek("set")
for a, b in f:lines("n", "n") do
  print(a + b)
end
--> 3
--> 7
print(io.type(f))                             --> file (still open)
f:close()
