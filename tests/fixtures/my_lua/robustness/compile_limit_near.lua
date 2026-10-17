-- The compiler's limit errors say "near" which token, as Lua's do (issue
-- #11): too many registers in arguments and in a list of results.
print(select(2, load("a = f(x" .. string.rep(",x", 260) .. ")")))
local t = {}
for i = 1, 300 do t[i] = "a" .. i end
local names = table.concat(t, ",", 1, 150)
print(select(2, load("local " .. names .. " return " .. names .. "," .. names)))
