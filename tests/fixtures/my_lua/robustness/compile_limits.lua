-- The limits of Lua's memory functions, which its compiler hits as errors
-- without a position (issue #11; the real Lua adds a traceback, left out
-- here), and the line of FORPREP.
local function first_line(_, message)
    return (message:match("^[^\n]*"))
end
print(first_line(load(("do local a = 1 end\n"):rep(33000))))
local s = {}
for i = 1, 33000 do s[#s + 1] = "goto l" .. i end
print(first_line(load(table.concat(s, "\n"))))
-- the prep instruction is on the `do` line, so its errors are too
print(pcall(load("for i = 1,\n'x'\ndo end")))
print(pcall(load("for i = 1,\n2, {}\ndo end")))
-- a NUL byte is not shown as the token "near" an error
print(select(2, load("x = 1 \0 2")):match(": .*"))
