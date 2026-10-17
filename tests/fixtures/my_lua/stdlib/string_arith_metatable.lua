-- String arithmetic goes through the string metatable (lstrlib's
-- metamethods), so changing or removing them changes it (issues #9, #12).
print("1" + "2", "10" * 2, 3 - "1", "2" ^ "3", "7" // "2", -"3")
print(pcall(function() return "1" // "0" end))
print(pcall(function() return "a" + 1 end))
print(pcall(function() local x = {} return "1" + x end))
print(pcall(function() return "1" & 3 end), pcall(function() return "1.5" | 0 end))
getmetatable("").__add = function() return "mt" end
print("1" + "2")
getmetatable("").__add = nil
print(pcall(function() local s = "1" return s + 2 end))
getmetatable("").__unm = nil
print(pcall(function() local s = "3" return -s end))
