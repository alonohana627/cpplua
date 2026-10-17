-- string.sub(s, i [, j]): the substring from i to j (inclusive); negatives count from the end.
local s = "Hello, Lua!"
print(s:sub(1, 5))                            --> Hello
print(s:sub(8))                               --> Lua!
print(s:sub(-4, -2))                          --> Lua
print(s:sub(5, 2) == "")                      --> true
