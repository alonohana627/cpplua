-- load(chunk [, chunkname [, mode [, env]]]): compiles a chunk without running it.
local add = load("local a, b = ... return a + b")
print(add(2, 3))                              --> 5

local f, err = load("return +")
print(f, err ~= nil)                          --> nil	true

-- A custom environment.
local env = { x = 10 }
print(load("return x * 2", "chunk", "t", env)())   --> 20

-- A reader function returning pieces until nil.
local parts = { "return ", "'pie", "ces'" }
local i = 0
print(load(function() i = i + 1 return parts[i] end)())  --> pieces
