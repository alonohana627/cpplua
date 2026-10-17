-- print(...): writes its arguments with tostring, separated by tabs.
print("hello", 42, 3.5, true, nil)            --> hello	42	3.5	true	nil
print()                                       --> (an empty line)
print(setmetatable({}, { __tostring = function() return "custom" end }))  --> custom
