-- An uncaught error (stage 9): table.insert takes two or three arguments.
local t = {1}
table.insert(t, 2)
print(#t)
table.insert(t, 1, 2, 3)
