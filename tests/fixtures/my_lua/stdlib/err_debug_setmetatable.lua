-- debug.setmetatable needs its second argument, even if it is nil (issue #7).
print(pcall(debug.setmetatable, 1))
print(pcall(debug.setmetatable))
print(debug.setmetatable(10, nil))
