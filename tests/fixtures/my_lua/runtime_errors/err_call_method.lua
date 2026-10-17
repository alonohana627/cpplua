-- Calling a method that does not exist names the method (stage 6).
local obj = {name = "thing"}
function obj:greet() return "hi " .. self.name end
print(obj:greet())
obj:m()
