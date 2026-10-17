-- os.clock(): CPU time used by the program, in seconds.
local start = os.clock()
local s = 0
for i = 1, 1000000 do s = s + i end
local elapsed = os.clock() - start
print(s, elapsed >= 0)                        --> 500000500000	true
print(string.format("summing took %.3f s", elapsed))
